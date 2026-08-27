// SessionPool 实现 — 会话池容器（D2 收敛）。

#include "agent/session/SessionPool.h"
#include "agent/session/SessionPersistence.h"
#include "agent/tool/ThreadPool.h"
#include "llm/LLMClientFactory.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>

namespace agent {

SessionPool::SessionPool(llm::LLMClientFactory& factory, ToolRegistry& registry)
    : factory_(factory)
    , registry_(registry)
    , pool_exec_(std::make_unique<ThreadPool>(4))           // 共享线程池 = 并发会话上限
{
}

SessionPool::~SessionPool() = default;

// ===========================================================================
// 会话生命周期
// ===========================================================================

std::unique_ptr<SessionRuntime> SessionPool::makeRuntime(const std::string& id) {
    // P5：核心依赖构造注入（一次性），对齐 D11「仅创建时生效」。
    return std::make_unique<SessionRuntime>(id, factory_, registry_, SessionRuntimeDeps{
        .persistence = persistence_,
        .calibrator = calibrator_,
        .exec_config = exec_config_,
        .config = SessionConfig{
            .system_prompt_provider = system_prompt_provider_,
            .model_limit = shared_model_limit_,
            .summary_sink = summary_sink_,
        },
        .system_prompt = shared_system_prompt_,
        .model_limit = shared_model_limit_,
    });
}

std::string SessionPool::makeSessionId() {
    return "s-multi-" + std::to_string(++session_seq_);
}

std::string SessionPool::createSession() {
    // id 唯一化（跨重启/重建）：session_seq_ 进程内从 0 计数，而持久层历史会话
    // 是上次运行生成的同类 id（s-multi-N）。直接递增会生成与旧会话相同的 id——
    // 同 id 入池失败（"新建"顶替不了旧 runtime）、会话列表按 id 去重隐藏旧条目、
    // 首条消息保存时 DELETE+重插覆盖旧数据（"新建盖掉旧会话"严重 bug 根因）。
    // 生成后逐一核对池内与持久层（含已归档行），被占用则继续递增，
    // 保证返回的 id 一定对应一个全新的、全局唯一的会话。
    std::string id;
    for (;;) {
        id = makeSessionId();
        if (pool_.count(id) != 0) continue;   // 池内已占用 → 换号
        if (persistence_) {
            bool taken = false;
            try {
                taken = persistence_->sessionIdExists(id);
            } catch (const std::exception& e) {
                // DB 异常不再继续查重：按未占用放行（创建动作本身不依赖 DB，
                // 事后保存路径另有异常兜底），避免查重失败阻塞新建。
                spdlog::warn("[SessionPool] 新建会话 id 查重失败: {}", e.what());
            }
            if (taken) continue;              // 持久层已占用（含归档）→ 换号
        }
        break;
    }
    // 先构造后入池：makeRuntime 抛异常时不会留下空 unique_ptr 条目
    //（否则后续按 id 命中却拿到 nullptr runtime，解引用即崩溃）。
    auto rt = makeRuntime(id);
    pool_.emplace(id, std::move(rt));
    return id;
}

void SessionPool::newSession() {
    // 新建会话并设为当前（方案 C：id 提前生成、文件延迟落盘）
    const std::string id = createSession();
    current_session_id_ = id;
    spdlog::info("[SessionPool] 新会话已就绪: {}", id);
}

bool SessionPool::pendingNewSession() const {
    if (current_session_id_.empty()) return false;
    auto it = pool_.find(current_session_id_);
    return it != pool_.end() && !it->second->persisted();
}

bool SessionPool::discardPendingNewSession() {
    if (!pendingNewSession()) return false;
    const std::string discarded = current_session_id_;
    auto it = pool_.find(discarded);
    if (it == pool_.end()) return false;
    // 未落盘但已有消息（落盘失败残留/首条消息处理中）不得丢弃：丢弃会丢失这些消息。
    // 走加锁快照读（GUI 线程可能正与池线程的 process 并发写同会话 memory）。
    if (!it->second->memory().snapshot().empty()) {
        spdlog::warn("[SessionPool] 会话 {} 未落盘但已有消息，拒绝丢弃（防数据丢失）", discarded);
        return false;
    }
    // 与 deleteSessionRuntime 同款防护：首条消息可能在跑/排队（persisted_ 要等整轮
    // 结束才置位），直接 erase 会让池线程持有的 runtime 裸指针悬垂（UAF）。
    // 先 cancel + delete_requested（排队任务启动即退出，不被 resetCancel 吞掉），
    // 再等 in-flight 退场（2s 超时则不强行删除，恢复标志后由调用方重试）。
    std::shared_future<llm::LLMResponse> fut;
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        auto it = in_flight_.find(discarded);
        if (it != in_flight_.end()) {
            if (auto rt_it = pool_.find(discarded); rt_it != pool_.end()) {
                rt_it->second->requestCancel();
                rt_it->second->setDeleteRequested();
            }
            fut = it->second;
        }
    }
    if (fut.valid()) {
        if (fut.wait_for(std::chrono::seconds(2)) == std::future_status::timeout
            && fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            spdlog::warn("[SessionPool] 丢弃未落盘会话 {} 超时（任务仍在执行），暂不丢弃", discarded);
            // 恢复会话标志：任务若结束则会话可继续使用，稍后重试删除
            if (auto rt_it = pool_.find(discarded); rt_it != pool_.end()) {
                rt_it->second->clearDeleteRequested();
                rt_it->second->resetCancel();
            }
            return false;
        }
    }
    pool_.erase(discarded);
    current_session_id_.clear();
    fallbackCurrentAfterDelete();
    return true;
}

bool SessionPool::switchSession(const std::string& id) {
    if (pool_.find(id) == pool_.end()) return false;
    current_session_id_ = id;
    return true;
}

bool SessionPool::deleteSessionRuntime(const std::string& id) {
    // 若该会话正在运行：先请求取消 + 置独立删除标志（排队任务启动时也会立即退出，
    // 不被 process 开头的 resetCancel 吞掉），再等 in-flight 任务退场（阶段 5，防 use-after-free）。
    // 注意：不能在持有 in_flight_mutex_ 时 wait——异步任务的清理逻辑也需要该锁（防死锁）。
    std::shared_future<llm::LLMResponse> fut;
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        auto it = in_flight_.find(id);
        if (it != in_flight_.end()) {
            auto rt_it = pool_.find(id);
            if (rt_it != pool_.end()) {
                rt_it->second->requestCancel();
                rt_it->second->setDeleteRequested();
            }
            fut = it->second;
        }
    }
    if (fut.valid()) {
        // 安全优先：最多等 2s。超时后不强制移除——池线程的异步任务捕获了 runtime 裸指针，
        // 此刻 pool_.erase(id) 析构 runtime 会导致 use-after-free。
        if (fut.wait_for(std::chrono::seconds(2)) == std::future_status::timeout) {
            // 恰在超时边界完成（任务已退出）则直接移除，避免回滚一个已按 cancelled 结束的会话。
            if (fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                return pool_.erase(id) > 0;
            spdlog::warn("[SessionPool] 会话 {} 运行中删除超时，暂不移除（任务仍在执行，稍后重试）", id);
            // 恢复会话标志（清除取消/删除请求），让在跑任务若结束则会话可继续使用，稍后再删。
            if (auto rt_it = pool_.find(id); rt_it != pool_.end()) {
                rt_it->second->clearDeleteRequested();
                rt_it->second->resetCancel();
            }
            return false;
        }
    }
    return pool_.erase(id) > 0;
}

bool SessionPool::deleteSession(const std::string& id) {
    const bool was_current = (id == current_session_id_);
    if (!deleteSessionRuntime(id)) return false;
    if (was_current) {
        current_session_id_.clear();
        fallbackCurrentAfterDelete();
    }
    return true;
}

void SessionPool::fallbackCurrentAfterDelete() {
    // 自动切到剩余池会话（map 首项；空池则留空，下次自动创建）
    if (!pool_.empty()) {
        current_session_id_ = pool_.begin()->first;
    }
}

// ===========================================================================
// 池访问
// ===========================================================================

SessionRuntime* SessionPool::session(const std::string& id) {
    auto it = pool_.find(id);
    return it == pool_.end() ? nullptr : it->second.get();
}

bool SessionPool::materializeSession(const std::string& id) {
    // 已在池：仅切当前焦点
    if (pool_.find(id) != pool_.end()) {
        current_session_id_ = id;
        return true;
    }
    // 不在池：须持久层存在该 id 才物化（避免为不存在的历史 id 建幽灵 runtime）。
    // DB 异常（磁盘错误/中断）不得穿透到 QML Q_INVOKABLE（与 sessionList 的
    // try/catch 对称）：按"不存在"处理，调用方应给出 UI 提示。
    // 存在性检查用点查 hasSession 替代 listSessions 全扫：每次点开会话全表扫描
    // 是 O(n) 浪费，且可能在 GUI 线程与池线程的长事务争锁（卡片级卡顿来源）。
    bool persisted_exists = false;
    if (persistence_) {
        try {
            persisted_exists = persistence_->hasSession(id);
        } catch (const std::exception& e) {
            spdlog::warn("[SessionPool] 物化会话 {} 时读取持久层失败: {}", id, e.what());
            return false;
        }
    }
    if (!persisted_exists) return false;
    // 先构造后入池（异常安全，见 createSession）
    auto rt = makeRuntime(id);
    pool_.emplace(id, std::move(rt));
    pool_[id]->loadSessionState();  // 恢复历史消息（无则保持空会话）
    // 排序时间戳恢复为库内真实 updated_at：runtime 构造会把"最近活动"刷成当前时刻，
    // 若不改回，每次物化（点开会话）都会让该会话跳到列表第一（"点击导致列表跳动"历史 bug）。
    if (persistence_) {
        try {
            const int64_t db_ms = persistence_->sessionUpdatedAtMs(id);
            if (db_ms > 0) pool_[id]->setUpdatedAtMs(db_ms);
        } catch (const std::exception& e) {
            spdlog::warn("[SessionPool] 读取会话 {} 活动时间失败: {}", id, e.what());
        }
    }
    current_session_id_ = id;
    spdlog::info("[SessionPool] 物化历史会话: {}", id);
    return true;
}

SessionRuntime* SessionPool::currentSession() {
    if (current_session_id_.empty() || pool_.find(current_session_id_) == pool_.end()) {
        current_session_id_ = createSession();  // 无当前会话则自动创建
    }
    return pool_[current_session_id_].get();
}

const SessionRuntime* SessionPool::currentSessionConst() const {
    auto it = pool_.find(current_session_id_);
    return it == pool_.end() ? nullptr : it->second.get();
}

std::string SessionPool::currentSessionId() {
    currentSession();  // 确保存在
    return current_session_id_;
}

std::string SessionPool::deferredToolsStub() {
    // 仅读已存在会话，绝不自动创建：SessionRuntime 构造期间的 provider 回调
    // 会走到这里，若当前焦点悬空且池为空，自动创建将再次触发构造 → 无限递归。
    if (auto it = pool_.find(current_session_id_); it != pool_.end())
        return it->second->deferredToolsStub();
    if (!pool_.empty())
        return pool_.begin()->second->deferredToolsStub();
    return "";
}

std::vector<std::string> SessionPool::sessionIds() const {
    std::vector<std::string> ids;
    ids.reserve(pool_.size());
    for (const auto& [id, _] : pool_) ids.push_back(id);
    return ids;
}

bool SessionPool::anyRunning() const {
    for (const auto& [_, rt] : pool_)
        if (rt && rt->running()) return true;
    // 排队/收尾中的任务同样算"忙"：作业已提交但尚未启动（4 并发满时的排队窗口）
    // 或已完成尚未清理——供 busy() 聚合信号正确反映"会话将占用/正占用线程池"，
    // 避免全局操作守卫（重建/删项目等）在任务排队的空档被放行。
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        if (!in_flight_.empty()) return true;
    }
    return false;
}

void SessionPool::releaseIdleClients() {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);
    for (auto& [id, rt] : pool_) {
        if (!rt || rt->running()) continue;
        // 有排队任务（running_ 尚未置位）的会话不释放 client：
        // 任务启动时会立即使用，释放只会导致白重建一次
        if (in_flight_.count(id) > 0) continue;
        rt->releaseClient();
    }
}

// ===========================================================================
// 执行
// ===========================================================================

llm::LLMResponse SessionPool::process(const std::string& session_id,
                                         const std::string& input,
                                         llm::StreamCallbacks callbacks) {
    SessionRuntime* rt = session(session_id);
    if (!rt) {
        spdlog::warn("[SessionPool] 会话 {} 不存在，拒绝输入", session_id);
        return llm::LLMResponse::rejected("session_not_found");
    }
    // P9：并发检查 + 同会话单飞检查 + 提交 + in-flight 占位在持锁内一次完成，
    // 避免 TOCTOU 突破上限；in_flight_ 每会话至多一条（不变量）：同一会话重复提交
    // 会导致同一 runtime 并发执行 process（内存无锁数据竞争）且两个条目互相覆盖，
    // 删除时 wait 到错误的 future 直接 erase → 池线程持悬垂 runtime 指针（UAF）。
    llm::LLMResponse rejected;
    std::shared_future<llm::LLMResponse> shared;
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        if (in_flight_.size() >= kMaxConcurrent) {
            rejected = llm::LLMResponse::rejected("concurrency_full");
        } else if (in_flight_.count(session_id) > 0) {
            rejected = llm::LLMResponse::rejected("session_busy");
        } else {
            shared = pool_exec_->submit([rt, input, cb = std::move(callbacks)]() mutable {
                return rt->process(input, std::move(cb));
            }).share();
            in_flight_[session_id] = shared;
        }
    }
    if (!shared.valid()) {
        spdlog::warn("[SessionPool] {}，拒绝提交会话 {}",
                     rejected.finish_reason == "concurrency_full"
                         ? "并发已满"
                         : "会话正在生成中",
                     session_id);
        return rejected;
    }
    struct Cleanup {
        SessionPool* self; std::string sid;
        ~Cleanup() {
            std::lock_guard<std::mutex> l(self->in_flight_mutex_);
            self->in_flight_.erase(sid);
        }
    } cleanup{this, session_id};
    return shared.get();
}

llm::LLMResponse SessionPool::process(const std::string& input,
                                         llm::StreamCallbacks callbacks) {
    return process(currentSessionId(), input, std::move(callbacks));
}

void SessionPool::submitProcess(const std::string& session_id,
                                   const std::string& input,
                                   llm::StreamCallbacks callbacks,
                                   std::function<void(const std::string&, llm::LLMResponse)> on_complete) {
    SessionRuntime* rt = session(session_id);
    if (!rt) {
        spdlog::warn("[SessionPool] 会话 {} 不存在，拒绝输入", session_id);
        if (on_complete) on_complete(session_id, llm::LLMResponse::rejected("session_not_found"));
        return;
    }
    // P9：并发检查 + 同会话单飞检查 + 提交 + in-flight 占位在持锁内一次完成，
    // 避免 TOCTOU 突破上限（拒绝回调移到锁外调用——回调内可能执行重型工作
    //（如 GUI 侧 indexAll），持锁同步调用会卡住所有池任务的收尾）。
    // in_flight_ 记录供 deleteSessionRuntime 在删除运行中会话时 cancel+wait。
    llm::LLMResponse rejected;
    bool rejected_flag = false;
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        if (in_flight_.size() >= kMaxConcurrent) {
            rejected = llm::LLMResponse::rejected("concurrency_full");
            rejected_flag = true;
        } else if (in_flight_.count(session_id) > 0) {
            // 同会话已在跑/排队：拒绝重复提交（防同一 runtime 并发执行 + 删除 UAF）
            rejected = llm::LLMResponse::rejected("session_busy");
            rejected_flag = true;
        } else {
            auto future = pool_exec_->submit(
                [this, rt, session_id, input, cb = std::move(callbacks),
                 oc = std::move(on_complete)]() mutable {
                    auto resp = rt->process(input, std::move(cb));
                    if (oc) oc(session_id, resp);
                    std::lock_guard<std::mutex> l(in_flight_mutex_);
                    in_flight_.erase(session_id);
                    return resp;
                });
            in_flight_[session_id] = future.share();
        }
    }
    if (rejected_flag) {
        spdlog::warn("[SessionPool] {}，拒绝提交会话 {}",
                     rejected.finish_reason == "concurrency_full"
                         ? "并发已满"
                         : "会话正在生成中",
                     session_id);
        if (on_complete) on_complete(session_id, rejected);
        return;
    }
}

bool SessionPool::cancelAllAndWait(std::chrono::milliseconds timeout) {
    // 收集全部 in-flight future 并逐个请求取消。不能在持有 in_flight_mutex_ 时 wait——
    // 异步任务清理逻辑也需该锁（防死锁，同 deleteSessionRuntime 约束）。
    std::vector<std::shared_future<llm::LLMResponse>> futures;
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        futures.reserve(in_flight_.size());
        for (const auto& [id, fut] : in_flight_) {
            if (auto it = pool_.find(id); it != pool_.end()) {
                it->second->requestCancel();
                // 同时置删除请求：排队未启动的任务在 process 开头会 resetCancel()
                // 清掉取消标志，不置删除标志则它仍会完整跑一轮 LLM——关闭/重建的
                // "2s 内退场"契约对排队任务失效（析构期间 GUI 可阻塞整轮生成时长）。
                it->second->setDeleteRequested();
            }
            futures.push_back(fut);
        }
    }
    bool all_exited = true;
    // 逐任务依次等待：任一任务超时即不再等待后续（已超时者大概率同样卡死），
    // 避免最坏 N×timeout 的总等待时长。
    for (auto& fut : futures) {
        if (!fut.valid()) continue;
        if (fut.wait_for(timeout) == std::future_status::timeout) {
            all_exited = false;
            break;
        }
    }
    return all_exited;
}

// ===========================================================================
// 消息级操作（转发当前会话 runtime）
// ===========================================================================

bool SessionPool::pinMessage(size_t index) { return currentSession()->pinMessage(index); }
bool SessionPool::unpinMessage(size_t index) { return currentSession()->unpinMessage(index); }
bool SessionPool::editMessage(size_t index, std::string new_content) {
    return currentSession()->editMessage(index, std::move(new_content));
}
bool SessionPool::rewindTo(size_t index) { return currentSession()->rewindTo(index); }
std::vector<size_t> SessionPool::checkpointIndices() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->checkpointIndices() : std::vector<size_t>{};
}
CompactionResult SessionPool::compactConversation(std::optional<std::string> focus) {
    return currentSession()->compactConversation(std::move(focus));
}
std::vector<std::string> SessionPool::contextWarnings() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->contextWarnings() : std::vector<std::string>{};
}
ContextUsage SessionPool::contextUsage() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->contextUsage() : ContextUsage{};
}
const llm::IMemory& SessionPool::memory() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->memory() : kEmptyMemory();
}
const llm::IMemory& SessionPool::kEmptyMemory() {
    static llm::Memory empty;
    return empty;
}

// ===========================================================================
// 持久化路由
// ===========================================================================

void SessionPool::saveSessionState() { currentSession()->saveSessionState(); }
void SessionPool::loadSessionState() { currentSession()->loadSessionState(); }

// ===========================================================================
// 装配配置（D11：共享源 + 创建时注入，仅新会话生效）
// ===========================================================================

void SessionPool::setSystemPrompt(std::string prompt) {
    // 仅更新共享源；已存在会话不更新（D11「仅创建时生效」；新会话经 makeRuntime 注入）
    shared_system_prompt_ = std::move(prompt);
}

void SessionPool::setModelLimit(int limit) {
    shared_model_limit_ = limit;
}

void SessionPool::setPersistence(SessionPersistence* p) {
    persistence_ = p;
}

void SessionPool::setCalibrator(llm::TokenCounter* cal) {
    calibrator_ = cal;
}

void SessionPool::setSummarySink(std::function<void(const std::string&)> sink) {
    summary_sink_ = std::move(sink);
}

void SessionPool::setExecutionConfig(AgentExecutionConfig config) {
    exec_config_ = std::move(config);
}

} // namespace agent