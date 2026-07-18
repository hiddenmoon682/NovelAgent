// 端到端集成测试 — 验证 reasoning_content 在循环结束后被 strip。
//
// 验证闭环：
//   用户输入 → Agent.process()
//            → ToolCallLoop（循环中保留 reasoning_content）
//            → conversation.stripReasoningContent()（循环后清理）
//
// 手动执行：./build/tests/test_e2e_reasoning_strip.exe（需 config.json 或环境变量）

#include "agent/Agent.h"
#include "agent/ToolRegistry.h"
#include "agent/tools/ChapterTools.h"
#include "config/AppConfig.h"
#include "llm/LLMClientFactory.h"
#include "project/ProjectIO.h"

#include <cstdio>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

struct TestProject {
    std::string path;
    Project project;

    TestProject() {
        path = (fs::temp_directory_path() / "novelagent_e2e_reasoning_test").string();
        std::error_code ec;
        fs::remove_all(path, ec);

        ProjectIO::createProjectDir(path, "端到端测试小说");

        project = ProjectIO::load(path);

        Chapter ch;
        ch.id = "ch-001";
        ch.title = "序幕";
        ch.order = 1;
        ch.file_path = "chapters/ch-001.md";
        ch.synopsis = "故事的开端，主角登场。";
        project.outline.chapters.push_back(ch);

        ProjectIO::save(project);

        ProjectIO::writeChapter(path, ch.file_path,
            "# 序幕\n\n夜幕降临，城市的灯火在细雨中模糊成一片温暖的光晕。\n\n"
            "李明站在破旧的公寓楼顶，看着手中的信封。信封里是一张泛黄的照片——"
            "二十年前的母亲，笑得那样灿烂。\n\n"
            "'是时候了。'他轻声说。\n");
    }

    ~TestProject() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

int main() {
    std::cout << "=== 端到端测试: reasoning_content strip 验证 ===\n\n";

    // 1. 加载配置
    std::cout << "[1/5] 加载配置...\n";
    auto config = AppConfig::load();
    auto* provider = config.getProvider("deepseek");
    if (!provider) {
        std::cerr << "错误: 未找到 deepseek 配置\n";
        return 1;
    }
    std::cout << "  Provider: " << provider->name << "\n";
    std::cout << "  Model: " << provider->model << "\n";

    // 2. 创建测试项目
    std::cout << "\n[2/5] 创建测试项目...\n";
    TestProject tp;
    std::cout << "  项目路径: " << tp.path << "\n";

    // 3. 注册工具
    std::cout << "\n[3/5] 注册工具...\n";
    llm::LLMClientFactory factory(*provider);
    agent::ToolRegistry registry;

    registry.registerBuiltInTool(
        std::make_unique<agent::ListChaptersTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));
    registry.registerBuiltInTool(
        std::make_unique<agent::ReadChapterTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));

    std::cout << "  已注册 " << registry.toolCount() << " 个工具\n";

    // 4. 创建 Agent
    std::cout << "\n[4/5] 创建 Agent...\n";
    agent::Agent agent(factory, registry);
    agent.setSystemPrompt(
        "你是一个小说写作助手。你可以使用工具来管理小说项目。\n"
        "当用户要求列出章节、读取内容时，请使用对应的工具。\n"
        "每次使用工具后，用简洁的中文总结执行结果。\n"
    );
    std::cout << "  系统提示词已设置\n";

    // 5. 执行测试
    std::cout << "\n[5/5] 执行测试...\n";
    std::cout << "═══════════════════════════════════════════\n\n";

    int passed = 0;
    int failed = 0;

    // ── 测试: 触发工具调用 → 验证 reasoning_content 被 strip ──
    {
        std::cout << "▶ 用户: 列出章节\n\n";
        std::cout << "▶ Agent: " << std::flush;

        llm::StreamCallbacks cb;
        cb.on_content = [](const std::string& delta) {
            std::cout << delta << std::flush;
        };

        try {
            auto response = agent.process("请列出当前项目的所有章节", cb);
            std::cout << "\n\n  ── 响应摘要 ──\n";
            std::cout << "  Model: " << response.model << "\n";
            std::cout << "  Tokens: " << response.total_tokens << "\n";
            std::cout << "  Finish: " << response.finish_reason << "\n";

            // ── 关键验证: conversation 中的 assistant 消息没有 reasoning_content ──
            bool has_reasoning = false;
            for (const auto& msg : agent.conversation().messages()) {
                if (msg.role == llm::MessageRole::Assistant
                    && !msg.tool_calls.empty()
                    && !msg.reasoning_content.empty()) {
                    has_reasoning = true;
                    std::cout << "  ❌ 发现 reasoning_content ("
                              << msg.reasoning_content.size() << " 字符)\n";
                    break;
                }
            }

            if (has_reasoning) {
                std::cout << "\n  ❌ 失败: reasoning_content 未清除\n";
                ++failed;
            } else {
                std::cout << "\n  ✅ 通过: reasoning_content 已清除\n";
                ++passed;
            }
        } catch (const std::exception& e) {
            std::cerr << "\n  错误: " << e.what() << "\n";
            ++failed;
        }
    }

    std::cout << "\n───────────────────────────────────────────\n\n";

    // ── 对话统计 ──
    std::cout << "▶ 对话历史: " << agent.conversation().size() << " 条消息\n";
    auto all_msgs = agent.conversation().all();
    for (size_t i = 0; i < all_msgs.size(); ++i) {
        const auto& msg = all_msgs[i];
        std::string role_str;
        switch (msg.role) {
            case llm::MessageRole::User:      role_str = "User     "; break;
            case llm::MessageRole::Assistant: role_str = "Assistant"; break;
            case llm::MessageRole::Tool:      role_str = "Tool     "; break;
            default:                           role_str = "Other    "; break;
        }
        std::string preview = msg.content.substr(0, 80);
        if (msg.content.size() > 80) preview += "...";
        std::cout << "  [" << i << "] " << role_str << " | " << preview << "\n";
        if (msg.role == llm::MessageRole::Assistant && !msg.reasoning_content.empty()) {
            std::cout << "       ⚠️ reasoning_content: "
                      << msg.reasoning_content.substr(0, 60) << "...\n";
        }
    }

    std::cout << "\n═══════════════════════════════════════════\n";
    std::cout << "结果: " << passed << " 通过, " << failed << " 失败\n";
    return failed > 0 ? 1 : 0;
}
