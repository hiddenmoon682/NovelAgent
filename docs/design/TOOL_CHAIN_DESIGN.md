# ToolChain 设计文档

> 日期：2026-07-21
> 状态：设计提案（待实现）
> 来源：QuantClaw 参考审查
> 相关文档：[TOOLCHAIN_AND_PARAMETER_VALIDATION_2026-07-20.md](../review/TOOLCHAIN_AND_PARAMETER_VALIDATION_2026-07-20.md)

---

## 一、动机

### 1.1 问题

当前串行工具调用路径中，每个工具步骤都是一次独立的 LLM 往返：

```
LLM → (调 toolA) → 返回结果 → LLM 思考 → (调 toolB) → 返回结果 → LLM 思考 → ...
```

对于**步骤明确、无分支**的流程（如"读大纲→读前一章→查角色→写新章"），每步之间的 LLM 思考是冗余的——LLM 在看结果之前就已经知道下一步该做什么。

### 1.2 收益

- **减少 LLM 调用次数**：4 步链式流程从 4 轮降为 1 轮
- **降低 Token 消耗**：跳过 N-1 次对话历史 + system_prompt + tool definitions 的重传
- **缩短响应时间**：链内步骤零网络往返，全在本地执行

---

## 二、设计目标

1. **元工具模式** — 注册为普通工具 `chain`，走现有 ToolPipeline 的同一套权限/校验/截断管线
2. **与 AgentLoop 正交** — chain 是单次工具调用的内部展开，不改变外层 `ToolCallLoop` 的循环语义
3. **模板传递** — 支持 `{{prev.result}}` / `{{steps[N].result}}` 占位符引用前序结果
4. **错误安全** — 链内任一工具失败不污染对话、不妨碍其他步骤
5. **渐进复杂度** — MVP 仅实现 `stop_on_error` + 线性步骤，后续可扩展

---

## 三、数据结构

### 3.1 链步骤定义

```cpp
// 链中的一个步骤
struct ChainStep {
    std::string tool_name;         // 工具名（如 "read_chapter"）
    nlohmann::json arguments;      // 参数 JSON，支持模板语法
};

// LLM 传入 chain 工具的顶层参数
struct ChainParams {
    std::string name;                          // 链名称（日志/调试用）
    std::vector<ChainStep> steps;              // 有序步骤列表
    std::string error_policy = "stop_on_error"; // stop_on_error | continue_on_error
};
```

### 3.2 步骤执行结果

```cpp
struct ChainStepResult {
    std::string tool_name;
    bool success = false;
    std::string error;                         // 失败原因（仅 !success 时）
    nlohmann::json result;                     // 执行结果 JSON（仅 success 时）
    int duration_ms = 0;                       // 该步骤耗时
};

struct ChainResult {
    std::vector<ChainStepResult> step_results;
    bool all_success = false;
    bool partial = false;                      // 部分步骤失败（continue_on_error 时）
    std::string final_result;                  // 汇总文本
};
```

### 3.3 隶属关系

```
ToolCallLoop           ← 外层串行循环
  └── ToolPipeline     ← 单次工具调用执行
        └── chain      ← 元工具（注册为普通工具）
              ├── step 0: read_outline
              ├── step 1: read_chapter      ← 链内步骤，不触发 LLM
              ├── step 2: list_characters
              └── step 3: write_chapter     ← 最后一步的 result 汇总为 chain 的最终输出
```

---

## 四、模板引擎

### 4.1 设计

`ChainTemplateEngine` 负责递归遍历 JSON 参数中的所有字符串值，替换 `{{...}}` 占位符。

```cpp
class ChainTemplateEngine {
public:
    // 在 arguments 中替换模板变量，返回替换后的 JSON。
    // step_results      已完成步骤的结果列表
    // current_step_idx  当前正在处理的步骤索引
    static nlohmann::json resolve(
        const nlohmann::json& arguments,
        const std::vector<ChainStepResult>& step_results,
        size_t current_step_idx);
};
```

### 4.2 支持的占位符

| 语法 | 含义 | 示例 |
|------|------|------|
| `{{prev.result}}` | 上一个步骤的完整结果 JSON（dump 后） | `"outline": "{{prev.result}}"` |
| `{{steps[N].result}}` | 第 N 步的完整结果 JSON | `"prev_chapter": "{{steps[1].result}}"` |
| `{{prev.result.field}}` | 上一个步骤结果中的某个字段 | `"title": "{{prev.result.title}}"` |
| `{{steps[N].result.field}}` | 第 N 步结果中的某个字段 | `"pov": "{{steps[0].result.pov_character_id}}"` |

### 4.3 实现要点

```cpp
nlohmann::json ChainTemplateEngine::resolve(
    const nlohmann::json& args,
    const std::vector<ChainStepResult>& done_steps,
    size_t current_idx)
{
    if (args.is_string()) {
        std::string s = args.get<std::string>();
        // 替换 {{prev.result}} → done_steps[current_idx-1].result.dump()
        s = replacePlaceholder(s, "prev.result", [&] {
            if (current_idx == 0) return std::string{};
            return done_steps[current_idx - 1].result.dump();
        });
        // 替换 {{steps[N].result}} → done_steps[N].result.dump()
        // 替换 {{prev.result.field}} / {{steps[N].result.field}}
        // ...
        return s;
    }
    if (args.is_object()) {
        nlohmann::json result = nlohmann::json::object();
        for (auto& [key, val] : args.items())
            result[key] = resolve(val, done_steps, current_idx);
        return result;
    }
    if (args.is_array()) {
        nlohmann::json result = nlohmann::json::array();
        for (auto& val : args)
            result.push_back(resolve(val, done_steps, current_idx));
        return result;
    }
    return args;  // 数字、布尔、null 不处理
}
```

**安全措施**：
- 替换前调用 `result.dump()` 产生 JSON 字符串，注入到参数后由下游工具的 JSON Schema 校验
- 字段引用（`field`）用 nlohmann::json::at() 做，key 不存在时替换为空字符串而非抛异常
- 最大替换层级 5 层，防止循环引用死递归（安全网）

---

## 五、执行引擎

### 5.1 核心流程

```
ChainExecutor::run(params)
  │
  ├─ for (i = 0; i < steps.size(); ++i)
  │   ├─ ChainTemplateEngine::resolve(args, done_steps, i)
  │   ├─ result = executor(tool_name, resolved_args)  ← 直接调 executeTool，不走 LLM
  │   ├─ record step_result
  │   ├─ if (!result.success && error_policy == stop_on_error)
  │   │     └─ 终止，返回失败
  │   └─ continue
  │
  └─ 汇总 all step_results → ChainResult
```

### 5.2 与 ToolPipeline 的集成

通过 `IToolProvider` 复用现有的工具注册和权限校验：

```cpp
class ChainExecutor {
public:
    ChainExecutor(IToolProvider& tools)
        : tools_(tools) {}

    ChainResult execute(const ChainParams& params) {
        ChainResult result;
        for (size_t i = 0; i < params.steps.size(); ++i) {
            auto& step = params.steps[i];
            ChainStepResult sr;
            sr.tool_name = step.tool_name;

            // ① 模板替换
            auto resolved = ChainTemplateEngine::resolve(
                step.arguments, result.step_results, i);

            // ② 执行工具（走 IToolProvider.execute，过权限+校验+截断管线）
            auto t_start = std::chrono::steady_clock::now();
            try {
                auto json_result = tools_.execute(step.tool_name, resolved);
                auto t_end = std::chrono::steady_clock::now();
                sr.duration_ms = /* ... */;
                sr.success = !json_result.contains("error");
                sr.result = std::move(json_result);
            } catch (...) {
                sr.success = false;
                sr.error = "系统异常";
            }

            result.step_results.push_back(std::move(sr));

            // ③ 错误策略判断
            if (!sr.success && params.error_policy == "stop_on_error")
                return result;
        }
        result.all_success = true;
        return result;
    }

private:
    IToolProvider& tools_;
};
```

### 5.3 对工具结果的处理

链内步骤的工具结果**不写入 Conversation**。以写章节 chain 为例：

```
chain 执行前：       对话 = [user: "写第5章"]

  ├─ read_outline → 结果存在 ChainStepResult 中（不进对话）
  ├─ read_chapter → 同上
  ├─ list_characters → 同上
  └─ write_chapter → 同上

chain 执行后：
  chain 工具返回 → {"step_results": [...], "final_result": "...", "all_success": true}
  该结果以普通 tool_result 消息写入 Conversation（由 ToolPipeline 统一处理）
  LLM 看到的是 chain 的汇总结果
```

这样设计的理由：
- **对话不会被链内中间结果污染** —— 5 步链不会塞入 5 条 tool_result + 5 条 assistant
- **与现有管线完全兼容** —— chain 工具返回的是 `nlohmann::json`，走标准截断流程
- **LLM 看到的是汇总而非细节** —— 除非某步骤失败需要分析原因

---

## 六、Schema 定义

```cpp
// chain 工具的 parameters schema
json chain_parameters = utils::schema::object({
    {"name", utils::schema::stringProp("链的名称，用于日志和调试")},
    {"error_policy", utils::schema::stringEnum("错误策略",
        {"stop_on_error", "continue_on_error"})},
    {"steps", utils::schema::arrayProp("步骤列表（按顺序执行）",
        utils::schema::object({
            {"tool_name", utils::schema::stringProp("工具名")},
            {"arguments", utils::schema::objectProp("工具参数，支持 {{prev.result}} 模板语法")}
        }, {"tool_name", "arguments"}))}
}, {"name", "steps"});
```

---

## 七、使用场景

### 7.1 写新章节（最有用）

```json
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

### 7.2 批量更新角色

```json
{
  "tool": "chain",
  "arguments": {
    "name": "update_character_arc",
    "steps": [
      {"tool": "read_chapter", "arguments": {"chapter_id": 3, "include_content": true}},
      {"tool": "update_character", "arguments": {
        "character_id": "char-001",
        "fields": {
          "arc_summary": "第 3 章中：{{steps[0].result.content}}"
        }
      }}
    ]
  }
}
```

### 7.3 简单双步骤（模板测试）

```json
{
  "tool": "chain",
  "arguments": {
    "name": "read_then_verify",
    "steps": [
      {"tool": "get_chapter_context", "arguments": {"chapter_id": "ch-001"}},
      {"tool": "read_chapter", "arguments": {
        "chapter_id": "{{prev.result.chapter_id}}",
        "include_content": true
      }}
    ]
  }
}
```

---

## 八、错误策略详解

### `stop_on_error`（默认）

```
步骤 0: ✅ success
步骤 1: ❌ error("角色不存在")
         → 立即终止，返回：
           {"all_success": false,
            "step_results": [
              {..., "success": true},
              {..., "success": false, "error": "角色不存在"}
            ]}
         → LLM 看到错误，自行修正后重试
```

### `continue_on_error`

```
步骤 0: ✅ success
步骤 1: ❌ error("角色不存在")
步骤 2: ✅ success（不依赖步骤1）
         → 返回：
           {"all_success": false,
            "partial": true,
            "step_results": [
              {..., "success": true},
              {..., "success": false, "error": "角色不存在"},
              {..., "success": true}
            ]}
         → LLM 看到部分失败，决定忽略还是修正后重试
```

**MVP 仅实现 `stop_on_error`**。`continue_on_error` 复杂度在于：部分步骤失败后后续步骤的模板引用可能拿到空值，需要定义行为。留待 Phase 2。

---

## 九、实现计划

### Phase 1 — MVP（估算：~250 行）

| 步骤 | 文件 | 内容 | 估算行数 |
|------|------|------|---------|
| 1 | `src/agent/tools/ChainTool.h` | 数据结构 + 引擎声明 | ~60 |
| 2 | `src/agent/tools/ChainTool.cpp` | 模板引擎 + 执行器 + 工具注册 | ~120 |
| 3 | `tests/test_chain_tool.cpp` | 基础测试：单步/多步/模板/错误中止 | ~100 |

**MVP 范围**：
- [x] 线性顺序执行
- [x] `{{prev.result}}` / `{{steps[N].result}}` 模板替换
- [x] `stop_on_error` 策略
- [x] 通过 `REGISTER_TOOL` 注册
- [x] 基础错误返回（工具不存在、执行异常）

### Phase 2 — 增强（待定）

| 功能 | 说明 |
|------|------|
| `continue_on_error` | 部分失败后继续，结果标注 `partial: true` |
| `{{prev.result.field}}` / `{{steps[N].result.field}}` | 字段级模板引用 |
| 链内超时 | 单步骤最长执行时间（防止 write_chapter 无限等待） |
| `retry` 策略 | 失败自动重试（调用 `IToolProvider::execute` 本身就有重试） |

### Phase 3 — 高级（待定）

| 功能 | 说明 |
|------|------|
| 条件分支 | `if_success`/`if_failed` 按前一步结果选择下一步 |
| 并行步骤 | 无数据依赖的步骤可以并发执行 |
| 嵌套 chain | steps 内再调 chain（当前 IToolProvider 天然支持） |

---

## 十、风险与注意事项

| 风险 | 影响 | 缓解 |
|------|------|------|
| LLM 过度使用 chain | 丧失灵活适应能力 | 不作为默认推荐，仅当步骤序列确定时使用 |
| 模板替换后 JSON 非法 | 下游工具解析失败 | 模板替换后的参数仍走现有 Schema 校验 |
| 链内错误信息不完整 | LLM 难以定位失败原因 | `ChainStepResult` 包含完整 error + 失败步骤的入参快照 |
| 长 chain 超时 | 链内 10 步骤耗时过长 | 外层 `ToolCallLoop.max_rounds` 不受影响（chain 算 1 轮），但单次调用总时间应加软限制 |
| 模板引擎递归循环 | 栈溢出 | 最大替换层级 5 层安全网 |

---

## 十一、未解决问题

1. **`steps[N].result` 引用未完成的步骤** — LLM 可能在 chain 参数中引用还未执行的步骤（如 `{{steps[2].result}}` 但 steps 只有 2 步）。当前设计直接替换为空字符串，不做警告。需要决定：是静默替换还是返回错误？
2. **结果过大** — chain 聚合返回的结果可能超过 `kMaxResultChars`（22000 字符）。但现有 `ToolPipeline::executeOne()` 已有 content 字段截断逻辑，chain 的汇总结果也会走同一套截断。
3. **LLM 如何学会用 chain** — 需要在 system_prompt 的工具使用说明中加入 chain 的用法示例，或将 chain 作为高级工具仅在特定场景推荐。
4. **与现有工具的配合** — 某些工具（如 `write_chapter`）已有 `auto_save` 参数，在 chain 内调用时是否需要有特殊行为？

---

## 附录 A：LLM 参数传错的问题（来自 QuantClaw 参考审查）

### A.1 当前防护机制

工具参数通过 **JSON Schema** 定义，LLM 返回的是**命名参数**（key-value 对）而非位置参数：

```json
{"path": "chapter_1.md", "content": "第一章内容..."}
```

命名参数天然避免位置参数的"搞反"问题——JSON 对象的键名决定了值的归属。

### A.2 LLM 可能犯的参数错误

| 错误类型 | 例子 | 检测方式 |
|----------|------|----------|
| **字段名拼错** | `charcter_id` 而不是 `character_id` | ✅ `additionalProperties: false` + `ParameterValidator` 拦截 |
| **缺少必填字段** | 没传 `content` | ✅ `checkRequired` 拦截 |
| **类型不对** | `chapter_id` 传了字符串 `"five"` 而非整数 5 | ✅ `checkTypes` 拦截 |
| **枚举值越界** | `tone` 传了 `"extreme"` 不在允许范围内 | ✅ `checkEnums` 拦截 |
| **值内容错误** | 把角色 A 的 ID 传给了角色 B 的字段 | ❌ 无法检测（语义错误） |
| **相似语义字段混淆** | `source` 和 `target` 互换 | ❌ 无法检测（语义错误） |

### A.3 当前应对措施

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

### A.4 改进建议（未来参考）

如果以后发现 LLM 频繁搞混某些参数，可以考虑：

1. **参数描述更清晰** — 在 `ToolDefinition.description` 和参数 `description` 中强调区分
2. **拆分工具** — 将同类型参数的工具拆分为多个单参数工具，如 `remove_character_from_chapter(chapter_id)` + `add_character_to_chapter(chapter_id)` 替代 `move_character(from, to)`
3. **工具执行内部加语义校验** — 如 `from_chapter_id` 和 `to_chapter_id` 不能相同这类 Schema 校验无法表达的约束

### A.5 对 ToolChain 的意义

ToolChain 的模板替换（`{{prev.result}}` / `{{steps[N].result}}`）引入了一种新的参数传错风险：**模板替换后的 JSON 可能因类型不匹配或字段路径错误而被下游工具的 Schema 校验拒绝**。好在现有 `ParameterValidator` 会拦截这类错误并以 `retryable: true` 返回，LLM 自行修正即可。不需要为 chain 设计额外的参数校验机制。
