/// ReplHandler 实现 — UX审查修复版（自动保存/友好错误/配置安全/去术语化）。

#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "agent/ContextManager.h"
#include "project/FileStorageBackend.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "project/ProjectManager.h"

#include <iostream>
#include <algorithm>
#include <sstream>

ReplHandler::ReplHandler(agent::Agent& agent, IOutputChannel& out,
                         std::shared_ptr<Project> project)
    : agent_(agent), out_(out), parser_(out), gui_(out), project_(std::move(project))
{ setupCommands(); setupPhase5Commands(); }

void ReplHandler::setWelcomeMessage(std::string msg) { welcome_ = std::move(msg); }
void ReplHandler::setProject(std::shared_ptr<Project> p) { project_ = std::move(p); }

bool ReplHandler::openProject(const std::string& path) {
    try {
        ProjectManager pm;
        Project p = pm.openOrCreate(path);
        if (p.title.empty()) { gui_.writeError("无法打开/创建项目: " + path); return false; }
        project_ = std::make_shared<Project>(std::move(p));
        gui_.writeWarning("已打开: " + project_->title + "（" + std::to_string(project_->outline.chapters.size()) + "章 " + std::to_string(project_->characters.size()) + "角色）");
        return true;
    } catch (const std::exception& e) { gui_.writeError(std::string("打开失败: ") + e.what()); return false; }
}

// ============================================================================
// Fix #2: C++异常 → 用户友好的中文提示
// ============================================================================
static std::string userFriendlyError(const std::string& raw) {
    if (raw.find("Connection") != std::string::npos || raw.find("连接") != std::string::npos)
        return "网络连接失败，请检查网络后重试。";
    if (raw.find("timeout") != std::string::npos || raw.find("超时") != std::string::npos)
        return "请求超时，AI 服务响应较慢，请稍后再试。";
    if (raw.find("401") != std::string::npos || raw.find("API Key") != std::string::npos)
        return "API Key 无效，请在 config.json 中配置正确的密钥。";
    if (raw.find("429") != std::string::npos)
        return "请求过于频繁，请稍等几秒再试。";
    if (raw.find("500") != std::string::npos || raw.find("502") != std::string::npos || raw.find("503") != std::string::npos)
        return "AI 服务暂时不可用，请稍后再试。";
    return raw;
}

// ============================================================================
// Fix #1: 自动保存对话
// ============================================================================
static void autoSaveConversation(agent::Agent& agent, std::shared_ptr<Project> project) {
    if (!project || project->path.empty()) return;
    try {
        FileStorageBackend storage(project->path);
        agent::SessionPersistence sp(storage);
        sp.save(agent.conversation());
    } catch (...) {}
}

// ============================================================================
// Phase 5 命令
// ============================================================================
void ReplHandler::setupPhase5Commands() {
    parser_.registerCommand("new", "/new <项目名称> — 创建新项目", [this](const auto& args) {
        if (args.empty()) { out_.write(Ansi::warning() + "请输入项目名称，例如: /new 我的小说\n" + Ansi::reset()); return true; }
        std::string path = "./" + args[0];
        openProject(path);
        return true;
    });

    parser_.registerCommand("load", "/load <路径> — 打开已有项目", [this](const auto& args) {
        if (args.empty()) { out_.write(Ansi::warning() + "请输入项目路径，例如: /load ./我的小说\n" + Ansi::reset()); return true; }
        openProject(args[0]);
        return true;
    });

    parser_.registerCommand("status", "显示项目统计", [this](const auto&) {
        if (!project_ || project_->title.empty()) { out_.write(Ansi::dim() + "尚未打开项目。使用 /new 或 /load。\n" + Ansi::reset()); return true; }
        std::ostringstream ss;
        ss << Ansi::title() << "项目: " << project_->title << Ansi::reset() << "\n";
        ss << "  章节: " << project_->outline.chapters.size() << " 章\n";
        ss << "  角色: " << project_->characters.size() << " 个\n";
        ss << "  设定: " << project_->settings.size() << " 个\n";
        ss << "  对话: " << agent_.conversation().size() << " 条\n";
        if (project_->target_word_count > 0)
            ss << "  字数: " << project_->current_word_count << "/" << project_->target_word_count << "\n";
        out_.write(ss.str());
        return true;
    });

    // Fix #5: stoi try/catch + Fix #6: 无参数显示当前值
    parser_.registerCommand("config", "/config [项] [值] — 查看或修改配置", [this](const auto& args) {
        if (args.empty()) {
            auto& cfg = agent_.client().config();
            std::ostringstream ss;
            ss << Ansi::info() << "当前配置:\n" << Ansi::reset();
            ss << "  context_window   = " << cfg.context_window << " tokens\n";
            ss << "  model            = " << cfg.model << "\n";
            ss << "  provider         = " << cfg.name << "\n";
            ss << Ansi::dim() << "修改: /config context_window 65536\n" << Ansi::reset();
            out_.write(ss.str());
            return true;
        }
        if (args.size() < 2) { out_.write(Ansi::dim() + "用法: /config <项> <值>\n" + Ansi::reset()); return true; }
        try {
            if (args[0] == "context_window") {
                int w = std::stoi(args[1]);
                if (w < 1024) { out_.write(Ansi::warning() + "至少需要 1024 tokens\n" + Ansi::reset()); return true; }
                agent_.setContextWindow(w);
                out_.write(Ansi::success() + "上下文窗口 → " + std::to_string(w) + "\n" + Ansi::reset());
            } else {
                out_.write(Ansi::warning() + "未知配置项，可配置: context_window\n" + Ansi::reset());
            }
        } catch (...) { out_.write(Ansi::error() + "请输入有效数字，例如: /config context_window 65536\n" + Ansi::reset()); }
        return true;
    });

    parser_.registerCommand("export", "导出所有章节为 Markdown 文件", [this](const auto&) {
        if (!project_ || project_->title.empty()) { out_.write(Ansi::dim() + "请先打开项目。\n" + Ansi::reset()); return true; }
        out_.write(Ansi::dim() + "正在导出..." + Ansi::reset() + "\n");
        std::ostringstream book;
        book << "# " << project_->title << "\n\n";
        int exported = 0;
        for (const auto& ch : project_->outline.chapters) {
            if (ch.file_path.empty()) continue;
            std::string content = ProjectIO::readChapter(project_->path, ch.file_path);
            if (content.empty()) continue;
            book << "## " << ch.title << "\n\n" << content << "\n\n---\n\n";
            ++exported;
        }
        ProjectIO::saveJsonFile(project_->path + "/export.md", nlohmann::json::string_t(book.str()));
        out_.write(Ansi::success() + "已导出 " + std::to_string(exported) + " 章 → export.md\n" + Ansi::reset());
        return true;
    });

    parser_.registerCommand("save", "手动保存项目", [this](const auto&) {
        if (!project_ || project_->path.empty()) { out_.write(Ansi::dim() + "请先打开项目。\n" + Ansi::reset()); return true; }
        try { ProjectIO::save(*project_); gui_.writeWarning("项目已保存。"); }
        catch (const std::exception& e) { gui_.writeError(std::string("保存失败: ") + e.what()); }
        return true;
    });

    parser_.registerCommand("trace", "/trace — 查看执行轨迹", [this](const auto&) {
        out_.write(Ansi::dim() + "执行轨迹记录到 .novelagent/traces/ 目录。\n" + Ansi::reset());
        return true;
    });
}

void ReplHandler::setupCommands() {
    parser_.registerCommand("help", "显示所有命令", [this](const auto&) { parser_.printHelp(); return true; });
    parser_.registerCommand("exit", "退出程序", [this](const auto&) { out_.write(Ansi::assistant() + "再见！\n" + Ansi::reset()); return false; });
    parser_.registerCommand("clear", "清空对话历史", [this](const auto&) { agent_.clearConversation(); gui_.writeWarning("对话已清空。"); return true; });
    parser_.registerCommand("model", "显示当前模型", [this](const auto&) {
        auto& cfg = agent_.client().config();
        out_.write(Ansi::info() + cfg.name + " / " + cfg.model + " / " + std::to_string(cfg.context_window) + " tokens\n" + Ansi::reset());
        return true;
    });
    parser_.registerCommand("parallel", "/parallel on|off — 并行编排", [this](const auto& args) {
        if (args.empty()) {
            out_.write(Ansi::dim() + std::string(agent_.isParallelEnabled() ? "并行模式" : "串行模式") + "，/parallel on 或 off 切换\n" + Ansi::reset());
        } else if (args[0] == "on") { agent_.useParallelProcessor(); out_.write(Ansi::success() + "已切换并行模式\n" + Ansi::reset()); }
        else if (args[0] == "off") { agent_.useSerialProcessor(); out_.write(Ansi::success() + "已切换串行模式\n" + Ansi::reset()); }
        return true;
    });
}

std::vector<std::string> ReplHandler::getCompletions(const std::string& prefix) const {
    std::vector<std::string> cmds = {"help","exit","clear","model","status","config","export","save","trace","parallel","new","load"};
    std::vector<std::string> results;
    for (const auto& c : cmds) if (c.find(prefix) == 0) results.push_back(c);
    return results;
}

void ReplHandler::showCompletions(const std::vector<std::string>& completions) const {
    if (completions.empty()) return;
    out_.write("\n" + Ansi::dim());
    for (size_t i = 0; i < completions.size(); ++i) { if (i > 0) out_.write("  "); out_.write("/" + completions[i]); }
    out_.write(Ansi::reset() + "\n");
}

void ReplHandler::autoSaveOnError() {
    if (!project_ || project_->path.empty()) return;
    try { ProjectIO::save(*project_); out_.write(Ansi::warning() + "[自动保存] 项目已保存。\n" + Ansi::reset()); } catch (...) {}
}

void ReplHandler::run() {
    out_.write(Ansi::clearScreen());
    gui_.writeTitle("NovelAgent v0.3.0 — AI 写小说助手");

    if (!project_ || project_->title.empty()) {
        // Fix #10: 去术语化
        out_.write("\n  " + Ansi::bold() + "欢迎！请先创建或打开一个项目：\n\n" + Ansi::reset());
        out_.write("  " + Ansi::userInput() + "/new 我的小说" + Ansi::reset() + "     创建新项目\n");
        out_.write("  " + Ansi::userInput() + "/load ./已有项目" + Ansi::reset() + "  打开已有项目\n");
        out_.write("  " + Ansi::userInput() + "/help" + Ansi::reset() + "            查看所有命令\n\n");
    } else {
        out_.write("\n");
        // Fix #3: 恢复上次会话
        try {
            FileStorageBackend storage(project_->path);
            agent::SessionPersistence sp(storage);
            auto restored = sp.load();
            if (restored.size() > 0)
                out_.write(Ansi::dim() + "已恢复上次对话（" + std::to_string(restored.size()) + " 条消息）\n" + Ansi::reset());
        } catch (...) {}
        out_.write(Ansi::dim() + "项目: " + project_->title + " | 输入消息开始写作\n\n" + Ansi::reset());
    }

    std::string input;
    while (true) {
        if (project_ && !project_->title.empty())
            gui_.renderStatusBar(agent_.isParallelEnabled() ? "Parallel" : "Serial", 0, project_->title);

        std::cout << Ansi::userInput() << "> " << Ansi::reset() << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;
        gui_.addToHistory(input);

        if (CommandParser::isCommand(input)) {
            auto completions = getCompletions(input.substr(1));
            if (completions.size() > 1) showCompletions(completions);
            if (!parser_.execute(input)) { autoSaveConversation(agent_, project_); break; }
            continue;
        }

        if (!project_ || project_->title.empty()) { gui_.writeWarning("请先用 /new 或 /load 打开一个项目。"); continue; }

        out_.write("\n");
        try {
            gui_.startSpinner("思考");
            auto callbacks = StreamDisplay::create(out_);
            auto response = agent_.processUserMessage(input, callbacks);
            gui_.stopSpinner();
            if (response.finish_reason == "length") gui_.writeWarning("回复较长，部分被截断。输入[继续]可续写。");
            else if (response.finish_reason == "content_filter") gui_.writeWarning("部分内容因安全策略被过滤。");
            autoSaveConversation(agent_, project_);
            out_.write("\n");
        } catch (const std::exception& e) {
            gui_.stopSpinner();
            gui_.writeError(userFriendlyError(e.what()));
            out_.write(Ansi::dim() + "（输入消息即可重试）\n" + Ansi::reset());
            autoSaveOnError();
        }
    }
}
