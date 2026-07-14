# 渐进式工具加载设计

**状态**: 未实现 | **优先级**: 低
**关联**: 减少 Prompt Token 消耗，提升 LLM 工具选择准确性

---

## 动机

当前所有 ~30 个工具定义的 JSON Schema 在每轮 LLM 调用时全量发送。每个工具的 `description` + `parameters`（含属性描述、枚举值等）累积占用大量 prompt token，且 LLM 需要在众多工具中做选择，增加了决策噪声。

**预期收益**：
- 每轮减少约 50-60% 的工具 definitions token 开销
- LLM 的选择空间缩小，工具调用准确率提升
- 为后续 MCP 工具、插件工具的加入预留扩展空间

---

## 行业参考：Claude Code vs OpenCode

### Claude Code：内置 `defer_loading` 机制（成熟方案）

Claude Code 通过一套内置的 `defer_loading` 机制实现了工具和上下文的渐进式加载，解决多 MCP 服务器场景下工具定义数万 Token 的上下文膨胀问题。

**判定规则**：

| 工具类型 | 加载策略 |
|---------|---------|
| MCP 工具（第三方） | 默认延迟加载——这是机制存在的主要原因 |
| `alwaysLoad: true` 的 MCP 工具 | 不延迟——适用于高频核心工具 |
| `ToolSearch` 工具 | **永远不延迟**——模型发现和加载其他工具的入口 |
| 内置工具（Bash, Read 等） | 默认不延迟，除非被显式标记 |

**工作流程**：

```
启动时
  └─ 只加载工具的名称 + 简要描述（"存根"），不加载 input_schema
  └─ 保证 ToolSearch 工具始终可用

运行时（LLM 需要某工具）
  └─ LLM 调用 ToolSearch(query="select:tool_name")
  └─ 系统找到该工具的完整定义，加载到当前上下文
  └─ LLM 获得完整 input_schema 后，正常调用该工具
```

### OpenCode：社区驱动的渐进式披露

OpenCode 的核心机制是 Skills 的"渐进式披露"：

| 阶段 | 行为 |
|------|------|
| **发现** | 启动时只加载每个 Skill 的 `name` 和 `description` |
| **激活** | 任务匹配 Skill 描述时，AI 读取完整的 `SKILL.md` |
| **资源加载** | Skill 执行中按需加载 `scripts/`、`references/` 等附属资源 |

### 总结对比

| 特性 | Claude Code | OpenCode |
|------|-------------|----------|
| 核心机制 | 内置 `defer_loading` | Skills 渐进式披露 |
| MCP 工具加载 | 默认延迟，有明确判定规则 | 内置懒加载仍在开发 |
| 实现状态 | 成熟、内置 | 社区驱动，部分成熟 |
| 主要目标 | 解决 MCP 工具过多导致的上下文膨胀 | 提升 Skills 模块化能力的上下文效率 |

---

## 本项目的设计思路

参考 Claude Code 的 `defer_loading` 方案，采用 **"Stub（存根）+ ToolSearch 发现工具 + API 层硬约束"** 的三层架构，而非简单的关键词匹配。

### 核心概念：解耦"知道存在"与"知道怎么用"

```
传统方案（本项目当前）：
  全量工具定义 → 给 LLM → LLM 直接调用
  (所有信息一次加载)

渐进式方案（目标）：
  工具存根（名+简介） → 给 LLM → LLM 知道"有这个东西"
  ToolSearch 工具       → 给 LLM → LLM 按需查找完整定义
  完整 input_schema     → 按需加载 → LLM 获得调用能力
```

### 分层策略

| 层级 | 策略 | 包含 | 说明 |
|------|------|------|------|
| **核心层**（始终完整加载） | 始终 | `Content`, `Outline`, `Project`, `System` | 写作核心能力，schema 轻量，必须随时可用 |
| **存根层**（仅名称+描述） | 始终 | `Character`, `Setting`, `WorldRule` | 告知 LLM 存在但不可直接调用 |
| **完整加载**（按需） | 经 ToolSearch | 同上扩展层工具 | LLM 通过 ToolSearch 获取完整 schema 后可用 |

---

## 三层约束：确保 LLM 遵循加载流程

### 🚧 第一层：API 层"硬拦截"（最关键）

系统在向 LLM 发起 API 请求时，**不把延迟加载的工具放到 `tools` 参数列表里**。

```
API 请求中的 tools 参数：
  tools = [
    ReadChapterTool(完整 schema),    // 核心工具
    WriteChapterTool(完整 schema),   // 核心工具
    ListChaptersTool(完整 schema),    // 核心工具
    ToolSearch(完整 schema),          // 发现工具——始终可用
    // ✗ 没有 get_character / get_setting 等延迟工具
  ]
```

延迟工具的信息只以**纯文本**形式出现在 system prompt 中（存根）。如果 LLM 试图直接调用，API 网关返回 `Invalid tool name` 硬错误。

### 📜 第二层：系统提示词的"思维绑定"

在 system prompt 中植入明确的加载规则：

> **重要规则**：如果你需要使用未加载的工具（仅看到名字），**绝对禁止**猜测其参数格式。你必须先调用 `ToolSearch` 获取完整的 `input_schema`。如果跳过此步骤直接调用，你将无法获得任何数据。

### 🔒 第三层：中间件校验

`ToolPipeline` 在执行工具前检查该工具的完整 schema 是否已加载到当前上下文。如果尚未加载，拒绝执行并返回错误消息：

> 错误：缺失 `get_character` 的完整模式定义，请先使用 `ToolSearch` 加载。

### 主动注入（兜底策略）

如果 LLM 连续多轮未调用任何延迟工具，系统可主动触发 `ToolSearch` 将最可能的工具预加载到上下文，或根据用户输入关键词后台预加载相关工具。

---

## 具体实现方案

### Step 1: ToolRegistry 增加分级查询接口

```cpp
// ToolRegistry.h 新增
class ToolRegistry : public IToolProvider {
public:
    // ... 现有接口 ...

    // ── 分级查询（渐进式加载用） ──

    // 获取完整定义（按类别筛选）。
    std::vector<llm::ToolDefinition> getDefinitions(
        const std::vector<ToolCategory>& categories) const;

    // 获取存根定义（仅 name + description，不含 parameters）。
    // 用于在 system prompt 中以纯文本形式告知 LLM 工具的存在。
    std::vector<llm::ToolStub> getStubs(
        const std::vector<ToolCategory>& categories) const;

    // 获取某工具的完整定义（供 ToolSearch 按名查找）。
    std::optional<llm::ToolDefinition> getDefinition(
        const std::string& name) const;

    // 获取某个工具所属的类别。
    ToolCategory categoryOf(const std::string& name) const;
};
```

`ToolStub` 结构定义：

```cpp
// llm/Message.h 或单独文件
struct ToolStub {
    std::string name;
    std::string description;
    // 不包含 parameters/input_schema
};
```

### Step 2: 实现 ToolSearch 工具

新建 `src/agent/tools/ToolSearchTool.h/.cpp`，作为始终可用的内置工具：

```cpp
class ToolSearchTool : public BuiltInTool {
    ToolRegistry& registry_;
public:
    explicit ToolSearchTool(ToolRegistry& registry) : registry_(registry) {}

    std::string name() const override { return "tool_search"; }
    std::string description() const override {
        return "查找并加载工具的完整参数定义。当你需要使用某个仅知道名字的延迟加载工具时，"
               "调用此工具获取其完整的 input_schema。";
    }
    nlohmann::json parameters() const override {
        return utils::schema::object({
            {"query", utils::schema::stringProp(
                "要查找的工具名，格式为 'select:tool_name' 或关键词搜索")}
        }, {"query"});
    }
    nlohmann::json execute(const nlohmann::json& args) override {
        // 查找工具完整定义 → 加载到当前上下文 → 返回 schema
        // ...
    }
    ToolCategory category() const override { return ToolCategory::System; }
};
```

> **注意**：`ToolSearchTool` 需要访问 `ToolRegistry` 来按名查找工具定义。由于其他 `BuiltInTool` 接受 `shared_ptr<Project>` 而非 `ToolRegistry&`，需要为 `ToolSearchTool` 单独处理构造函数依赖。可以使用 `REGISTER_TOOL_NP` + 在 `AgentSetup` 中特殊处理，或扩展 `BuiltInTool` 的工厂签名。

### Step 3: SerialProcessor 集成

修改 `SerialProcessor::process()` 中的工具列表获取逻辑：

```cpp
// ── 步骤 3: 准备工具列表（渐进式加载） ──
// 核心工具：直接加载完整定义
auto tools = registry_.getDefinitions({
    ToolCategory::Content,
    ToolCategory::Outline,
    ToolCategory::Project,
    ToolCategory::System
});

// 同时将延迟工具的存根信息嵌入 system prompt
// （在 buildEffectivePrompt 中通过 ToolSelector 生成存根文本）
```

同时修改 `buildEffectivePrompt()`，在 system prompt 尾部追加延迟工具存根列表：

```
你还可以使用以下延迟加载的工具（仅知道名称，调用前需通过 tool_search 获取完整 schema）：
- get_character — 获取指定角色的详细档案
- get_setting — 获取世界观设定/地点的完整信息
- get_world_rule — 获取世界观规则的完整信息
- ...
```

### Step 4: ToolCallLoop 支持运行时加载

`ToolCallLoop` 需要在工具执行前检查当前工具是否已加载完整 schema。可以通过在 `ToolPipeline` 中维护一个 `loaded_tools_` 集合来实现：

```cpp
// ToolPipeline 新增
std::unordered_set<std::string> loaded_tools_;  // 已加载完整 schema 的工具名

// executeOne() 中校验：
auto schema_it = schema_cache_.find(tc.function_name);
if (schema_it == schema_cache_.end() && !isCoreTool(tc.function_name)) {
    return {
        {"error", "工具 '" + tc.function_name + "' 尚未加载"},
        {"suggestion", "请先使用 tool_search 加载此工具的完整 schema"}
    }.dump();
}
```

---

## ToolStub 在 system prompt 中的格式

延迟工具的存根信息以 `<available-deferred-tools>` 块的形式嵌入 system prompt：

```xml
<available-deferred-tools>
你还可以使用以下延迟加载的工具。在调用前，必须先通过 tool_search 获取完整的参数定义：

工具列表：
  - get_character(chapter_id, max_count) — 获取与本章最相关的角色详情
  - get_relevant_characters(chapter_id) — 获取本章相关的角色（按关联度排序）
  - get_setting(chapter_id) — 获取本章相关的设定/地点信息
  - get_world_rule(chapter_id) — 获取本章相关的世界观规则
  - create_character(name, description) — 创建新角色
  - update_character(character_id) — 更新现有角色信息
  - create_setting(name) — 创建新的世界观设定

使用流程：
1. 当你需要调用上述某个工具时，先调用 tool_search(query="select:tool_name")
2. 系统会将该工具的完整 input_schema 加载到当前上下文
3. 然后你可以正常调用该工具
</available-deferred-tools>
```

---

## 边界情况处理

| 情况 | 处理方式 |
|------|---------|
| LLM 直接调用未加载工具 | API 层硬拦截 → 返回 `Invalid tool name` 错误 |
| LLM 连续多轮未用延迟工具 | 系统主动预加载最可能需要的工具（兜底） |
| 用户明确要求使用某类工具 | 系统后台自动预加载该类工具 |
| `ToolSearch` 找不到工具名 | 返回相似工具名列表，引导 LLM 纠正 |
| 禁用渐进式加载 | 通过配置开关 `enable_deferred_loading = false`，退化为全量加载 |
| 没有配置 `ToolSelector` | 退化为全量加载（向后兼容） |

---

## 与现有架构的兼容性

- **不修改** `BuiltInTool`、`REGISTER_TOOL` 宏、现有 `ToolRegistry::registerBuiltInTool()` 流程
- **新增** `ToolRegistry::getDefinitions(categories)`、`getDefinition(name)`、`getStubs(categories)` 接口
- **新增** `ToolSearchTool` 工具（需处理 `ToolRegistry&` 的依赖注入）
- **新增** `ToolStub` 数据结构
- **修改** `SerialProcessor::buildEffectivePrompt()` 追加存根块
- **修改** `ToolPipeline::executeOne()` 添加加载状态校验
- **配置化**：通过 `ToolCallLoopConfig` 或 `SerialProcessor` 配置控制是否启用

---

## 实施步骤

1. 定义 `ToolStub` 数据结构
2. `ToolRegistry` 新增 `getDefinitions(categories)`、`getDefinition(name)`、`getStubs(categories)`、`categoryOf(name)` 方法
3. 实现 `ToolSearchTool`（发现工具，按名加载完整 schema）
4. 修改 `SerialProcessor::buildEffectivePrompt()` 拼接存根块
5. 修改 `ToolPipeline::executeOne()` 添加加载状态校验（中间件拦截）
6. `AgentSetup` 中特殊处理 `ToolSearchTool` 的注册（依赖 `ToolRegistry&`）
7. 编写存根块的 system prompt 模板
8. 单元测试：ToolSearch 查找、加载状态管理、API 拦截模拟
9. 集成测试：对比渐进加载 vs 全量加载的 token 消耗和 LLM 行为

---

## 未解决的问题

- `ToolSearchTool` 如何获得 `ToolRegistry&` 引用？当前所有工具只接受 `shared_ptr<Project>`，需要扩展工厂签名或通过全局访问
- 存根块的存在是否会让 LLM 产生"试试看"的冲动，还是说 API 硬拦截足够抑制？
- 加载状态的维护边界：已加载的完整定义在当前轮有效，还是在整个对话生命周期有效？
- 需要实验确定核心层/延迟层的划分是否合理，某些场景下 `Character` 工具可能比 `Outline` 更常用
