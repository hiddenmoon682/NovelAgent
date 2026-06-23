#pragma once

// PromptContextBuilder 负责把结构化小说模型裁剪成“当前任务真正需要的上下文”。
//
// 设计思路：
// 1. 先从 Project 中筛出和当前章节/任务最相关的对象。
// 2. 再按 GenerationControl 对字段做白名单/黑名单过滤。
// 3. 同时产出结构化 payload 和可直接发送给 LLM 的文本版本。
#include "project/Models/ModelsFwd.h"

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace prompt {

// 构建上下文时的可调参数。
struct PromptContextOptions {
    std::string task = "write_chapter"; // write_chapter|write_scene|revise|summarize|continuity_check
    std::string chapter_id;             // 目标章节 ID；按章节写作时必填
    std::string scene_id;               // 可选：只聚焦某个场景

    bool include_project_summary = true;
    bool include_outline_context = true;
    bool include_style = true;
    bool include_scenes = true;
    bool include_chapter_text = false;  // 读取并附加已存在的正文
    bool include_metadata = false;      // 是否把 metadata 也暴露给 LLM

    std::size_t max_plot_threads = 6;
    std::size_t max_characters = 8;
    std::size_t max_settings = 8;
    std::size_t max_world_rules = 6;
};

// Builder 的产物：
// - payload：适合程序继续加工或调试查看
// - rendered_prompt：适合直接喂给 LLM
struct PromptContext {
    std::string task;
    std::string chapter_id;
    std::string scene_id;
    std::vector<std::string> notes; // 构建过程中的提示，例如“未找到正文文件”
    nlohmann::json payload;
    std::string rendered_prompt;
};

class PromptContextBuilder {
public:
    // 为“按章节写作”类任务构建上下文。
    static std::optional<PromptContext> buildForChapter(
        const Project& project,
        const PromptContextOptions& options);

    // 仅渲染文本；适合上层已经缓存了 payload 的情况。
    static std::string renderPrompt(const PromptContext& context);
};

} // namespace prompt
