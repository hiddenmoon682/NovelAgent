# 审查记录 — PLAN.md 之外的待调整/优化/补充事项

> 创建时间: 2026-05-28
> 用途: 记录代码审查过程中发现的、PLAN.md 未覆盖或需修正的问题

---

## 1. `LLMResponse` 结构不完整

**文件**: `src/llm/Message.h`

**问题**: 当前 `LLMResponse` 仅包含 5 个字段，与 DeepSeek API 实际返回的 JSON 差距较大，缺少以下字段：

| 缺失字段 | 类型 | 用途 |
|---------|------|------|
| `id` | `std::string` | 响应唯一 ID（`chatcmpl-xxx`），用于日志追踪和调试 |
| `created` | `int64_t` | Unix 时间戳 |
| `total_tokens` | `int` | 总 token 数，省去每次手动相加 |
| `reasoning_content` | `std::string` | **DeepSeek 特有** — `deepseek-reasoner` 模型返回的思维链内容 |
| `completion_tokens_details.reasoning_tokens` | `int` | 思维链消耗的 token 数 |
| `prompt_tokens_details.cached_tokens` | `int` | 缓存命中的 prompt token 数 |
| `system_fingerprint` | `std::string` | 系统指纹（可选） |

**影响**: 后续若使用 `deepseek-reasoner`（R1）系列模型，`reasoning_content` 缺失会导致思维链内容丢失。

---

## 2. `Message.h` 缺少 JSON 序列化/反序列化

**文件**: `src/llm/Message.h`

**问题**: `Message`、`ToolCall`、`LLMResponse` 三个结构体都没有 `to_json`/`from_json` 方法，而 `project/Models.h` 中所有 10 个 struct 都手写了序列化。这会导致：

- `LLMClient` 发送请求时需手拼 JSON，容易出错
- 接收响应时无法直接 `j.get<LLMResponse>()`
- 对话历史持久化（`conversation.json`）无法直接序列化 `vector<Message>`

**建议**: 为三个结构体补充 `to_json`/`from_json`，与 `Models.h` 保持一致的风格。

---

## 3. `SSEParser` 流式 tool_calls 未按 index 合并

**文件**: `src/llm/SSEParser.cpp`

**问题**: OpenAI 兼容 API 的流式 tool_calls 是按 `index` 分片增量返回的，例如：

```
chunk 1: tool_calls[0] = { id: "call_xxx", function: { name: "write_chapter", arguments: "" } }
chunk 2: tool_calls[0] = { function: { arguments: "{\"chap" } }
chunk 3: tool_calls[0] = { function: { arguments: "ter_id\": \"ch3\"}" } }
```

当前 `processChunk()` 对每个 delta chunk 都直接触发 `on_tool_call` 回调，**没有按 index 合并**。这意味着外层收到的将是多个不完整的 `ToolCall` 对象（arguments 被切割成片段）。

**建议**: 在 `SSEParser` 内部维护一个按 index 索引的 `ToolCall` 累积映射，只有遇到 `finish_reason` 或 `[DONE]` 时才触发完整的回调。

---

## 4. `TokenCounter::countMessages` 未统计 `tool_call_id` 和 `name`

**文件**: `src/llm/TokenCounter.cpp`

**问题**: `countMessages()` 中统计了 `content`、`tool_calls` 的 `function_name` 和 `arguments`，但遗漏了：

- `msg.tool_call_id` — 回传工具调用的 ID 也有 token 开销
- `msg.name` — 可选参与者名称

**影响**: 预算估算偏小，可能导致上下文窗口溢出（不过当前系数是启发式，误差较小）。

---

## 5. `migrateProject()` 迁移逻辑不完整

**文件**: `src/project/ProjectIO.cpp`

**问题**: 当前 `migrateProject()` 仅执行：
```cpp
if (project.format_version < kCurrentFormatVersion) {
    project.format_version = kCurrentFormatVersion;
}
```
没有实际的字段迁移逻辑。但历史上发生过破坏性变更：
- **v1 → v2**: 引入 `tags` + `metadata`，Setting 移除 `attributes`
- **v2 → v3**: 引入 `Scene`、`Relationship`、`WorldRule`，Character 关系从 `map` 升级到 `vector<Relationship>`

当前 `from_json` 通过 `getMetadataWithUnknownKeys` 被动吸收了未知字段，但如果旧项目用 `v1` 格式，`attributes` 字段不会自动迁移到 `metadata`。

---

## 6. `docs/` 目录中部分文档过时

**文件**: `docs/PROJECT_ANALYSIS.md`, `docs/MODULES.md`

**问题**: 这两个文档仍将 Phase 1 标记为"待实现"，但实际上 Phase 1 已超规格完成（10 个 struct、GenerationControl、PromptContextBuilder 等）。文档内容停留在 Phase 0 阶段。

**建议**: 更新文档状态，或考虑删除不再维护的旧文档，避免新人阅读时产生混淆。

---

## 7. `ProjectIO::createProjectDir` 中 `world_rules.json` 初始化为数组，但 `Style` 初始化为对象

**文件**: `src/project/ProjectIO.cpp`

**问题**: 一致性检查——`world_rules.json` 初始化为 `json::array()`，而 `style.json` 初始化为 `Style{}`（对象），行为正确但写法风格不统一。`characters.json` 和 `settings.json` 也是 `json::array()`。建议统一风格，或全部用对应 struct 的默认构造。

**影响**: 极小，仅风格一致性。

---

## 8. `LLMClient` 尚未实现（PLAN.md 已规划，但需要确认细节）

**文件**: 待创建 `src/llm/LLMClient.h`, `src/llm/LLMClient.cpp`

**PLAN.md 已规划但需注意的设计细节**：

- 流式模式下 `chat()` 需要同时处理 `on_token`（逐 token 显示）和 `on_tool_call`（增量合并后回调）
- 非流式模式下直接解析完整 JSON → `LLMResponse`
- 需考虑与 `SSEParser` 的 tool_calls 合并逻辑对接
- HTTP 错误码处理：401（API Key 无效）、429（限流）、500（服务端错误）
- 超时设置：连接超时 + 读取超时

---

## 9. 工具函数的命名空间不一致

**文件**: `src/utils/` vs `src/project/Models.h`

**问题**: `utils::file`、`utils::string`、`utils::json` 使用嵌套命名空间，但 `Models.h` 中使用 `project::model_detail` 命名空间。`StringUtils.h` 中的函数是自由函数（无命名空间封装），而 `FileUtils.h` 中的函数封装在 `utils::file` 下。存在一致性差异。

**影响**: 极小，不影响功能，但长期维护可能造成困惑。

---

## 10. 测试覆盖率缺口

**问题**: 当前测试（`test_main.cpp`, `test_models.cpp`, `test_project_io.cpp`, `test_prompt_context.cpp`, `test_sse_parser.cpp`, `test_token_counter.cpp`）覆盖了 Phase 0-2 核心逻辑，但缺少：

| 缺少的测试 | 原因 |
|-----------|------|
| `test_file_utils.cpp` | FileUtils 无独立测试，依赖其他模块间接覆盖 |
| `test_json_utils.cpp` | JsonUtils 无独立测试 |
| `test_string_utils.cpp` | StringUtils 无独立测试 |
| 边界: 超大 JSON 文件 | ProjectIO 加载性能未测试 |
| 边界: 编码问题 | UTF-8 BOM、非 UTF-8 编码的章节文件未测试 |

---

## 附录：添加新条目

发现新的问题后，按以下格式追加：

```markdown
## N. 标题

**文件**: `相关文件路径`

**问题**: 描述...

**建议**: 如何修复...
```
