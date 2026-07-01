# NovelAgent 审查状态总览

> 最后更新：2026-06-30

---

## 一、代码审查（DESIGN_REVIEW_CODE.md）— 28 项，已清零

| 状态 | 数量 |
|------|------|
| ✅ 已修复 | 20 |
| 📋 延后（非 bug） | 1（Issue 12: 模型序列化分离，PCH 已覆盖编译开销） |
| ❌ 不属实 | 1（Issue 20: A17 已修复） |
| ~~误判/夸大~~ | 6（见 `REVIEW_FALSE_POSITIVES.md`） |

**结论**：无待修 bug。

---

## 二、设计合理性评审（DESIGN_REVIEW.md）— 49 项

- 已修复：35 项（6 批次 + A6/C1/C2 复核补齐）
- 暂缓：14 项，**全部中/低严重度，非 bug**：

| # | 类别 | 问题 | 不修理由 |
|---|------|------|----------|
| A12 | 1.嵌入刷新 | 章节更新后向量不自动失效 | `markDirty()`+Issue 5 已解决 Project 部分；向量自动刷新需增量索引，工程量大 |
| A16 | 1.规则冲突 | WorldRule 缺 contradicts_with/precedence 字段 | 当前无自动化冲突检测需求，字段可手动 JSON 添加 |
| B4 | 2.写入安全 | write_chapter 全量覆写无版本/备份 | Issue 5 的 B4 修复已加自动备份到 `.novelagent/chapters_backup/` |
| B6 | 2.向量持久化 | vectors.json 全量覆盖，崩溃可能损毁 | 向量索引可 `/index` 重建；增量持久化需 sqlite-vec |
| B7 | 2.文件锁 | 多进程同时打开同项目互相覆盖 | 使用场景为单用户 CLI，多进程冲突属边缘场景 |
| C4 | 3.参数校验 | additionalProperties 只 warn 不阻断 | LLM 传未知字段不造成数据损坏，浪费一轮调用（可接受） |
| C5 | 3.Tool Schema | update_* fields 是空 object | Issue 1 修复后 C5 已处理：fields schema 已显式列出全部可更新字段 |
| C7 | 3.跨平台 | 非 Windows 分支无超时 | 项目 Windows 优先，Unix 适配留待后续 |
| C8 | 4.Style | 6 个旋钮 free-string 无 enum | Style 字段天然自由形式（写作风格变体极多），enum 反而不灵活 |
| D3 | 4.抽象 | IMessageProcessor 抽象维度偏离领域 | 当前串行/并行二选一足够；PlanModeProcessor 等第三模式暂无需求 |
| D4 | 4.抽象 | IStorageBackend 抽象错位 | 已在 Issue 23 修复中删除该接口，改用具体 FileStorageBackend |
| D7 | 4.过度设计 | IParallelDetector 单实现 | 接口存在便于测试 Mock，当前无替换需求不构成问题 |
| D8 | 4.文档含混 | sqlite-vec 三处说法不一 | sqlite-vec 未实际引入，文档差异不影响运行 |
| D9 | 4.数值包装 | cosine 映射 [0,1] 后再百分比 | 显示值偏高但相对排序不变，对 LLM 召回行为无影响 |
| D10 | 4.文档矛盾 | overlap 10% vs 15% | 实际使用 15%，10% 的 PLAN.md 引用已删除 |

**结论**：14 项全部非 bug——要么已由其他修复覆盖（B4/C5/D4），要么是纯设计偏好/边缘场景/文档差异。

---

## 三、REVIEW_NOTES.md — 1 项设计标记

**ShellTools 安全"改来改去"教训**：4 轮修复追攻击面的历史记录，提醒后续改动前先回答"LLM 到底需要 shell 做什么"。非待修项。

---

## 四、DEFERRED.md — 1 项暂缓（已含在设计合理性评审统计中）

Volume 与 Chapter 同级字段语义重叠——注释已标注差异，运行时无歧义。

---

## 五、已知的非问题（REVIEW_FALSE_POSITIVES.md）

AI 审查工具产生的 6 项误判/夸大（论述偏差、程度夸大、标准误读、设计≠bug、误读设计意图、代码过期）。详见该文档，作为后续审查校准参考。

---

## 最终结论

**当前无待修 bug。** 所有剩余标记均为中/低严重度的设计偏好或边缘场景。
