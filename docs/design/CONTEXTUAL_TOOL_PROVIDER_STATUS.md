# ContextualToolProvider 实现状态分析

**创建**: 2026-07-23 | **优先级**: 低（不影响功能）
**关联**: PROGRESSIVE_TOOL_LOADING.md（远期设计方案）

---

## 背景

代码中存在两个与"渐进式工具加载"相关的工件：

| 工件 | 方案 | 状态 |
|------|------|------|
| `docs/design/PROGRESSIVE_TOOL_LOADING.md` | Stub + ToolSearch + API 硬约束（三层架构） | **设计阶段**，未实现 |
| `src/agent/tool/ContextualToolProvider.h/.cpp` | 关键词匹配自动激活类别（简化方案） | **已实现**，但未接入流水线 |

两者是**两条独立的技术路线**，并非迭代关系。

---

## ContextualToolProvider 现状

### 已实现的功能 ✅

1. **初始化** — 构造函数接收 `ToolRegistry&`，在 `Agent` 初始化列表中构造
2. **核心工具列表** — 8 个工具始终暴露（`read_chapter`, `write_chapter`, `append_to_chapter`, `list_chapters`, `get_latest_chapter`, `get_outline`, `get_project_status`, `get_chapter_context`）
3. **关键词→类别映射** — 检测用户输入中的关键词，自动激活对应工具类别：

   | 关键词 | 激活类别 |
   |--------|---------|
   | 角色、人物、character、关系、成长 | Character |
   | 设定、setting、场景 | Setting |
   | 世界、规则、world | WorldRule |
   | 大纲、卷、情节、outline、线索 | Outline |
   | 风格、文风、style | Content |
   | 搜索、记忆、search、命令、执行、shell | System |

4. **类别过滤** — `getActiveDefinitions()` 返回核心工具 + 已激活类别的工具定义
5. **兜底入口** — `activateCategory(cat)` 供 LLM 调用了未暴露工具时强制激活
6. **重置** — `reset()` 清空所有激活类别

### 未接入的点 ❌

1. **`updateContext()` 从未被调用** — `processSerial()` 入口处没有分析用户输入来激活类别
2. **`getActiveDefinitions()` 从未被使用** — `processSerial()` 第 259 行仍直接使用 `registry_.getToolDefinitions()` 返回全量工具
3. **`toolContext()` getter 从未被外部调用** — 虽然 `Agent` 暴露了该方法，但无消费者
4. **`ToolCallLoop` 中无兜底逻辑** — LLM 调用了未暴露工具时，没有调用 `activateCategory()` 的机制

### 接入所需的最小改动

```
processSerial() 入口处：
  ① tool_context_.updateContext(input)     // 分析关键词激活类别
  ② auto tools = tool_context_.getActiveDefinitions()  // 替代 registry_.getToolDefinitions()
```

---

## 与 PROGRESSIVE_TOOL_LOADING.md 的关系

`PROGRESSIVE_TOOL_LOADING.md` 描述的是更复杂的方案：

| 维度 | ContextualToolProvider | PROGRESSIVE_TOOL_LOADING.md |
|------|----------------------|---------------------------|
| 激活机制 | 关键词匹配，自动激活 | LLM 主动通过 ToolSearch 发现工具 |
| 工具暴露方式 | 完整 schema / 不暴露 | 完整 schema / 仅存根（name + description）|
| API 层约束 | 无 | API 请求中不传延迟工具的 schema |
| 实现状态 | 已编码，未接入 | 仅设计文档 |
| 复杂度 | 低 | 中高（需 ToolSearch 工具、ToolStub 结构、中间件校验）|

**建议**：如果短期目标是快速减少 token 消耗，可以接入 `ContextualToolProvider` 作为过渡方案；如果远期目标是 `PROGRESSIVE_TOOL_LOADING.md` 的完整方案，则 `ContextualToolProvider` 的关键词逻辑可作为"主动注入（兜底策略）"中的预加载机制复用。

---

## 相关代码位置

| 文件 | 关键行 | 说明 |
|------|--------|------|
| `src/agent/tool/ContextualToolProvider.h` | 全文件 | 类声明 |
| `src/agent/tool/ContextualToolProvider.cpp` | 全文件 | 完整实现 |
| `src/agent/core/Agent.h` | L147, L177 | `toolContext()` getter 和 `tool_context_` 成员 |
| `src/agent/core/Agent.cpp` | L58 | 初始化列表 |
| `src/agent/core/Agent.cpp` | L259 | `registry_.getToolDefinitions()` — 应替换的位置 |
