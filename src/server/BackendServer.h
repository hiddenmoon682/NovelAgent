#pragma once

/// 后端 HTTP+SSE 服务器（网络配置增强版）。
/// Fix #1: httplib Server 超时/keepalive/idle/payload 配置。
/// Fix #4: 并发连接上限。
/// Fix #5: 请求体大小检查。

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

    // Fix #1: 网络层配置
    int read_timeout_sec = 30;
    int write_timeout_sec = 30;
    int idle_interval_sec = 60;
    int keep_alive_timeout_sec = 5;
    size_t keep_alive_max_count = 128;
    size_t payload_max_bytes = 10 * 1024 * 1024;  // 10MB
    size_t max_body_bytes = 1 * 1024 * 1024;       // JSON body 上限 1MB

    // Fix #4: 并发上限
    int max_clients = 16;
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

    /// Fix #5: 请求体大小检查。
    bool checkBodySize(const std::string& body, httplib::Response& res) const;

    /// Fix #4: 连接上限检查。
    bool checkClientLimit(httplib::Response& res);
};

} // namespace server
