# A4 预思考步骤重构：条件化 Thinking Step Detector

> 编写日期：2026-07-15
> 状态：待审查

## Context

当前 `use_thinking_step` 是 `ToolCallLoopConfig` 中的一个死布尔开关——定义了但**从未在任何地方设为 true**。用户希望将其改造为一个条件检测组件，类似已有的 `IParallelDetector` / `KeywordParallelDetector` 策略模式，只有当检测到用户当前执行的是**复杂任务**时才自动激活预思考步骤。

## 设计

### 核心思路

创建 `IThinkingStepDetector` 策略接口 + 默认 `KeywordThinkingStepDetector` 实现，完全复用 `IParallelDetector` 的既有模式。

### 1. 新增：`src/agent/ThinkingStepDetector.h`

策略接口 + 默认实现（纯头文件，无需 .cpp）：

```cpp
// IThinkingStepDetector — 预思考步骤的条件检测接口，
// 决定是否在 tool_call 循环前插入一轮无工具的 LLM 推理。
class IThinkingStepDetector {
public:
    virtual ~IThinkingStepDetector() = default;
    // user_input: 当前用户输入（最后一条 user 消息），用作复杂度判断依据。
    virtual bool shouldThink(const std::string& user_input) const = 0;
};
```

默认实现 `KeywordThinkingStepDetector`：
- **负向规则**（明显简单操作不思考）：
  - 读取/列出类："读一下", "列出", "show", "list", "查看", "打开" 等
  - 简单修改："改", "修改", "替换", "delete", "删除" + 短输入（< 80 字）
  - 简短提问：输入 < 30 字
- **正向规则**（复杂任务触发思考）：
  - 规划类："规划", "计划", "设计", "create", "新建", "创建"
  - 分析类："分析", "检查", "审查", "review", "评估"
  - 多步骤模式："先...然后", "首先...其次"
  - 多实体引用：提到多个章节/角色名（长度暗示）
  - 长输入（> 300 字）
  - 含"步骤" / "step" / "方案" 等规划关键词
- 计分制：正向命中 - 负向命中 >= 1 时触发，避免规则冲突

### 2. 修改：`src/agent/ToolCallLoop.h`

```cpp
// ToolCallLoopConfig 替换：
//   bool use_thinking_step = false;
// →
//   const IThinkingStepDetector* thinking_detector = nullptr;
//
// nullptr = 不启用预思考（默认，向后兼容）
// 非 null = 用此 detector 判断当前轮是否执行预思考
```

### 3. 修改：`src/agent/ToolCallLoop.cpp`

`run()` 方法中替换第 80 行的检查：

```cpp
// 旧：
if (config.use_thinking_step) { ... }

// 新：提取最后一条 user 消息，调用 detector 判断
if (config.thinking_detector) {
    // 从消息列表中提取当前用户输入
    std::string user_input;
    const auto& msgs = (initial_messages && !initial_messages->empty())
        ? *initial_messages : conversation.messages();
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
        if (it->role == llm::MessageRole::User) {
            user_input = it->content;
            break;
        }
    }
    if (!user_input.empty() && config.thinking_detector->shouldThink(user_input)) {
        // ... 原有 thinking step 逻辑不变
    }
}
```

### 4. 修改：`src/agent/IMessageProcessor.cpp`

`SerialProcessor::process()` 中配置 ToolCallLoop 时注入 detector：

```cpp
// 在创建 config 后添加：
static const KeywordThinkingStepDetector kDefaultThinkingDetector;
config.thinking_detector = &kDefaultThinkingDetector;
```

`SubAgent::execute()` 不变（detector 默认 nullptr，sub-agent 不触发性思考）。

### 5. 修改：`cmake/Sources.cmake`

在 `NOVELAGENT_AGENT` 列表中添加 `src/agent/ThinkingStepDetector.h`。

## 修改文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/agent/ThinkingStepDetector.h` | **新增** | 接口 + 默认实现，纯头文件 |
| `src/agent/ToolCallLoop.h` | 修改 | 替换 `use_thinking_step` 为 `thinking_detector` 指针 |
| `src/agent/ToolCallLoop.cpp` | 修改 | 条件检测代替布尔判断 |
| `src/agent/IMessageProcessor.cpp` | 修改 | SerialProcessor 注入 KeywordThinkingStepDetector |
| `cmake/Sources.cmake` | 修改 | 添加新文件到 NOVELAGENT_AGENT |

**不需要修改**：SubAgent.cpp、Agent.cpp、AgentOrchestrator.h（detector 默认 nullptr，行为不变）

## 验证

1. **编译**：`cmake --build build` 通过无 warning
2. **测试**：`ctest --test-dir build` 全部通过（现有测试不设 thinking_detector，行为完全不变）
3. **行为验证**：在 SerialProcessor 中临时在创建 detector 后打印 `shouldThink` 结果，验证短输入返回 false、长/复杂输入返回 true
4. **回归**：`grep -r "use_thinking_step" src/` 应只有旧注释（无代码引用）
