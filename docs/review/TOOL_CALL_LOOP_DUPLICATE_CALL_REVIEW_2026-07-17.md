# ToolCallLoop 首轮 LLM 调用冗余审查 — 2026-07-17

## 问题

`ToolCallLoop::run()` 中存在 **3 处 `client_.chat()` 调用**，但只需要 1 处统一入口即可。

### 现状

```
executeLoop:
  ├── ① 首轮 client_.chat()                       ← 与循环底部逻辑完全重复
  │
  └── for (round = 0; round < max_rounds; ++round)
        ├── 无工具调用 → 返回
        ├── 执行工具
        └── ④ client_.chat()                       ← 与 ① 逻辑完全重复，且导致每轮调两次 LLM
```

**核心问题：** 首轮（①）和循环底部（④）的 LLM 调用完全一致——都是 `client_.chat(messages, tools, system_prompt, callbacks)` + token 统计。首轮没有理由独立于循环存在。

### 影响

- **每轮两次 LLM 调用**：第 N 轮底部的 `client_.chat()` 结果被第 N+1 轮顶部覆盖，浪费一次 API 调用和 token 额度。
- **维护成本**：多处 token 统计逻辑散落，修改时容易遗漏。

## 修改方案

### 目标架构

将 LLM 调用统一到循环顶部，首轮作为第 0 次迭代进入循环，后续轮次在工具执行后自然进入下一次迭代。

```
executeLoop:
  └── for (round = 0; round <= max_rounds; ++round)
        ├── client_.chat()                          ← 统一入口
        ├── token 统计
        ├── 无工具调用 → 返回
        ├── 执行工具
        └── （自动进入下一轮迭代）
```

### 具体修改

| 位置 | 修改内容 |
|------|---------|
| 首轮 `client_.chat()` (L72-88) | **删除**，合并到循环顶部 |
| 循环条件 `round < max_rounds` | 改为 `round <= max_rounds`，使首轮作为第 0 次迭代 |
| 循环顶部 `if (tool_calls.empty())` 之前 | **新增** `client_.chat()` + token 统计 |
| 循环底部 `client_.chat()` (L167-180) | **删除**，由下一轮顶部统一处理 |
| 正常路径注释 `"追加 assistant + 执行工具 + 调 LLM"` | 改为 `"追加 assistant + 执行工具"` |

### 行为等价性验证

| 场景 | 修改前 | 修改后 |
|------|--------|--------|
| 无工具调用 | 首轮 chat → 进入循环 → round=0 时检测到空 → 返回 | 循环 round=0 → chat → 检测到空 → 返回 |
| 1 轮工具后完成 | 首轮 chat → 循环 round=0 执行工具 → chat → round=1 空检测 | 循环 round=0 → chat → 执行工具 → round=1 → chat → 空检测 |
| 达到 max_rounds | 首轮 chat + max_rounds 次循环 chat | max_rounds+1 次循环 chat |

## 相关文档

- **重复调用循环终止后的错误消息污染上下文**：反思机制（现已删除）在 `loop_detected` 时曾向 conversation 注入 Tool 消息，后经评审已修正为不写入任何消息
