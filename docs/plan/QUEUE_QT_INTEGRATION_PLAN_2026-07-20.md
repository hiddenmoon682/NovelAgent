# 计划：消息队列 + Qt 前端集成

> 日期：2026-07-20
> 状态：待定（讨论阶段）
> 来源：QuantClaw 参考审查 + Qt 前端需求

---

## 背景

当前项目为 CLI 模式，REPL 阻塞等待 Agent 完成后用户才能输入下一条。后续要加入 Qt 前端，这带来几个新问题：

1. Agent 执行时用户想取消或纠正指令
2. 用户连续快速输入多条消息
3. Qt 主线程不能阻塞（Agent 需要在后台线程跑）

这些问题需要一个消息队列作为 Agent 工作线程和 Qt UI 线程之间的边界设施。

---

## 需求分析

### 场景

| 场景 | 需要的队列行为 |
|------|--------------|
| Agent 在写章节，用户想取消 | `kInterrupt` — 中止当前，处理新指令 |
| 用户发现上条指令写错了想纠正 | `kInterrupt` — 覆盖当前上下文 |
| 用户连续快速发多条指令 | `kCollect` — 缓冲到一批，等空闲一次性交付 |
| "写慢一点，多加点描写"（不改目标） | `kSteer` — 注入到运行中的上下文 |
| Qt 窗口关闭 | 优雅停止后台 agent 线程 |

### 不需要的

- 多通道并发（我们只有 Qt 一个前端）
- RPC 回调、WebSocket 事件推送
- `SessionLane` 多会话隔离
- `Followup` 模式（可被 Collect 覆盖）

---

## 设计方案

### 队列接口（轻量级）

```cpp
// 消息体
struct AgentCommand {
    std::string id;
    std::string message;

    enum Mode {
        kNormal,     // 正常排队，等 Agent 空闲后执行
        kInterrupt,  // 中止当前执行，处理新消息
        kSteer,      // 注入到当前上下文，不中止
    };
    Mode mode = kNormal;
};

// 线程安全队列
class AgentCommandQueue {
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<AgentCommand> pending_;
    std::atomic<bool> running_{true};

    void push(AgentCommand cmd);
    std::optional<AgentCommand> try_pop();   // Qt 侧非阻塞检查
    AgentCommand wait_and_pop();              // Agent 线程阻塞等待
    void clear();                             // 清空 pending
    void shutdown();                          // 停止队列（通知 Agent 退出）
};
```

### Agent 工作线程集成

```cpp
void AgentWorker::run() {
    while (running) {
        auto cmd = queue_.wait_and_pop();

        if (cmd.mode == kInterrupt) {
            tool_call_loop_.setCancelled(&cancelled_);
            // 等当前工具循环自己停（cancelled_ 检查点在每轮循环开头）
            // 停后用新消息重新开始
        }

        if (cmd.mode == kSteer) {
            // 将 cmd.message 注入到 conversation 末尾
            // 不中止，等下次 tool_call 边界生效
        }

        auto result = agent_.process(cmd.message);
        emit signal_result_ready(result);
    }
}
```

### 与现有代码的关系

```
                      ┌──────────────┐
  Qt UI 线程 ──push──▶│ CommandQueue │──wait_and_pop──▶ Agent 工作线程
                      │   (线程安全)  │
                      └──────────────┘
```

- **不修改 `ToolCallLoop` 核心逻辑** — 队列在 Agent 外层，不影响工具调用循环
- `ToolCallLoop` 已有 `cancelled_` 原子标志 + `setCancelled()` 接口，`kInterrupt` 直接利用
- `kSteer` 需要 `Conversation` 支持"注入消息到运行中对话"的方法（后续设计）

### 阶段建议

| 阶段 | 内容 | 依赖 |
|------|------|------|
| P1 | 实现 `AgentCommandQueue` 线程安全队列 | 无 |
| P2 | `AgentWorker` 封装：消息循环 + 队列 wait_and_pop | P1 |
| P3 | Qt 信号槽绑定 + `kInterrupt` 完整流程 | P2 |
| P4 | `kSteer` 注入模式 + Conversation 扩展 | P3 |

---

## 参考资料

- QuantClaw `CommandQueue` (`_ref/QuantClaw/src/gateway/command_queue.cpp`)
  - 五种 `QueueMode` 设计：Collect / Followup / Steer / SteerBacklog / Interrupt
  - Dispatcher + Worker 线程模型
  - 队列溢出控制（Summarize / DropOldest / Reject）
- 我们现有 `ToolCallLoop.h` 中的 `cancelled_` 原子标志（`setCancelled()` 接口已存在）
