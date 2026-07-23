// ReplHandler 实现 — UX审查修复版（自动保存/友好错误/配置安全/去术语化）。

#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "agent/index/IIndexService.h"
#include "project/FileStorageBackend.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "project/ProjectManager.h"
#include "agent/skill/ISkillProvider.h"

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
    if (raw.find("json.exception") != std::string::npos || raw.find("解析") != std::string::npos)
        return "API 响应解析失败，可能是 API Key 无效或网络异常。";
    return raw;
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
        ss << "  对话: " << agent_.memory().size() << " 条\n";
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
            ss << "  max_context_tokens = " << cfg.max_context_tokens << " tokens\n";
            ss << "  model              = " << cfg.model << "\n";
            ss << "  provider           = " << cfg.name << "\n";
            ss << Ansi::dim() << "修改: /config max_context_tokens 131072\n" << Ansi::reset();
            out_.write(ss.str());
            return true;
        }
        if (args.size() < 2) { out_.write(Ansi::dim() + "用法: /config <项> <值>\n" + Ansi::reset()); return true; }
        try {
            if (args[0] == "max_context_tokens") {
                int w = std::stoi(args[1]);
                if (w < 1024) { out_.write(Ansi::warning() + "至少需要 1024 tokens\n" + Ansi::reset()); return true; }
                auto budget = agent_.tokenBudget();
                budget.model_limit = w;
                agent_.setTokenBudget(budget);
                out_.write(Ansi::success() + "max_context_tokens → " + std::to_string(w) + "\n" + Ansi::reset());
            } else {
                out_.write(Ansi::warning() + "未知配置项，可配置: max_context_tokens\n" + Ansi::reset());
            }
        } catch (...) { out_.write(Ansi::error() + "请输入有效数字，例如: /config max_context_tokens 131072\n" + Ansi::reset()); }
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
        try { project_->markDirty(Project::DIRTY_ALL); ProjectIO::save(*project_); gui_.writeWarning("项目已保存。"); }
        catch (const std::exception& e) { gui_.writeError(std::string("保存失败: ") + e.what()); }
        return true;
    });

    parser_.registerCommand("trace", "/trace — 查看执行轨迹", [this](const auto&) {
        out_.write(Ansi::dim() + "执行轨迹记录到 .novelagent/traces/ 目录。\n" + Ansi::reset());
        return true;
    });

    parser_.registerCommand("index", "/index — 为项目内容建立向量索引（语义检索）", [this](const auto&) {
        if (!project_ || project_->path.empty()) {
            out_.write(Ansi::dim() + "请先打开项目。\n" + Ansi::reset());
            return true;
        }
        if (!index_service_) {
            out_.write(Ansi::error() + "内部错误：索引服务未设置。\n" + Ansi::reset());
            return true;
        }

        // Issue 6: 通过 IIndexService 接口调用，进度通过 lambda 输出
        auto result = index_service_->indexAll([this](const std::string& msg) {
            out_.write(Ansi::dim() + msg + "\n" + Ansi::reset());
        });

        if (!result.ok()) {
            if (result.error == "没有可索引的内容") {
                out_.write(Ansi::warning() + "没有可索引的内容。请先创建章节/角色/设定。\n" + Ansi::reset());
            } else {
                out_.write(Ansi::error() + "索引失败: " + result.error + "\n" + Ansi::reset());
            }
        }
        return true;
    });
}

void ReplHandler::setupCommands() {
    parser_.registerCommand("help", "显示所有命令", [this](const auto&) { parser_.printHelp(); return true; });
    parser_.registerCommand("exit", "退出程序", [this](const auto&) { out_.write(Ansi::assistant() + "再见！\n" + Ansi::reset()); return false; });
    parser_.registerCommand("clear", "重置整个会话（对话 + 上下文 + 压缩摘要）", [this](const auto&) { agent_.resetSession(); gui_.writeWarning("会话已重置（对话/上下文追踪/压缩摘要已清空）。"); return true; });
    parser_.registerCommand("model", "显示当前模型", [this](const auto&) {
        auto& cfg = agent_.client().config();
        out_.write(Ansi::info() + cfg.name + " / " + cfg.model + " / " + std::to_string(cfg.max_context_tokens) + " max_context_tokens\n" + Ansi::reset());
        return true;
    });

    // ── 上下文管理命令 ──
    parser_.registerCommand("context", "/context — 显示上下文用量明细", [this](const auto&) {
        auto& budget = agent_.tokenBudget();

        std::ostringstream ss;
        ss << Ansi::title() << "上下文用量\n" << Ansi::reset();
        ss << "  模型上限:   " << budget.model_limit << " tokens\n";
        ss << "  对话消息:   " << agent_.memory().size() << " 条\n";
        ss << "  压缩阈值:   " << budget.compact_pct << "%\n";

        // 显示当前警告
        auto warnings = agent_.contextWarnings();
        if (!warnings.empty()) {
            ss << Ansi::warning() << "  活跃警告:\n" << Ansi::reset();
            for (const auto& w : warnings) {
                ss << "    ⚠ " << w << "\n";
            }
        }

        auto pinned = agent_.memory().pinnedIndices();
        if (!pinned.empty()) {
            ss << "  保留消息:   " << pinned.size() << " 条 (索引:";
            for (size_t i = 0; i < pinned.size() && i < 10; ++i) {
                ss << " " << pinned[i];
            }
            if (pinned.size() > 10) ss << " ...";
            ss << ")\n";
        }
        out_.write(ss.str());
        return true;
    });

    parser_.registerCommand("compact", "/compact [焦点] — 压缩对话历史为摘要", [this](const auto& args) {
        std::optional<std::string> focus;
        if (!args.empty()) {
            std::ostringstream oss;
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) oss << " ";
                oss << args[i];
            }
            focus = oss.str();
        }

        out_.write(Ansi::dim() + "正在生成对话摘要..." + Ansi::reset() + "\n");
        auto result = agent_.compactConversation(focus);

        std::ostringstream ss;
        if (result.messages_compacted > 0) {
            ss << Ansi::success() << "上下文已压缩：" << result.messages_compacted
               << " 条消息 → 摘要（" << result.tokens_before << " → "
               << result.tokens_after << " tokens";
            if (result.tokens_before > 0) {
                int saved = (1.0 - static_cast<double>(result.tokens_after) / result.tokens_before) * 100;
                ss << "，节省 " << saved << "%";
            }
            ss << "）" << Ansi::reset() << "\n";
        } else {
            ss << Ansi::dim() << result.summary << Ansi::reset() << "\n";
        }
        out_.write(ss.str());
        return true;
    });

    parser_.registerCommand("pin", "/pin last|<N> — 保留消息不被截断", [this](const auto& args) {
        if (args.empty() || args[0] == "last") {
            auto& conv = agent_.memory();
            if (conv.size() == 0) {
                out_.write(Ansi::dim() + "对话为空，无消息可保留。\n" + Ansi::reset());
                return true;
            }
            size_t last = conv.size() - 1;
            if (agent_.pinMessage(last)) {
                out_.write(Ansi::success() + "已保留最后一条消息 (索引 " + std::to_string(last) + ")\n" + Ansi::reset());
            }
        } else {
            try {
                size_t idx = static_cast<size_t>(std::stoi(args[0]));
                if (agent_.pinMessage(idx)) {
                    out_.write(Ansi::success() + "已保留消息 #" + std::to_string(idx) + "\n" + Ansi::reset());
                } else {
                    out_.write(Ansi::warning() + "索引越界，当前共 " + std::to_string(agent_.memory().size()) + " 条消息\n" + Ansi::reset());
                }
            } catch (...) {
                out_.write(Ansi::error() + "用法: /pin last 或 /pin <数字>\n" + Ansi::reset());
            }
        }
        return true;
    });

    parser_.registerCommand("unpin", "/unpin <N> — 取消保留标记", [this](const auto& args) {
        if (args.empty()) {
            out_.write(Ansi::dim() + "用法: /unpin <索引>\n" + Ansi::reset());
            return true;
        }
        try {
            size_t idx = static_cast<size_t>(std::stoi(args[0]));
            if (agent_.unpinMessage(idx)) {
                out_.write(Ansi::success() + "已取消保留消息 #" + std::to_string(idx) + "\n" + Ansi::reset());
            } else {
                out_.write(Ansi::warning() + "索引越界。\n" + Ansi::reset());
            }
        } catch (...) {
            out_.write(Ansi::error() + "请输入有效数字。\n" + Ansi::reset());
        }
        return true;
    });

    parser_.registerCommand("edit", "/edit <N> <新内容> — 编辑指定消息", [this](const auto& args) {
        if (args.size() < 2) {
            out_.write(Ansi::dim() + "用法: /edit <索引> <新内容>\n" + Ansi::reset());
            return true;
        }
        try {
            size_t idx = static_cast<size_t>(std::stoi(args[0]));
            // 从 args[1] 开始拼接剩余内容（支持空格）
            std::ostringstream oss;
            for (size_t i = 1; i < args.size(); ++i) {
                if (i > 1) oss << " ";
                oss << args[i];
            }
            if (agent_.editMessage(idx, oss.str())) {
                out_.write(Ansi::success() + "已编辑消息 #" + std::to_string(idx) + "\n" + Ansi::reset());
            } else {
                out_.write(Ansi::warning() + "无法编辑消息 #" + std::to_string(idx)
                    + "（索引越界或消息类型不允许）\n" + Ansi::reset());
            }
        } catch (...) {
            out_.write(Ansi::error() + "用法: /edit <数字> <新内容>\n" + Ansi::reset());
        }
        return true;
    });

    parser_.registerCommand("rewind", "/rewind <N> — 回滚到指定消息", [this](const auto& args) {
        if (args.empty()) {
            // 无参数时显示可选的回滚点
            auto checkpoints = agent_.checkpointIndices();
            if (checkpoints.empty()) {
                out_.write(Ansi::dim() + "没有可回滚的消息。\n" + Ansi::reset());
            } else {
                std::ostringstream ss;
                ss << Ansi::info() << "可回滚的消息 (" << checkpoints.size() << " 个用户消息):\n" << Ansi::reset();
                auto all_msgs = agent_.memory().all();
                for (auto idx : checkpoints) {
                    const auto& msg = all_msgs[idx];
                    std::string preview = msg.content.substr(0, 80);
                    ss << "  [" << idx << "] " << preview;
                    if (msg.content.size() > 80) ss << "...";
                    ss << "\n";
                }
                out_.write(ss.str());
            }
            return true;
        }
        try {
            size_t idx = static_cast<size_t>(std::stoi(args[0]));
            if (agent_.rewindTo(idx)) {
                out_.write(Ansi::success() + "已回滚到消息 #" + std::to_string(idx)
                    + "（保留 " + std::to_string(agent_.memory().size()) + " 条）\n" + Ansi::reset());
            } else {
                out_.write(Ansi::warning() + "索引越界。\n" + Ansi::reset());
            }
        } catch (...) {
            out_.write(Ansi::error() + "用法: /rewind <数字> 或 /rewind 查看回滚点\n" + Ansi::reset());
        }
        return true;
    });

    parser_.registerCommand("pins", "/pins — 列出所有保留消息", [this](const auto&) {
        auto pinned = agent_.memory().pinnedIndices();
        if (pinned.empty()) {
            out_.write(Ansi::dim() + "没有保留消息。使用 /pin last 保留最近一条。\n" + Ansi::reset());
        } else {
            std::ostringstream ss;
            ss << Ansi::info() << "保留消息 (" << pinned.size() << " 条):\n" << Ansi::reset();
            auto all_msgs = agent_.memory().all();
            for (auto idx : pinned) {
                const auto& msg = all_msgs[idx];
                std::string role;
                switch (msg.role) {
                    case llm::MessageRole::User: role = "用户"; break;
                    case llm::MessageRole::Assistant: role = "助手"; break;
                    case llm::MessageRole::Tool: role = "工具"; break;
                    default: role = "?"; break;
                }
                std::string preview = msg.content.substr(0, 60);
                ss << "  [" << idx << "] " << role << ": " << preview;
                if (msg.content.size() > 60) ss << "...";
                ss << "\n";
            }
            out_.write(ss.str());
        }
        return true;
    });

    // 技能斜杠命令
    if (skill_provider_) {
        for (const auto& cmd : skill_provider_->getAllCommands()) {
            std::string skill_cmd_name = cmd.name;
            std::string help = "/" + cmd.name;
            if (!cmd.description.empty())
                help += " — " + cmd.description;

            parser_.registerCommand(cmd.name, help,
                [this, skill_cmd_name](const std::vector<std::string>& args) {
                    std::string input = "/" + skill_cmd_name;
                    for (const auto& a : args)
                        input += " " + a;
                    auto callbacks = StreamDisplay::create(out_);
                    agent_.process(input, callbacks);
                    out_.write("\n");
                    return true;
                });
        }
    }
}

std::vector<std::string> ReplHandler::getCompletions(const std::string& prefix) const {
    std::vector<std::string> cmds = {"help","exit","clear","model","status","config","export","save","trace","new","load","context","compact","pin","unpin","pins","rewind","edit"};
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
    try { project_->markDirty(Project::DIRTY_ALL); ProjectIO::save(*project_); out_.write(Ansi::warning() + "[自动保存] 项目已保存。\n" + Ansi::reset()); } catch (...) {}
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
        // Fix #3: 恢复上次会话（对话 + 元数据）
        try { agent_.loadSessionState(); } catch (...) {}
        if (agent_.memory().size() > 0)
            out_.write(Ansi::dim() + "已恢复上次会话（" + std::to_string(agent_.memory().size()) + " 条消息）\n" + Ansi::reset());
        out_.write(Ansi::dim() + "项目: " + project_->title + " | 输入消息开始写作\n\n" + Ansi::reset());
    }

    std::string input;
    while (true) {
        if (project_ && !project_->title.empty())
            gui_.renderStatusBar("", 0, project_->title);

        std::cout << Ansi::userInput() << "> " << Ansi::reset() << std::flush;
        if (!std::getline(std::cin, input)) {
            if (std::cin.eof()) break;  // Ctrl+D → 退出
            std::cin.clear();           // Ctrl+C → 清空错误状态，继续循环
            out_.write("\n" + Ansi::dim() + "按 Ctrl+C 以取消 AI 生成" + Ansi::reset() + "\n");
            continue;
        }
        if (input.empty()) continue;
        gui_.addToHistory(input);

        if (CommandParser::isCommand(input)) {
            auto completions = getCompletions(input.substr(1));
            if (completions.size() > 1) showCompletions(completions);
            if (!parser_.execute(input)) { agent_.saveSessionState(); break; }
            continue;
        }

        if (!project_ || project_->title.empty()) { gui_.writeWarning("请先用 /new 或 /load 打开一个项目。"); continue; }

        out_.write("\n");
        try {
            gui_.startSpinner("思考");
            auto callbacks = StreamDisplay::create(out_);
            auto response = agent_.process(input, callbacks);
            gui_.stopSpinner();
            if (response.finish_reason == "length") gui_.writeWarning("回复较长，部分被截断。输入[继续]可续写。");
            else if (response.finish_reason == "content_filter") gui_.writeWarning("部分内容因安全策略被过滤。");

            // ── 上下文警告展示 ──
            auto warnings = agent_.contextWarnings();
            for (const auto& w : warnings) {
                out_.write(Ansi::warning() + "⚠ " + w + Ansi::reset() + "\n");
            }

            agent_.saveSessionState();
            out_.write("\n");
        } catch (const std::exception& e) {
            gui_.stopSpinner();
            agent_.tracer().record("error", 0, 0,
                agent::ErrorPayload{.reason = e.what()});
            gui_.writeError(userFriendlyError(e.what()));
            out_.write(Ansi::dim() + "（输入消息即可重试）\n" + Ansi::reset());
            autoSaveOnError();
        }
    }
}
