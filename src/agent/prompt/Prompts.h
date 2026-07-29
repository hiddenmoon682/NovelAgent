#pragma once

// 内置提示词常量 — 定义见 Prompts.cpp。

namespace agent::prompt {

// 主人格 system prompt：定义 NovelAgent 的角色、能力与创作原则，
// 启动时由 NovelAgentApp 与技能/工具上下文一起组装。
extern const char* kMainPersonality;
// 上下文压缩专用 system prompt：指导 LLM 对对话历史做
// 情节事实 + 创作状态的双层中文摘要（Compactor 使用）。
extern const char* kCompactSystemPrompt;
// 按需获取上下文的工具使用指引：列举各查询工具的适用时机，
// 配合 buildLightweight 的轻量上下文使用，避免一次性注入全量详情。
extern const char* kToolUseInstructions;

} // namespace agent::prompt
