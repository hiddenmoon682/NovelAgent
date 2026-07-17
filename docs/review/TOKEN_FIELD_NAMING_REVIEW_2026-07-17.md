# Token 计数字段命名不一致问题

## 发现时间
2026-07-17

## 问题描述

`llm::LLMResponse` 中的 token 计数字段沿用了 OpenAI API 的原始命名 `prompt_tokens` / `completion_tokens`，但在上层业务代码（`ToolCallLoopResult`、`ContextManager::recordUsage`）中对应的概念使用的是 `input_tokens` / `output_tokens`，两套命名混用，容易造成混淆。

## 涉及位置

| 文件 | 行号 | 字段 |
|------|------|------|
| `src/llm/Message.h` | 221-222 | `LLMResponse::prompt_tokens` / `completion_tokens` |
| `src/agent/ToolCallLoop.h` | 73-74 | `ToolCallLoopResult::input_tokens` / `output_tokens` |
| `src/agent/ToolCallLoop.cpp` | 多处 | `response.prompt_tokens` → `r.input_tokens` 的累加 |

## 命名对照

| LLMResponse (API 原始命名) | ToolCallLoopResult (业务命名) | 含义 |
|---------------------------|------------------------------|------|
| `prompt_tokens` | `input_tokens` | 发送给模型的输入 token 数 |
| `completion_tokens` | `output_tokens` | 模型生成的输出 token 数 |

## 建议方案

1. **给 `LLMResponse` 增加业务别名字段**（向后兼容）：
   ```cpp
   int input_tokens = 0;    // 等价于 prompt_tokens
   int output_tokens = 0;   // 等价于 completion_tokens
   ```
   修改 `from_json` 同时填充两套字段。

2. **或全局重命名**：将 `prompt_tokens` / `completion_tokens` 改为 `input_tokens` / `output_tokens`，修改所有引用处及 JSON 序列化/反序列化。

## 影响范围

- `from_json` / `to_json` 序列化（需保持 API 兼容，JSON 中仍应输出 `prompt_tokens` / `completion_tokens`）
- `ToolCallLoop.cpp` 中所有 `response.prompt_tokens` / `response.completion_tokens` 引用
- 所有直接读取 `LLMResponse` token 字段的测试代码
