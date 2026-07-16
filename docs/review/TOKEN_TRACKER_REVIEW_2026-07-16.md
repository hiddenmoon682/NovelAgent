# TokenTracker 校准与检查机制审查 — 2026-07-16

## 背景

代码审查中发现了 TokenTracker 相关的问题记录，暂不修复，留待后续仔细审查。

---

## 问题 4：check() 无参版与 check(realtime) 计量体系不一致

### 现象

| 位置 | 方法 | 数据来源 |
|------|------|----------|
| `Agent.cpp:260` (on_round_complete 回调) | `check()` 无参 | `current_context_size_` = API 返回的原始 `prompt_tokens` |
| `ContextManager.cpp:336` (assemble) | `check(total_tokens)` | `updateMessageTokens()` 启发式估算 × 校正因子 |

当校正因子偏离 1.0 时，同一时刻的上下文用量在两个路径可能得到不同的判断结果（如一个 Normal、一个 Warning）。

### 示例

假设 `model_limit=10000`，真实对话约 7142 tokens，校正因子 = 0.7：

- assemble 路径：`total_tokens = 5000 × 0.7 = 3500` → `check(3500)` → 35% Normal
- hook 路径：API 返回 `prompt_tokens = 7142` → `record(7142)` → `check()` → 71% Warning

两个路径判断不一致。

### 当前影响

低。AutoCompact 阈值（95%）较高，两者一般都不会误触。且下次 `assemble()` 时会重新同步。

### 潜在修复方向

- Hook 回调中也改用带参版本 `check(currentContextSize())`，统一为同一数据源
- 或确认两个路径各有用途（估算 vs 真实），接受差异

---

## 问题 3：setCurrentContextSize() 死代码

`TokenTracker.h:89` 声明了 `setCurrentContextSize()`，注释说"供 assemble() 在 LLM 调用前写入启发式估算值"，但 `assemble()` 从未调过它（只调了 `setCurrentTotalTokens()`）。

### 影响

极低。`current_context_size_` 由 `record()`（API 返回）和 `restore()`（持久化）写入，基本路径无影响。

### 修复方向

要么删掉这个死方法，要么在 `assemble()` 末尾同步写入 `setCurrentContextSize(result.total_tokens)`。
