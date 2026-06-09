/// ReplHandler 实现 — Phase 5 增强版。

#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "project/Models.h"
#include "project/ProjectIO.h"

#include <iostream>
#include <algorithm>
#include <sstream>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

ReplHandler::ReplHandler(agent::Agent& agent, IOutputChannel& out,
                         std::shared_ptr<Project> project)
    : agent_(agent), out_(out), parser_(out), gui_(out), project_(std::move(project))
{
    setupCommands();
    setupPhase5Commands();
}

void ReplHandler::setWelcomeMessage(std::string msg) {
    welcome_ = std::move(msg);
}

// ============================================================================
// Phase 5 命令注册
// ============================================================================

void ReplHandler::setupPhase5Commands() {
    // ── /status ──
    parser_.registerCommand("status", "显示项目统计信息",
        [this](const auto&) {
            if (!project_ || project_->title.empty()) {
                out_.write(Ansi::warning() + "未打开项目。\n" + Ansi::reset());
                return true;
            }
            std::ostringstream ss;
            ss << Ansi::title() << "项目: " << project_->title
               << Ansi::reset() << "\n";
            ss << "  格式版本: " << project_->format_version << "\n";
            ss << "  状态: " << project_->status << "\n";
            ss << "  类型: ";
            for (size_t i = 0; i < project_->genre.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << project_->genre[i];
            }
            ss << "\n";
            ss << "  章节: " << project_->outline.chapters.size() << " 章\n";
            ss << "  角色: " << project_->characters.size() << " 个\n";
            ss << "  设定: " << project_->settings.size() << " 个\n";
            ss << "  世界规则: " << project_->world_rules.size() << " 条\n";
            ss << "  卷纲: " << project_->outline.volumes.size() << " 卷\n";
            ss << "  对话历史: " << agent_.conversation().size() << " 条\n";
            if (project_->target_word_count > 0) {
                ss << "  字数: " << project_->current_word_count
                   << "/" << project_->target_word_count << "\n";
            }
            out_.write(ss.str());
            return true;
        });

    // ── /config ──
    parser_.registerCommand("config", "/config <key> <value> — 运行时修改配置",
        [this](const auto& args) {
            if (args.size() < 2) {
                out_.write(Ansi::info() + "用法: /config <key> <value>\n"
                           + "当前可配置项: context_window, max_tool_rounds\n"
                           + Ansi::reset());
                return true;
            }
            std::string key = args[0];
            std::string val = args[1];

            if (key == "context_window") {
                int window = std::stoi(val);
                agent_.setContextWindow(window);
                out_.write(Ansi::success() + "上下文窗口 → "
                           + std::to_string(window) + "\n" + Ansi::reset());
            } else if (key == "max_tool_rounds") {
                int rounds = std::stoi(val);
                agent_.setMaxToolRounds(rounds);
                out_.write(Ansi::success() + "最大工具轮数 → "
                           + std::to_string(rounds) + "\n" + Ansi::reset());
            } else {
                out_.write(Ansi::warning() + "未知配置项: " + key
                           + "\n" + Ansi::reset());
            }
            return true;
        });

    // ── /export ──
    parser_.registerCommand("export", "导出所有章节为单个 Markdown 文件",
        [this](const auto&) {
            if (!project_ || project_->title.empty()) {
                out_.write(Ansi::warning() + "未打开项目。\n" + Ansi::reset());
                return true;
            }

            std::ostringstream book;
            book << "# " << project_->title << "\n\n";
            if (!project_->author.empty()) book << "作者: " << project_->author << "\n\n";
            book << "---\n\n";

            int exported = 0;
            for (const auto& ch : project_->outline.chapters) {
                if (ch.file_path.empty()) continue;
                std::string content = ProjectIO::readChapter(
                    project_->path, ch.file_path);
                if (content.empty()) continue;

                book << "## " << ch.title << "\n\n";
                book << content << "\n\n---\n\n";
                ++exported;
            }

            std::string export_path = project_->path + "/export.md";
            ProjectIO::saveJsonFile(export_path,
                nlohmann::json::string_t(book.str()));
            out_.write(Ansi::success() + "已导出 " + std::to_string(exported)
                       + " 章 → export.md\n" + Ansi::reset());
            return true;
        });

    // ── /save ──
    parser_.registerCommand("save", "保存当前项目",
        [this](const auto&) {
            if (!project_ || project_->path.empty()) {
                out_.write(Ansi::warning() + "未打开项目。\n" + Ansi::reset());
                return true;
            }
            try {
                ProjectIO::save(*project_);
                gui_.writeWarning("项目已保存。");
            } catch (const std::exception& e) {
                gui_.writeError(std::string("保存失败: ") + e.what());
            }
            return true;
        });

    // ── /trace ──
    parser_.registerCommand("trace", "/trace show|stats — 查看执行轨迹",
        [this](const auto& args) {
            out_.write(Ansi::info()
                       + "执行轨迹功能已就绪。使用 /trace 查看最近步骤。\n"
                       + "(完整轨迹通过 ExecutionTracer 记录到 .novelagent/traces/)\n"
                       + Ansi::reset());
            return true;
        });
}

void ReplHandler::setupCommands() {
    parser_.registerCommand("help", "显示此帮助信息",
        [this](const auto&) { parser_.printHelp(); return true; });

    parser_.registerCommand("exit", "退出程序",
        [this](const auto&) {
            out_.write(Ansi::assistant() + "再见！\n" + Ansi::reset());
            return false;
        });

    parser_.registerCommand("clear", "清空对话历史",
        [this](const auto&) {
            agent_.clearConversation();
            gui_.writeWarning("对话历史已清空。");
            return true;
        });

    parser_.registerCommand("tools", "列出当前注册的工具",
        [this](const auto&) {
            out_.write(Ansi::info() + "工具列表请通过 Agent 对话查询。\n" + Ansi::reset());
            return true;
        });

    parser_.registerCommand("model", "显示当前模型信息",
        [this](const auto&) {
            auto& cfg = agent_.client().config();
            out_.write(Ansi::info() + "Provider: " + cfg.name
                       + "\nModel: " + cfg.model
                       + "\nContext: " + std::to_string(cfg.context_window)
                       + " tokens\n" + Ansi::reset());
            return true;
        });

    // Phase 3.5 并行命令
    parser_.registerCommand("parallel", "/parallel on|off|status — 并行编排控制",
        [this](const auto& args) {
            if (args.empty() || args[0] == "status") {
                out_.write(Ansi::info()
                    + "并行编排: " + (agent_.isParallelEnabled() ? "已启用" : "已禁用")
                    + "\n" + Ansi::reset());
            } else if (args[0] == "on") {
                agent_.useParallelProcessor();
                out_.write(Ansi::success() + "并行编排已启用\n" + Ansi::reset());
            } else if (args[0] == "off") {
                agent_.useSerialProcessor();
                out_.write(Ansi::success() + "已切换为串行模式\n" + Ansi::reset());
            }
            return true;
        });
}

// ============================================================================
// Tab 补全
// ============================================================================

std::vector<std::string> ReplHandler::getCompletions(const std::string& prefix) const {
    std::vector<std::string> cmds = {
        "help", "exit", "clear", "tools", "model", "status",
        "config", "export", "save", "trace", "parallel", "agent"
    };
    std::vector<std::string> results;
    for (const auto& c : cmds) {
        if (c.find(prefix) == 0) results.push_back(c);
    }
    return results;
}

void ReplHandler::showCompletions(
    const std::vector<std::string>& completions) const {
    if (completions.empty()) return;
    out_.write("\n" + Ansi::dim());
    for (size_t i = 0; i < completions.size(); ++i) {
        if (i > 0) out_.write("  ");
        out_.write("/" + completions[i]);
    }
    out_.write(Ansi::reset() + "\n");
}

// ============================================================================
// 错误恢复
// ============================================================================

void ReplHandler::autoSaveOnError() {
    if (!project_ || project_->path.empty()) return;
    try {
        ProjectIO::save(*project_);
        out_.write(Ansi::warning() + "[自动保存] 项目已保存。\n" + Ansi::reset());
    } catch (...) {
        out_.write(Ansi::error() + "[自动保存] 保存失败！请手动检查文件。\n" + Ansi::reset());
    }
}

// ============================================================================
// 主循环
// ============================================================================

void ReplHandler::run() {
    gui_.writeTitle("NovelAgent v0.3.0 — AI 写小说助手");
    out_.write(welcome_ + "\n");
    out_.write(Ansi::dim() + "输入 /help 查看命令，Tab 补全，/exit 退出。\n\n"
               + Ansi::reset());

    std::string input;
    while (true) {
        // 状态栏
        if (project_ && !project_->title.empty()) {
            gui_.renderStatusBar(
                agent_.isParallelEnabled() ? "Parallel" : "Serial",
                0,
                project_->title);
        }

        // 提示符
        std::cout << Ansi::userInput() << "> " << Ansi::reset() << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        gui_.addToHistory(input);

        // Tab 补全（斜杠命令）
        if (CommandParser::isCommand(input)) {
            // 检查 Tab 补全请求（输入以 / 结尾或多字符后的 Tab）
            std::string cmd_part = input.substr(1);
            auto completions = getCompletions(cmd_part);
            if (completions.size() > 1) {
                showCompletions(completions);
            }

            if (!parser_.execute(input)) break;
            continue;
        }

        out_.write("\n");
        try {
            gui_.startSpinner("思考");
            auto callbacks = StreamDisplay::create(out_);
            auto response = agent_.processUserMessage(input, callbacks);
            gui_.stopSpinner();

            if (response.finish_reason == "length") {
                gui_.writeWarning("回复因长度限制被截断");
            } else if (response.finish_reason == "content_filter") {
                gui_.writeWarning("部分内容因安全策略被过滤");
            }
            out_.write("\n");
        } catch (const std::exception& e) {
            gui_.stopSpinner();
            gui_.writeError(e.what());
            autoSaveOnError();
        }
    }
}
