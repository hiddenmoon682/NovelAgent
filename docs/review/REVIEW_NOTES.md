# 待修复问题

> 创建时间: 2026-05-28 | 最后更新: 2026-06-28
> 用途: 记录代码审查中发现的、尚未修复或暂缓的问题
>
> 已修复的问题请记录在 `RESOLVED.md`。

---

## 当前状态

**有 1 个活跃待议项。**

审查历史:

| 轮次 | 日期 | 发现 | 修复 | 暂缓 |
|------|------|------|------|------|
| 第一轮 | 2026-05-28 | 10 | 9 | 1 |
| 第二轮 | 2026-05-29 | 1 | 1 | 0 |
| 第三轮 | 2026-06-09 | 11 | 9 | 2 |
| 第四轮 | 2026-06-09 | 4 | 3 | 1 |
| 第五轮 | 2026-06-09 | 8 | 6 | 2 |
| 第六轮 | 2026-06-09 | 5 | 4 | 1 |
| **合计** | | **39** | **32** | **7→5** |

> 注: 2 个暂缓项已通过后续修复解决（PCH → 对象库优化，CreateChapter 部分保存 → 全量保存），
> 最终 5 个活跃暂缓项移入 `DEFERRED.md`。

> 2026-06-28: Token 预算分配策略已移除（commit 51b7616），`context_window` 已重命名为 `max_context_tokens`。
> 两个待议项已关闭，剩余 1 个活跃待议项。

> 2026-06-28: 新增**设计合理性评审**（独立于上述代码审查轮次）。
> 站在「长篇网络小说创作」目标场景，对全项目做结构性评审，发现 **20 高 / 19 中 / 10 低** 共 49 项问题，
> 按「长篇创作一致性 / 数据可靠性 / 安全与工具 / 架构抽象」四维度组织，并附修复优先级排序。
> 详见 [`DESIGN_REVIEW.md`](./DESIGN_REVIEW.md)。该报告不修改业务代码，仅供后续挑选条目开修复任务。

---

## 设计标记：ShellTools 安全"改来改去"教训

> 日期: 2026-06-29 | 状态: 设计标记（非待修）

ShellTools 安全在第四轮、第五轮、设计评审（C1/C2）、06-29 复核共 **4 轮修复**中反复被触及。每次都修了具体 bypass 向量（补充关键词/变白名单/去 foreach-object/修段内参数误拦），但每次都遗漏新角度。

### 教训

**根因**: 每次修复只堵"当前发现的具体 bypass 路径"，从未先回答"LLM 在写作场景中到底需要 shell 做什么"这个根本问题。设问不前置，修多少轮都追不上攻击面。

**后续改动前必须回答**:

1. LLM 写作场景中实际需要的 shell 命令具体是哪些？
2. 这些命令能否通过已有结构化工具覆盖（`ProjectIO` / `read_chapter` / `write_chapter` 等）？
3. 如果确实需要保留 Shell 工具——是走白名单、硬编码允许列表、还是直接移除？

**在回答这些问题之前，不要对 ShellTools 做增量安全修复**（加关键词/改匹配规则这类"堵漏"式修改），那本质上是原地打转。

---

## 待议：`GenerationControl` + `tags` 过度设计（可能为死代码）

> 日期: 2026-06-28 | 状态: 待议

`GenerationControl` 结构体与所有模型上的 `tags` 字段构成了一套复杂的 prompt 裁剪系统，但在当前实现中几乎没有任何实质作用。

### 设计意图

系统本意是通过两层过滤来控制哪些数据进入 prompt：
- **对象级**：`required_tags` / `blocked_tags` — 按对象 `tags` 决定整个对象是否跳过
- **字段级**：`include_fields` / `exclude_fields` — 对象通过后，再逐字段筛选

### 实际问题

**1. `tags` 无人维护 — 标签过滤形同虚设**

`tags` 字段散布在 14 个模型 struct 中（`Project`、`Chapter`、`Character`、`Scene`、`Outline`、`Volume`、`Style`、`Setting`、`WorldRule`、`PlotThread`、`Relationship`、`CharacterDevelopment` 等），但没有任何自动化机制来填充它们。让用户手动维护不现实，结果就是 `tags` 全部为空，`required_tags` / `blocked_tags` 因空列表永不触发。

**2. `generation` 只能人配 — 无人会配置**

所有 Agent 工具的字段白名单中都显式排除了 `generation`（注释写"静默忽略"），LLM 无法修改。而期望用户直接编辑 JSON 来配置 `include_fields` / `exclude_fields` 也不现实。结果就是全部保持默认值（`enabled=true`，其余为空），不过滤任何字段。

**3. LLM 可通过工具完全绕过**

即使配置了过滤，LLM 只需调用 `get_character`、`get_setting` 等工具即可获取完整数据（`json(*ch)`，不做任何过滤）。所以这套系统作为"安全墙"也不成立——它只是一个可被轻易绕过的 prompt 预装优化器。

**4. `prompt_hint` 存而不用**

`prompt_hint` 字段在 `shouldUseField()` 过滤逻辑中从未被使用，仅 `StyleTools.cpp` 读取后暴露给 LLM 看。

### 实际效果

```
generation: enabled=true, 其余全空 → 不过滤
tags: 全空 → required_tags/blocked_tags 永不触发
prompt_hint: 存了但过滤逻辑不用
```

等于什么都没做。`filterObject()` 的实际有效操作只剩：跳过 `"generation"`/`"metadata"` 键 + 保留 `alwaysInclude` 字段 + 去掉空值。

### 删除可影响的文件

| 文件 | 影响 |
|------|------|
| `GenerationControl.h` | 整文件删除（struct + to_json + from_json + shouldUseField） |
| 14 个 Model 头文件 | 删除 `tags`、`generation` 字段及 `#include` |
| `PromptContextBuilder.cpp` | 简化 `filterObject()`，删除所有 `generation`/`tags` 参数 |
| `StyleTools.cpp` | 删除 `generation_prompt_hint` 读取逻辑 |
| 各 Agent Tool 源文件 | 删除 `tags` 白名单条目 |
| `ModelDetail.h` | 评估 `hasAnyTag()` 是否可删 |

### 建议方向

- **首选**：直接删除 `GenerationControl` 和所有模型上的 `tags` 字段，将 `filterObject()` 简化为仅做空值检查和 `alwaysInclude` 保留
- **备选**：保留架构但补充 AI 自动打标签机制，使标签过滤真正可用
- **保留现状**：仅关闭此议题，承认这是预留扩展点

### 后续行动

- [ ] 决定是否删除 `GenerationControl` 和 `tags`，或保留作为预留扩展点
- [ ] 若删除，评估 `hasAnyTag()` / `contains()` 等辅助函数是否仍有其他用途
- [ ] 简化 `filterObject()`：去掉 `generation`/`tags` 参数，仅保留 `alwaysInclude` + 空值检查
