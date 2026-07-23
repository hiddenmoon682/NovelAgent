// 端到端集成测试 — Agent + Chapter 工具 + DeepSeek API。
//
// 验证完整闭环：
//   用户输入 → Agent.process()
//            → ContextManager.assemble()
//            → LLMClient.chat() [DeepSeek API]
//            → ToolRegistry.executeTool() [Chapter 工具]
//            → 循环...
//
// 手动执行：./build/tests/test_e2e_chapter.exe（需 config.json 或环境变量）

#include "agent/core/Agent.h"
#include "agent/tool/ToolRegistry.h"
#include "agent/tools/ChapterTools.h"
#include "config/AppConfig.h"
#include "agent/context/Memory.h"
#include "llm/LLMClientFactory.h"
#include "project/ProjectIO.h"

#include <cstdio>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// ── 辅助：创建测试项目 ──

struct TestProject {
    std::string path;
    Project project;

    TestProject() {
        path = (fs::temp_directory_path() / "novelagent_e2e_test").string();
        std::error_code ec;
        fs::remove_all(path, ec);

        ProjectIO::createProjectDir(path, "端到端测试小说");

        project = ProjectIO::load(path);

        // 添加初始章节
        Chapter ch;
        ch.id = "ch-001";
        ch.title = "序幕";
        ch.order = 1;
        ch.file_path = "chapters/ch-001.md";
        ch.synopsis = "故事的开端，主角登场。";
        project.outline.chapters.push_back(ch);

        Chapter ch2;
        ch2.id = "ch-002";
        ch2.title = "启程";
        ch2.order = 2;
        ch2.file_path = "chapters/ch-002.md";
        ch2.synopsis = "主角踏上旅途。";
        project.outline.chapters.push_back(ch2);

        ProjectIO::save(project);

        // 写入章节内容
        ProjectIO::writeChapter(path, ch.file_path,
            "# 序幕\n\n夜幕降临，城市的灯火在细雨中模糊成一片温暖的光晕。\n\n"
            "李明站在破旧的公寓楼顶，看着手中的信封。信封里是一张泛黄的照片——"
            "二十年前的母亲，笑得那样灿烂。\n\n"
            "'是时候了。'他轻声说。\n");

        ProjectIO::writeChapter(path, ch2.file_path,
            "# 启程\n\n火车缓缓驶出站台。窗外的景色从高楼大厦变成一望无际的田野。\n\n"
            "李明靠在座位上，脑海中反复回放着昨晚看到的那张照片。\n");
    }

    ~TestProject() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

int main() {
    std::cout << "=== 端到端测试: Agent + Chapter 工具 + DeepSeek API ===\n\n";

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
    std::cout << "  章节数: " << tp.project.outline.chapters.size() << "\n";
    for (const auto& ch : tp.project.outline.chapters) {
        std::cout << "    - " << ch.id << ": " << ch.title << "\n";
    }

    // 3. 注册 Chapter 工具
    std::cout << "\n[3/5] 注册 Chapter 工具...\n";
    llm::LLMClientFactory factory(*provider);
    agent::ToolRegistry registry;

    registry.registerBuiltInTool(
        std::make_unique<agent::ListChaptersTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));
    registry.registerBuiltInTool(
        std::make_unique<agent::ReadChapterTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));
    registry.registerBuiltInTool(
        std::make_unique<agent::WriteChapterTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));
    registry.registerBuiltInTool(
        std::make_unique<agent::AppendChapterTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));
    registry.registerBuiltInTool(
        std::make_unique<agent::CreateChapterTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));

    std::cout << "  已注册 " << registry.toolCount() << " 个工具:\n";
    for (const auto& name : registry.toolNames()) {
        std::cout << "    - " << name << "\n";
    }

    // 4. 创建 Agent
    std::cout << "\n[4/5] 创建 Agent...\n";
    llm::Memory memory;
    agent::Agent agent(factory, registry, memory);
    agent.setSystemPrompt(
        "你是一个小说写作助手。你可以使用工具来管理小说项目。\n"
        "当用户要求列出章节、读取内容、写入章节时，请使用对应的工具。\n"
        "每次使用工具后，用简洁的中文总结执行结果。\n"
    );
    std::cout << "  系统提示词已设置\n";

    // 5. 执行端到端测试
    std::cout << "\n[5/5] 发送测试消息...\n";
    std::cout << "═══════════════════════════════════════════\n\n";

    // ── 测试 1: 列出章节 ──
    {
        std::cout << "▶ 用户: 请列出当前项目的所有章节\n\n";
        std::cout << "▶ Agent: " << std::flush;

        llm::StreamCallbacks cb;
        cb.on_content = [](const std::string& delta) {
            std::cout << delta << std::flush;
        };
        cb.on_tool_call_start = []() {
            std::cout << "\n  [🔧 正在调用工具...]\n" << std::flush;
        };

        try {
            auto response = agent.process(
                "请列出当前项目的所有章节", cb);
            std::cout << "\n\n  ── 响应摘要 ──\n";
            std::cout << "  Model: " << response.model << "\n";
            std::cout << "  Tokens: " << response.total_tokens << "\n";
            std::cout << "  Finish: " << response.finish_reason << "\n";
        } catch (const std::exception& e) {
            std::cerr << "\n  错误: " << e.what() << "\n";
        }
    }

    std::cout << "\n───────────────────────────────────────────\n\n";

    // ── 测试 2: 读取章节 ──
    {
        std::cout << "▶ 用户: 请读取第一章（序幕）的内容，并简要总结\n\n";
        std::cout << "▶ Agent: " << std::flush;

        llm::StreamCallbacks cb;
        cb.on_content = [](const std::string& delta) {
            std::cout << delta << std::flush;
        };
        cb.on_tool_call_start = []() {
            std::cout << "\n  [🔧 正在调用工具...]\n" << std::flush;
        };

        try {
            auto response = agent.process(
                "请读取第一章（ch-001 序幕）的内容，并用一句话总结", cb);
            std::cout << "\n\n  ── 响应摘要 ──\n";
            std::cout << "  Model: " << response.model << "\n";
            std::cout << "  Tokens: " << response.total_tokens << "\n";
            std::cout << "  Finish: " << response.finish_reason << "\n";
        } catch (const std::exception& e) {
            std::cerr << "\n  错误: " << e.what() << "\n";
        }
    }

    std::cout << "\n───────────────────────────────────────────\n\n";

    // ── 对话历史统计 ──
    std::cout << "▶ 对话历史: " << agent.memory().size() << " 条消息\n";
    auto all_msgs = agent.memory().all();
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
    }

    std::cout << "\n═══════════════════════════════════════════\n";
    std::cout << "✅ 端到端测试完成\n";
    return 0;
}
