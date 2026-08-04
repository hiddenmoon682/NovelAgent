// SessionManager 实现 — 会话池容器（D2 收敛）。

#include "agent/core/SessionManager.h"
#include "agent/session/SessionPersistence.h"
#include "agent/tool/ThreadPool.h"
#include "llm/LLMClientFactory.h"

#include <spdlog/spdlog.h>
#include <algorithm>

namespace agent {

SessionManager::SessionManager(llm::LLMClientFactory& factory, ToolRegistry& registry)
    : factory_(factory)
    , registry_(registry)
    , pool_exec_(std::make_unique<ThreadPool>(4))           // 共享线程池 = 并发会话上限
    , project_lock_(std::make_shared<std::shared_mutex>())  // 跨会话共享项目锁
{
}

SessionManager::~SessionManager() = default;

// ===========================================================================
// 会话生命周期
// ===========================================================================

std::unique_ptr<SessionRuntime> SessionManager::makeRuntime(const std::string& id) {
    // P5：核心依赖构造注入（一次性），对齐 D11「仅创建时生效」。
    return std::make_unique<SessionRuntime>(id, factory_, registry_, SessionRuntimeDeps{
        .persistence = persistence_,
        .calibrator = calibrator_,
        .project_lock = project_lock_,
        .exec_config = exec_config_,
        .config = SessionConfig{
            .system_prompt_provider = nullptr,
            .model_limit = shared_model_limit_,
            .summary_sink = summary_sink_,
        },
        .system_prompt = shared_system_prompt_,
        .model_limit = shared_model_limit_,
    });
}

std::string SessionManager::makeSessionId() {
    return "s-multi-" + std::to_string(++session_seq_);
}

std::string SessionManager::createSession() {
    const std::string id = makeSessionId();
    pool_[id] = makeRuntime(id);
    return id;
}

void SessionManager::newSession() {
    // 新建会话并设为当前（方案 C：id 提前生成、文件延迟落盘）
    const std::string id = createSession();
    current_session_id_ = id;
    spdlog::info("[SessionManager] 新会话已就绪: {}", id);
}

bool SessionManager::pendingNewSession() const {
    if (current_session_id_.empty()) return false;
    auto it = pool_.find(current_session_id_);
    return it != pool_.end() && !it->second->persisted();
}

bool SessionManager::discardPendingNewSession() {
    if (!pendingNewSession()) return false;
    const std::string discarded = current_session_id_;
    pool_.erase(discarded);
    current_session_id_.clear();
    fallbackCurrentAfterDelete();
    return true;
}

bool SessionManager::switchSession(const std::string& id) {
    if (pool_.find(id) == pool_.end()) return false;
    current_session_id_ = id;
    return true;
}

bool SessionManager::deleteSessionRuntime(const std::string& id) {
    // 若该会话正在运行：先取消，再等 in-flight 任务退场（阶段 5，防 use-after-free）。
    // 注意：不能在持有 in_flight_mutex_ 时 wait——异步任务的清理逻辑也需要该锁（防死锁）。
    std::shared_future<llm::LLMResponse> fut;
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        auto it = in_flight_.find(id);
        if (it != in_flight_.end()) {
            auto rt_it = pool_.find(id);
            if (rt_it != pool_.end()) rt_it->second->requestCancel();
            fut = it->second;
        }
    }
    if (fut.valid()) fut.wait();
    return pool_.erase(id) > 0;
}

bool SessionManager::deleteSession(const std::string& id) {
    const bool was_current = (id == current_session_id_);
    if (!deleteSessionRuntime(id)) return false;
    if (was_current) {
        current_session_id_.clear();
        fallbackCurrentAfterDelete();
    }
    return true;
}

void SessionManager::fallbackCurrentAfterDelete() {
    // 自动切到剩余池会话（map 首项；空池则留空，下次自动创建）
    if (!pool_.empty()) {
        current_session_id_ = pool_.begin()->first;
    }
}

// ===========================================================================
// 池访问
// ===========================================================================

SessionRuntime* SessionManager::session(const std::string& id) {
    auto it = pool_.find(id);
    return it == pool_.end() ? nullptr : it->second.get();
}

SessionRuntime* SessionManager::currentSession() {
    if (current_session_id_.empty() || pool_.find(current_session_id_) == pool_.end()) {
        current_session_id_ = createSession();  // 无当前会话则自动创建
    }
    return pool_[current_session_id_].get();
}

const SessionRuntime* SessionManager::currentSessionConst() const {
    auto it = pool_.find(current_session_id_);
    return it == pool_.end() ? nullptr : it->second.get();
}

std::string SessionManager::currentSessionId() {
    currentSession();  // 确保存在
    return current_session_id_;
}

std::vector<std::string> SessionManager::sessionIds() const {
    std::vector<std::string> ids;
    ids.reserve(pool_.size());
    for (const auto& [id, _] : pool_) ids.push_back(id);
    return ids;
}

void SessionManager::releaseIdleClients() {
    for (auto& [_, rt] : pool_) {
        if (rt && !rt->running()) rt->releaseClient();
    }
}

// ===========================================================================
// 执行
// ===========================================================================

bool SessionManager::canSubmit() const {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);
    return in_flight_.size() < kMaxConcurrent;
}

llm::LLMResponse SessionManager::process(const std::string& session_id,
                                         const std::string& input,
                                         llm::StreamCallbacks callbacks) {
    SessionRuntime* rt = session(session_id);
    if (!rt) {
        spdlog::warn("[SessionManager] 会话 {} 不存在，拒绝输入", session_id);
        return llm::LLMResponse{.finish_reason = "session_not_found"};
    }
    // P9：并发上限拒绝（已提交未完成会话数 ≥ 上限时不排队，直接拒绝）
    if (!canSubmit()) {
        spdlog::warn("[SessionManager] 并发已满（{}），拒绝提交会话 {}", kMaxConcurrent, session_id);
        return llm::LLMResponse{.finish_reason = "concurrency_full"};
    }
    auto future = pool_exec_->submit([rt, input, cb = std::move(callbacks)]() mutable {
        return rt->process(input, std::move(cb));
    });
    auto shared = future.share();
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        in_flight_[session_id] = shared;
    }
    struct Cleanup {
        SessionManager* self; std::string sid;
        ~Cleanup() {
            std::lock_guard<std::mutex> l(self->in_flight_mutex_);
            self->in_flight_.erase(sid);
        }
    } cleanup{this, session_id};
    return shared.get();
}

llm::LLMResponse SessionManager::process(const std::string& input,
                                         llm::StreamCallbacks callbacks) {
    return process(currentSessionId(), input, std::move(callbacks));
}

void SessionManager::submitProcess(const std::string& session_id,
                                   const std::string& input,
                                   llm::StreamCallbacks callbacks,
                                   std::function<void(const std::string&, llm::LLMResponse)> on_complete) {
    SessionRuntime* rt = session(session_id);
    if (!rt) {
        spdlog::warn("[SessionManager] 会话 {} 不存在，拒绝输入", session_id);
        if (on_complete) on_complete(session_id, llm::LLMResponse{.finish_reason = "session_not_found"});
        return;
    }
    // P9：并发上限拒绝（已提交未完成会话数 ≥ 上限时不排队，直接拒绝）
    if (!canSubmit()) {
        spdlog::warn("[SessionManager] 并发已满（{}），拒绝提交会话 {}", kMaxConcurrent, session_id);
        if (on_complete) on_complete(session_id, llm::LLMResponse{.finish_reason = "concurrency_full"});
        return;
    }
    // 异步提交：池线程执行 process，完成后调用 on_complete 并清理 in-flight。
    // in_flight_ 记录供 deleteSessionRuntime 在删除运行中会话时 cancel+wait。
    auto future = pool_exec_->submit(
        [this, rt, session_id, input, cb = std::move(callbacks),
         oc = std::move(on_complete)]() mutable {
            auto resp = rt->process(input, std::move(cb));
            if (oc) oc(session_id, resp);
            std::lock_guard<std::mutex> l(in_flight_mutex_);
            in_flight_.erase(session_id);
            return resp;
        });
    {
        std::lock_guard<std::mutex> lock(in_flight_mutex_);
        in_flight_[session_id] = future.share();
    }
}

// ===========================================================================
// 消息级操作（转发当前会话 runtime）
// ===========================================================================

bool SessionManager::pinMessage(size_t index) { return currentSession()->pinMessage(index); }
bool SessionManager::unpinMessage(size_t index) { return currentSession()->unpinMessage(index); }
bool SessionManager::editMessage(size_t index, std::string new_content) {
    return currentSession()->editMessage(index, std::move(new_content));
}
bool SessionManager::rewindTo(size_t index) { return currentSession()->rewindTo(index); }
std::vector<size_t> SessionManager::checkpointIndices() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->checkpointIndices() : std::vector<size_t>{};
}
CompactionResult SessionManager::compactConversation(std::optional<std::string> focus) {
    return currentSession()->compactConversation(std::move(focus));
}
std::vector<std::string> SessionManager::contextWarnings() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->contextWarnings() : std::vector<std::string>{};
}
ContextUsage SessionManager::contextUsage() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->contextUsage() : ContextUsage{};
}
const llm::IMemory& SessionManager::memory() const {
    const SessionRuntime* rt = currentSessionConst();
    return rt ? rt->memory() : kEmptyMemory();
}
const llm::IMemory& SessionManager::kEmptyMemory() {
    static llm::Memory empty;
    return empty;
}

// ===========================================================================
// 持久化路由
// ===========================================================================

void SessionManager::saveSessionState() { currentSession()->saveSessionState(); }
void SessionManager::loadSessionState() { currentSession()->loadSessionState(); }

// ===========================================================================
// 装配配置（D11：共享源 + 创建时注入，仅新会话生效）
// ===========================================================================

void SessionManager::setSystemPrompt(std::string prompt) {
    // 仅更新共享源；已存在会话不更新（D11「仅创建时生效」；新会话经 makeRuntime 注入）
    shared_system_prompt_ = std::move(prompt);
}

void SessionManager::setModelLimit(int limit) {
    shared_model_limit_ = limit;
}

void SessionManager::setPersistence(SessionPersistence* p) {
    persistence_ = p;
}

void SessionManager::setCalibrator(llm::TokenCounter* cal) {
    calibrator_ = cal;
}

void SessionManager::setSummarySink(std::function<void(const std::string&)> sink) {
    summary_sink_ = std::move(sink);
}

void SessionManager::setExecutionConfig(AgentExecutionConfig config) {
    exec_config_ = std::move(config);
}

} // namespace agent