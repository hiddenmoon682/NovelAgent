# 全局规则层改造：分离「常驻规则」与「任务技能」

> 日期：2026-07-30
> 状态：方案待评审，未实施
> 关联：`2026-07-30-yaml-cpp-integration.md`

## Context（背景）

当前 NovelAgent 用技能系统的 `always=true` 标志承担"始终注入 system prompt"的职责。但这个标志**混淆了两类语义截然不同的内容**：

| 类别 | 性质 | 是否该常驻 system prompt |
|------|------|--------------------------|
| **全局规则** | 无论做什么都适用（文风、人称、术语、篇幅约定） | ✅ 应该常驻 |
| **任务流程** | 仅特定任务适用的执行步骤序列 | ❌ 不该常驻（会干扰无关任务） |

把"任务流程型"内容标 `always` 常驻，会在无关任务中持续向 LLM 注入无关指令，造成**注意力干扰 + token 浪费**。这正是 Anthropic Agent Skills 强调渐进式披露、反对把内容堆进系统提示词的根本原因。

业界通行做法是把这两类机制**分离**：
- **全局规则** → 独立的规则文件（Claude Code 的 `CLAUDE.md`、Cursor 的 rules、通用的 `Agent.md`），始终注入上下文；
- **技能** → 默认按需加载（目录进系统区、全文经工具结果进对话）。

本改造目标：**引入独立的全局规则层**，让"始终生效的规则"回归类似 `Agent.md` 的正当载体，技能系统回归"按需"本职。

## 现状关键事实（调研结论）

### 唯一的 always 技能
- 全项目仅 **`plot-structure`** 一个 `always:true` 技能（`skills/plot-structure/SKILL.md:5`）。
- 其正文（约 20 行 / 644 字节）主体是**全局规则型**（三幕式/起承转合/节奏准则等静态参考），但带一条**任务流程型尾巴**（`/structure` 命令 + `commands` 字段）。
- 内置技能 `create-skill`（`BuiltinSkills.cpp:13`）为按需（always 默认 false），且其正文明确规定"always 仅当内容极短且每次都需要时才设 true"。
- 当前 always 常驻内容量级约 **1 KB**；机制无上限，技能增多会线性膨胀。

### always 的完整数据流
```
SKILL.md "always: true"
  → SkillLoader::parseFrontmatter (SkillLoader.cpp:218)
  → checkGating 短路放行 (SkillLoader.cpp:111-112)
  → SkillRegistry::getSkillContext always 分支渲染全文 (SkillRegistry.cpp:104-136)
  → buildSystemPrompt 拼入 "## 可用技能" (NovelAgentApp.cpp:109)
  → role: system 首条消息 (LLMClient.cpp:59-72)
```

### system prompt 现有组装顺序（NovelAgentApp.cpp:100-117）
```
① kMainPersonality        (Prompts.cpp:5-17)   人格
② kToolUseInstructions    (Prompts.cpp:34-57)  工具指引（仅有项目时）
③ ## 可用技能              getSkillContext()    技能目录 + 常驻全文
④ deferredToolsStub()                          延迟工具存根
```

### 当前无任何规则机制
- `Project` 模型（`Project.h:26-92`）**无** rules/instructions/conventions 字段；最接近的 `must_have_elements`/`Style` 属小说内容层面，非 agent 行为指令。
- `.novelagent/` 目录有 memories/vectors/skills/sessions 等，**无规则文件**。
- `AppConfig` 无全局规则字段。
- 根目录 `CLAUDE.md` 仅供开发工具，**运行时不读取**。

### UI 与持久化模式（可复用）
- 设置走 `SettingsDialog.qml`（模态 Popup + TabBar/StackLayout，现有 Provider/Project/Debug 三选项卡，`openAt(tab)` 定位）。
- 桥接层 `QmlBridge`：`Q_INVOKABLE` 先判 `if(!app_)`，写操作判 `busy_`，成功后 `emit xxxChanged()`。
- 持久化分层：项目级写 `<project>/.novelagent/*.json`，全局写 `config.json`。技能用"全局 `configDir()` + 项目级覆盖"双路径（`NovelAgentApp.cpp:41-45`）。

## 设计方案

### 1. 存储：Markdown 规则文件（双层）
采用 `Agent.md`/`CLAUDE.md` 式的纯 Markdown 文件，分全局与项目两层（**叠加生效**，非覆盖）：

| 层 | 路径 | 内容定位 |
|----|------|----------|
| 全局 | `~/.novelagent/rules.md`（`configDir()/rules.md`） | 用户通用偏好（如"始终用简体中文""避免套路化开篇"） |
| 项目 | `<project>/.novelagent/rules.md` | 本书约定（人称、篇幅、术语、结构规范） |

选 Markdown 而非 JSON：规则是散文式指令，Markdown 最自然，且与业界 `Agent.md` 约定一致。

### 2. 注入点：buildSystemPrompt 新增「全局规则」段
在工具指引之后、技能上下文之前插入：
```
① kMainPersonality
② kToolUseInstructions
③ ## 全局规则        ← 新增（全局 rules.md + 项目 rules.md 拼接）
④ ## 可用技能
⑤ deferredToolsStub
```
规则段为空时不注入该段（保持 prompt 干净）。该位置稳定，利于 prompt cache。

### 3. 加载机制：轻量 RulesProvider
新增 `src/agent/rules/RulesProvider.{h,cpp}`：
- `std::string combined() const`：读取全局 + 项目 rules.md，按"全局在前、项目在后"拼接，各自带来源小标题；文件缺失则跳过。
- 在 `buildSystemPrompt` 调用时读取（该函数仅在装配/会话边界/技能开关时触发，频率低，无需复杂缓存；如需可按 mtime 缓存）。
- `NovelAgentApp` 持有 `RulesProvider`，构造时注入项目路径与 `configDir()`。

### 4. 技能系统：回归按需，迁移 plot-structure
- **迁移 `plot-structure`**：
  - 静态参考内容（三幕式/起承转合/节奏准则）→ 上提为**项目规则**（或内置默认规则模板）；
  - `/structure` 命令逻辑 → 保留为**按需技能**（去掉 `always:true`）。
- **`always` 标志处理**（分阶段，见下）：最终目标是废弃 `always`，技能一律按需；过渡期可保留但文档标注"仅用于全局规则型内容，且优先改用规则层"。

### 5. UI：SettingsDialog 新增「规则」选项卡
- 在 `SettingsDialog.qml` 增加第 4 个选项卡"规则"，含两个 `TextArea`（全局规则 / 项目规则）+ 保存按钮。
- `QmlBridge` 新增：
  - `Q_INVOKABLE QString globalRules() const` / `projectRules() const`
  - `Q_INVOKABLE bool saveGlobalRules(const QString&)` / `saveProjectRules(const QString&)`
  - `signal rulesChanged()`；保存成功后重建 system prompt（`agent_.setSystemPrompt(buildSystemPrompt())`）并 `emit rulesChanged()`。
- 遵循现有约束：`if(!app_)` 守卫、`busy_` 守卫、`Theme.*` 样式。

## 实施步骤（建议分三期）

**Phase 1 — 规则层核心（无 UI）**
1. 新增 `RulesProvider`（读取/拼接双层 rules.md）。
2. `NovelAgentApp` 集成：`buildSystemPrompt` 注入「## 全局规则」段。
3. 迁移 `plot-structure` 参考内容到规则（提供默认规则模板），`/structure` 改为按需。
4. 单元测试：RulesProvider 读取/缺失容错；buildSystemPrompt 含/不含规则段。

**Phase 2 — 收敛 always 标志**
5. 文档/校验约束 `always` 仅用于全局规则型；评估后废弃 `always`（涉及 `SkillMetadata`/`SkillLoader::checkGating`/`SkillRegistry::getSkillContext`/`SkillPopup` 常驻标签/相关测试）。

**Phase 3 — 规则管理 UI**
6. `QmlBridge` 规则读写接口 + `rulesChanged` 信号。
7. `SettingsDialog` 新增「规则」选项卡。

## 受影响文件

| 文件 | 改动 |
|------|------|
| `src/agent/rules/RulesProvider.{h,cpp}` | **新增**：规则读取/拼接 |
| `src/NovelAgentApp.{h,cpp}` | 持有 RulesProvider；buildSystemPrompt 注入规则段 |
| `cmake/Sources.cmake` | 登记新源文件 |
| `skills/plot-structure/SKILL.md` | 去 always；参考内容迁出 |
| （可选）内置默认规则模板 | 随应用分发的基础规则 |
| `src/agent/skill/*` | Phase 2：收敛/废弃 always |
| `src/novelagent_qt/QmlBridge.{h,cpp}` | Phase 3：规则读写接口 |
| `src/novelagent_qt/qml/SettingsDialog.qml` | Phase 3：规则选项卡 |
| `tests/` | 新增 RulesProvider 测试；调整 always 相关测试 |

## 验证

1. 构建：`cmake --preset default && cmake --build build`。
2. 单测：`ctest --test-dir build`（RulesProvider 读取/拼接/缺失容错；buildSystemPrompt 规则段注入；现有技能测试保持绿色）。
3. 手工：在 `<project>/.novelagent/rules.md` 写一条规则（如"回复始终使用简体中文"），新建会话，确认其出现在 system prompt（可加调试日志或测试断言），并观察 LLM 遵循。
4. 迁移验证：确认 `plot-structure` 的 `/structure` 命令仍可按需调用；结构参考内容仍对 LLM 可见（经规则层）。
5. UI（Phase 3）：在设置「规则」选项卡编辑并保存，确认落盘 + system prompt 即时更新。

## 风险与备注

- **迁移勿丢功能**：`plot-structure` 的 `/structure` 命令须完整保留为按需技能，仅迁出静态参考内容。
- **废弃 always 影响面**：触及解析、门控、渲染、UI、测试多处的 `always` 逻辑，建议放在 Phase 2 独立评估，不与 Phase 1 耦合。
- **读取频率**：`buildSystemPrompt` 触发频率低，直接读文件可接受；若后续频繁重建再引入 mtime 缓存。
- **双层叠加语义**：全局 + 项目规则同时注入（非覆盖），需在规则段内用来源小标题区分，避免冲突时语义含糊。
- **与 yaml-cpp 改造的关系**：两者独立，无强依赖；若先落地 yaml-cpp，规则文件本身是纯 Markdown，不受影响。

## 参考资料

> 注：以下文档全文抓取当时被限流，结论基于搜索摘要与既有领域知识，实施前建议复核原文。

- Anthropic《Skill authoring best practices》— 技能渐进式披露（元数据/正文/资源三级加载）：https://platform.claude.com/docs/en/agents-and-tools/agent-skills/best-practices
- Anthropic Engineering《Equipping agents for the real world with Agent Skills》：https://www.anthropic.com/engineering/equipping-agents-for-the-real-world-with-agent-skills
- Claude Code《How Claude remembers your project》— CLAUDE.md 记忆层级（用户级/项目级/子目录）：https://code.claude.com/docs/en/memory
- 《Writing a good CLAUDE.md》— 常驻规则文件的内容组织建议：https://www.humanlayer.dev/blog/writing-a-good-claude-md
