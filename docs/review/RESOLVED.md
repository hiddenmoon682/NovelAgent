# 已修复问题记录

> 创建时间: 2026-05-29
> 用途: 记录已排查并修复的问题，按时间倒序排列
>
> 未修复的问题请记录在 `REVIEW_NOTES.md` 中。

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
