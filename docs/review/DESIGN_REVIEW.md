# NovelAgent 设计合理性评审报告

> 评审日期: 2026-06-28 | 评审范围: 全项目（约 15000 行 C++20）| 方法: 三路并行只读审查 + 关键断言人工核实
> 性质: **设计合理性评审**，不修改业务代码。所有条目按严重度分级，供后续挑选修复。

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

### 高严重度

**A1. compact() 不删除已压缩消息——压缩形同虚设**
- `ContextManager.cpp:563-652`：`compact()` 签名是 `const llm::Conversation&`（const 引用，无法修改对话），只把摘要存进 `compacted_summary_`，**被压缩的旧消息仍留在 conversation 中**。
- 后果：`assemble()` 下次仍面对完整对话做贪心截断，`current_context_size_` 不降，`shouldAutoCompact()` 反复触发，每轮白做一次 LLM 摘要调用（额外 token 成本）。这是上下文链路的**根因缺陷**。

**A2. 语义检索子系统整条空转——/index 是桩，向量库永远空**
- `ReplHandler.cpp:156-167`：`/index` 只打印提示，不调用任何构建逻辑。全库 grep 确认 `NovelChunker`、`EmbeddingGenerator::generateEmbeddings`、`VectorStore::insert` **从未被任何工具/命令调用**。
- 后果：`VectorStore::entries_` 永远空，`ContextManager.cpp:331` 的 `count() > 0` 守卫永远 false，语义召回分支**从未执行**。最重的基础设施（接口/实现/chunker/embedding/dirty 标记/风格冲突解决全写好了）却无一条路径把内容喂进向量库。PLAN.md:269「章节写入后增量更新」从未实现。

**A3. 「三层相关性融合排序 0.5/0.2/0.3」仅存于 PLAN.md，代码零实现**
- `PLAN.md:262-266` 定义确定性 0.5 + 启发式 0.2 + 语义 0.3 融合；全库 grep `0.5|融合|fusion|relevance_score` 在检索源码中**零命中**。`VectorStore::search` 是纯余弦 Top-K，`PromptContextBuilder` 角色排序是 6 级优先级，二者**从未在同一排序里加权**。
- 后果：长篇容量瓶颈（一个贯穿全程的角色出现在数百章，确定性召回塞爆 prompt）的核心解决方案未落地。这是「长篇一致性」卖点的核心承诺未兑现。

**A4. 摘要 500 字上限——长期一致性无法承载**
- `ContextManager.cpp:42`：`kCompactSystemPrompt` 末尾「总长度控制在 500 字以内」；`:607` 项目设定参考也截断到 500 字节。
- 后果：500 字要承载角色决策/情节转折/世界观变更/未解决伏笔/待办+风格样本六类信息，对几十万字长篇远远不够。「主角第 5 章决定隐藏身份」这类关键设定极可能在压缩中丢失，后续章节穿帮。

**A5. project.json 错路径致 mtime 一致性保障整条失效【已核实】**
- `ContextManager.cpp:127/155/219/224` 三处写死 `/project.json`，但实际元数据文件是 `novel.json`（`ProjectIO.cpp:24` `kNovelJson`，`ProjectManager.cpp:62` 用 novel.json 判有效项目）。
- 后果：`last_write_time(".../project.json")` 因文件不存在必然置 `ec`，`isVectorStoreStale()` 永远返回 false，`saveSessionState/loadSessionState` 的 `project_mtime` 永远 0 → 「Project 修改后清空旧摘要」的保障（`current_mtime > 0 && meta.project_mtime > 0`）永远不成立。**用户改了角色名后继续写，含旧名字的摘要不会被清空，LLM 基于矛盾设定写作。** 这是最危险的静默失效。

**A6. 跨实体引用无完整性保护——删除/更新可致 ID 悬空**
- `Relationship.target_character_id`、`Setting.related_characters`、`Chapter.pov_characters`、`PlotThread.related_characters` 全是裸 `std::string` ID，无任何外键约束。全库 grep `dangling|orphan|referential` 零命中。
- `UpdateChapterTool`/`UpdateWorldRuleTool`/`UpdateSettingTool` 更新关联 ID 时不校验存在性。对长篇（角色多、关系网复杂）会直接导致设定崩溃。

**A7. 5 个核心模型对 LLM 不可写——模型层与工具层割裂**
- `ChapterTools.cpp:309` 注释「不在白名单中的字段→静默忽略（包括 scenes）」→ `update_chapter` **无法修改 scenes**。
- `CharacterTools.cpp:188-213` 白名单**不含 `relationships`** → `update_character` 无法维护角色关系。
- `CharacterDevelopment`（弧光追踪）、`Volume`（卷纲）、`PlotThread`（剧情线）**无任何写入工具**，只能用户手改 JSON。
- 后果：10+ 模型中 5 个（Scene/Volume/PlotThread/Relationship/CharacterDevelopment）对 LLM 不可写，与「Agent 自主写小说」定位严重脱节。`PromptContextBuilder.cpp:443-484` 还对 development 做了时间线过滤，说明设计本意是要用它做弧光追踪，但工具层没接上。

**A8. 完全没有 delete/remove 工具**
- 全 `tools/` grep `delete|remove` 只在 ShellTools 黑名单命中。无法废弃角色/章节/设定，只能 `update_*` 清空字段或绕道 Shell 删文件（更危险）。

**A18. 并行编排误判 + 子 Agent 能力不足——一致性检查场景反而做不好**
- `AgentOrchestrator.h:46-50`：`shouldParallelize` 含「所有/检查/分析」即返回 true。写小说常言「把所有角色语气改紧张」「检查这段错别字」→ 误判触发 5 个子 Agent 空跑（`AgentOrchestrator.cpp:27-37` 遍历全部内置模板），成本 5-6 倍。
- `AgentOrchestrator.cpp:163`：SubAgent `max_tool_rounds=3`，chapter-consistency 要跨章对比，3 轮最多读 3 章就得结论，**无法做跨章节一致性检查**。
- `IMessageProcessor.cpp:236-266`：并行模式**完全绕过 ContextManager**，不调 assemble，不注入项目设定/角色发展/世界观——而这些都是一致性检查的核心数据。子 Agent 用只读工具却看不到 `get_character` 之外的角色发展时间线。
- 后果：「检查全书一致性」这个并行编排本应擅长的场景，从检测到执行到汇总全程脱节。

### 中严重度

**A9. 降级纯贪心截断，不识别「设定类消息」** — `ContextManager.cpp:658-724` 从新到旧贪心，用户第 3 轮确立的「主角不能杀人」铁律会被第 50 轮闲聊挤掉（无 /pin 自动提升机制）。

**A10. 向量检索用「最后一条 user 消息」做查询** — `ContextManager.cpp:346-353`，「继续」「改得更有张力」这类指令性短句召回质量差，续写场景基本失效。

**A11. NovelChunker 中文适配缺陷** — `NovelChunker.cpp:227-229` 场景标记只认 Markdown `## Scene N`（漏判中文 `※/～/————/第N节`）；`:348` 段落只认空行（中文单换行连排退化成单超长 chunk，无硬切兜底）；`:374-389` overlap 按字节 substr，UTF-8 中文中间断字节产乱码喂嵌入 API（`EmbeddingGenerator.cpp:176` 同病）。目标语种是中文却全程按字节操作。

**A12. 嵌入刷新时机缺失** — 章节/角色更新后向量不自动失效，staleness 只看（且路径错的）project.json，颗粒度不足。

**A13. word_count/current_word_count 无写入点** — `Chapter.h:55`、`Project.h:49` 全库只有 from_json 读取，`get_project_status` 展示给 LLM 的进度是假数据。

**A14. CharacterDevelopment 通道断开** — 见 A7，弧光追踪字段能读不能写。

**A15. 32KB 截断在 JSON 字符串中间切割** — `ToolPipeline.cpp:55-67` 在已 dump 的 JSON 字符串上找 `,/}/]` 截断当 preview，几乎必然产非法 JSON 片段；`read_chapter` 中文长章节（6000-10000 字）静默丢尾部。应改为对象层面截断。

**A19. 汇总策略截断 800 字** — `ISynthesisStrategy.h:43`，chapter-consistency 产出的详细不一致清单被腰斩，汇总质量下降。

### 低严重度

**A16. WorldRule 缺 contradicts_with/precedence** — 无法支撑自动化规则冲突检测。
**A17. list_chapters 不按 order 排序** — `ChapterTools.cpp:128-142`。

---

## 维度二：数据可靠性 / 崩溃恢复（安全底线）

> 小说项目是用户长期不可再生创作产物，可靠性工程是最该做却最薄弱的环节。

### 高严重度

**B1. writeText 非原子——所有持久化经此【已核实】**
- `FileUtils.cpp:21-27`：直接 `std::ofstream f(path); f << content;`，无 write-to-temp-then-rename，无 fsync。
- 被**所有持久化**复用：`ProjectIO::saveJsonFile`（进而 `ProjectIO::save` 连写 6 个 JSON）、`SessionPersistence`、`VectorStore::saveToFile`、`writeChapter`。
- 后果：`ProjectIO::save` 写到第 3 个文件时崩溃 → 该文件半截 JSON，`loadJsonFile` 抛 parse_error 返回 nullopt，**对应实体整体丢失**（characters.json 损坏 = 所有角色消失）。应至少改 `writeText` 为写 `.tmp` 再 `fs::rename`。

**B2. 会话仅退出时保存——运行中途崩溃丢全部本轮对话【已核实】**
- `ReplHandler.cpp:443/480` 是 `saveSessionState()` 仅有的两个调用点，都在退出路径。`Agent::processUserMessage`（`Agent.cpp:259-357`）全程无 save。
- `NovelAgentApp.cpp:71-82` `saveConversationIfNeeded` 是**空壳**，body 只有 `// TODO: Phase 4 添加增量保存`。
- 后果：用户写完一章（多轮 tool_call + 数千字生成），只要没退出 REPL，对话历史全在内存，崩溃即丢。重启后 Agent 不知写到哪、用户意图是什么，续写断裂。CONTEXT_MANAGEMENT_SYSTEM.md 第 7.2 节声称「自动保存」，文档与实现不符。

**B3. SubAgent 超时 use-after-free——注释自承认【已核实】**
- `SubAgent.cpp:81-88`：超时后 `cancelled_=true`，再等 `timeout*2`，仍未完成则「放弃等待」返回。但 lambda 捕获 `[this]`（`:36`），SubAgent 返回后被销毁，异步线程仍可能在 HTTP 返回后访问 `this->conversation_/cancelled_`。`:87-88` 注释明确承认「此 SubAgent 返回后将被销毁」却未解决。一次超时的子任务可能撞坏主 Agent 内存。

**B4. write_chapter 全量覆写无版本/备份/回退**
- `ChapterTools.cpp:68-81` + `ProjectIO.cpp:249-255` 直接覆盖磁盘，无快照。LLM 一次错误 `write_chapter` 永久毁一章正文。`Chapter.status` 支持 outlined/drafting/revised/final 状态机，却无草稿/正式版分离存储。

**B5. 主循环无超时——HTTP 半开挂起永久卡死**
- `IMessageProcessor.cpp:114-118`：SerialProcessor 构造 config 时**不设 timeout**；`ToolCallLoop.cpp:158` 仅 `timeout > 0` 才走超时分支。SubAgent 有 120s 超时，**主循环反而没有**，保护不一致。TCP 半开/服务端卡死时主线程无限阻塞，一次卡死丢整段创作上下文。

### 中严重度

**B6. vectors.json 非原子全量覆盖** — `VectorStore.cpp:223-243`，万级向量数百 MB 全量覆盖写，崩溃损毁整个向量库；`close()` 只在 dirty 时 save，无周期性自动 save，进程崩溃全丢。

**B7. 无文件锁——多 Agent/多进程互相覆盖** — `ProjectIO::save` 是 read-modify-write 全量覆盖；`BackendServer.cpp:289` 每请求 `new tempAgent`。两进程开同一项目，A 写完 B 用旧内存覆盖，A 的修改静默丢失。

**B8. 异常后状态卡 Thinking 永久拒输入——真实死锁** — `Agent.cpp:331-346`：`processUserMessage` 在 `transition(Thinking)` 后、`transition(Idle)` 前抛异常，异常穿透到 REPL catch，但 `state_` 仍停 Thinking。下一轮 `canAcceptInput()` 返回 false，Agent **永久拒输入直到重启**。`isError()` 自动恢复只处理 Error 不处理 Thinking。

---

## 维度三：安全与工具合理性

### 高严重度

**C1. Shell 工具过度授权——与写作任务无必要关联**
- `ShellTools.cpp:68` 用 `CreateProcessA` 执行 PowerShell。项目已有完整 `ProjectIO` + 结构化工具覆盖文件/数据操作，**Shell 在写作任务中无不可替代用途**，却打开任意命令执行面。对一个能被 LLM prompt 注入触发的工具，这是明显过度授权。

**C2. 危险命令黑名单子串匹配易绕过**
- `ShellTools.cpp:23-45` 用 `lower.find(kw) != npos`。缺 `Copy-Item/Move-Item/Rename-Item/cmd/wscript/cscript/mshta/certutil -decode` 等向量；字符串拼接（`& "Invoke-Express"+"ion"`）可绕；可读/覆盖任意文件。正确做法是白名单（只读 cmdlet）或直接移除该工具。

### 中严重度

**C3. 跨实体 ID 引用无存在性校验** — 见 A6，update_* 写入可破坏一致性。
**C4. additionalProperties 只 warn 不阻断** — `ParameterValidator.cpp:114-135`，LLM 拼错字段（如 `charcter_id`）被静默吞，浪费一轮工具调用。
**C5. update_* 的 fields schema 是空 object** — LLM 看不到可更新字段清单，靠 description 猜，易传错字段名被静默忽略。应在 schema 用 propertyNames 列出。
**C6. create_chapter 不校验 ID 唯一性** — `ChapterTools.cpp:188-191`，非标准 ID（如 `ch-prologue`）致 `stoi` 失败被吞，编号回退冲突。对比 CreateCharacter 有重名检查。
**C7. 非 Windows 分支无超时且 powershell 可能不存在** — `ShellTools.cpp:103-113`，跨平台不完整。

### 低严重度

**C8. Style 24 个旋钮过度工程化且无 enum 约束** — `Style.h:13-18` 全自由字符串，LLM 难稳定区分 6 个 density/distance 类参数；对比 Character.role 用了 stringEnum，风格不一致。
**C9. ToolPipeline 每次全量拷贝 getDefinitions 查找 schema** — `ToolPipeline.cpp:37`，应用 map 缓存。

---

## 维度四：架构抽象与过度设计 + 静默失效

### 高严重度

**D1. 状态机四状态从未触发——装饰性代码**
- `AgentState.cpp:25-67` 定义完整转换图，但全库 grep `state_.transition` 仅 `Agent.cpp:316/343/345/375/389` 用到 **Idle 和 Thinking 两个状态**。`AwaitingTool/WaitingUser/Error/Fatal` 从未触发——`Agent.cpp:341` 注释甚至承认「isError 在 processor 内部被置位」，但 SerialProcessor 全程不持有 state_ 引用，根本无法置位。
- 后果：状态机提供虚假可观测性保证。`WaitingUser` 本可用于「覆盖章节前确认」（写小说很需要的安全机制）却完全没实现，与 B4「无回退」叠加放大毁稿风险。

**D2. config 字段名漂移——用户配置静默失效【已核实】**
- `config.json` 用 `context_window`，但 `AppConfig.h:19/26` 字段名是 `max_context_tokens`（`NLOHMANN_DEFINE_TYPE_INTRUSIVE` 严格匹配）。CHANGELOG 记录 commit 51b7616 重命名，但**未迁移旧 config.json**。
- 后果：用户按文档配 `context_window: 65536`（DeepSeek 真实窗口），系统按默认 131072 算预算，塞超出真实窗口 → API 400。与 A5 同属「字段名漂移」类静默失效。

### 中严重度

**D3. IMessageProcessor 抽象偏离领域语义** — 抽象「串行 vs 并行」而非「写作/修订/一致性检查」任务类型。用户需手动 `/parallel on|off` 判断该不该并行，而并行检测又靠关键词（A18）。接口方向偏离领域。

**D4. IStorageBackend 抽象错位——为虚构扩展造接口却抽象了错误的东西**
- `IStorageBackend.h:3` 注释「为 Phase 4 sqlite-vec 做准备」，但 sqlite-vec 替换的是 `IVectorStore`（向量存储），**不是** IStorageBackend（项目文件 I/O）。二者本应独立，注释混为一谈。
- `FileStorageBackend.cpp` 每个方法一行 `ProjectIO::xxx()` 转发；`ProjectIO::save/load` 主读写**根本不走 IStorageBackend**，直接调静态函数。抽象没统一入口、没换来可测试性，违背 YAGNI。CLAUDE.md:29 同样把 sqlite-vec 错归 IStorageBackend。

**D5. dynamic_cast 向下转型——切并行后配置静默丢失**
- `Agent.cpp:67-78`：`setMaxToolRounds/setContextManager/setMaxContextTokens` 用 `dynamic_cast<SerialProcessor*>` 探测。IMessageProcessor 接口无这些 setter，切到 ParallelProcessor 后 `max_tool_rounds/context_manager/max_context_tokens` **全部丢失，无告警**。

**D6. 并行模式 token 统计归零——预算管理失效**
- `IMessageProcessor.cpp:251-252` 注释「保持 total_tokens 为 0」。并行模式不经 `SerialProcessor::process:129-131` 的 `recordUsage`，`shouldAutoCompact/checkThresholds` 永远 Normal，**上下文预算管理在并行模式下完全失效**。

**D7. IParallelDetector/IDecompositionStrategy 单实现疑似过度设计** — `KeywordParallelDetector` 质量过低（A18），接口存在反而给「可替换」假象。ISynthesisStrategy 的 Concat/Custom 尚有合理用途。

### 低严重度

**D8. sqlite-vec 与文件存储关系文档含混** — PLAN.md:206 写 `vectors.db`（sqlite-vec），实现用 `vectors.json`（`NovelAgentApp.cpp:62`），CLAUDE.md:29 把 sqlite-vec 错归 IStorageBackend，三处说法不一；且 CMakeLists 从未引入 sqlite-vec 依赖。
**D9. cosine 相似度映射 [0,1] 后再显示百分比** — `VectorStore.cpp:272-274` + `ContextManager.cpp:366`，真实余弦 0.7 显示成 85%，语义双重包装误导。
**D10. 「10% vs 15%」重叠率文档自相矛盾** — PLAN.md:271 写 10%，NovelChunker.h:101 默认 15%。

---

## 严重度汇总表

| 维度 | 高 | 中 | 低 |
|------|----|----|-----|
| 一·长篇创作一致性 | 9（A1-3,5-8,18） | 7（A9-15,19） | 2（A16-17） |
| 二·数据可靠性/崩溃恢复 | 5（B1-5） | 3（B6-8） | — |
| 三·安全与工具合理性 | 2（C1-2） | 5（C3-7） | 2（C8-9） |
| 四·架构抽象与过度设计 | 2（D1-2） | 5（D3-7） | 3（D8-10） |

> 注：A6 跨实体引用完整性同时归入维度一与维度三（C3），统计按主维度计。

---

## 建议的修复优先级（仅排序，不展开实现）

1. **第一优先——堵住会丢用户数据的洞**：B1（writeText 原子化）、B2（会话增量保存）、A5（project.json→novel.json）、D2（config 字段迁移）。这四项是静默失效/数据丢失，修复成本低、收益最高。
2. **第二优先——修掉内存安全与死锁**：B3（SubAgent 超时不放弃等待，或改为不捕获 this）、B5（主循环加超时）、B8（异常时强制 transition(Idle/Error)）。
3. **第三优先——兑现长篇一致性核心承诺**：A1（compact 真正删除消息）、A2+A3（接通向量索引 + 三层融合排序，或诚实地从文档/代码中移除该承诺）、A7+A8（补齐 Scene/Relationship/PlotThread/delete 工具）、A6（引用完整性校验）。
4. **第四优先——收紧安全面**：C1+C2（Shell 改白名单或移除）、A15（截断改对象层面）、D1（实现 WaitingUser 覆盖确认，呼应 B4 回退）。
5. **第五优先——清理抽象与文档**：D4（IStorageBackend 去掉或重定向到 IVectorStore）、D5/D6（并行模式配置与 token 传递）、D8（统一 vectors 存储说辞）。

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
- 每个高严重度条目可单独开修复任务；修复后按项目工作流（更新 CHANGELOG → commit → push）并在 `RESOLVED.md` 记录。
- 索引已登记在 `REVIEW_NOTES.md` 审查历史表。
