# NovelAgent 设计合理性评审报告

> 评审日期: 2026-06-28 | 评审范围: 全项目（约 15000 行 C++20）| 方法: 三路并行只读审查 + 关键断言人工核实
> 性质: **设计合理性评审**。
>
> **已修复的 35 项详情 → [REVIEW_STATUS.md#二设计合理性评审](./REVIEW_STATUS.md)**
>
> 本文件仅保留尚未修复的暂缓问题的原始分析，供后续改版时参考。
> 修复汇总: 批次① (1674246) → 批次② (1b19a2b) → 批次③ (460e5aa) → 批次④ (6dc457a) → 批次⑤ (0f68d0a) → A6 (58c0dcc) → 收尾 (A17+C6+A10)
> 复核: 2026-06-29 对全部 35 项已修复条目做逐项源码复核，补齐 A6 覆盖遗漏 + C1+C2 绕过向量 + 段内参数解析 bug，详见 CHANGELOG 同日条目。

---

## Context（为什么做这次评审）

NovelAgent 是一个 C++20 CLI「写小说 Agent」，PLAN.md 显示 Phase 0–5 全部标记完成，代码约 15000 行，子系统齐全（Agent 循环、上下文管理、多 Agent 编排、语义检索、持久化、终端 GUI、HTTP 后端）。

但「Phase 完成」只代表代码写出来了，不代表作为「写小说 Agent」的设计合理。本次评审的目标是：**站在「长篇网络小说创作」这个目标场景下，判断现有设计是否存在结构性不合理**——不是找零散 bug，而是判断架构方向、核心机制、可靠性底线是否真正服务于「让 AI 连贯地写出一本几十万字小说」这个诉求。

评审方法：三路并行 Explore agent 分别深入「Agent 核心循环+上下文+编排」「工具+数据模型」「检索+持久化」三条链路，再由人对最严重、影响最大的断言逐一回到源码核实（compact 签名、project.json 错路径、config 字段名、writeText 非原子、SubAgent 悬空——全部属实，下文以【已核实】标注）。

---

## 总体判断

NovelAgent 的**模型层建模相当用心**（Chapter 的 goal/conflict/foreshadowing/payoff、Character 的 internal/external_conflict/arc、Scene 的戏剧单元分解都专业），**LLM/流式/工具管线等基础设施工程质量高**。但作为「写小说 Agent」，存在**系统性的「承诺—兑现」落差**和**可靠性工程缺位**，集中在四个维度：

1. **长篇创作一致性**——核心差异化卖点最薄弱：压缩不删消息、语义检索整条空转、跨实体引用无保护、5 个模型不可写。
2. **数据可靠性/崩溃恢复**——用户长期创作产物暴露在单次 I/O 失败风险下：非原子写、仅退出保存、SubAgent 悬空。
3. **安全与工具合理性**——Shell 工具过度授权，工具覆盖只到模型一半可写表面。
4. **架构抽象与过度设计**——状态机空转、IStorageBackend 抽象错位、多条静默失效链路。

严重度统计：**高 20 项 / 中 19 项 / 低 10 项**（去重合并后）。

---

## 维度一：长篇创作一致性（核心差异化卖点）

> 写小说 Agent 的核心诉求是「跨几十万字保持角色弧光、伏笔回收、世界规则不穿帮」。本维度问题最多最严重。

### 中严重度

**【⏭ 暂缓】A12. 嵌入刷新时机缺失** — 章节/角色更新后向量不自动失效，staleness 只看（且路径错的）project.json，颗粒度不足。

### 低严重度

**【⏭ 暂缓】A16. WorldRule 缺 contradicts_with/precedence** — 无法支撑自动化规则冲突检测。

---

## 维度二：数据可靠性 / 崩溃恢复（安全底线）

> 小说项目是用户长期不可再生创作产物，可靠性工程是最该做却最薄弱的环节。

### 高严重度

**【⏭ 暂缓】B4. write_chapter 全量覆写无版本/备份/回退**
- `ChapterTools.cpp:68-81` + `ProjectIO.cpp:249-255` 直接覆盖磁盘，无快照。LLM 一次错误 `write_chapter` 永久毁一章正文。`Chapter.status` 支持 outlined/drafting/revised/final 状态机，却无草稿/正式版分离存储。

### 中严重度

**【⏭ 暂缓】B6. vectors.json 非原子全量覆盖** — `VectorStore.cpp:223-243`，万级向量数百 MB 全量覆盖写，崩溃损毁整个向量库；`close()` 只在 dirty 时 save，无周期性自动 save，进程崩溃全丢。

**【⏭ 暂缓】B7. 无文件锁——多 Agent/多进程互相覆盖** — `ProjectIO::save` 是 read-modify-write 全量覆盖；`BackendServer.cpp:289` 每请求 `new tempAgent`。两进程开同一项目，A 写完 B 用旧内存覆盖，A 的修改静默丢失。

---

## 维度三：安全与工具合理性

### 中严重度

**【⏭ 暂缓】C4. additionalProperties 只 warn 不阻断** — `ParameterValidator.cpp:114-135`，LLM 拼错字段（如 `charcter_id`）被静默吞，浪费一轮工具调用。

**【⏭ 暂缓】C5. update_* fields schema schema 是空 object** — LLM 看不到可更新字段清单，靠 description 猜，易传错字段名被静默忽略。应在 schema 用 propertyNames 列出。

**【⏭ 暂缓】C7. 非 Windows 分支无超时且 powershell 可能不存在** — `ShellTools.cpp:103-113`，跨平台不完整。

### 低严重度

**【⏭ 暂缓】C8. Style 个旋钮过度工程化且无 enum 约束** — `Style.h:13-18` 全自由字符串，LLM 难稳定区分 6 个 density/distance 类参数；对比 Character.role 用了 stringEnum，风格不一致。

**【⏭ 暂缓】C9. ToolPipeline全量拷贝 getDefinitions 查找 schema** — `ToolPipeline.cpp:37`，应用 map 缓存。

---

## 维度四：架构抽象与过度设计 + 静默失效

### 中严重度

**【⏭ 暂缓】D3. IMessageProcessor 抽象偏离领域语义** — 抽象「串行 vs 并行」而非「写作/修订/一致性检查」任务类型。用户需手动 `/parallel on|off` 判断该不该并行，而并行检测又靠关键词（A18）。接口方向偏离领域。

**【⏭ 暂缓】D4. IStorageBackend 抽象错位——为虚构扩展造接口却抽象了错误的东西**
- `IStorageBackend.h:3` 注释「为 Phase 4 sqlite-vec 做准备」，但 sqlite-vec 替换的是 `IVectorStore`（向量存储），**不是** IStorageBackend（项目文件 I/O）。二者本应独立，注释混为一谈。
- `FileStorageBackend.cpp` 每个方法一行 `ProjectIO::xxx()` 转发；`ProjectIO::save/load` 主读写**根本不走 IStorageBackend**，直接调静态函数。抽象没统一入口、没换来可测试性，违背 YAGNI。CLAUDE.md:29 同样把 sqlite-vec 错归 IStorageBackend。

**【⏭ 暂缓】D5. dynamic_cast 向下转型——切并行后配置静默丢失**
- `Agent.cpp:67-78`：`setMaxToolRounds/setContextManager/setMaxContextTokens` 用 `dynamic_cast<SerialProcessor*>` 探测。IMessageProcessor 接口无这些 setter，切到 ParallelProcessor 后 `max_tool_rounds/context_manager/max_context_tokens` **全部丢失，无告警**。

**【⏭ 暂缓】D6. 并行模式 token 统计归零——预算管理失效**
- `IMessageProcessor.cpp:251-252` 注释「保持 total_tokens 为 0」。并行模式不经 `SerialProcessor::process:129-131` 的 `recordUsage`，`shouldAutoCompact/checkThresholds` 永远 Normal，**上下文预算管理在并行模式下完全失效**。

**【⏭ 暂缓】D7. IParallelDetector/IDecompositionStrategy 单实现疑似过度设计** — `KeywordParallelDetector` 质量过低（A18），接口存在反而给「可替换」假象。ISynthesisStrategy 的 Concat/Custom 尚有合理用途。

### 低严重度

**【⏭ 暂缓】D8. sqlite-vec 与文件存储关系文档含混** — PLAN.md:206 写 `vectors.db`（sqlite-vec），实现用 `vectors.json`（`NovelAgentApp.cpp:62`），CLAUDE.md:29 把 sqlite-vec 错归 IStorageBackend，三处说法不一；且 CMakeLists 从未引入 sqlite-vec 依赖。
**【⏭ 暂缓】D9. cosine 相似度映射 [0,1] 后再显示百分比** — `VectorStore.cpp:272-274` + `ContextManager.cpp:366`，真实余弦 0.7 显示成 85%，语义双重包装误导。
**【⏭ 暂缓】D10. 重叠率文档 vs 15%」重叠率文档自相矛盾** — PLAN.md:271 写 10%，NovelChunker.h:101 默认 15%。

---

## 附：原始严重度汇总（含已修复项）

| 维度 | 高 | 中 | 低 |
|------|----|----|-----|
| 一·长篇创作一致性 | 9（A1-3,5-8,18） | 7（A9-15,19） | 2（A16-17） |
| 二·数据可靠性/崩溃恢复 | 5（B1-5） | 3（B6-8） | — |
| 三·安全与工具合理性 | 2（C1-2） | 5（C3-7） | 2（C8-9） |
| 四·架构抽象与过度设计 | 2（D1-2） | 5（D3-7） | 3（D8-10） |

> 注：A6 跨实体引用完整性同时归入维度一与维度三（C3），统计按主维度计。

---

## 建议的修复优先级（原始，已全部处理）

1. **第一优先——堵住会丢用户数据的洞**：B1（writeText 原子化）、B2（会话增量保存）、A5（project.json→novel.json）、D2（config 字段迁移）。这四项是静默失效/数据丢失，修复成本低、收益最高。
2. **第二优先——修掉内存安全与死锁**：B3（SubAgent 超时不放弃等待，或改为不捕获 this）、B5（主循环加超时）、B8（异常时强制 transition(Idle/Error)）。
3. **第三优先——兑现长篇一致性核心承诺**：A1（compact 真正删除消息）、A2+A3（接通向量索引 + 三层融合排序，或诚实地从文档/代码中移除该承诺）、A7+A8（补齐 Scene/Relationship/PlotThread/delete 工具）、A6（引用完整性校验）。
4. **第四优先——收紧安全面**：C1+C2（Shell 改白名单或移除）、A15（截断改对象层面）、D1（实现 WaitingUser 覆盖确认，呼应 B4 回退）。
5. **第五优先——清理抽象与文档**：D4（IStorageBackend 去掉或重定向到 IVectorStore）、D5/D6（并行模式配置与 token 传递）、D8（统一 vectors 存储说辞）。

> 上述优先级中标注的 35 项均已修复。留此列表供了解当时修复顺序的决策逻辑。

---

## 积极面（不应在修复中破坏）

- 模型层建模专业（Chapter/Character/Scene 的叙事字段、GenerationControl 字段级控制）。
- IVectorStore/IEmbeddingGenerator 接口解耦合理；SessionPersistence 的 conversation.json + session_meta.json 分离 + 防御式 `getOrDefault` 解析是好的鲁棒性设计。
- TokenCounter 做了真正的 UTF-8 解码（虽系数待校正，见 A11/A12 相关）。
- 工具自注册机制（REGISTER_TOOL）、读写分离（IProjectReader/Writer）、门面装配（NovelAgentApp）等架构约束执行到位。
- LLM 重试退避、工具结果上限、PCH/对象库等编译加速基础设施完善。

问题不在于「不会写代码」，而在于**设计与实现、文档与代码之间存在系统性「承诺—兑现」落差**，且**可靠性工程（原子写、增量保存、崩溃恢复、内存安全）被系统性低估**。

---

## 后续使用方式

- 本报告不修改任何业务代码，所有断言基于源码静态核实（5 个最关键断言经人工二次确认，以【已核实】标注）。
- 暂缓问题的详细分析保留在本文件中，供后续改版时参考。
- 已修复的 35 项汇总 → [REVIEW_STATUS.md#二设计合理性评审](./REVIEW_STATUS.md)
