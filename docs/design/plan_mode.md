# Plan Mode：用户可控的预思考步骤

> **状态：已废弃（OBSOLETE）**
> 旧预思考代码（`use_thinking_step`）已于 2026-07-16 全面清理，此文档中的代码引用全部失效。
> 实现 Plan Mode 时需从零设计，请勿参考此处的代码实现。
>
> 编写日期：2026-07-15

## 问题

A4 预思考步骤的实现存在两个问题：

1. **无用户控制入口**：`use_thinking_step` 是死布尔开关，从未设为 true
2. **思考消息永久驻留**：`conversation.addAssistant()` 将完整思考输出留在对话中，每轮都被 LLM 重复读取，浪费上下文

## 方案：Plan Mode

增加 **Plan Mode（规划模式）**，用户通过 `/plan on|off` 手动切换（与 `/parallel on|off` 对等）。

开启后，thinking step 的输出**拆分为两部分**：推理部分（analysis）用完即弃，计划部分（plan）持久化到对话中供后续参考。

```
/plan on
  → 首轮 LLM 推理（无工具）
    → 输出拆解：
      ├─ 推理分析（"用户想要分析第三章角色弧光…"）→ 临时，仅首轮 LLM 可见
      └─ 执行计划（"【计划】1. 读第三章 2. 查角色设定 3. 对比分析"）→ 持久化到 conversation
  → 首轮 LLM 调用（带工具）：看到原消息 + 完整思考输出
  → 后续轮次：conversation 中含【计划】，LLM 可对照执行
```

## 设计

### 1. 修改 thinking prompt，约定分隔标记

**`src/agent/ToolCallLoop.cpp`**，第 81-87 行的 prompt 追加：

```cpp
std::string thinking_prompt = system_prompt + "\n\n"
    "在调用任何工具之前，请先分析当前任务：\n"
    "1. 用户想要什么？\n"
    "2. 我已经知道哪些信息？\n"
    "3. 我还需要获取哪些信息？需要调用什么工具？\n"
    "4. 我的计划是什么？\n\n"
    "请先输出你的分析，然后以"【计划】"开头列出具体执行步骤。\n"
    ""【计划】"后面的内容将作为后续执行的参考计划。";
```

### 2. 拆分思考输出：推理临时 + 计划持久

**`src/agent/ToolCallLoop.cpp`**，第 80-96 行替换为：

```cpp
if (config.use_thinking_step) {
    const auto& think_msgs = (initial_messages && !initial_messages->empty())
        ? *initial_messages : conversation.messages();
    auto think_response = client_.chatNonStreaming(think_msgs, {}, thinking_prompt);

    if (!think_response.content.empty()) {
        // 按分隔标记拆分：推理部分临时、计划部分持久
        std::string reasoning_part, plan_part;
        auto sep = think_response.content.find("【计划】");
        if (sep != std::string::npos) {
            reasoning_part = think_response.content.substr(0, sep);
            plan_part = think_response.content.substr(sep);
        } else {
            reasoning_part = think_response.content;
        }

        // 构建临时消息列表：原消息 + 完整思考输出（推理+计划），供首轮 LLM 使用
        augmented_msgs = think_msgs;
        augmented_msgs.push_back(
            llm::Message::assistant(think_response.content));
        has_augmented = !augmented_msgs.empty();

        // 计划部分持久化到 conversation，后续轮次可对照执行
        if (!plan_part.empty()) {
            conversation.addAssistant(plan_part);
        }

        if (tracer_)
            tracer_->record("thinking_step", think_response.total_tokens, 0);
    }
}
```

三个关键点：
- **完整思考输出（推理+计划）→ `augmented_msgs`**：首轮 LLM 调用能同时看到推理过程和执行计划
- **计划部分 → `conversation`**：计划消息留在对话中，后续 tool_call 各轮次均可引用
- **推理部分不单独保留**：`reasoning_part` 是局部 string，随 `augmented_msgs` 一起析构

### 3. 首轮 LLM 调用使用临时消息列表

```cpp
const auto& first_msgs = has_augmented 
    ? augmented_msgs 
    : (initial_messages && !initial_messages->empty() ? *initial_messages : conversation.messages());
```

`has_augmented` 为 true 时使用临时列表（含完整的推理+计划），否则回退到原始行为。

### 4. Agent 层：添加 plan_mode_ 标记

**`src/agent/Agent.h`**：

```cpp
void setPlanMode(bool on) { plan_mode_ = on; if (processor_) processor_->setPlanMode(on); }
bool isPlanMode() const { return plan_mode_; }
```

私有成员：`bool plan_mode_ = false;`

### 5. IMessageProcessor 接口：传递 plan mode

**`src/agent/IMessageProcessor.h`**：

```cpp
// 接口
virtual void setPlanMode(bool /*on*/) {}

// SerialProcessor override
void setPlanMode(bool on) override { plan_mode_ = on; }
bool plan_mode_ = false;  // 私有成员
```

### 6. SerialProcessor：注入 ToolCallLoopConfig

**`src/agent/IMessageProcessor.cpp`**：

```cpp
config.use_thinking_step = plan_mode_;
```

### 7. ReplHandler：CLI 命令

**`src/cli/ReplHandler.cpp`**：

```cpp
parser_.registerCommand("plan", "/plan on|off — 规划模式（预思考）", [this](const auto& args) {
    if (args.empty()) {
        out_.write(Ansi::dim() + std::string(agent_.isPlanMode() ? "规划模式" : "非规划模式") + "，/plan on 或 off 切换\n" + Ansi::reset());
    } else if (args[0] == "on") { agent_.setPlanMode(true); out_.write(Ansi::success() + "已开启规划模式\n" + Ansi::reset()); }
    else if (args[0] == "off") { agent_.setPlanMode(false); out_.write(Ansi::success() + "已关闭规划模式\n" + Ansi::reset()); }
    return true;
});
```

更新 tab 命令列表，添加 `plan`。

### 8. 不需要修改的文件

| 文件 | 原因 |
|------|------|
| `ToolCallLoop.h` | `use_thinking_step` 字段已存在，无需改动 |
| `SubAgent.cpp` | 子任务不需要预思考 |
| `Agent.cpp` | `setPlanMode` 在头文件中内联 |
| `cmake/Sources.cmake` | 无新增文件 |

## 验证

1. **编译**：`cmake --build build` 通过
2. **回归**：`ctest --test-dir build` 全部通过（`plan_mode_` 默认 false，零行为变化）
3. **CLI**：`/plan`、`/plan on`、`/plan off` 正常工作
4. **功能**：plan mode ON 时发复杂请求 → 日志先输出思考步骤；首轮 LLM 调用包含推理+计划；对话中含【计划】消息
5. **Token 验证**：首轮完成后检查对话，应只有【计划】部分，无推理杂音
6. **多轮验证**：plan mode 下执行多轮 tool_call，每轮 LLM 都能看到【计划】并对照执行
