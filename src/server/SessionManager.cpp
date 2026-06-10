/// SessionManager 实现。

#include "server/SessionManager.h"
#include "agent/AgentSetup.h"
#include "project/Models.h"

#include <spdlog/spdlog.h>
#include <random>
#include <sstream>

namespace server {

namespace {
std::string uuid4() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static const char* hex = "0123456789abcdef";

    std::ostringstream ss;
    for (int i = 0; i < 8; ++i) ss << hex[dis(gen)];
    ss << '-';
    for (int i = 0; i < 4; ++i) ss << hex[dis(gen)];
    ss << "-4";
    for (int i = 0; i < 3; ++i) ss << hex[dis(gen)];
    ss << '-';
    ss << hex[8 + dis(gen) % 4];
    for (int i = 0; i < 3; ++i) ss << hex[dis(gen)];
    ss << '-';
    for (int i = 0; i < 12; ++i) ss << hex[dis(gen)];
    return ss.str();
}
} // namespace

SessionManager::SessionManager(
    llm::ILLMClient& client, agent::ToolRegistry& registry,
    std::shared_ptr<Project> project)
    : client_(client), registry_(registry), project_(std::move(project))
{}

std::string SessionManager::createSession() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto session = std::make_unique<Session>();
    session->id = uuid4();
    session->agent = std::make_unique<agent::Agent>(client_, registry_);

    // 配置 Agent
    session->agent->setSystemPrompt(
        "你是一个专业的网络小说写作助手 NovelAgent。");
    session->agent->useSerialProcessor();

    // 工具注册由调用方在创建 SessionManager 之前完成

    session->state = std::make_unique<agent::StateMachine>();
    session->created = std::chrono::steady_clock::now();
    session->last_active = session->created;

    std::string id = session->id;
    sessions_[id] = std::move(session);

    spdlog::info("[SessionManager] 创建会话: {} (总数: {})", id, sessions_.size());
    return id;
}

void SessionManager::destroySession(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        sessions_.erase(it);
        spdlog::info("[SessionManager] 销毁会话: {} (剩余: {})", id, sessions_.size());
    }
}

agent::Agent* SessionManager::getAgent(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(id);
    if (it != sessions_.end()) {
        it->second->last_active = std::chrono::steady_clock::now();
        return it->second->agent.get();
    }
    return nullptr;
}

std::vector<std::string> SessionManager::activeSessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, _] : sessions_) ids.push_back(id);
    return ids;
}

int SessionManager::sessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(sessions_.size());
}

void SessionManager::cleanupIdleSessions(std::chrono::minutes timeout) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> to_remove;
    for (const auto& [id, s] : sessions_) {
        if (now - s->last_active > timeout) {
            to_remove.push_back(id);
        }
    }
    for (const auto& id : to_remove) {
        sessions_.erase(id);
    }
    if (!to_remove.empty()) {
        spdlog::info("[SessionManager] 清理 {} 个闲置会话", to_remove.size());
    }
}

} // namespace server
