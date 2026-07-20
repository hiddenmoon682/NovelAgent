# ToolChain 功能记录 & 参数校验讨论

> 日期：2026-07-20
> 来源：QuantClaw 参考审查

---

## 一、ToolChain 记录

### 什么是 ToolChain

ToolChain 是 QuantClaw 的一个**元工具**——注册为普通工具 `chain`，由 LLM 自主调用。其本质是"一个预定义步骤序列的宏"：

```
LLM 调 chain → 传入 steps: [toolA, toolB, toolC]
             → 系统按顺序执行，支持模板传递中间结果
             → 返回完整的步骤执行报告
```

### 核心数据结构

```cpp
struct ChainStep {
    std::string tool_name;
    nlohmann::json arguments;  // 可含 {{prev.result}} / {{steps[N].result}}
};

struct ToolChainDef {
    std::string name;
    std::vector<ChainStep> steps;
    ChainErrorPolicy error_policy;  // stop_on_error | continue_on_error | retry
    int max_retries = 1;
};

struct ChainResult {
    std::string final_result;
    std::vector<ChainStepResult> step_results;
    bool success;
};
```

### 模板变量机制

`ChainTemplateEngine` 递归遍历 JSON 参数中的所有字符串值，替换 `{{...}}` 模板：

- `{{prev.result}}` → 上一个步骤的结果全文
- `{{steps[0].result}}` → 第 0 步的结果全文
- 支持嵌套在 object 或 array 中

### 三种错误策略

| 策略 | 行为 |
|------|------|
| `stop_on_error`（默认） | 任一步骤失败立即停止，返回失败结果 |
| `continue_on_error` | 记录失败错误，继续后续步骤 |
| `retry` | 失败后重试，最多 `max_retries` 次 |

### 注册方式

```cpp
void ToolRegistry::RegisterChainTool() {
    // executor 注册到 tools_
    tools_["chain"] = [this](const json& params) {
        auto chain = ToolChainExecutor::ParseChain(params);
        ToolChainExecutor executor(
            [this](const string& name, const json& args) {
                return ExecuteTool(name, args);  // 链内工具也过权限检查
            }, logger_);
        auto result = executor.Execute(chain);
        return ResultToJson(result).dump();
    };
    // schema 注册到 tool_schemas_
    tool_schemas_.push_back({"chain", "Execute a pipeline of tools...",
                             {/* steps, name, error_policy, max_retries */}});
}
```

### LLM 调用示例

```json
// LLM 决定写新章节时，可以一次性调 chain
{
  "tool": "chain",
  "arguments": {
    "name": "write_chapter_pipeline",
    "error_policy": "stop_on_error",
    "steps": [
      {"tool": "read_outline",  "arguments": {"chapter_id": 5}},
      {"tool": "read_chapter",  "arguments": {"chapter_id": 4}},
      {"tool": "list_characters", "arguments": {"scope": "active"}},
      {"tool": "write_chapter", "arguments": {
        "chapter_id": 5,
        "outline": "{{steps[0].result}}",
        "previous": "{{steps[1].result}}",
        "characters": "{{steps[2].result}}"
      }}
    ]
  }
}
```

### 设计特点

1. **元工具模式** — 注册为普通工具 `chain`，走同一套权限/校验/截断管线
2. **链中链** — steps 内的工具仍可调 `chain`，形成嵌套
3. **与 AgentLoop 正交** — ToolChain 是 AgentLoop 中一次工具调用的内部展开，不改变外层循环
4. **LLM 自主决定** — LLM 可以选择用 chain 批量执行，也可以一步步手动调

### 对我们项目的价值

写小说场景的潜在用途：

| 流程 | 手动步骤 | 用 chain |
|------|---------|----------|
| 写新章节 | read_outline + read_prev_chapter + check_characters + write | 四条链成一个 chain |
| 修改角色 | read_character + list_appearances + update_character | 三步链 |
| 重构设定 | read_setting + find_cross_refs + update_setting + update_refs | 四步链 |

不过 LLM 手动一步步调也能完成，chain 只是减少轮次的优化。

---

## 二、LLM 参数传错的问题

### 当前防护

我们的工具参数通过 **JSON Schema** 定义，LLM 返回的是**命名参数**（key-value 对），不是位置参数：

```json
// 正确的传参
{"path": "chapter_1.md", "content": "第一章内容..."}

// 如果是位置参数，可能搞反：f(path, content) → f("第一章内容...", "chapter_1.md")
// 但命名参数不会！因为 key 不同
```

在我们的系统中，LLM 不可能"交换两个参数"——JSON 对象的键名决定了值的归属。

### 但 LLM 能犯的错误

| 错误类型 | 例子 | 检测方式 |
|----------|------|----------|
| **字段名拼错** | `charcter_id` 而不是 `character_id` | ✅ `additionalProperties: false` + `ParameterValidator` 拦截 |
| **缺少必填字段** | 没传 `content` | ✅ `checkRequired` 拦截 |
| **类型不对** | `chapter_id` 传了字符串 `"five"` 而非整数 5 | ✅ `checkTypes` 拦截 |
| **枚举值越界** | `tone` 传了 `"extreme"` 不在允许范围内 | ✅ `checkEnums` 拦截 |
| **值内容错误** | 把角色 A 的 ID 传给了角色 B 的字段 | ❌ 无法检测（语义错误） |
| **相似语义字段混淆** | `source` 和 `target` 互换 | ❌ 无法检测（语义错误） |

### 具体的"搞反"场景

两个同类型的字段确实可能被 LLM 搞混，比如：

```json
// 工具定义：
//   move_character(from_chapter_id: int, to_chapter_id: int)
//
// LLM 可能传：
{"from_chapter_id": 3, "to_chapter_id": 5}
// 但其实用户想从 5 移到 3

// 或者工具定义：
//   update_relationship(source: string, target: string, relationship: string)
// LLM 可能把 source 和 target 的 ID 填反
```

这种**语义级别的参数混淆**，Schema 校验无法检测，因为类型、字段名都正确。

### 当前的应对

1. **`additionalProperties: false`**（C4 修复）— 拼错字段名时直接拒绝，带建议信息
2. **参数校验失败 → 带 `retryable: true` 的错误返回** — LLM 看到错误描述后自行修正
3. **工具名区分度** — 把容易混淆的参数放到不同的工具中

```
ToolPipeline::executeOne() 中的逻辑：
  ① JSON parse 失败 → error + "请检查参数格式"
  ② Schema 校验失败 → error + "请根据 details 修正参数" (retryable=true)
  ③ 执行失败 → error (retryable 由具体工具决定)
  ④ 成功 → 返回结果
```

### 改进建议

如果以后发现 LLM 频繁搞混某些参数，可以考虑：

1. **参数描述更清晰** — 在 `ToolDefinition.description` 和参数 `description` 中强调区分
2. **合并相似工具** — 比如不要 `move_character(from, to)`，而是 `remove_character_from_chapter(chapter_id)` + `add_character_to_chapter(chapter_id)` 两步
3. **在工具执行内部加校验** — 比如 "from_chapter_id 和 to_chapter_id 不能相同" 这种语义约束
