/// BackendServer 实现 — 网络审查修复版（#1~#7全部修复）+ Phase 4 线程安全（工厂模式会话隔离）。

#include "server/BackendServer.h"
#include "server/SSEQueue.h"

#include "llm/LLMClientFactory.h"
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
    llm::LLMClientFactory& factory, agent::ToolRegistry& registry,
    std::shared_ptr<Project> project, const ServerConfig& config)
    : factory_(factory), registry_(registry), project_(std::move(project)),
      config_(config), session_mgr_(factory_, registry_, project_)
{}

BackendServer::~BackendServer() { stop(); }

void BackendServer::stop() {
    running_ = false;
    if (server_) server_->stop();
    if (server_thread_.joinable()) server_thread_.join();
    removePortFile();
}

// Fix #4: 连接上限检查
bool BackendServer::checkClientLimit(httplib::Response& res) {
    if (active_clients_.load() >= config_.max_clients) {
        res.status = 503;
        res.set_content("{\"error\":\"服务器繁忙，请稍后再试\"}", "application/json");
        return false;
    }
    return true;
}

// Fix #5: 请求体大小检查
bool BackendServer::checkBodySize(const std::string& body, httplib::Response& res) const {
    if (body.size() > config_.max_body_bytes) {
        res.status = 413;
        res.set_content("{\"error\":\"请求体过大\"}", "application/json");
        return false;
    }
    return true;
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
    if (!f) { spdlog::error("[BackendServer] 无法写入端口文件: {}", path); return; }
    f << config_.port << "\n";
    f.close();
    if (!f.good()) spdlog::error("[BackendServer] 端口文件写入失败: {}", path);
    else spdlog::info("[BackendServer] 端口文件: {} (port={})", path, config_.port);
}

void BackendServer::removePortFile() const {
    std::string path = portFilePath();
    if (path.empty()) return;
    utils::file::removeFile(path);
}

void BackendServer::setupRoutes() {
    // ── 全局 CORS 中间件 ──
    // 为所有 API 路由添加跨域头，并处理 OPTIONS 预检请求
    server_->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        // OPTIONS 预检请求直接返回 204，不进入后续路由处理
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ── 桌面窗口前端页面 ──
    server_->Get("/", [](const httplib::Request&, httplib::Response& res) {
        char buf[MAX_PATH];
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string dir(buf);
        auto pos = dir.rfind('\\'); if (pos != std::string::npos) dir = dir.substr(0, pos);
        pos = dir.rfind('\\'); if (pos != std::string::npos) dir = dir.substr(0, pos);
        std::ifstream f(dir + "\\tui-web\\index.html");
        if (f) { std::stringstream ss; ss << f.rdbuf(); res.set_content(ss.str(), "text/html; charset=utf-8"); }
        else { res.status = 404; res.set_content("frontend not found", "text/plain"); }
    });

    server_->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        json r;
        r["status"] = "ok";
        r["sessions"] = session_mgr_.sessionCount();
        r["clients"] = active_clients_.load();
        res.set_content(r.dump(), "application/json");
    });

    server_->Post("/api/session/create", [this](const httplib::Request& req, httplib::Response& res) {
        // Fix #4: 并发上限
        if (!checkClientLimit(res)) return;

        // Fix #5: body 大小检查
        if (!checkBodySize(req.body, res)) return;

        std::string sid = session_mgr_.createSession();
        json r;
        r["session_id"] = sid;
        res.set_content(r.dump(), "application/json");
        active_clients_++;
    });

    server_->Post("/api/session/destroy", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkBodySize(req.body, res)) return;
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

    // ── 聊天（SSE 流式）──
    server_->Post("/api/chat", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkBodySize(req.body, res)) return;

        auto j = json::parse(req.body);
        std::string sid = j.value("session_id", "");
        std::string message = j.value("message", "");

        if (sid.empty() || message.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"缺少 session_id 或 message\"}", "application/json");
            return;
        }

        auto session = session_mgr_.getSession(sid);
        if (!session || !session->agent) {
            res.status = 404;
            res.set_content("{\"error\":\"会话不存在\"}", "application/json");
            return;
        }

        // Fix #2: 带取消检测的有界队列
        auto queue = std::make_shared<SSEQueue>();
        auto done_flag = std::make_shared<std::atomic<bool>>(false);

        auto llm_thread = std::make_shared<std::thread>(
            [session, message, queue, done_flag]() {
                // 会话级互斥锁：同一会话的并发 /api/chat 请求串行化，
                // 防止对 Agent 内部状态（conversation/tracer/LLMClient）的数据竞争
                std::lock_guard<std::mutex> lock(session->request_mutex);

                llm::StreamCallbacks cb;
                bool cb_error_called = false;

                cb.on_content = [queue](const std::string& delta) {
                    json e;
                    e["type"] = "content";
                    e["delta"] = jsonEscapeNewlines(delta);
                    queue->push(sseLine(e.dump()));
                };
                cb.on_reasoning = [queue](const std::string& delta) {
                    json e;
                    e["type"] = "reasoning";
                    e["delta"] = jsonEscapeNewlines(delta);
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
                cb.on_error = [queue, &cb_error_called](const std::string& err) {
                    cb_error_called = true;
                    json e;
                    e["type"] = "error";
                    e["message"] = jsonEscapeNewlines(err);
                    queue->push(sseLine(e.dump()));
                };

                try {
                    if (!queue->cancelled.load())
                        session->agent->processUserMessage(message, cb);
                } catch (const std::exception& e) {
                    spdlog::error("[BackendServer] LLM异常: {}", e.what());
                    if (!cb_error_called) {
                        json err;
                        err["type"] = "error";
                        err["message"] = jsonEscapeNewlines(e.what());
                        queue->push(sseLine(err.dump()));
                    }
                } catch (...) {
                    spdlog::error("[BackendServer] LLM未知异常");
                    if (!cb_error_called) {
                        json err;
                        err["type"] = "error";
                        err["message"] = "AI 服务调用失败，请稍后重试。";
                        queue->push(sseLine(err.dump()));
                    }
                }
                done_flag->store(true);
            });

        res.set_chunked_content_provider(
            "text/event-stream",
            [queue, done_flag, llm_thread](size_t /*offset*/, httplib::DataSink& sink) -> bool {
                std::string data;
                // 等待数据或完成信号
                while (!done_flag->load() && !queue->pop_wait(data, 100)) {
                    sink.os << ": heartbeat\n\n";
                    sink.os.flush();  // 确保心跳立即发送
                }

                // pop_wait 成功 → 发送已获取的数据
                if (!data.empty()) {
                    sink.os << data;
                    sink.os.flush();
                }

                // 非阻塞排空剩余事件
                while (queue->pop(data)) {
                    sink.os << data;
                    sink.os.flush();
                }

                // LLM 线程完成且队列已空 → 发送结束信号
                if (done_flag->load()) {
                    sink.os << "data: [DONE]\n\n";
                    sink.os.flush();
                    if (llm_thread->joinable()) llm_thread->join();
                    return false;
                }

                return true;
            },
            // Fix #2: 资源释放器 — 客户端断开时取消 LLM 线程
            [queue, done_flag, llm_thread](bool /*success*/) {
                queue->cancelled.store(true);
                done_flag->store(true); // 唤醒 provider 线程
                if (llm_thread->joinable()) llm_thread->join();
            });

    });

    // ── 单次执行 ──
    server_->Post("/api/execute", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkBodySize(req.body, res)) return;
        auto j = json::parse(req.body);
        std::string command = j.value("command", "");
        if (command.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"缺少 command\"}", "application/json");
            return;
        }
        agent::Agent tempAgent(factory_, registry_);
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

    // ── 章节列表（供 GUI 侧边栏使用）──
    server_->Get("/api/project/chapters", [this](const httplib::Request&, httplib::Response& res) {
        if (!project_) {
            res.set_content("{\"error\":\"未打开项目\"}", "application/json");
            return;
        }
        json chapters = json::array();
        for (const auto& ch : project_->outline.chapters) {
            json item;
            item["id"] = ch.id;
            item["title"] = ch.title;
            item["order"] = ch.order;
            item["synopsis"] = ch.synopsis;
            item["status"] = ch.status;
            item["scenes_count"] = ch.scenes.size();
            item["pov_characters"] = ch.pov_characters;
            chapters.push_back(item);
        }
        res.set_content(chapters.dump(), "application/json");
    });

    // ── 角色列表（供 GUI 侧边栏使用）──
    server_->Get("/api/project/characters", [this](const httplib::Request&, httplib::Response& res) {
        if (!project_) {
            res.set_content("{\"error\":\"未打开项目\"}", "application/json");
            return;
        }
        json characters = json::array();
        for (const auto& c : project_->characters) {
            json item;
            item["id"] = c.id;
            item["name"] = c.name;
            item["role"] = c.role;
            item["traits"] = c.traits;
            item["appearances_count"] = c.chapter_appearances.size();
            characters.push_back(item);
        }
        res.set_content(characters.dump(), "application/json");
    });
}

void BackendServer::run() {
    server_ = std::make_unique<httplib::Server>();

    // Fix #1: 网络层配置
    server_->set_read_timeout(config_.read_timeout_sec, 0);
    server_->set_write_timeout(config_.write_timeout_sec, 0);
    server_->set_idle_interval(config_.idle_interval_sec, 0);
    server_->set_keep_alive_timeout(config_.keep_alive_timeout_sec);
    server_->set_keep_alive_max_count(config_.keep_alive_max_count);
    server_->set_payload_max_length(config_.payload_max_bytes);

    setupRoutes();
    writePortFile();
    running_ = true;
    spdlog::info("[BackendServer] 启动 HTTP+SSE 服务器 → http://localhost:{} "
                 "(read_to={}s write_to={}s keepalive={}s max_clients={})",
                 config_.port, config_.read_timeout_sec, config_.write_timeout_sec,
                 config_.keep_alive_timeout_sec, config_.max_clients);
    server_->listen("localhost", config_.port);
    running_ = false;
    removePortFile();
    spdlog::info("[BackendServer] 服务器已停止");
}

} // namespace server
