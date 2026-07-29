#include "agent/core/SessionManager.h"
#include "agent/session/SessionPersistence.h"
#include "agent/context/Memory.h"

#include <spdlog/spdlog.h>

namespace agent {

// ===========================================================================
// 会话生命周期
// ===========================================================================

void SessionManager::resetSession() {
    // 多会话语义：旧会话保留在列表中（不归档），新建空会话并激活。
    // 当前会话为空时不新建，避免反复点击堆积空会话。
    if (persistence_ && !memory_.messages().empty()) {
        try {
            persistence_->save(memory_);       // 保存当前会话，保留在列表中
            persistence_->createSession();     // 新建空会话并设为 active（已落盘空数组）
        } catch (const std::exception& e) {
            spdlog::warn("[SessionManager] 新建会话落盘失败（继续重置内存）: {}", e.what());
        }
    }

    // system prompt 由 NovelAgentApp 装配（人格/工具指令/技能），clear()
    // 不能连它一起清掉。会话边界优先经提供者重建，使运行期变化
    //（如 save_skill 新增的技能目录）在新会话生效；无提供者时沿用旧值
    std::string prompt = system_prompt_provider_ ? system_prompt_provider_()
                                                 : memory_.systemPrompt();
    memory_.clear();
    memory_.setSystemPrompt(std::move(prompt));

    if (boundary_reset_hook_) boundary_reset_hook_();
    if (usage_refresh_hook_) usage_refresh_hook_();
}

bool SessionManager::switchSession(const std::string& id) {
    if (!persistence_) return false;
    try {
        if (id == persistence_->activeSessionId()) return true;
        persistence_->save(memory_);  // 切走前保存当前会话
        if (!persistence_->switchSession(id)) return false;
    } catch (const std::exception& e) {
        spdlog::warn("[SessionManager] 切换会话失败: {}", e.what());
        return false;
    }
    reloadActiveSession();
    return true;
}

bool SessionManager::deleteSession(const std::string& id) {
    if (!persistence_) return false;
    bool was_active = false;
    try {
        was_active = (id == persistence_->activeSessionId());
        if (!persistence_->deleteSession(id)) return false;
    } catch (const std::exception& e) {
        spdlog::warn("[SessionManager] 删除会话失败: {}", e.what());
        return false;
    }
    // 删除非 active 会话不影响当前对话；删除 active 时持久层已切好新 active，重载即可
    if (was_active) reloadActiveSession();
    return true;
}

void SessionManager::reloadActiveSession() {
    // 会话边界：同 resetSession，prompt 优先经提供者重建
    std::string prompt = system_prompt_provider_ ? system_prompt_provider_()
                                                 : memory_.systemPrompt();
    memory_.clear();
    memory_.setSystemPrompt(std::move(prompt));

    if (boundary_reset_hook_) boundary_reset_hook_();

    loadSessionState();  // 从 active 会话恢复消息（空会话则保持空，内部已刷新用量）
}

// ===========================================================================
// 会话持久化
// ===========================================================================

void SessionManager::saveSessionState() {
    if (!persistence_) return;
    persistence_->save(memory_);
}

void SessionManager::loadSessionState() {
    if (!persistence_) {
        return;
    }
    // 启动路径不得抛异常：会话恢复失败降级为空会话，不能阻止应用初始化
    try {
        auto loaded = persistence_->load();
        if (!loaded.messages().empty()) {
            // 只恢复对话消息；system prompt 以本次启动装配的为准（文件中也不存储 system）
            auto snapshot = loaded.checkpoint();
            snapshot.system_prompt = memory_.systemPrompt();
            memory_.restore(snapshot);
        }
    } catch (const std::exception& e) {
        spdlog::warn("[SessionManager] 会话恢复失败（从空会话开始）: {}", e.what());
    }
    if (usage_refresh_hook_) usage_refresh_hook_();
}

// ===========================================================================
// 消息级操作
// ===========================================================================

bool SessionManager::pinMessage(size_t index) {
    return memory_.pin(index);
}

bool SessionManager::unpinMessage(size_t index) {
    return memory_.unpin(index);
}

bool SessionManager::editMessage(size_t index, std::string new_content) {
    return memory_.edit(index, std::move(new_content));
}

// ===========================================================================
// 对话回滚
// ===========================================================================

bool SessionManager::rewindTo(size_t index) {
    if (index >= memory_.size()) return false;

    auto pinned = memory_.pinnedIndices();
    std::vector<size_t> lost_pins;
    for (auto pi : pinned) {
        if (pi > index) lost_pins.push_back(pi);
    }
    if (!lost_pins.empty()) {
        std::string ids;
        for (size_t i = 0; i < lost_pins.size(); ++i) {
            if (i > 0) ids += ", ";
            ids += "#" + std::to_string(lost_pins[i]);
        }
        spdlog::warn("[SessionManager] 回滚到 #{} 将丢弃 {} 条 pinned 消息 ({})",
                     index, lost_pins.size(), ids);
    }

    memory_.truncateTo(index + 1);
    spdlog::info("[SessionManager] 回滚到消息 #{} (保留 {} 条)", index, memory_.size());
    return true;
}

std::vector<size_t> SessionManager::checkpointIndices() const {
    std::vector<size_t> result;
    const auto& all = memory_.all();
    for (size_t i = 0; i < all.size(); ++i) {
        if (all[i].role == llm::MessageRole::User) {
            result.push_back(i);
        }
    }
    return result;
}

} // namespace agent
