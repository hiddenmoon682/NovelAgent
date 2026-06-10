/// BackendServer 实现 — HTTP+SSE 多会话服务器（审查修复版）。

#include "server/BackendServer.h"
#include "server/SSEQueue.h"

#include "project/Models.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <thread>

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
    if (!f) {
        spdlog::error("[BackendServer] 无法写入端口文件: {}", path);
        return;
    }
    f << config_.port << "\n";
    f.close();
    if (!f.good()) {
        spdlog::error("[BackendServer] 端口文件写入失败: {}", path);
    } else {
        spdlog::info("[BackendServer] 端口文件: {} (port={})", path, config_.port);
    }
}

void BackendServer::removePortFile() const {
    std::string path = portFilePath();
    if (path.empty()) return;
    utils::file::removeFile(path);
}

// ============================================================================
// 路由设置
// ============================================================================

void BackendServer::setupRoutes() {
    server_->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        json r;
        r["status"] = "ok";
        r["sessions"] = session_mgr_.sessionCount();
        r["clients"] = active_clients_.load();
        res.set_content(r.dump(), "application/json");
    });

    server_->Post("/api/session/create", [this](const httplib::Request&, httplib::Response& res) {
        std::string sid = session_mgr_.createSession();
        json r;
        r["session_id"] = sid;
        res.set_content(r.dump(), "application/json");
        active_clients_++;
    });

    server_->Post("/api/session/destroy", [this](const httplib::Request& req, httplib::Response& res) {
        auto j = json::parse(req.body);
        std::string sid = j.value("session_id", "");
        session_mgr_.destroySession(sid);
        active_clients_--;
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    server_->Get("/api/session/list", [this](const httplib::Request&, httplib::Response& res) {
        auto ids = session_mgr_.activeSessions();
        json r = json::array();
        for (const auto& id : ids) r.push_back(id);
        res.set_content(r.dump(), "application/json");
    });

    // ── 聊天（SSE 流式，使用 shared_ptr<Session> 防止 use-after-free）──
    server_->Post("/api/chat", [this](const httplib::Request& req, httplib::Response& res) {
        auto j = json::parse(req.body);
        std::string sid = j.value("session_id", "");
        std::string message = j.value("message", "");

        if (sid.empty() || message.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"缺少 session_id 或 message\"}", "application/json");
            return;
        }

        // Fix #1: 使用 shared_ptr<Session> 防止在 LLM 调用期间被销毁
        auto session = session_mgr_.getSession(sid);
        if (!session || !session->agent) {
            res.status = 404;
            res.set_content("{\"error\":\"会话不存在\"}", "application/json");
            return;
        }

        auto queue = std::make_shared<SSEQueue>();
        auto done_flag = std::make_shared<bool>(false);

        // Lambda 持有 shared_ptr<Session>，确保 Agent 生命周期
        auto llm_thread = std::make_shared<std::thread>(
            [session, message, queue, done_flag]() {
                llm::StreamCallbacks cb;

                cb.on_content = [queue](const std::string& delta) {
                    json e;
                    e["type"] = "content";
                    e["delta"] = delta;
                    queue->push(sseLine(e.dump()));
                };
                cb.on_reasoning = [queue](const std::string& delta) {
                    json e;
                    e["type"] = "reasoning";
                    e["delta"] = delta;
                    queue->push(sseLine(e.dump()));
                };
                cb.on_tool_call_start = [queue]() {
                    json e;
                    e["type"] = "tool_call_start";
                    queue->push(sseLine(e.dump()));
                };
                cb.on_complete = [queue](const llm::LLMResponse& resp) {
                    json e;
                    e["type"] = "done";
                    e["tokens"] = resp.total_tokens;
                    e["finish_reason"] = resp.finish_reason;
                    queue->push(sseLine(e.dump()));
                };
                cb.on_error = [queue](const std::string& err) {
                    json e;
                    e["type"] = "error";
                    e["message"] = err;
                    queue->push(sseLine(e.dump()));
                };

                session->agent->processUserMessage(message, cb);
                *done_flag = true;
            });

        res.set_chunked_content_provider(
            "text/event-stream",
            [queue, done_flag, llm_thread](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                std::string data;
                int waited = 0;
                while (!*done_flag && !queue->pop(data)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    waited += 10;
                    if (waited > 100) {
                        sink.os << ": heartbeat\n\n";
                        waited = 0;
                    }
                }

                if (queue->pop(data)) {
                    sink.os << data;
                    return true;
                }

                if (*done_flag) {
                    sink.os << "data: [DONE]\n\n";
                    if (llm_thread->joinable()) llm_thread->join();
                    return false;
                }

                return true;
            });

        res.set_header("Access-Control-Allow-Origin", "*");
    });

    // ── 单次执行 ──
    server_->Post("/api/execute", [this](const httplib::Request& req, httplib::Response& res) {
        auto j = json::parse(req.body);
        std::string command = j.value("command", "");
        if (command.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"缺少 command\"}", "application/json");
            return;
        }
        agent::Agent tempAgent(client_, registry_);
        auto response = tempAgent.execute(command);
        json r;
        r["content"] = response.content;
        r["tokens"] = response.total_tokens;
        res.set_content(r.dump(), "application/json");
    });

    server_->Get("/api/project/status", [this](const httplib::Request&, httplib::Response& res) {
        json r;
        if (project_) {
            r["title"] = project_->title;
            r["chapters"] = project_->outline.chapters.size();
            r["characters"] = project_->characters.size();
            r["settings"] = project_->settings.size();
            r["status"] = project_->status;
        } else {
            r["error"] = "未打开项目";
        }
        res.set_content(r.dump(), "application/json");
    });

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
}

void BackendServer::run() {
    server_ = std::make_unique<httplib::Server>();
    setupRoutes();
    writePortFile();
    running_ = true;
    spdlog::info("[BackendServer] 启动 HTTP+SSE 服务器 → http://localhost:{}", config_.port);
    server_->listen("localhost", config_.port);
    running_ = false;
    removePortFile();
    spdlog::info("[BackendServer] 服务器已停止");
}

} // namespace server
