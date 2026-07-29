#pragma once

// PromptContextBuilder 负责把结构化小说模型裁剪成“当前任务真正需要的上下文”。
//
// 设计思路：
// 1. 先从 Project 中筛出和当前章节/任务最相关的对象。
// 2. 再对字段做裁剪（去空值、保留关键标识字段）。
// 3. 同时产出结构化 payload 和可直接发送给 LLM 的文本版本。
#include "project/Models/ModelsFwd.h"

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace prompt {

// 构建上下文时的可调参数。
// 调用方可透传此结构来控制 PromptContextBuilder 从 Project 中筛选哪些信息，
// 以及每类信息的最大数量，从而精确控制送入 LLM 的上下文大小和内容范围。
struct PromptContextOptions {
    // 当前任务类型，决定上下文构建的侧重点。
    // 可选值：
    //   "write_chapter"    — 按章节写作（默认），聚焦章节大纲与相关线索
    //   "write_scene"      — 按场景写作，仅关注某个场景内的人物与设定
    //   "revise"           — 修订已有章节，需附带正文供 LLM 参考
    //   "summarize"        — 生成故事摘要，需更宏观的全局信息
    //   "continuity_check" — 一致性检查，需对比前后章节设定
    std::string task = "write_chapter";

    std::string chapter_id;             // 目标章节 ID；按章节写作时必填，用于定位章节大纲与关联线索
    std::string scene_id;               // 可选：只聚焦某个场景，为空时使用章节下全部场景

    // ========== 开关控制 ==========

    bool include_project_summary = true;    // 是否附加项目级摘要（如故事梗概、核心主题）
    bool include_outline_context = true;    // 是否附加章节大纲及关联的剧情线/伏笔
    bool include_style = true;              // 是否附加风格指南（文风、叙事视角、标点规则等）
    bool include_scenes = true;             // 是否附加场景列表（场景编号、地点、参与人物）
    bool include_chapter_text = false;      // 是否读取并附加章节已有正文（修订/续写时开启）
    bool include_metadata = false;          // 是否把 metadata（版本号、创建时间等扩展字段）暴露给 LLM

    // ========== 数量上限 ==========

    std::size_t max_plot_threads = 6;   // 最大关联剧情线条数，超出时按优先级截断
    std::size_t max_characters = 8;     // 最大关联角色数，超出时按出场频率/重要性截断
    std::size_t max_settings = 8;       // 最大关联场景/设定地点数
    std::size_t max_world_rules = 6;    // 最大关联世界观规则数
};

// Builder 的最终产物，封装一次上下文构建的全部结果。
//
// 设计定位：将 PromptContextOptions 指定的筛选规则应用于 Project 后，
// 同时产出一份结构化数据（payload）和一份可直接送入 LLM 的文本（rendered_prompt），
// 上层调用方可按需取用。
//
// 典型流程：
//   PromptContextOptions opts;
//   opts.task = "write_chapter";
//   opts.chapter_id = "ch-003";
//   auto ctx = PromptContextBuilder::buildForChapter(project, opts);
//   if (ctx) sendToLLM(ctx->rendered_prompt);
struct PromptContext {
    // 回显构建时使用的任务类型，方便上层日志/调试时追溯
    std::string task;

    // 回显构建时使用的目标章节 ID
    std::string chapter_id;

    // 回显构建时使用的目标场景 ID（为空表示使用章节下全部场景）
    std::string scene_id;

    // 构建过程中的非致命提示信息集合，例如：
    // - "未找到章节正文文件，跳过 include_chapter_text"
    // - "关联角色超过 max_characters，已截断至 8 个"
    // - "场景文件格式异常，已降级为空列表"
    std::vector<std::string> notes;

    // 结构化 JSON 载荷，包含按 options 筛选后的小说模型子集。
    // 可供程序继续加工（如注入额外指令、重新排序）或保存为调试快照。
    // 结构示例：{ "task": "write_chapter", "chapters": [...], "characters": [...], ... }
    nlohmann::json payload;

    // 渲染完成的纯文本提示词，已包含所有筛选后的上下文信息，
    // 格式化为 LLM 友好的自然语言描述，可直接作为 system/user prompt 发送。
    std::string rendered_prompt;
};

// 上下文构建器（纯静态工具类，无状态）。
class PromptContextBuilder {
public:
    // 为”按章节写作”类任务构建完整上下文（含角色/设定/规则详情）。
    // @param project 小说项目数据源。
    // @param options 筛选规则与数量上限，见 PromptContextOptions。
    // @return 构建结果；options.chapter_id 为空或项目中找不到
    //         该章节时返回 nullopt。
    static std::optional<PromptContext> buildForChapter(
        const Project& project,
        const PromptContextOptions& options);

    // 轻量级上下文构建 — 仅输出章节元数据 + 风格，不注入角色/设定/规则详情。
    // 与 buildForChapter 共享参数校验逻辑，但跳过全部详情注入步骤。
    // rendered_prompt 通常 < 500 tokens，配合 kToolUseInstructions 使用。
    // @return 同 buildForChapter：chapter_id 为空或章节不存在时返回 nullopt。
    static std::optional<PromptContext> buildLightweight(
        const Project& project,
        const PromptContextOptions& options);

    // 仅渲染文本；适合上层已经缓存了 payload 的情况。
    static std::string renderPrompt(const PromptContext& context);
};

} // namespace prompt
