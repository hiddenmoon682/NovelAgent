# Volume 与 Chapter 同级字段语义重叠分析

> 创建时间: 2026-05-29
> 状态: 已知问题，暂不修改，留待 Phase 3/4 处理

---

## 问题描述

`Volume` 和 `Chapter` 在代码层面是同级 struct（都是 `Outline` 的直接子成员），但在语义上 Volume 是 Chapter 的上级。这导致以下字段在 Volume 和 Chapter 中同名同类型，存在语义重叠：

| 字段 | Volume | Chapter | 重叠程度 |
|------|--------|---------|---------|
| `key_events` | 本卷关键事件（跨章里程碑） | 本章推进剧情的关键事件 | 🔴 |
| `focus_characters` | 本卷重点角色 ID（跨章视角归纳） | 本章重点角色 ID | 🔴 |
| `active_plot_threads` | 本卷推进的剧情线 ID（跨章汇总） | 本章推动的剧情线 ID | 🔴 |
| `goal` | 本卷叙事目标（跨章大目标） | 本章叙事目标（非单场景目的） | 🟡 |

---

## 为何暂不修改

1. **注释已明确语义差异** — Volume 和 Chapter 的注释分别声明了"卷级汇总（由 AI 归纳）"和"章级创作指导（由 AI 归纳）"，代码可读性足够。

2. **运行时无歧义** — 它们分属不同的容器：
   ```cpp
   std::vector<Volume> volumes;      // volumes[i].key_events
   std::vector<Chapter> chapters;    // chapters[i].key_events
   ```
   不会在同一数组中出现同名 key 冲突。

3. **修复成本高** — 真正让 Volume 包含 Chapter 需要：
   - Volume 内嵌 `chapter_ids` 或 `chapters` 字段
   - 调整 `outline.json` 的 JSON 结构
   - 更新 `PromptContextBuilder` 遍历逻辑
   - 属于架构级重构，当前 Phase 不必要。

4. **时机未到** — 项目处于 Phase 2（LLM 客户端），这个重叠不阻塞功能开发。等 Phase 3/4 实现 Agent 工具和上下文管理时，如果出现实际的数据同步问题，再重构。

---

## 未来可能的修复方案

| 方案 | 描述 | 优缺点 |
|------|------|--------|
| A. Volume 内嵌 `chapter_ids` | Volume 通过 ID 列表关联 Chapter，保留各自字段 | ✅ 改动小 / ❌ 字段仍重叠 |
| B. Volume 内嵌 `chapters` | Volume 直接持有 Chapter 列表，Chapter 不再独立存储 | ✅ 层次清晰 / ❌ JSON 结构大改 |
| C. 删除 Volume 汇总字段 | Volume 只保留 `summary`/`theme`，不汇总 Chapter | ✅ 无重叠 / ❌ 失去卷级简报价值 |

当前倾向于方案 A，由 `PromptContextBuilder` 在构建上下文时按 `chapter_ids` 查找并注入卷纲信息。

---

## 关联审查记录

参见 `docs/review/REVIEW_NOTES.md` 中第 1 轮审查记录。
