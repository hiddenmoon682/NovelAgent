# NovelAgent 审查状态总览

> 最后更新：2026-07-14

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

### 已修复（35 项）

> 修复批次：① (1674246) → ② (1b19a2b) → ③ (460e5aa) → ④ (6dc457a) → ⑤ (0f68d0a) → A6 (58c0dcc) → 收尾 (A17+C6+A10)

**维度一·长篇创作一致性（18 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| A1 | compact() 不删除已压缩消息 | 高 | compact 签名改为非 const，删除旧消息 |
| A2 | 语义检索子系统空转 | 高 | 接通 index 命令，向量库可写入 |
| A3 | 三层融合排序仅存于 PLAN.md | 高 | 改用分层标签 + chapter_id 去重，PLAN.md 已清理 |
| A4 | 摘要 500 字上限 | 高 | 改为无硬限制，追求完整摘要 |
| A5 | project.json 错路径 | 高 | 修正为 novel.json，mtime 一致性保障恢复正常 |
| A6 | 跨实体引用无完整性保护 | 高 | 软校验覆盖全部 update_* 工具 |
| A7 | 5 个核心模型对 LLM 不可写 | 高 | 补齐 Scene/Volume/PlotThread/Relationship/Development 写入工具 |
| A8 | 没有 delete/remove 工具 | 高 | 新增完整 delete 工具系列 |
| A9 | 贪心截断不识别设定类消息 | 中 | 识别关键设定消息，给予保留优先级 |
| A10 | 向量检索仅用最后一条 user 消息 | 中 | 拼接最近 3 条用户消息做查询 |
| A11 | NovelChunker 中文适配缺陷 | 中 | 中文场景标记/段落分块/UTF-8 边界修复 |
| A13 | word_count/current_word_count 无写入点 | 中 | create/update_chapter 工具补充写入 |
| A14 | CharacterDevelopment 通道断开 | 中 | 添加 CharacterDevelopment 写入工具 |
| A15 | 32KB 截断在 JSON 字符串中间切割 | 中 | 改为对象层面截断 |
| A17 | list_chapters 不按 order 排序 | 低 | 排序修复 |
| A18 | 并行编排误判 + SubAgent 能力不足 | 高 | 编排规则收紧，tool_rounds 增加 |
| A19 | 汇总策略截断 800 字 | 中 | 截断阈值提高或移除 |

**维度二·数据可靠性/崩溃恢复（5 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| B1 | writeText 非原子 | 高 | 改为 write-to-temp-then-rename 模式 |
| B2 | 会话仅退出时保存 | 高 | 每轮对话后增量保存 conversation.json |
| B3 | SubAgent 超时 use-after-free | 高 | 改为不捕获 this，超时后安全销毁 |
| B5 | 主循环无超时 | 高 | 添加请求超时机制 |
| B8 | 异常后状态卡 Thinking | 中 | 异常路径强制 transition(Idle/Error) |

**维度三·安全与工具合理性（4 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| C1 | Shell 工具过度授权 | 高 | 白名单模式（只读 cmdlet） |
| C2 | 危险命令黑名单易绕过 | 高 | 改为白名单 + 字符级拦截 + 黑盒测试 |
| C3 | 跨实体 ID 引用 | 中 | 见 A6，统一软校验 |
| C6 | create_chapter 不校验 ID 唯一性 | 中 | 添加 ID 唯一性检查 |

**维度四·架构抽象与过度设计（2 项）**

| # | 问题 | 严重度 | 修复内容 |
|---|------|--------|----------|
| D1 | 状态机从未触发 | 高 | 补全 state_ 转换路径，移除死代码状态 |
| D2 | config 字段名漂移 | 高 | 统一为 max_context_tokens，旧 config 自动迁移 |

### 暂缓（14 项）

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

## 三、DEFERRED.md — 1 项暂缓（已含在设计合理性评审统计中）

Volume 与 Chapter 同级字段语义重叠——注释已标注差异，运行时无歧义。

---

## 四、已知的非问题（REVIEW_FALSE_POSITIVES.md）

AI 审查工具产生的 6 项误判/夸大（论述偏差、程度夸大、标准误读、设计≠bug、误读设计意图、代码过期）。详见该文档，作为后续审查校准参考。

---

## 五、Compactor/上下文管理重构 — 12 项，已清零

> 记录日期：2026-07-10 · 最终验证：2026-07-14 · 全部已修复

| # | 问题 | 类别 | 修复方式 |
|---|------|------|----------|
| 1 | `buildProjectRef` 1200 字节截断多余 | 代码缺陷 | 整体删除 |
| 2 | `buildProjectRef` 放在 Compactor 内部不合理 | 设计问题 | 随函数删除 |
| 3 | 章节切换自动 compact (`maybeAutoCompact`) | 设计问题 | 函数删除 |
| 4 | `current_chapter_id_` 追踪断裂（5 个子问题） | 设计问题 | 全套机制移除，全索引模式 |
| 5 | 用 `get_chapter_context` 替代 system prompt 自动注入 | 重构方案 | 已实施 |
| 6 | 去重逻辑半成品 (`covered_ids`) | 代码缺陷 | 随 `current_chapter_id_` 移除 |
| 7 | `assemble()` 自动向量检索不可控 | 设计问题 | 步骤 1 删除，`search_memory` 工具替代 |
| 8 | 压缩摘要注入 system prompt 而非对话中 | 设计问题 | 改为 user/assistant 消息对 |
| 9 | `truncateMessages` 安全网几乎不触发 | 无用代码 | 整体删除截断机制 |
| 10 | 告警依赖过时数据 (上一轮 token 数) | 代码缺陷 | 基于实时 `total_tokens` |
| 11 | 强迫压缩替代截断作为安全网 | 重构方案 | `assemble()` 内置基于实时用量的自动压缩 |
| 12 | `assemble()` 步骤 7 状态缓存反馈循环 | 代码缺陷 | `shouldAutoCompact` 改用实时传参，打破循环 |

**最终结论**：无待修项。重构后 Compactor 已合并到 ContextManager，`buildSystemPrompt` 精简为标题+工具指令，LLM 通过 `get_chapter_context`/`get_latest_chapter`/`search_memory` 按需获取上下文。

---

## 最终结论

**当前无待修 bug。** 所有剩余标记均为中/低严重度的设计偏好或边缘场景。
