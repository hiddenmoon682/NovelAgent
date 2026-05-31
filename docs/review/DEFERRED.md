# 暂缓问题

> 创建时间: 2026-05-31
> 用途: 记录已知但当前阶段不修复的问题，留待后续评估

---

## 1. 构建性能优化 — 预编译头（PCH）

**文件**: `CMakeLists.txt`, `cmake/CompilerSettings.cmake`

**创建时间**: 2026-05-28（来自 REVIEW_NOTES）

**状态**: 暂缓 — 已通过 MSYS2 pacman 预编译包大幅缓解

### 问题

PCH 将稳定的头文件提前编译为编译器内部快照，跳过重复的 I/O 和语法分析阶段。最初提出是因为项目用 FetchContent 编译 spdlog 源码（6+ 个 .cpp），且 `nlohmann/json.hpp`（~2.5 万行）被 13 个翻译单元各自解析。

### 当前缓解

2026-05-31 已将 nlohmann_json、CLI11、spdlog 迁移到 MSYS2 pacman 预编译包：
- spdlog 的源码编译已消除（之前是最大的编译瓶颈）
- 当前全量构建 ~20s，PCH 能额外节省的仅 ~1-2s
- 增量构建（改单个 .cpp）节省更微乎其微

### 决定

13 个源文件的规模不值得为 PCH 引入维护成本（include 顺序依赖、隐式包含 bug）。

**触发重新评估的条件**：源文件增长到 50+，或引入新的巨型 header-only 库（如 Boost）。

### 参考

- CMake 官方文档: `target_precompile_headers`
- GCC: `-include` + `.gch`, MSVC: `/Yu` + `.pch`

---

## 2. Volume 与 Chapter 同级字段语义重叠

**文件**: `src/project/Models.h`

**创建时间**: 2026-05-29（来自 VOLUME_CHAPTER_FIELD_OVERLAP.md）

**状态**: 暂缓 — 当前阶段不阻塞功能，留待 Phase 3/4 处理

### 问题

`Volume` 和 `Chapter` 在代码层面是同级 struct（都是 `Outline` 的直接子成员），但在语义上 Volume 是 Chapter 的上级。这导致以下字段在 Volume 和 Chapter 中同名同类型，存在语义重叠：

| 字段 | Volume | Chapter | 重叠程度 |
|------|--------|---------|---------|
| `key_events` | 本卷关键事件（跨章里程碑） | 本章推进剧情的关键事件 | 🔴 |
| `focus_characters` | 本卷重点角色 ID（跨章视角归纳） | 本章重点角色 ID | 🔴 |
| `active_plot_threads` | 本卷推进的剧情线 ID（跨章汇总） | 本章推动的剧情线 ID | 🔴 |
| `goal` | 本卷叙事目标（跨章大目标） | 本章叙事目标（非单场景目的） | 🟡 |

### 当前缓解

1. **注释已明确语义差异** — Volume 和 Chapter 的注释分别声明了"卷级汇总"和"章级创作指导"。
2. **运行时无歧义** — 它们分属不同的容器，不会出现同名 key 冲突。
3. **项目处于 Phase 2** — 这个架构问题不阻塞 LLM 客户端功能。

### 决定

等 Phase 3/4 实现 Agent 工具和上下文管理时，若出现实际数据同步问题再重构。

**未来方案**（当前倾向 A）：

| 方案 | 描述 | 优缺点 |
|------|------|--------|
| A. Volume 内嵌 `chapter_ids` | Volume 通过 ID 列表关联 Chapter | ✅ 改动小 / ❌ 字段仍重叠 |
| B. Volume 内嵌 `chapters` | Volume 直接持有 Chapter 列表 | ✅ 层次清晰 / ❌ JSON 结构大改 |
| C. 删除 Volume 汇总字段 | Volume 只保留 `summary`/`theme` | ✅ 无重叠 / ❌ 失去卷级简报价值 |

---

## 附录：添加新条目

发现新的暂缓项后，按以下格式追加：

```markdown
## N. 标题

**文件**: `相关文件路径`

**创建时间**: YYYY-MM-DD

**状态**: 暂缓 — 原因简述

### 问题
描述...

### 当前缓解
已采取的措施...

### 决定
为何暂不修复，触发条件是什么...
```
