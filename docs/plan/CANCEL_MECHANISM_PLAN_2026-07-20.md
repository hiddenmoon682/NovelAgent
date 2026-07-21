# 用户取消机制 —— 设计实现计划

> 日期：2026-07-20
> 来源：SERIAL_TOOL_CALL_REVIEW_PHASE2 #9 + #11
> 涉及修复：#9（取消检查位置）、#11（取消路径不设 rounds_executed）
> 新增功能：SSE 流式回调取消 + 主 Agent 取消信号 + Ctrl+C 处理

---

## 一、现状分析

### 当前取消机制的缺陷

| 问题 | 描述 |
|------|------|
| **#9** | `cancelled_` 检查在 `chat()` 调用**之后**，而非循环开始处（与 `ToolCallLoop.h:68` 注释矛盾） |
| **#11** | 取消退出路径不设置 `rounds_executed`，始终返回 0 |
| **无 SSE 级别取消** | `chat()` 的流式回调永远 `return true`，即使设置了 cancel 也要等当前 HTTP 请求跑完 |
| **主 Agent 未接取消信号** | `cancelled_` 仅由 `SubAgent` 超时路径设置（`SubAgent.cpp:85`）；主 Agent（`Agent::processSerial`）的 `ToolCallLoop.cancelled_` 恒为 `nullptr` |
| **无 Ctrl+C 处理** | `main.cpp` / `ReplHandler.cpp` 均无信号处理代码 |

### 核心调用链

```
ReplHandler::run()
  → Agent::process()
    → Agent::processSerial()
      → ToolCallLoop::run()
        → client_.chat()       ← 同步阻塞的 HTTP 请求
          → http_.postStreaming()
            → SSE 回调 (永远 return true)
        → 检查 cancelled_      ← 在 chat() 之后，太晚了！
```

---

## 二、总体设计

### 设计目标

1. **极速路径** — SSE 回调内检查取消信号，收到 `true` 后 `return false` 切断 socket 连接
2. **循环路径** — `ToolCallLoop` 每轮开始处检查取消信号（修复 #9）
3. **信号源** — `ReplHandler` 处理 Ctrl+C 后设置 Agent 的原子标志
4. **生命周期安全** — `atomic<bool>` 的所有权在 `Agent`，取消信号指针不悬空

### 最终时序（修完后）

```
用户按 Ctrl+C
    ↓ (SIGINT → signal handler)
Agent::cancel_requested_ = true
    ↓
 ┌─ ToolCallLoop::run() 循环开始 ──────────────┐
 │  if (cancelled_ && *cancelled_) → 直接退出   │  ← 修复 #9
 │                                              │
 │  response = client_.chat(..., cancel_flag)   │
 │    → http_.postStreaming(..., [&](data) {    │
 │        if (*cancel_flag) return false;        │  ← 新增：SSE 回调查看取消
 │        pipeline.feed(data);                  │
 │        return true;                          │
 │    })                                        │
 │    → 若 cancel_flag==true：conn 断开、chat()  │
 │      带部分数据返回或抛出                     │
 └──────────────────────────────────────────────┘
    ↓
chat() 返回（可能不完整）→ 处理部分数据后退出
    ↓
r.cancelled = true; r.rounds_executed = round;
```

---

## 三、详细变更

### 变更 1：`ILLMClient` 接口新增 `cancel_flag` 参数

**文件**: `src/llm/ILLMClient.h:63`

```cpp
// 新增可选参数：取消标志指针（非拥有）。
// 当 *cancel_flag == true 时，chat() 应在下一次 SSE 数据块到达时中止。
virtual LLMResponse chat(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools = {},
    const std::string& system_prompt = "",
    StreamCallbacks callbacks = {},
    const std::atomic<bool>* cancel_flag = nullptr) = 0;  // 新增
```

**理由**：
- 默认 `nullptr` → 不传时行为完全不变，已有代码无需改动
- `const std::atomic<bool>*` → 只读指针，`chat()` 不会修改调用方的标志
- 非拥有指针 → `chat()` 返回后不保留引用，调用方管理生命周期

**影响范围**：
- `LLMClient` 实现类
- 4 个 mock 类（`test_tool_call_loop.cpp:35`、`test_context_manager.cpp:36`、`test_sub_agent.cpp:19/47`）
- `ContextManager::compact()` 中的 `chatNonStreaming` 调用（不需要改，非流式不取消）
- `AgentOrchestrator` 中的 SubAgent 创建路径（不需要改，SubAgent 自行管理）

### 变更 2：`LLMClient::chat()` 实现中加入 SSE 回调检查

**文件**: `src/llm/LLMClient.cpp:160-164`

```cpp
auto res = http_.postStreaming(
    "/v1/chat/completions",
    body.dump(),
    [&](const char* data, size_t len) {
        // 新增：检查取消标志
        if (cancel_flag && *cancel_flag) {
            spdlog::warn("[LLMClient] 收到取消信号，中断流式响应");
            return false;  // httplib 关闭 socket
        }
        raw_response_body.append(data, len);
        pipeline.feed(std::string(data, len));
        return true;
    });
```

**为什么在回调里检查而非开启新线程**：
- `chat()` 是同步的，但 SSE 回调本来就是"中断点"
- httplib 的 `ContentReceiver` 返回 `false` → `read_content_with_length` 立刻 `return false`（`httplib.h:4287`）→ socket 关闭 → `postStreaming()` 返回
- 不需要额外线程、Future、Promise

### 变更 3：取消后 `chat()` 返回什么？

`cancel_flag == true` 后，SSE 回调 `return false` → httplib 关闭连接 → `postStreaming()` 返回 `httplib::Result`（错误状态）。

有两种处理方式可选：

**方案 A（推荐）：取消视为正常停止，返回部分响应**

```cpp
if (cancel_flag && *cancel_flag) {
    return false;
}
// ...
// httplib 返回错误（连接被中止）
if (!res) {
    // 检查是否因为取消导致的（last_error 留空）
    if (cancel_flag && *cancel_flag) {
        spdlog::warn("[LLMClient] 因用户取消而中断流式请求");
        // 返回已解析的部分 pipeline 响应
        if (pipeline.completed()) {
            return pipeline.response();
        }
        // pipeline 不完整：构造一个带 partial 标记的响应
        LLMResponse partial = pipeline.response();
        partial.content += "\n\n[... 响应因用户取消而中断 ...]";
        return partial;
    }
    throw std::runtime_error("LLM 请求失败: " + err);
}
```

**方案 B（简单）：取消视为异常，抛出**

```cpp
if (!res) {
    if (cancel_flag && *cancel_flag) {
        throw std::runtime_error("用户取消");
    }
    throw ...;
}
```

**推荐方案 A** 的理由：
- ToolCallLoop 的取消路径（`ToolCallLoop.cpp:86-108`）已经可以处理部分响应
- 调用方得到部分响应比收到异常更可控
- `buildRequestBody` 已经发送出去了，部分 token 已经消耗了，拿回来总比扔掉好

### 变更 4：`ToolCallLoop::run()` — 修复 #9

**文件**: `src/agent/ToolCallLoop.cpp:66-108`

将 `cancelled_` 检查从 `chat()` 之后移到循环开始处 + 传入 `chat()`：

```cpp
for (int round = 0; round < config.max_rounds; ++round) {
    // ── [修复 #9] 循环开始处检查取消，不浪费 LLM API 调用 ──
    if (cancelled_ && *cancelled_) {
        spdlog::warn("[ToolCallLoop] 取消信号，退出循环 (round={})", round);
        r.cancelled = true;
        r.rounds_executed = round;              // [修复 #11] 记录已执行轮数
        r.error = "任务已取消";
        return r;
    }

    // ── LLM 调用（传入 cancel_flag）──
    int estimated = ...;
    response = client_.chat(conversation.messages(), tools, system_prompt,
                            callbacks, cancelled_);   // 传入取消指针
    // ...
}
```

同时删除原来的取消检查块（原第 86-108 行）。

**注意**：
- `cancelled_` 本身是指针（`std::atomic<bool>*`），传入 chat 时直接传指针值即可
- 取消了就不处理 tool_calls，不追加 assistant 消息，不执行工具

### 变更 5：`Agent` 添加 `cancel_requested_` 成员

**文件**: `src/agent/Agent.h:160`（成员区）

```cpp
// ── 取消支持 ──
std::atomic<bool> cancel_requested_{false};   //  外部取消请求
```

**文件**: `src/agent/Agent.cpp:250`（`processSerial` 中）

```cpp
ToolCallLoop loop(*client_, registry_, &state_);
loop.setCancelled(&cancel_requested_);           // 新增：传入取消标志
```

**`Agent` 还需要添加一个公开方法供外部（信号处理器）调用**：

```cpp
// Agent.h
void requestCancel() { cancel_requested_ = true; }
```

### 变更 6：`ReplHandler` 注册 Ctrl+C 信号处理

**文件**: `src/main.cpp`

有两种方案：

**方案 A：`main.cpp` 中注册全局 SIGINT handler**

```cpp
#include <csignal>
#include <atomic>

std::atomic<bool>* g_cancel_target = nullptr;  // 全局指针，指向当前 Agent 的 cancel 标志

extern "C" void signalHandler(int) {
    if (g_cancel_target) {
        *g_cancel_target = true;
    }
}

int main() {
    signal(SIGINT, signalHandler);
    // ...
}
```

**缺点**：全局指针，信号处理器只能在 `C` 链接中做极简单的事（写原子变量）。

**方案 B（推荐）：`ReplHandler` 在输入循环中检查**

`ReplHandler::run()` 的输入循环（`ReplHandler.cpp:441-500`）是 `std::getline` 阻塞的。无法在信号处理器中直接设置取消后，可以通过让 `std::getline` 在信号后返回空或特殊输入来处理。

但是 `std::getline` 在收到 SIGINT 后，`std::cin` 的状态会变成 `fail()`，后续 `std::getline` 立即返回。需要重置 `std::cin` 状态。

更可靠的方案：

```cpp
// ReplHandler.cpp
#include <csignal>

static std::atomic<bool>* s_cancel_target = nullptr;

extern "C" void sigint_handler(int) {
    if (s_cancel_target) s_cancel_target->store(true);
}

void ReplHandler::run() {
    // 注册信号处理器
    auto old = signal(SIGINT, sigint_handler);
    s_cancel_target = &agent_.cancel_requested_;  // 暴露 cancel 目标

    // ... 现有代码 ...

    while (true) {
        // 输入前检查 agent 是否在处理请求 → 如果在处理，提示可按 Ctrl+C 取消
        // ...

        std::cout << Ansi::userInput() << "> " << Ansi::reset() << std::flush;
        if (!std::getline(std::cin, input)) {
            // Ctrl+C 导致 std::cin 进入 fail 状态
            if (std::cin.eof()) break;                    // Ctrl+D → 退出
            std::cin.clear();                             // 清空 fail 位
            // 如果有正在运行的任务，取消它
            if (!agent_.canAcceptInput()) {
                agent_.requestCancel();
                // 等待 Agent 处理完成
                // 输出取消确认消息？
                out_.write("\n" + Ansi::warning() + "已取消 AI 生成\n" + Ansi::reset());
            }
            continue;
        }
        // ...
    }

    // 恢复原信号处理器
    signal(SIGINT, old);
    s_cancel_target = nullptr;
}
```

**信号处理的关键时序**：

```
用户输入消息 → Agent::process() 开始 → chat() 开始 →
用户在流式输出中按 Ctrl+C
    ↓
SIGINT → signal_handler → Agent::cancel_requested_ = true
    ↓
cin 进入 fail 状态 → getline 返回 → clear() → 检查 agent 状态
    ↓
Agent::requestCancel() 已被信号处理器完成，进入等待
    ↓
chat() 的 SSE 回调检测到 cancel_flag == true
    ↓
return false → socket 关闭 → chat() 返回部分响应
    ↓
ToolCallLoop 循环顶部检查 cancelled_ → 退出
    ↓
Agent::process() 返回（响应可能不完整）
    ↓
ReplHandler 输出取消确认
```

### 变更 7：处理部分 `chat()` 返回后的对话一致性

当 `chat()` 因取消返回时，**什么消息都没有追加到对话中**——`ToolCallLoop` 的取消路径在 `chat()` 返回后直接退出循环，不调用 `addAssistantFromResponse()`。

所以对话状态就是：**用户消息在，但 LLM 没有输出 assistant 消息**。这比混入半截响应要干净。

如果需要下一轮用户输入能继续对话，`Agent::processSerial()` 的调用方（`Agent::process()`）在收到 `cancelled` 的 `InternalResult` 时，应该确保 conversation 仍然合法。

**检查点**：查看 `Agent::process()` 中是否对 `cancelled` 有特殊处理。

```cpp
// Agent.cpp:~280
if (result.cancelled)
    r.raw_response.finish_reason = "cancelled";
```

目前只有 finish_reason 标记。不再需要额外处理——对话状态一致。

---

## 四、变更清单汇总

| # | 文件 | 改动 | 性质 |
|---|------|------|------|
| 1 | `src/llm/ILLMClient.h:63` | `chat()` 新增 `const std::atomic<bool>* cancel_flag = nullptr` 参数 | 接口变更 |
| 2 | `src/llm/LLMClient.cpp:160` | SSE 回调中检查 `cancel_flag`，true 时 `return false` | 新增逻辑 |
| 3 | `src/llm/LLMClient.cpp:166+` | 取消后的 httplib 错误路径：区分取消与真错误，返回部分响应 | 新增逻辑 |
| 4 | `src/agent/ToolCallLoop.cpp:66` | 将 `cancelled_` 检查移到循环开始处；传入 `chat()` 的 `cancel_flag` | 修复 #9 |
| 5 | `src/agent/ToolCallLoop.cpp:86-108` | 删除旧的取消检查块（已移至循环顶部） | 清理 |
| 6 | `src/agent/ToolCallLoop.cpp:112` | 取消路径设置 `r.rounds_executed = round` | 修复 #11 |
| 7 | `src/agent/Agent.h:160` | 新增 `std::atomic<bool> cancel_requested_{false}` 成员 | 新增字段 |
| 8 | `src/agent/Agent.h:113+` | 新增 `void requestCancel()` 公开方法 | 新增接口 |
| 9 | `src/agent/Agent.cpp:250` | `processSerial()` 中 `loop.setCancelled(&cancel_requested_)` | 接入取消 |
| 10 | `src/main.cpp` | 注册 SIGINT 信号处理器 | 新增 |
| 11 | `src/cli/ReplHandler.cpp:441` | 在输入循环中处理 `cin` fail 状态 + 取消通知 | 新增处理 |
| 12 | 4 个 mock 文件 | `chat()` 签名增加 `cancel_flag` 默认参数 | 适配编译 |

### 受影响 mock 文件

```
tests/test_tool_call_loop.cpp:35  — MockSeqLLMClient::chat()
tests/test_context_manager.cpp:36 — CompactMockLLMClient::chat()
tests/test_sub_agent.cpp:19      — MockLLMClient::chat()
tests/test_sub_agent.cpp:47      — SlowMockLLMClient::chat()
```

每个 mock 的 `chat()` 签名加一个默认参数即可，mock 内部不需要实现取消逻辑（不影响现有测试行为）。

### `ContextManager::compact()` 的 `chatNonStreaming()` 调用

`ContextManager.cpp` 调用的是 `chatNonStreaming()`（非流式），不是 `chat()`。非流式不需要取消（它是一个同步 POST 等待完整响应），所以不改动。

---

## 五、测试计划

### 单元测试

| 测试场景 | 测试方法 | 覆盖 |
|---------|---------|------|
| SSE 回调检查 `cancel_flag` | 在 mock 的 `chat()` 中模拟 flag 为 true → 验证 SSE 回调返回 false | 变更 2 |
| `ToolCallLoop` 循环开始处取消 | `loop.setCancelled(&flag); flag = true; loop.run()` → 验证 0 轮 chat 调用，rounds_executed=0 | 变更 4 |
| 多轮后取消 | 设置 `max_rounds=5`，第 3 轮前设 flag → 验证 rounds_executed=3 | 变更 6 |
| 取消后不追加任何消息 | 取消路径返回后验证 conversation 状态不变 | 变更 7 |
| 传入 nullptr | 不设 cancel_flag → chat() 行为不变，回归测试 | 变更 1 |
| `Agent::requestCancel()` | 在异步线程调 requestCancel → 验证 `cancel_requested_` 变为 true | 变更 8 |

### 端到端测试（手动）

| 场景 | 步骤 | 预期 |
|------|------|------|
| Ctrl+C 中断流式输出 | 输入长提示，LLM 输出时按 Ctrl+C | 输出立刻停，对话可继续输入 |
| 工具循环中取消 | LLM 多轮工具调用时按 Ctrl+C | 当前轮 LLM 调用中止，退出循环 |
| 两次 Ctrl+C | 快速按两次 Ctrl+C | 安全，原子变量幂等 |
| 无任务时 Ctrl+C | 输入等待状态按 Ctrl+C | cin 恢复，无异常 |

---

## 六、设计决策记录

### 为什么选 `const std::atomic<bool>*` 而非其他方案？

| 方案 | 评价 |
|------|------|
| `std::shared_ptr<std::atomic<bool>>` | 引入引用计数开销，且 chat() 不拥有该标志 |
| `std::function<bool()> isCancelled` | 灵活但每次回调调用虚函数，且可引入闭包生命周期问题 |
| **`const std::atomic<bool>*`** | 零开销、语义清晰（非拥有、只读）、与 `ToolCallLoop::cancelled_` 类型一致 |

### 为什么不做成真正的异步 `chat()`？

| 维度 | 异步 | 本方案（同步+回调中断） |
|------|------|----------------------|
| 改动范围 | ILLMClient 返回类型、所有调用方、所有 mock | 加一个参数 |
| 并发安全 | Future/Promise 生命周期、线程同步 | 无新线程 |
| 取消延迟 | 取决于轮询间隔或 `std::future` 能否被中断 | 一个 TCP 数据包以内 |
| 现有异常处理 | 需要兼容同步/异步异常 | 完全不变 |

httplib 的 ContentReceiver 返回 false 就是"中断"语义——不需要改为异步。

### 信号处理为什么不放在 Agent 内部？

`Agent` 不含 `main` 循环或事件循环，无法注册信号处理器。信号处理器必须注册在有事件循环或输入循环的层（`ReplHandler` / `main.cpp`）。

---

## 七、风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| `chat()` 返回部分响应后 pipeline 不完整 | `pipeline.response()` 可能包含不完整的 tool_calls | 方案 A 返回部分 content，放弃不完整的 tool_calls |
| `std::cin` 在 Ctrl+C 后状态不确定 | 平台差异（Windows vs Linux） | `std::cin.clear()` + `ignore()` 确保状态恢复 |
| 取消后对话历史缺少 assistant 消息 | 用户下一条消息直接追加 user 消息，LLM 看到 user→user 序列 | 这是预期的——取消就是丢弃本轮 LLM 回复。与 `rewindTo()` 行为一致 |
| SIGINT 在信号处理器中写全局指针 | 信号不安全函数 | 原子变量 `store()` 是信号安全的 |
