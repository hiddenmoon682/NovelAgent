# 工具调用模块设计评审与改进方案

> 日期：2026-07-20
> 来源：代码审查 + QuantClaw 参考
> 评审范围：`Agent::processSerial()` → `ToolCallLoop::run()` → `ToolPipeline::execute()` → `Conversation::apply()`

---

## Context

当前项目的工具调用模块经历了多轮审查（SERIAL_TOOL_CALL_REVIEW 两轮共 30 项发现，已全部修复）。在进入 Qt 前端阶段前，有必要审视整体架构是否合理。

---

## 当前架构总览

```
Agent::process()                        # 入口：输入校验 → 状态机 → 选路径
  └─ processSerial()                    # 串行路径
       ├─ conversation.addUser()        # 追加用户消息
       ├─ buildEffectivePrompt()        # 上下文组装 + 自动压缩
       ├─ ToolCallLoop::run()           # 核心循环
       │    ├─ LLM chat()              # 调 API
       │    ├─ ToolPipeline::execute()  # 执行所有工具（串行）
       │    └─ conversation.apply()     # 追加工具结果
       └─ 最终 assistant 消息           # 追加 LLM 回复到对话
```

**关键抽象**：
- `IToolProvider` — 工具提供者接口，ToolRegistry + RestrictedToolProvider 两个实现
- `ToolPipeline` — 管线：校验 → 执行 → 截断 → 生成 diff
- `ParameterValidator` — JSON Schema 校验（required/types/enums/additionalProperties）
- `ConversationDiff` — 批量修改描述，apply() 使用 copy-then-swap

---

## 当前设计中已经做对的地方

1. **`IToolProvider` 分层** — ToolCallLoop 和 ToolPipeline 都依赖接口而非具体类，RestrictedToolProvider 让 SubAgent 安全调用工具
2. **`ConversationDiff` + `apply()`** — copy-then-swap 提供强异常保证，避免局部修改污染对话
3. **`on_round_complete` hook** — 每轮回调用于 Token 记录 + 触发自动压缩
4. **Schema 缓存** — ToolPipeline 按工具名缓存 parameters schema，O(1) 查找
5. **Agent::process() 快照回滚** — `conversation_snapshot` 在异常时恢复对话（Fix #1）
6. **工具自注册** — `REGISTER_TOOL` 宏，新增工具只需两步

---

## 存在的设计问题与改进方案

### 问题一：工具执行全串行，无并行能力

**现状**：`ToolPipeline::execute()` 中 `for (const auto& tc : tool_calls)` 逐个执行。LLM 一次返回多个独立工具调用（如同时查角色和章节）时，耗时线性叠加。

**改进方案**：引入 `ThreadPool` 并行执行独立工具。

- 已有 `src/agent/tool/ThreadPool.h`（Phase 4 的并行子 Agent 基础设施）
- 工具间无依赖时并行，有依赖推断目前不现实（LLM 不表达依赖关系）
- 简单策略：所有工具并行，`ThreadPool::parallelFor` 即可

```cpp
// ToolPipeline 新增 parallelExecute()
std::vector<std::string> results(tool_calls.size());
ThreadPool pool(4);
pool.parallelFor(0, tool_calls.size(), [&](size_t i) {
    results[i] = executeOne(tool_calls[i]);
});
```

**收益**：多工具轮次延迟减半。风险低——状态工具在自己的数据文件上操作，不会冲突。

**注意**：默认串行（保持行为可预测），通过配置开关启用并行模式。

### 问题二：响应式溢出恢复（含错误码检测）

**现状**：ContextManager 只有主动式压缩（调用前检查用量百分比），缺少响应式兜底。当 LLM 返回 `context_length_exceeded` 时直接抛异常。

`HttpClient::parseApiError()` 返回的 JSON 错误码已包含 `context_length_exceeded` 等标记，但 ToolCallLoop 的 catch 块未区分处理。

**注意**：溢出错误（HTTP 400）不会触发 HttpClient 的 `isRetryableStatus()` 自动重试，因此会传播到 ToolCallLoop。

**方案**：在 `ToolCallLoop::run()` 的 `client_.chat()` try-catch 块中：
1. 检测错误消息是否包含 `context_length_exceeded` / `max context` 等溢出标记
2. 区分溢出 vs 其他异常
3. 溢出时调用 `ContextManager::compact()` 压缩后重试，最多 3 次

### 问题三：重复检测 JSON 键顺序不归一化

**现状**：`isRepeatedCall()` 原始 JSON 字符串做 key，`{"a":1,"b":2}` 和 `{"b":2,"a":1}` 被视为不同调用。连续两次调用同一工具但键顺序不同 → 永远不触发重复检测。

**方案**：parse + dump 归一化：

```cpp
std::string key = tool_name + ":" + nlohmann::json::parse(args_json).dump();
```

已在 Phase2 审查中确认，简单修复。

### 问题四：工具调用循环缺少动态轮数控制

**现状**：`max_rounds = 10` 硬上限。写小说场景可能有 3-5 轮工具调用（查设定→读章节→写内容→校验），但偶尔需要更多。

**方案**：让 `ToolCallLoop` 根据上下文用量动态调整轮数：
- `max_rounds = clamp(10, 32, 剩余预算 / 每轮估算)`
- 预算宽松时允许更多轮次，预算紧张时提前退出
- 参考 QuantClaw 的 `DynamicMaxIterations()`（32-160 范围）

### 问题五：工具粒度偏大——缺少 ToolChain（元工具）

**现状**：LLM 每次只能调一个工具，写新章节需要逐步调用 `read_outline → read_prev_chapter → write_chapter`，至少 3 轮。

**方案**：实现轻量 `chain` 工具——LLM 一次性定义步骤序列，系统顺序执行。

- 模板变量 `{{steps[0].result}}` 在步骤间传递
- 三种错误策略（stop/continue/retry）
- 注册为普通工具，走同一套管线

**写小说用例**：

| 场景 | 步骤 | 收益 |
|------|------|------|
| 写新章节 | read_outline → read_prev → check_characters → write | 3 轮→1 轮 |
| 批量更新角色 | read_characters → bulk_update | 2 轮→1 轮 |
| 重构设定 | read_setting → find_refs → update_setting → update_refs | 4 轮→1 轮 |

### 问题六：工具执行缺乏重试机制

**现状**：`executeOne()` 工具执行失败后返回 JSON error（含 `retryable: true`），但 `ToolPipeline` 自身不做重试。LLM 看到错误后在下一轮重试，浪费一轮 LLM 调用。

**方案**：在 Pipeline 层对带 `retryable: true` 的错误做即时重试（最多 2 次）：

```cpp
for (int attempt = 0; attempt <= max_retries; ++attempt) {
    auto result = tools_.execute(tc.function_name, args);
    if (result.contains("retryable") && result["retryable"].get<bool>() && attempt < max_retries) {
        spdlog::warn("重试工具 {} (第 {} 次)", tc.function_name, attempt + 1);
        continue;
    }
    return result;
}
```

### 问题七（可选）：完整的工具生命周期事件

**现状**：工具执行过程对外不透出事件。Qt 前端无法获取"工具 X 正在执行"的实时状态。

**方案**：在 ToolPipeline 中增加工具生命周期回调：

```cpp
struct ToolLifecycleCallbacks {
    std::function<void(const std::string& name, const nlohmann::json& args)> on_tool_start;
    std::function<void(const std::string& name, const std::string& result)> on_tool_complete;
    std::function<void(const std::string& name, const std::string& error)> on_tool_error;
};
```

通过 `StreamCallbacks` 透传到 UI 层，Qt 前端可在工具执行时显示进度。

---

## 优先级建议

| 优先级 | 改进 | 工作量 | 收益 |
|--------|------|--------|------|
| P0 | JSON 键顺序归一化（Bug #4） | ~5 行 | 重复检测生效，防止无限循环 |
| P0 | 响应式溢出恢复 | ~30 行 | 防止 context overflow 崩溃 |
| P1 | 工具级重试 | ~20 行 | 减少瞬态失败浪费的 LLM 轮次 |
| P1 | ToolChain 元工具 | ~200 行 | 减少 50%+ 工具调用轮次 |
| P2 | 动态轮数控制 | ~30 行 | 更好利用可用预算 |
| P2 | 并行工具执行 | ~50 行 | 多工具轮次加速 30-50% |
| P3 | 工具生命周期事件 | ~40 行 | Qt 前端实时显示进度 |

---

## 不推荐的改动

1. **拆 ToolCallLoop / ToolPipeline 负责面** — 当前界限已清晰，LC 负责"决策"，PL 负责"执行"
2. **把 ToolPipeline 合并到 ToolCallLoop** — 职责耦合，难以测试
3. **引入工具依赖图（DAG）** — 超出当前需求，写小说工具之间没有明确的数据流依赖
4. **将本轮所有工具结果合并成一条回复** — LLM 需要对每个工具结果独立响应

---

## 受影响的文件

| 文件 | 改动概要 |
|------|---------|
| `src/agent/core/CoreLoop.cpp` | JSON 归一化 + 溢出恢复 catch + 动态轮数 |
| `src/agent/tool/ToolPipeline.h/cpp` | 并行执行（可选）+ 工具级重试 + 生命周期回调 |
| `src/agent/tool/ToolPipeline.h` | `kMaxRetries` 常量 |
| `src/llm/ILLMClient.h` | 确保 chat() 异常含 context overflow 信息 |
| `src/tools/`（新建 chain_tool） | ToolChain 元工具实施 |
| 不修改：`Conversation.h`、`ParameterValidator.h`、`IToolProvider.h` | 接口稳定无需动 |

---

## 验证方式

1. **单元测试**：
   - `test_is_repeated_call_normalized()` — JSON 键顺序不同的重复检测
   - `test_tool_retry_transient()` — retryable 工具失败后即时重试
   - `test_parallel_execution()` — 多工具并行执行结果正确
   - `test_chain_basic()` / `test_chain_template()` — ToolChain 基本功能和模板替换

2. **端到端测试**：
   - 手动测试：连续两次相同参数调 `read_chapter` → 触发重复检测
   - 手动测试：触发 context overflow → 自动压缩 → 重试成功

3. **集成测试**：
   - `test_tool_call_loop.cpp` 补充溢出恢复 case

---

## 设计原则

- **默认保守，配置灵活**：串行是默认，并行需 opt-in
- **不破坏现有边界**：IToolProvider / ConversationDiff 接口不修改
- **增量改进**：不搞大重构，每个改进独立可测试
