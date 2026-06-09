/// ReplHandler 实现 — Phase 5 增强版（Claude Code 风格交互）。

#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "project/Models.h"
#include "project/ProjectIO.h"
#include "project/ProjectManager.h"

#include <iostream>
#include <algorithm>
#include <sstream>

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

void ReplHandler::setProject(std::shared_ptr<Project> p) {
    project_ = std::move(p);
}

bool ReplHandler::openProject(const std::string& path) {
    try {
        ProjectManager pm;
        Project p = pm.openOrCreate(path);
        if (p.title.empty()) {
            gui_.writeError("无法打开/创建项目: " + path);
            return false;
        }
        project_ = std::make_shared<Project>(std::move(p));
        gui_.writeWarning("已打开项目: " + project_->title
            + "（章节: " + std::to_string(project_->outline.chapters.size())
            + "，角色: " + std::to_string(project_->characters.size()) + "）");
        return true;
    } catch (const std::exception& e) {
        gui_.writeError(std::string("打开项目失败: ") + e.what());
        return false;
    }
}

// ============================================================================
// Phase 5 命令
// ============================================================================

void ReplHandler::setupPhase5Commands() {
    // ── /new ──
    parser_.registerCommand("new", "/new <项目名称> — 创建新项目",
        [this](const auto& args) {
            if (args.empty()) {
                out_.write(Ansi::warning() + "用法: /new <项目名称>\n" + Ansi::reset());
                return true;
            }
            std::string path = "./" + args[0];
            if (openProject(path)) {
                gui_.writeWarning("项目已创建，开始写作吧！");
            }
            return true;
        });

    // ── /load ──
    parser_.registerCommand("load", "/load <路径> — 打开已有项目",
        [this](const auto& args) {
            if (args.empty()) {
                out_.write(Ansi::warning() + "用法: /load <项目目录路径>\n" + Ansi::reset());
                return true;
            }
            openProject(args[0]);
            return true;
        });

    // ── /status ──
    parser_.registerCommand("status", "显示项目统计信息",
        [this](const auto&) {
            if (!project_ || project_->title.empty()) {
                out_.write(Ansi::dim() + "尚未打开项目。使用 /new 或 /load。\n" + Ansi::reset());
                return true;
            }
            std::ostringstream ss;
            ss << Ansi::title() << "项目: " << project_->title
               << Ansi::reset() << "\n";
            ss << "  章节: " << project_->outline.chapters.size() << " 章\n";
            ss << "  角色: " << project_->characters.size() << " 个\n";
            ss << "  设定: " << project_->settings.size() << " 个\n";
            ss << "  卷纲: " << project_->outline.volumes.size() << " 卷\n";
            ss << "  对话: " << agent_.conversation().size() << " 条\n";
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
                out_.write(Ansi::dim() + "用法: /config <key> <value>\n"
                           + "可配置: context_window, max_tool_rounds\n" + Ansi::reset());
                return true;
            }
            if (args[0] == "context_window") {
                int w = std::stoi(args[1]);
                agent_.setContextWindow(w);
                out_.write(Ansi::success() + "上下文窗口 → " + std::to_string(w) + "\n" + Ansi::reset());
            } else if (args[0] == "max_tool_rounds") {
                int r = std::stoi(args[1]);
                agent_.setMaxToolRounds(r);
                out_.write(Ansi::success() + "最大工具轮数 → " + std::to_string(r) + "\n" + Ansi::reset());
            } else {
                out_.write(Ansi::warning() + "未知配置项: " + args[0] + "\n" + Ansi::reset());
            }
            return true;
        });

    // ── /export ──
    parser_.registerCommand("export", "导出所有章节为单个 Markdown 文件",
        [this](const auto&) {
            if (!project_ || project_->title.empty()) {
                out_.write(Ansi::dim() + "请先打开项目。\n" + Ansi::reset());
                return true;
            }
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
            ProjectIO::saveJsonFile(project_->path + "/export.md",
                nlohmann::json::string_t(book.str()));
            out_.write(Ansi::success() + "已导出 " + std::to_string(exported) + " 章 → export.md\n" + Ansi::reset());
            return true;
        });

    // ── /save ──
    parser_.registerCommand("save", "保存当前项目",
        [this](const auto&) {
            if (!project_ || project_->path.empty()) {
                out_.write(Ansi::dim() + "请先打开项目。\n" + Ansi::reset());
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
        [this](const auto&) {
            out_.write(Ansi::dim()
                       + "执行轨迹记录到 .novelagent/traces/ 目录。\n"
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

    parser_.registerCommand("model", "显示当前模型信息",
        [this](const auto&) {
            auto& cfg = agent_.client().config();
            out_.write(Ansi::info() + "Provider: " + cfg.name
                       + "\nModel: " + cfg.model
                       + "\nContext: " + std::to_string(cfg.context_window)
                       + " tokens\n" + Ansi::reset());
            return true;
        });

    parser_.registerCommand("parallel", "/parallel on|off — 并行编排开关",
        [this](const auto& args) {
            if (args.empty() || args[0] == "status") {
                out_.write(Ansi::dim() + "并行: "
                    + (agent_.isParallelEnabled() ? "开启" : "关闭")
                    + "\n" + Ansi::reset());
            } else if (args[0] == "on") {
                agent_.useParallelProcessor();
                out_.write(Ansi::success() + "并行编排已开启\n" + Ansi::reset());
            } else if (args[0] == "off") {
                agent_.useSerialProcessor();
                out_.write(Ansi::success() + "已切换串行模式\n" + Ansi::reset());
            }
            return true;
        });
}

// ============================================================================
// Tab 补全
// ============================================================================

std::vector<std::string> ReplHandler::getCompletions(const std::string& prefix) const {
    std::vector<std::string> cmds = {
        "help", "exit", "clear", "model", "status",
        "config", "export", "save", "trace", "parallel",
        "new", "load"
    };
    std::vector<std::string> results;
    for (const auto& c : cmds) {
        if (c.find(prefix) == 0) results.push_back(c);
    }
    return results;
}

void ReplHandler::showCompletions(const std::vector<std::string>& completions) const {
    if (completions.empty()) return;
    out_.write("\n" + Ansi::dim());
    for (size_t i = 0; i < completions.size(); ++i) {
        if (i > 0) out_.write("  ");
        out_.write("/" + completions[i]);
    }
    out_.write(Ansi::reset() + "\n");
}

void ReplHandler::autoSaveOnError() {
    if (!project_ || project_->path.empty()) return;
    try {
        ProjectIO::save(*project_);
        out_.write(Ansi::warning() + "[自动保存] 项目已保存。\n" + Ansi::reset());
    } catch (...) {}
}

// ============================================================================
// 主循环
// ============================================================================

void ReplHandler::run() {
    // 清屏 + 标题
    out_.write(Ansi::clearScreen());
    gui_.writeTitle("NovelAgent v0.3.0 — AI 写小说助手");

    if (!project_ || project_->title.empty()) {
        // 无项目 → 显示欢迎引导
        out_.write("\n");
        out_.write(Ansi::bold() + "  欢迎使用 NovelAgent！\n\n" + Ansi::reset());
        out_.write("  " + Ansi::userInput() + "/new <名称>" + Ansi::reset()
                   + "    创建新项目\n");
        out_.write("  " + Ansi::userInput() + "/load <路径>" + Ansi::reset()
                   + "   打开已有项目\n");
        out_.write("  " + Ansi::userInput() + "/help" + Ansi::reset()
                   + "        查看所有命令\n\n");
        out_.write(Ansi::dim() + "  也可以直接用 -p 参数启动：novelagent -p ./我的小说\n\n"
                   + Ansi::reset());
    } else {
        out_.write("\n" + Ansi::dim() + "项目: " + project_->title
                   + " | 输入 /help 查看命令，输入消息开始写作。\n\n" + Ansi::reset());
    }

    std::string input;
    while (true) {
        // 状态栏（有项目时显示）
        if (project_ && !project_->title.empty()) {
            gui_.renderStatusBar(
                agent_.isParallelEnabled() ? "Parallel" : "Serial",
                0, project_->title);
        }

        // 提示符
        std::cout << Ansi::userInput() << "> " << Ansi::reset() << std::flush;
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        gui_.addToHistory(input);

        // 斜杠命令
        if (CommandParser::isCommand(input)) {
            std::string cmd_part = input.substr(1);
            auto completions = getCompletions(cmd_part);
            if (completions.size() > 1) showCompletions(completions);

            if (!parser_.execute(input)) break;
            continue;
        }

        // 无项目时的提示
        if (!project_ || project_->title.empty()) {
            gui_.writeWarning("请先用 /new 或 /load 打开一个项目。");
            continue;
        }

        out_.write("\n");
        try {
            gui_.startSpinner("思考");
            auto callbacks = StreamDisplay::create(out_);
            auto response = agent_.processUserMessage(input, callbacks);
            gui_.stopSpinner();

            if (response.finish_reason == "length")
                gui_.writeWarning("回复因长度限制被截断");
            else if (response.finish_reason == "content_filter")
                gui_.writeWarning("部分内容因安全策略被过滤");
            out_.write("\n");
        } catch (const std::exception& e) {
            gui_.stopSpinner();
            gui_.writeError(e.what());
            autoSaveOnError();
        }
    }
}
