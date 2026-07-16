# Plan Mode 实现前清理计划

> 编写日期：2026-07-16
> 目的：彻底清理当前代码库中所有与预思考相关的代码，为 Plan Mode 从零设计扫清障碍。
> 原则：不留旧代码干扰，实现 Plan Mode 时重新设计、重新写入。

---

## 一、清理范围总览

当前需清理的预思考代码分两类：

| 类别 | 范围 | 数量 |
|------|------|------|
| 🔴 源码（需删除） | `ToolCallLoop.h` + `ToolCallLoop.cpp` 中的预思考逻辑 | **2 处** |
| 🟡 引用文档（需同步更新） | `CHANGELOG.md`、`REVIEW_STATUS.md`、`plan_mode.md` | **3 处** |
| ✅ 已删除 | `docs/design/thinking_step_detector.md` | 1 处（已完成） |

---

## 二、🔴 需删除的源码

### 2.1 `src/agent/ToolCallLoop.h` — 删除 `use_thinking_step` 字段

**位置**：`ToolCallLoopConfig` 结构体，`max_reflection_rounds` 之后

**删除内容**（含注释，共 5 行）：
```cpp
    // A4: 启用预思考步骤。开启后在工具调用循环前额外做一轮无工具的 LLM 调用，
    // 让 LLM 先推理再行动（ReAct 模式：思考→行动→观察）。
    // 默认 false 以节省 token。对复杂创作任务（规划章节、分析角色弧光）建议开启。
    bool use_thinking_step = false;
```

**操作**：删除上述 5 行，保留紧邻的 `};` 不变。

---

### 2.2 `src/agent/ToolCallLoop.cpp` — 删除预思考步骤代码块

**位置**：`executeLoop` lambda 内，首轮 LLM 调用之前

**删除内容**（含注释，共 18 行）：
```cpp
        // ── A4: 预思考步骤（可选）—— ReAct 模式的"思考"阶段 ──
        // 在暴露工具之前让 LLM 先推理任务目标和信息需求。
        // 此步骤不提供工具定义，LLM 只能输出文本推理，不能调用工具。
        // 推理文本作为 assistant 消息注入对话，后续工具循环可以引用。
        if (config.use_thinking_step) {
            std::string thinking_prompt = system_prompt + "\n\n"
                "在调用任何工具之前，请先分析当前任务：\n"
                "1. 用户想要什么？\n"
                "2. 我已经知道哪些信息？\n"
                "3. 我还需要获取哪些信息？需要调用什么工具？\n"
                "4. 我的计划是什么？\n\n"
                "请先输出你的分析，然后我会让你调用工具来执行计划。";
                
            const auto& think_msgs = (initial_messages && !initial_messages->empty())
                ? *initial_messages : conversation.messages();
            auto think_response = client_.chatNonStreaming(think_msgs, {}, thinking_prompt);
            if (!think_response.content.empty()) {
                conversation.addAssistant(think_response.content);
                if (tracer_) tracer_->record("thinking_step", think_response.total_tokens, 0);
            }
        }
```

**操作**：删除上述 19 行（含注释 + `if` 块），保留后续 `// ── 首轮（带工具）──` 注释不变。

**注意**：删除后 `config` 参数中不再使用 `use_thinking_step` 字段，它是 `ToolCallLoopConfig` 中唯一引用该字段的地方，因此 `ToolCallLoop.h` 中的字段删除后不会产生编译错误。

---

## 三、🟡 需同步更新的引用文档

### 3.1 `CHANGELOG.md`

| 行 | 当前内容 | 操作 |
|----|---------|------|
| L8 | `> 新增设计文档：Plan Mode 用户可控预思考步骤、A4 条件化 Thinking Step Detector。` | 删除 `、A4 条件化 Thinking Step Detector` |
| L24-25 | `- `docs/design/plan_mode.md`：Plan Mode 用户可控预思考步骤设计（状态：待审查）`<br>`- `docs/design/thinking_step_detector.md`：A4 条件化 Thinking Step Detector 重构设计（状态：待审查）` | 删除第 25 行（`thinking_step_detector.md` 的条目） |

### 3.2 `docs/review/REVIEW_STATUS.md` — L157

**当前**：`| A4 | ReAct 预思考步骤 | 完善预思考逻辑 |`

**操作**：将整行标记为删除。因为预思考代码已全部清空，待 Plan Mode 实现时重新设计。

### 3.3 `docs/design/plan_mode.md`

**问题**：整个设计文档基于旧的 `use_thinking_step` 字段编写，清理后该字段不复存在，文档中的代码引用全部失效。

**需改写的引用（共 5 处）：**

| 行号 | 当前内容 | 操作 |
|------|---------|------|
| L10 | `use_thinking_step` 是死布尔开关，从未设为 true | 改写为描述"旧预思考代码已清理，Plan Mode 从零设计" |
| L51 | `if (config.use_thinking_step) {` | 整段代码需重新设计，不再依赖旧字段 |
| L79 | `tracer_->record("thinking_step", ...)` | 对应的 trace 标识需重新设计 |
| L128 | `config.use_thinking_step = plan_mode_` | 旧的注入方式已失效，需设计新的触发机制 |
| L151 | `ToolCallLoop.h` \| `use_thinking_step` 字段已存在，无需改动 | 该字段已被删除，此条结论错误，需删除或改写 |

**操作**：建议在源码清理完成后，整体审视 `plan_mode.md` 并更新。或待实现 Plan Mode 时完全重写该文档。

---

## 四、删除后验证清单

清理完成后，需确认以下检查全部通过：

| # | 检查项 | 验证方式 |
|---|--------|---------|
| 1 | 编译通过 | `cmake --build build` 无 error |
| 2 | 测试通过 | `ctest --test-dir build` 全部通过 |
| 3 | 无残留引用 | `grep -rn "use_thinking_step\|thinking_step" src/` 返回空 |
| 4 | 无残留引用 | `grep -rn "ThinkingStepDetector\|thinking_detector" src/` 返回空 |
| 5 | 无残留引用 | `grep -rn "thinking_step_detector" . --include="*.md"` 仅返回清理计划自身 |

---

## 五、附录：待删代码定位速查

### `ToolCallLoop.h` — 定位 L38-42

```cpp
    int max_reflection_rounds = 3;
    // A4: 启用预思考步骤。...  ← 删除从这里开始
    ...
    bool use_thinking_step = false;  ← 删到这里结束
};                                   ← 保留此行
```

### `ToolCallLoop.cpp` — 定位 L76-94

```cpp
        // ── A4: 预思考步骤（可选）—— ReAct 模式的"思考"阶段 ──  ← 删除从这里开始
        ...
        }                                                         ← 删到这里结束

        // ── 首轮（带工具）──                                      ← 保留此行
```

共 **24 行代码**（含注释）待删除，全部在 `ToolCallLoop.h/.cpp` 两个文件中，集中可控。
