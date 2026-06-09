# 已修复问题记录

> 创建时间: 2026-05-29
> 用途: 记录已排查并修复的问题，按时间倒序排列
>
> 未修复的问题请记录在 `REVIEW_NOTES.md` 中。

---

## 第五轮审查（2026-06-09） — Phase 3.6-3.12 代码审查修复

### #16 ShellTools 黑名单误拦截管道/重定向 ✅

**修复**: 从黑名单移除 `|` `>` `>>` `;` `&&` `||`，补充 PowerShell 缩写变体（ri/rdr/saps/iwr/irm/spps/sasv/icm），添加注册表修改检测。

### #17 ShellTools 误导性白名单注释 ✅

**修复**: 删除未实现的白名单注释，替换为准确的安全说明 + TODO。

### #18 ShellTools 关键词绕过风险 ✅

**修复**: 补充 PowerShell 常见缩写到黑名单 + 添加安全声明"不可在完全不可信环境中运行"。

### #19 StreamDisplay Windows ANSI 初始化 ✅

**修复**: 添加 `SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)` 初始化，确保 Windows 终端正确渲染颜色码。

### #20 ChapterTools.h 注释过期 ✅

**修复**: `ListChaptersTool` header 注释从 `word_count` 更新为 `file_path, synopsis`。

### #22 ReplHandler 未使用 LLMResponse ✅

**修复**: 检查 `finish_reason`，在 `"length"` 和 `"content_filter"` 时给用户提示。

### #6 CreateChapterTool 部分保存风险 ✅ (第三轮已修复)

已恢复为 `ProjectIO::save()` 全量保存。

### #12 ShellTools 命令注入 ✅ (第四轮已修复)

添加 24 关键词黑名单 + 100KB 输出截断。

### #14 CommandParser 大小写敏感 ✅ (第四轮已修复)

命令名转小写比较。

### #15 ShellTools 输出无大小限制 ✅ (第四轮已修复)

100KB 截断。

---

## 第三轮审查（2026-06-09） — Phase 3.1-3.4 修复

来自 `REVIEW_NOTES.md` 的 11 个问题，修复了 9 个。

### #1 ContextManager 覆盖用户 system_prompt ✅

**修复**: `runToolLoop()` 中使用局部变量 `effective_prompt`，不再修改成员 `system_prompt_`。ContextManager 产出的上下文 prompt 与用户设置的人格 prompt 通过 `"\n\n"` 拼接。

### #2 truncateMessages token 公式不一致 ✅

**修复**: 截断循环中每次移除消息后调用 `TokenCounter::countMessages()` 重新计算，消除手工减法与初始估值的不一致。

### #3 buildSystemPrompt 无效章节返回空 ✅

**修复**: `buildForChapter()` 返回 `nullopt` 时，fallback 到无章节版本（返回项目概述），而非返回空字符串。

### #4 truncateMessages budget ≤ 0 返回全部消息 ✅

**修复**: `budget <= 0` 时返回空列表并设置 `truncated_count`，不再返回无法容纳的全部消息。

### #5 ChapterTools 持有 Project& 裸引用 🔵 暂缓

**决定**: 当前生命周期一致，无实际触发路径。在 `BuiltInTool` 文档中标注生命周期约束。等 Phase 3.5 多 Agent 并行编排时重新评估。

### #6 CreateChapter 部分保存风险 ✅

**修复**: 恢复为 `ProjectIO::save()` 全量保存，保证 outline.json 与其他 JSON 文件一致。

### #7 工具调用结果无大小限制 ✅

**修复**: `executeToolCallsAndAppend()` 中对结果 JSON 做 4000 字符截断。截断时附加原文长度提示。

### #8 空 try/catch 块 ✅

**修复**: 删除 `processUserMessage()` 中无操作的 `try { ... } catch (...) { throw; }` 块。

### #9 ListChaptersTool 描述与行为不一致 ✅

**修复**: `description()` 更新为"列出当前项目所有章节的 ID、标题、顺序、文件路径和摘要"，与实现了无 `word_count` 的行为一致。

### #10 SchemaUtils additionalProperties 硬编码 🔵 暂缓

**决定**: 当前安全默认值正确。在注释中记录此设计决策。

### #11 total_tokens 精度说明缺失 ✅

**修复**: 注释更新为"占用的 token 数（TokenCounter 启发式估算，非精确值）"。

---

## 第二轮审查（2026-05-29） — 流式架构重构

---

## 1. LLMResponse 无法覆盖流式响应 ✅

**文件**: `src/llm/Message.h`, `src/llm/SSEParser.h`, `src/llm/SSEParser.cpp`, `src/llm/StreamAccumulator.h`, `src/llm/StreamAccumulator.cpp`

**问题**: `SSEParser` 混入了"协议解析"和"跨 chunk 状态管理"两种职责，`LLMResponse` 无法表示流式中间状态。

**修复**: 引入三层职责分离架构——

1. **`Message.h`** 新增 `ToolCallDelta`、`UsageInfo`、`StreamChunk` 三个流式中间类型
2. **`SSEParser`** 简化为纯协议解析——单个 `onChunk(StreamChunk)` 回调替代原有的 `onToken`/`onToolCall`/`onDone` 三回调，移除 `pending_tool_calls_` 和 `flushToolCalls()`
3. **`StreamAccumulator`**（新建）负责跨 chunk 合并——文本拼接、tool_calls 按 index 累积、流结束时产出完整 `LLMResponse`
4. **`LLMResponse` 不变** — `to_json`/`from_json` 保持不变

数据流：`SSE 文本 → SSEParser → StreamChunk → StreamAccumulator → LLMResponse`

---

## 第一轮审查（2026-05-28） — 全部已排查/已修复

---

## 1. `LLMResponse` 结构不完整 ✅

**文件**: `src/llm/Message.h`

**问题**: 当前 `LLMResponse` 仅包含 5 个字段，与 DeepSeek API 实际返回的 JSON 差距较大，缺少 `id`、`created`、`total_tokens`、`reasoning_content` 等字段。

**修复**: 已补充完整字段，包含 DeepSeek 特有的 `reasoning_content`。

---

## 2. `Message.h` 缺少 JSON 序列化/反序列化 ✅

**文件**: `src/llm/Message.h`

**问题**: `Message`、`ToolCall`、`LLMResponse` 三个结构体都没有 `to_json`/`from_json` 方法。

**修复**: 已为三个结构体补充手写 `to_json`/`from_json`，与 `Models.h` 风格一致。

---

## 3. `SSEParser` 流式 tool_calls 未按 index 合并 ✅

**文件**: `src/llm/SSEParser.cpp`

**问题**: OpenAI 兼容 API 的流式 tool_calls 按 `index` 分片增量返回，当前 `processChunk()` 对每个 delta chunk 都直接触发回调，未合并。

**修复**: 已在 `SSEParser` 内部维护按 index 索引的 `ToolCall` 累积映射，遇到 `finish_reason` 或 `[DONE]` 时才触发完整回调。

---

## 4. `TokenCounter::countMessages` 未统计 `tool_call_id` 和 `name` ✅

**文件**: `src/llm/TokenCounter.cpp`

**问题**: `countMessages()` 遗漏了 `tool_call_id` 和 `name` 的 token 开销。

**修复**: 已补充统计。

---

## 5. `migrateProject()` 迁移逻辑不完整 ✅

**文件**: `src/project/ProjectIO.cpp`

**问题**: 当前仅执行版本号升级，无实际字段迁移。但经确认，`from_json` 通过 `getMetadataWithUnknownKeys` 被动吸收未知字段，旧数据中 `attributes` 等信息已自动落入 `metadata`，数据不丢失。

**结论**: 当前机制已足够，无需显式迁移逻辑。关闭。

---

## 6. `docs/` 目录中部分文档过时 ✅

**文件**: `docs/PROJECT_ANALYSIS.md`, `docs/MODULES.md`

**问题**: 文档仍标记 Phase 1 为"待实现"。

**修复**: 已更新状态。

---

## 7. `ProjectIO::createProjectDir` 初始化风格不统一 ✅

**文件**: `src/project/ProjectIO.cpp`

**问题**: `world_rules.json` 初始化为 `json::array()`，`style.json` 初始化为 `Style{}`，风格不一致。

**结论**: 影响极小，可在后续统一重构时处理。关闭。

---

## 8. `LLMClient` 设计细节备忘 ✅

**问题**: PLAN.md 已规划但需确认的 `LLMClient` 设计细节。

**修复**: 相关注意事项已纳入 PLAN.md Step 2.5 和 Step 2.7 中。

---

## 9. 工具函数命名空间一致性 ✅

**问题**: 原怀疑命名空间不一致，经复查确认三个 utils 模块均封装在对应命名空间内，`Models.h` 使用独立的 `project::model_detail`，设计合理。

**结论**: 此条目为误判，关闭。

---

## 10. 测试覆盖率缺口

**问题**: FileUtils、JsonUtils、StringUtils 无独立测试。

**状态**: 已知，不阻塞开发，留待后续补充。
