#pragma once

/// 后端 HTTP+SSE 服务器。
/// 提供 REST API + SSE 流式对话，支持多终端同时连接。

#include "server/SessionManager.h"
#include <httplib.h>

#include <memory>
#include <string>
#include <thread>
#include <atomic>

namespace server {

struct ServerConfig {
    int port = 8899;
    std::string project_path;
    int idle_timeout_minutes = 30;
};

class BackendServer {
public:
    BackendServer(llm::ILLMClient& client, agent::ToolRegistry& registry,
                  std::shared_ptr<Project> project, const ServerConfig& config);
    ~BackendServer();

    void run();
    void stop();
    bool isRunning() const { return running_.load(); }
    int port() const { return config_.port; }
    int activeClients() const { return active_clients_.load(); }

private:
    llm::ILLMClient& client_;
    agent::ToolRegistry& registry_;
    std::shared_ptr<Project> project_;
    ServerConfig config_;
    SessionManager session_mgr_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> active_clients_{0};

    void setupRoutes();
    void writePortFile() const;
    void removePortFile() const;
    std::string portFilePath() const;
};

} // namespace server
