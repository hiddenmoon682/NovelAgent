# ContextAssembler → ContextBudgetEvaluator 重构方案

## 动机

目前 `ContextAssembler` 类名与实际职责不匹配：

| 职责 | 现状 |
|------|------|
| 组装 system prompt（项目标题+logline+主题+工具指引） | 这些内容放到 system prompt 中实际用处不大，工具指引应由调用方在更外层处理 |
| Token 统计 + 预算评估 | 这是其剩下的核心工作 |

去掉无用的 project 元数据和工具指引后，这个类只剩 token 计数+状态评估，需要改名并简化。

## 涉及文件

| 操作 | 文件 |
|------|------|
| 🗑️ 删除 `buildSystemPrompt()` | `ContextAssembler.cpp` |
| 🔄 改名 | `ContextAssembler.h/cpp` → `ContextBudgetEvaluator.h/cpp` |
| 🔄 简化 | `AssemblyResult` → `BudgetEvaluationResult`（去掉 `system_prompt`、`system_tokens`） |
| 🔄 调用方适配 | `src/agent/core/Agent.h`（成员类型改名） |
| 🔄 调用方适配 | `src/agent/core/Agent.cpp`（2 处调用 + 工具指引迁入） |
| 🔄 测试适配 | `tests/test_context_manager.cpp` |
| 🔄 注册 | `cmake/Sources.cmake`（文件名变更） |

## 步骤

### Step 1: 清理 `ContextAssembler.cpp`

`buildSystemPrompt()` 整体删除。当前内容：

```cpp
std::string ContextAssembler::buildSystemPrompt(const Project& project) {
    std::string prompt;
    prompt += "# 项目: " + project.title + "\n";
    if (!project.logline.empty()) prompt += "Logline: " + project.logline + "\n";
    if (!project.theme.empty()) prompt += "主题: " + project.theme + "\n";
    prompt += "\n" + prompt::PromptContextBuilder::renderToolUseInstructions();
    return prompt;
}
```

- 项目元数据（title/logline/theme）→ 直接删除，没必要出现在 system prompt 中
- 工具指引（`renderToolUseInstructions()`）→ 移到 `Agent::buildEffectivePrompt()` 和 `Agent::execute()` 中，由调用方在更外层拼接

`assemble()` 随之简化：
- 不再需要 `project` 参数（system_prompt 不再需要构建）
- 不再计算 `system_tokens`（不再有 system prompt 动态部分）
- `system_prompt` 字段从结果中删掉

`#include "agent/prompt/PromptContextBuilder.h"` 从 `ContextBudgetEvaluator.cpp` 中移除（它不再引用任何 PromptContextBuilder 内容），迁入 `Agent.cpp`。

### Step 2: 改名 `ContextAssembler` → `ContextBudgetEvaluator`

头文件：
- 类名 `ContextAssembler` → `ContextBudgetEvaluator`
- 结果结构体 `AssemblyResult` → `BudgetEvaluationResult`
- `assemble()` → `evaluate()`（更准确反映其行为）

```cpp
struct BudgetEvaluationResult {
    int message_tokens = 0;      // 对话历史消息的 token 数
    int total_tokens = 0;        // 总 token 数
    ContextStatus status = ContextStatus::Normal;
    std::vector<std::string> warnings;
    bool fatal = false;
};

class ContextBudgetEvaluator {
public:
    BudgetEvaluationResult evaluate(const llm::IMemory& memory,
                                     const TokenBudget& budget,
                                     const std::string& model_name = {},
                                     const llm::TokenCounter* calibrator = nullptr) const;
};
```

对比旧 `AssemblyResult`，删掉了 `system_prompt` 和 `system_tokens`——它们已无实际用途（建出来的 system_prompt 没人用，旧的 system_prompt 通过 `memory.systemPrompt()` 单独管理）。

cmake 和 include 路径同步更新。

### Step 3: 迁入工具指引 → `Agent.cpp`

在 `Agent::buildEffectivePrompt()` 中，将工具指引放到 PromptComponents 的 task 段：

```cpp
std::string Agent::buildEffectivePrompt(llm::IMemory& memory)
{
    auto result = budget_evaluator_.evaluate(memory, budget_,
                                              client_->config().model, calibrator_);
    last_warnings_ = result.warnings;

    PromptComponents pc;
    pc.personality = memory.systemPrompt();
    if (project_) {
        pc.context = prompt::PromptContextBuilder::renderToolUseInstructions();
    }
    return PromptComposer::compose(pc);
}
```

在 `Agent::execute()` 中：

```cpp
std::string effective_system_prompt = memory_.systemPrompt();
if (project_) {
    auto result = budget_evaluator_.evaluate(memory_, budget_,
                                              client_->config().model, calibrator_);
    last_warnings_ = result.warnings;
    effective_system_prompt += "\n\n"
        + prompt::PromptContextBuilder::renderToolUseInstructions();
}
```

注意 `execute()` 之前调了 `assemble()` 但没用 `last_warnings_`，现在可以补上。

### Step 4: 更新 `Agent.h`

- `ContextAssembler assembler_;` → `ContextBudgetEvaluator budget_evaluator_;`
- 去掉 `#include "agent/context/ContextAssembler.h"`，改为新的头文件

### Step 5: 更新 `cmake/Sources.cmake`

- `ContextAssembler.cpp` → `ContextBudgetEvaluator.cpp`

### Step 6: 更新测试

`test_context_manager.cpp` 中：

1. `test_assemble_no_project` — 改为调用 `evaluate()`，不再检查 `system_prompt.empty()`（因为结果里不再有 system_prompt）
2. `test_build_system_prompt` — 整个删除（`buildSystemPrompt` 不存在了）
3. `test_build_system_prompt_no_chapter` — 整个删除
4. `test_total_tokens` — 更新为 `budget_evaluator_.evaluate(memory, budget)`，检查 `total_tokens`
5. `test_critical_warning` — 同样更新调用方式

### Step 7: 重命名文件

- `src/agent/context/ContextAssembler.h` → `src/agent/context/ContextBudgetEvaluator.h`
- `src/agent/context/ContextAssembler.cpp` → `src/agent/context/ContextBudgetEvaluator.cpp`

### Step 8: 构建+测试验证

```bash
cmake --build build --target test_context_manager  # 编译
./build/tests/test_context_manager.exe              # 测试
```

预计 `test_build_system_prompt*` 两个测试会被删除，其余测试全部通过。

## 验证方式

1. `cmake --build build` 编译通过
2. `./build/tests/test_context_manager.exe` 所有测试通过（减去已删除的两个）
3. 确认 `renderToolUseInstructions()` 仍出现在最终发给 LLM 的 system prompt 中（`buildEffectivePrompt()` 的日志或断点）
