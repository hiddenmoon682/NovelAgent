/// BackendServer 实现 — HTTP+SSE 多会话服务器。

#include "server/BackendServer.h"

#include "project/Models.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace server {

BackendServer::BackendServer(
    llm::ILLMClient& client, agent::ToolRegistry& registry,
    std::shared_ptr<Project> project, const ServerConfig& config)
    : client_(client), registry_(registry), project_(std::move(project)),
      config_(config), session_mgr_(client_, registry_, project_)
{}

BackendServer::~BackendServer() { stop(); }

void BackendServer::stop() {
    running_ = false;
    if (server_) server_->stop();
    if (server_thread_.joinable()) server_thread_.join();
    removePortFile();
}

std::string BackendServer::portFilePath() const {
    if (project_ && !project_->path.empty()) {
        return utils::file::joinPath(
            utils::file::joinPath(project_->path, ".novelagent"), "port");
    }
    return {};
}

void BackendServer::writePortFile() const {
    std::string path = portFilePath();
    if (path.empty()) return;
    std::ofstream f(path);
    f << config_.port << "\n";
    spdlog::info("[BackendServer] 端口文件: {} (port={})", path, config_.port);
}

void BackendServer::removePortFile() const {
    std::string path = portFilePath();
    if (path.empty()) return;
    utils::file::removeFile(path);
}

// ============================================================================
// SSE 辅助
// ============================================================================

void BackendServer::sendSSE(httplib::Response& res, const std::string& type,
                             const std::string& data) {
    std::string line = "data: {\"type\":\"" + type + "\"";
    if (!data.empty()) {
        // data 已经是 JSON 键值对，直接追加
        line += "," + data;
    }
    line += "}\n\n";
    res.body += line;
}

// ============================================================================
// 路由设置
// ============================================================================

void BackendServer::setupRoutes() {
    // ── 健康检查 ──
    server_->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        json r;
        r["status"] = "ok";
        r["sessions"] = session_mgr_.sessionCount();
        r["clients"] = active_clients_.load();
        res.set_content(r.dump(), "application/json");
    });

    // ── 创建会话 ──
    server_->Post("/api/session/create", [this](const httplib::Request&, httplib::Response& res) {
        std::string sid = session_mgr_.createSession();
        json r;
        r["session_id"] = sid;
        res.set_content(r.dump(), "application/json");
        active_clients_++;
    });

    // ── 销毁会话 ──
    server_->Post("/api/session/destroy", [this](const httplib::Request& req, httplib::Response& res) {
        auto j = json::parse(req.body);
        std::string sid = j.value("session_id", "");
        session_mgr_.destroySession(sid);
        active_clients_--;
        json r;
        r["status"] = "ok";
        res.set_content(r.dump(), "application/json");
    });

    // ── 会话列表 ──
    server_->Get("/api/session/list", [this](const httplib::Request&, httplib::Response& res) {
        auto ids = session_mgr_.activeSessions();
        json r = json::array();
        for (const auto& id : ids) r.push_back(id);
        res.set_content(r.dump(), "application/json");
    });

    // ── 聊天（流式 SSE）──
    server_->Post("/api/chat", [this](const httplib::Request& req, httplib::Response& res) {
        auto j = json::parse(req.body);
        std::string sid = j.value("session_id", "");
        std::string message = j.value("message", "");

        if (sid.empty() || message.empty()) {
            res.status = 400;
            json err;
            err["error"] = "缺少 session_id 或 message";
            res.set_content(err.dump(), "application/json");
            return;
        }

        auto* agent = session_mgr_.getAgent(sid);
        if (!agent) {
            res.status = 404;
            json err;
            err["error"] = "会话不存在: " + sid;
            res.set_content(err.dump(), "application/json");
            return;
        }

        // SSE 流式响应
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");

        // 设置流式回调 → 映射为 SSE 事件
        llm::StreamCallbacks cb;
        cb.on_content = [&res](const std::string& delta) {
            json d;
            d["delta"] = delta;
            sendSSE(res, "content", "\"delta\":\"" +
                json(delta).dump().substr(1, json(delta).dump().size() - 2) + "\"");
        };
        cb.on_reasoning = [&res](const std::string& delta) {
            sendSSE(res, "reasoning", "\"delta\":\"" + delta + "\"");
        };
        cb.on_tool_call_start = [&res]() {
            sendSSE(res, "tool_call_start", "");
        };
        cb.on_complete = [&res](const llm::LLMResponse& resp) {
            json d;
            d["tokens"] = resp.total_tokens;
            d["finish_reason"] = resp.finish_reason;
            sendSSE(res, "done", "\"tokens\":" + std::to_string(resp.total_tokens) +
                    ",\"finish_reason\":\"" + resp.finish_reason + "\"");
        };
        cb.on_error = [&res](const std::string& err) {
            sendSSE(res, "error", "\"message\":\"" + err + "\"");
        };

        // 执行（会触发回调，逐块填充 res.body）
        agent->processUserMessage(message, cb);
    });

    // ── 单次执行（非流式）──
    server_->Post("/api/execute", [this](const httplib::Request& req, httplib::Response& res) {
        auto j = json::parse(req.body);
        std::string command = j.value("command", "");

        if (command.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"缺少 command\"}", "application/json");
            return;
        }

        // 使用临时 Agent 执行单次命令
        agent::Agent tempAgent(client_, registry_);
        auto response = tempAgent.execute(command);

        json r;
        r["content"] = response.content;
        r["tokens"] = response.total_tokens;
        r["finish_reason"] = response.finish_reason;
        res.set_content(r.dump(), "application/json");
    });

    // ── 项目状态 ──
    server_->Get("/api/project/status", [this](const httplib::Request&, httplib::Response& res) {
        json r;
        if (project_) {
            r["title"] = project_->title;
            r["chapters"] = project_->outline.chapters.size();
            r["characters"] = project_->characters.size();
            r["settings"] = project_->settings.size();
            r["status"] = project_->status;
            r["word_count"] = project_->current_word_count;
            r["target_words"] = project_->target_word_count;
        } else {
            r["error"] = "未打开项目";
        }
        res.set_content(r.dump(), "application/json");
    });

    // ── 导出 ──
    server_->Post("/api/project/export", [this](const httplib::Request&, httplib::Response& res) {
        if (!project_ || project_->title.empty()) {
            res.set_content("{\"error\":\"未打开项目\"}", "application/json");
            return;
        }

        std::ostringstream book;
        book << "# " << project_->title << "\n\n";
        int count = 0;
        for (const auto& ch : project_->outline.chapters) {
            if (ch.file_path.empty()) continue;
            std::string content = ProjectIO::readChapter(project_->path, ch.file_path);
            if (content.empty()) continue;
            book << "## " << ch.title << "\n\n" << content << "\n\n---\n\n";
            ++count;
        }

        json r;
        r["content"] = book.str();
        r["chapters"] = count;
        res.set_content(r.dump(), "application/json");
    });

    // ── CORS（允许前端跨域访问）──
    server_->Options("/api/.*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });
}

// ============================================================================
// 运行
// ============================================================================

void BackendServer::run() {
    server_ = std::make_unique<httplib::Server>();
    setupRoutes();

    writePortFile();

    running_ = true;
    spdlog::info("[BackendServer] 启动 HTTP+SSE 服务器 → http://localhost:{}", config_.port);

    server_->listen("localhost", config_.port);

    // listen() 返回后（调用了 stop()）
    running_ = false;
    removePortFile();
    spdlog::info("[BackendServer] 服务器已停止");
}

} // namespace server
