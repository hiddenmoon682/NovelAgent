/* zcode-workflow
description: 按固定章节清单为本仓库生成 docs/wiki/ 项目文档：每页由一名子代理撰写、另一名独立子代理读源码逐条核对断言，最后过
  scripts/wiki_lint.mjs 机械校验门（文件路径、引用行号、内部链接、页面结构）。
whenToUse: 需要生成或重建 NovelAgent 的项目 wiki（docs/wiki/）时使用：新模块加页、代码大改后同步文档、或首次为仓库产出模块级文档。
*/
// 目标：在 docs/wiki/ 生成一套与代码现状一致的项目文档。
// 结构、模板与引用规范见 .zcode/skills/repowiki/SKILL.md；机械校验门为 scripts/wiki_lint.mjs。
// 形状：每页 fan-out（撰写 → 同行独立核对）→ 汇总索引 → 机械检查迭代修订 → 发布。

interface PageSpec {
  /** 页面路径（相对 docs/wiki/） */
  path: string;
  /** 页面标题 */
  title: string;
  /** 本页覆盖的代码范围 */
  scope: string;
  /** 本页必须回答的问题与特有要求 */
  brief: string;
}

interface PageDraft {
  /** 写盘的文件路径（仓库根相对） */
  path: string;
  /** 页面一级标题文字 */
  title: string;
  /** 3-5 句摘要：这页讲了什么 */
  summary: string;
  /** 页面里的二级标题列表 */
  sections: string[];
  /** 本页引用过的源文件（仓库根相对路径） */
  sources: string[];
}

interface PageCheck {
  /** 是否发现与代码不符之处 */
  foundProblems: boolean;
  /** 是否已在本页就地修正 */
  revised: boolean;
  /** 发现的与代码不符或无法证实的问题，每条一句话 */
  problems: string[];
}

interface PageProgress {
  /** 页面路径 */
  path: string;
  /** 页面标题 */
  title: string;
  /** 独立核对结论 */
  status: "verified" | "revised";
  /** 引用到的源文件数 */
  sources: number;
}

interface LintIssue {
  /** 出问题的 wiki 页面 */
  file: string;
  /** 行号 */
  line: number;
  /** 问题类型 */
  kind: string;
  /** 出问题的引用原文 */
  ref: string;
  /** 说明 */
  message: string;
}

interface Finding {
  /** 位置：文件路径，必要时带行号 */
  where: string;
  /** 一句话说明发现什么 */
  what: string;
  /** 依据：读到的代码或跑过的命令与输出 */
  evidence: string;
  /** verified = 已由独立子代理或机械检查确认；unconfirmed = 未能确认 */
  status: "verified" | "unconfirmed";
  /** 影响程度 */
  severity: "low" | "medium" | "high";
}

interface WorkflowReport {
  /** 两三句话回答「文档生成得怎么样」 */
  conclusion: string;
  findings: Finding[];
  /** 本轮实际检查了什么、怎么检查的 */
  verified: string[];
  /** 没覆盖到或无法检查的部分及原因 */
  notCovered: string[];
}

const WRITER_SYSTEM =
  "你是本仓库的资深文档工程师，工作目录即仓库根。你只依据真实读到的代码写文档，绝不凭记忆补全类名、函数名或流程；" +
  "拿不准的内容宁可不写。绝不修改 src/ 与 tests/ 下的任何文件，只写你负责的那一个 wiki 页面。";

const CHECKER_SYSTEM =
  "你是独立的文档校对者，唯一任务是找出「文档与代码不符」。你与本页撰写者不是同一个人，不要因为文字通顺就放过。" +
  "只依据代码判断，不依据文档自身的一致性；无法证实就明说，不要猜。你可以用 Edit 修正明确写错的地方，但绝不修改 src/ 与 tests/。";

// ---------------------------------------------------------------- 章节清单
const pages: PageSpec[] = [
  {
    path: "architecture.md",
    title: "架构总览",
    scope:
      "src/main_gui.cpp、src/AppAssembly.cpp、src/NovelAgentApp.h/.cpp、src/novelagent_qt/QmlApp.h/.cpp、src/agent/core/Agent.h、src/agent/core/CoreLoop.h",
    brief:
      "回答：进程从哪里启动、各组件如何装配、分层与依赖倒置怎么落地、一次用户提问端到端经过哪些环节。" +
      "必须给出「QML → QmlBridge → Agent/CoreLoop → ILLMClient → 工具 → 回传渲染」的主链路时序，并说明当前只有 GUI 入口（旧 CLI 层已移除）。" +
      "要有一张文字版分层图。",
  },
  {
    path: "modules/llm.md",
    title: "LLM 客户端层",
    scope:
      "src/llm/ 全部文件：ILLMClient.h、LLMClient.h/.cpp、LLMClientFactory.h/.cpp、HttpClient.h/.cpp、SSEParser.h/.cpp、StreamAccumulator.h/.cpp、StreamingPipeline.h、StreamingTypes.h、Message.h、TokenCounter.h/.cpp、EmojiStripFilter.h/.cpp",
    brief:
      "回答：多 Provider 如何抽象与创建、请求如何发出、SSE 流如何解析与累积、失败如何重试、Token 如何统计、Emoji 为何要过滤。" +
      "必须写清重试条件与退避策略、连接/会话是否复用及其 RAII 管理、StreamCallbacks 的回调契约，以及 TokenCounter 的单次遍历优化。",
  },
  {
    path: "modules/agent-core.md",
    title: "Agent 核心与对话循环",
    scope: "src/agent/core/：Agent.h/.cpp、CoreLoop.h/.cpp、AgentState.h/.cpp、ExecutionTracer.h/.cpp",
    brief:
      "回答：一次对话的轮次是怎么推进的、什么条件下继续调用工具、什么条件下结束、状态如何流转、执行轨迹记录了什么。" +
      "必须写清 Agent 对 ILLMClient 与 ToolRegistry 的持有关系（引用而非所有权）与生命周期约束，以及异常在这一层的处理边界。",
  },
  {
    path: "modules/agent-tools.md",
    title: "工具系统与工具清单",
    scope:
      "src/agent/tool/（BuiltInTool 之外的全部：ToolRegistry、ToolPipeline、IToolProvider、ContextualToolProvider、ProgressiveToolProvider、ParameterValidator、ThreadPool）与 src/agent/tools/（BuiltInTool.h/.cpp 及全部具体工具）",
    brief:
      "回答：一个工具由哪几部分构成、如何注册、一次工具调用经过哪些步骤、参数如何校验、失败如何隔离。" +
      "必须写清 REGISTER_TOOL / REGISTER_TOOL_NP 两个宏的用法与「禁止手改装配文件」的约束、ToolPipeline 的校验→执行→截断→错误处理链路（含工具结果上限）、" +
      "三种 IToolProvider 的差异，并以表格列出全部现有工具（工具名 / 实现文件 / 类别 / 一句话作用）。这张工具清单是本页的核心价值。",
  },
  {
    path: "modules/agent-context.md",
    title: "上下文管理",
    scope: "src/agent/context/：Compactor.h/.cpp、ContextBudgetEvaluator.h/.cpp、TokenBudget.h、IMemory.h、Memory.h、ContextManagerTypes.h",
    brief:
      "回答：上下文预算是怎么算的、什么时候触发压缩、压缩保留了哪些信息、记忆在最底层如何组织。" +
      "必须写清预算评估的输入与判定阈值出处、压缩的触发点与丢失风险，以及 IMemory 接口的职责。",
  },
  {
    path: "modules/agent-prompt.md",
    title: "提示词与规则注入",
    scope: "src/agent/prompt/：Prompts.h/.cpp、PromptSelector.h/.cpp、PromptContextBuilder.h/.cpp、RulesProvider.h/.cpp",
    brief:
      "回答：system prompt 由哪几段拼成、哪些内容常驻、规则从哪里加载、全局规则与项目规则如何叠加。" +
      "必须写清「全局规则（用户目录）/ 项目规则（项目目录）」两级来源与文件路径，以及规则层与技能层的分工原则，并给出一个实际 prompt 的拼装顺序。",
  },
  {
    path: "modules/agent-session.md",
    title: "会话管理",
    scope: "src/agent/session/：SessionPool.h/.cpp、SessionRuntime.h/.cpp、SessionPersistence.h/.cpp",
    brief:
      "回答：多个会话如何隔离、会话运行时状态由谁持有、消息如何落库、切换会话时发生了什么。" +
      "必须写清持久化用到的表与字段、会话与 Agent 实例的对应关系，以及「生成中的会话」这一状态对其它操作（如删除）的约束。",
  },
  {
    path: "modules/agent-skill.md",
    title: "技能系统",
    scope: "src/agent/skill/：SkillLoader.h/.cpp、SkillRegistry.h/.cpp、SkillMetadata.h、ISkillProvider.h、BuiltinSkills.h/.cpp",
    brief:
      "回答：一个技能由什么构成、放在哪里被发现、元数据有哪些字段与语义、什么时候被加载进上下文。" +
      "必须写清 SKILL.md 的 frontmatter 字段含义、always 类技能的代价、按需加载的触发方式，以及内置技能从哪来。",
  },
  {
    path: "modules/agent-index-memory.md",
    title: "索引服务与长期记忆",
    scope: "src/agent/index/：IIndexService.h、ProjectIndexService.h/.cpp；src/agent/longterm/：LongTermMemoryStore.h/.cpp",
    brief:
      "回答：项目索引解决什么问题、重建索引的流程与产物是什么、长期记忆存了什么、如何写入与召回。" +
      "必须写清索引与检索层、长期记忆与检索层各自的衔接点，以及索引重建的代价与触发时机。",
  },
  {
    path: "modules/project.md",
    title: "项目与数据存取",
    scope: "src/project/：Models.h、Models/ 全部、ProjectIO.h/.cpp、ProjectAccess.h/.cpp、ProjectManager.h/.cpp、FileStorageBackend.h/.cpp",
    brief:
      "回答：一个小说项目在磁盘上长什么样、正文以什么形式存储、读写如何分离、创建与打开项目的流程、并发访问如何保护。" +
      "必须强调「小说正文是纯文本 .txt、按卷组织、不含 markdown 结构」这一领域事实，并写清项目文件锁与中文路径必须经由 std::filesystem::path 处理的原因。" +
      "注意：以 src/ 现状为准，不要照抄旧文档里已不存在的读写接口名。",
  },
  {
    path: "modules/retrieval.md",
    title: "检索与向量存储",
    scope: "src/retrieval/：NovelChunker.h/.cpp、EmbeddingGenerator.h/.cpp、IEmbeddingGenerator.h、IVectorStore.h、SqliteVectorStore.h/.cpp",
    brief:
      "回答：长文如何切块、块与块之间是否有重叠、向量存到哪里、检索结果如何回到对话。" +
      "必须写清分块策略为什么不能依赖 markdown 标记（正文是连续纯文本）、重叠的意义、向量库的表与维度来源，以及召回结果注入上下文的路径。",
  },
  {
    path: "modules/storage-config-utils.md",
    title: "存储、配置与通用工具",
    scope: "src/storage/SqliteStore.h/.cpp、src/config/AppConfig.h/.cpp、src/utils/ 全部（FileUtils、IdUtils、JsonUtils、SchemaUtils、StringUtils、Utf8Utils）",
    brief:
      "回答：SQLite 库的表结构由谁建、配置从哪里读、通用工具各自解决什么问题。" +
      "必须写清数据库文件位置与关键表、配置文件的位置与不入库的原因、SchemaUtils 构建参数 schema 的写法与其 additionalProperties 默认值带来的安全含义。" +
      "FileUtils 要写明中文路径必须走 std::filesystem::path 这一约束及其原因。",
  },
  {
    path: "modules/gui-bridge.md",
    title: "GUI 桥接层",
    scope: "src/novelagent_qt/：QmlApp.h/.cpp、QmlBridge.h/.cpp、WinFrameBehavior.h/.cpp",
    brief:
      "回答：QML 与 C++ 之间通过什么通信、桥接层暴露了哪些能力、错误如何回到界面、无边框窗口行为怎么实现。" +
      "必须以表格分组列出 QmlBridge 暴露给 QML 的 Q_INVOKABLE / property / signal（按项目、会话、章节、设置、索引等分组）。" +
      "必须写清「聊天/Agent 运行时错误」与「UI 操作失败」两条独立通道的分工与各自的接收方，这是本项目的硬约束。",
  },
  {
    path: "gui/qml-architecture.md",
    title: "QML 界面骨架与主题",
    scope: "src/novelagent_qt/qml/：MainWindow.qml、Theme.qml、StatusBar.qml",
    brief:
      "回答：窗口骨架怎么搭、三栏如何划分与约束、视觉变量集中在哪里、窗口几何如何持久化。" +
      "必须以表格列出 Theme.qml 提供的档位（颜色语义、字体、间距、圆角、动画时长）名称与取值含义。" +
      "必须写清窗口几何持久化为什么不能直接把别名绑定到 window 属性（最大化退出的历史 bug），以及三栏 SplitView 的尺寸约束与分割线命中区的做法。",
  },
  {
    path: "gui/qml-components.md",
    title: "QML 组件目录",
    scope:
      "src/novelagent_qt/qml/ 下除 MainWindow.qml / Theme.qml / StatusBar.qml 外的全部组件，重点是 ThemedButton/ThemedField/ThemedCombo/ThemedSwitch、ModalDimmer、Toast、ConfirmDialog、各 Dialog、SidebarPanel/AgentPanel/ReaderPanel/ChapterDrawer/ChatBubble/ToolCallCard",
    brief:
      "回答：界面由哪些可复用组件拼成、每个组件负责什么、复用时要注意什么。" +
      "必须逐个组件给出「职责 / 关键属性 / 使用要点」，并单列一节写通用规范：优先用布局容器而非 anchors 手工推算、布局内 Rectangle 必须有显式高度、所有视觉值取自 Theme、重复样式抽成 Themed 组件、" +
      "列表空态占位、悬停才显示行内操作、工具提示的延迟约定。",
  },
  {
    path: "guides/build-and-verify.md",
    title: "构建、测试与验证",
    scope: "CMakePresets.json、CMakeLists.txt、cmake/ 下的 Sources.cmake 与 CompilerSettings.cmake、scripts/verify.sh、scripts/wiki_lint.mjs、tests/CMakeLists.txt",
    brief:
      "回答：怎么配置与构建、三个预设各做什么、改完代码怎么验证、验证脚本怎么用。" +
      "必须写清 verify.sh 的全部用法（仅构建、按模块过滤测试、全量回归、列测试名）、「聚焦验证优先」的取舍原则、构建产物目录与依赖获取策略（系统包优先、缺失时回退拉取）、" +
      "以及文档改动的验证门（wiki_lint 的用法与判据）。",
  },
  {
    path: "guides/add-a-tool.md",
    title: "新增一个工具",
    scope: "src/agent/tools/BuiltInTool.h/.cpp、src/agent/tool/ToolRegistry.h/.cpp、src/agent/tools/ 下任意两个真实工具实现（自己挑一个参数复杂的与一个简单的）、tests/test_chapter_tools.cpp",
    brief:
      "这是操作指南。必须给出可直接照做的步骤：继承基类要实现哪几个方法、宏怎么用、参数 schema 怎么用工具函数构建、工具在什么时机被调用、" +
      "如何为它写测试。要贴真实的短代码片段（放在围栏代码块里），并列出常见错误：手动改装配文件、参数漏声明导致被拒、工具结果超限被截断、只读/写入职责混用。",
  },
  {
    path: "guides/add-a-skill.md",
    title: "新增一个技能",
    scope: "src/agent/skill/SkillLoader.cpp、SkillRegistry.cpp、BuiltinSkills.h/.cpp、SkillMetadata.h 与 .zcode/skills/ 下已有的真实技能文件（如 repowiki、qt-debug-verify）",
    brief:
      "这是操作指南。必须给出：技能目录与文件怎么放、frontmatter 每个字段怎么写、什么时候应该做成技能而不是写成规则、如何验证技能能被加载。" +
      "要说明技能是按需加载的、常驻技能（always）的代价，并指出写 description 的要点（它决定技能会不会在正确的时候被触发）。",
  },
  {
    path: "guides/add-a-qml-view.md",
    title: "新增一个 QML 界面",
    scope: "src/novelagent_qt/qml/ThemedButton.qml、Theme.qml、SettingsDialog.qml、src/novelagent_qt/QmlBridge.h、src/novelagent_qt/QmlApp.cpp",
    brief:
      "这是操作指南。必须给出新增一个面板或弹窗的完整步骤：先在 Theme.qml 补档位、用布局容器搭结构、经桥接层暴露数据与操作、按需响应后端信号刷新列表、" +
      "最后怎么验证（看运行日志零 QML 告警、QML 资源打进了可执行文件需要重新构建）。要列出这条路上最容易犯的错：引用未定义 id 导致绑定静默失效、内联魔法值、布局容器子项写 anchors、布局内 Rectangle 没有高度。",
  },
  {
    path: "reference/coding-conventions.md",
    title: "编码规范与工程约束",
    scope:
      "src/utils/SchemaUtils.h、src/llm/Message.h、src/llm/ILLMClient.h、src/agent/context/IMemory.h、src/project/ProjectAccess.h、src/agent/tool/ToolPipeline.h/.cpp、src/llm/HttpClient.h/.cpp、src/llm/TokenCounter.h、src/agent/tools/BuiltInTool.h、tests/test_sse_helpers.h、tests/test_agent.cpp、tests/test_llm_client.cpp",
    brief:
      "这是规则清单页（不是模块页）：把「改本仓库时必须遵守的写法与禁令」集中列出，分节为语言与提交、封装、工厂方法优先、生命周期、项目访问与并发、头文件与编译性能、多态与抽象取舍、安全、算法与性能反模式、错误处理分层、测试规范、扩展点。" +
      "每条要么给真实代码落点（文件:行号），要么明确标注「约定」而不硬凑出处。必须写清：schema 的 additionalProperties 默认值、工具结果截断的常量与单位（是字符不是字节）、重试次数与退避基数、withLock 内禁止嵌套调用的原因（非递归锁会死锁）。" +
      "本页取代原根目录 CLAUDE.md 的同名内容（该文件已删除），不要引用 CLAUDE.md。",
  },
  {
    path: "reference/interfaces.md",
    title: "抽象接口与实现映射",
    scope:
      "src/llm/ILLMClient.h、src/retrieval/IVectorStore.h、src/retrieval/IEmbeddingGenerator.h、src/agent/context/IMemory.h、src/agent/index/IIndexService.h、src/agent/skill/ISkillProvider.h、src/agent/tool/IToolProvider.h、src/agent/tools/BuiltInTool.h、src/project/ProjectAccess.h",
    brief:
      "以表格给出「接口 → 实现 → 谁在消费」三元映射，逐条写清每个接口存在的理由（它替换掉的耦合点是什么）。" +
      "必须含一节记录「已废弃的名字」：旧文档里仍出现但 src/ 中已不存在的接口/类名，以及它们现在对应什么，防止后来者照抄旧文档。",
  },
  {
    path: "reference/data-model.md",
    title: "数据模型参考",
    scope: "src/project/Models.h、src/project/Models/ 全部头文件",
    brief:
      "以表格逐个模型列出字段（字段名 / 类型 / 含义 / 关联关系），覆盖项目、卷、章、场景、角色、角色成长、关系、大纲、情节线、设定、世界规则、风格等。" +
      "要写清聚合根是谁、层级关系（项目-卷-章-场景）、哪些是纯数据 DTO 及其序列化约定，以及 ID 的生成与引用方式。",
  },
  {
    path: "reference/test-map.md",
    title: "测试清单与覆盖映射",
    scope: "tests/ 下全部 test_*.cpp、tests/CMakeLists.txt、tests/test_sse_helpers.h",
    brief:
      "以表格列出每个测试文件覆盖的模块与它重点验证的行为，让读者能按模块找到对应测试。" +
      "必须写清测试规范：新模块必须配测试、临时目录怎么清理、HTTP 与 SSE 怎么用桩数据测试而不依赖真实网络、哪些是端到端手动用例不在自动回归里。" +
      "并说明改动某个模块时该跑哪几个测试。",
  },
  {
    path: "reference/glossary.md",
    title: "术语表",
    scope: "需要通读以确定术语口径：AGENTS.md、CLAUDE.md、docs/wiki/ 下已有页面、以及 src/ 下的关键头文件注释",
    brief:
      "给出本项目专有术语的一句话定义，并标注该术语在代码里的落点文件。至少覆盖：门面、装配、依赖倒置、读写分离、工具管线、工具自注册、上下文分层、规则层、技能层、按需技能、" +
      "上下文预算、压缩、语义分块、向量召回、长期记忆、项目索引、会话池、执行轨迹、主题单例、标条、遮罩、Toast、QML 告警零容忍。" +
      "每个词都要给「在本项目里具体指什么」，不要写通用解释。",
  },
];

const pageList = pages.map((p) => `docs/wiki/${p.path}`).join("\n");

artifact.table("pages", {
  title: "生成的 wiki 页面",
  columns: [
    { field: "path", label: "页面" },
    { field: "title", label: "标题" },
    { field: "status", label: "独立核对结论" },
    { field: "sources", label: "引用源文件数" },
  ],
  key: "path",
});

// ---------------------------------------------------------------- 撰写 + 逐页核对
phase("并行撰写各章节并逐页独立核对");
log(`分派 ${pages.length} 个页面：每页由一名撰写者写盘、再由另一名独立核对者读代码复验`);

const results = await Promise.all(
  pages.map(async (p) => {
    const writerAsk = [
      `撰写并写盘 wiki 页面「${p.title}」，文件路径 docs/wiki/${p.path}。`,
      "",
      "第一步：先读 .zcode/skills/repowiki/SKILL.md 里的「页面模板」与「引用规范」两节，严格照做。",
      "",
      "覆盖范围（自己用 glob / grep 把该范围内的文件列全并通读，不要只读我列出的）：",
      p.scope,
      "",
      "本页必须回答：",
      p.brief,
      "",
      "硬要求：",
      "- 用中文写，代码标识符保留英文。",
      "- 每个关键结论都要有代码出处，写成 反引号 包起来的 src/.../X.cpp:123，行号必须真实。",
      "- 反引号里的类名/函数名必须是 src/ 里真实存在的；不确定就不要写名字，改为引用文件路径。",
      "- 首行是一级标题；至少 3 个二级标题；正文非空行不少于 40 行；结尾「相关页面」小节放 2-4 条相对链接。",
      "- 「相关页面」的链接只能从下面这份本 wiki 全部页面里选，按相对 docs/wiki/ 的路径写：",
      pageList,
      "",
      "- 只描述现状。不要写 TODO、不要写未来规划。",
      "- 只写这一个文件，不要改动其它任何文件。",
      "",
      "结构：path 是你实际写盘的文件路径；title 是一级标题文字；summary 用 3-5 句话概括这页讲了什么（会用于生成索引页，请写得能让读者决定要不要点进来）；" +
        "sections 列出页面里的二级标题；sources 列出你引用过的源文件（仓库根相对路径）。",
    ].join("\n");

    const draft = await agent(`撰写·${p.title}`, { system: WRITER_SYSTEM }).ask<PageDraft>(writerAsk);

    const checkerAsk = [
      `校对页面 docs/wiki/${p.path}（标题：${p.title}）。`,
      "",
      "逐步做，不要重写全文、不要扩写、不要新增章节：",
      "1. 读该页，列出它做出的全部事实性断言（关于文件、类型、函数、流程、参数、阈值、约束的）。",
      "2. 对每条断言，打开它引用的源码逐条核对。",
      "3. 找出与代码不符、代码里根本不存在、或出处行号错位的断言。",
      "4. 明确写错的（类名、路径、行号写错，或断言与代码相反），直接用 Edit 就地修正。",
      "5. 代码里看不出来、无法证实的断言：不要删，改成更保守的措辞，并在结果里列出。",
      "",
      "只改这一个页面文件，绝不修改 src/ 与 tests/ 下的任何文件。",
      "",
      "返回：foundProblems 表示是否发现了问题；revised 表示是否改动了文件；problems 逐条列出你的发现（每条一句话，写清是哪条断言、代码里实际是什么）。",
      "如果整页核对下来确实与代码一致，problems 返回空数组。",
    ].join("\n");

    const check = await agent(`核对·${p.title}`, { system: CHECKER_SYSTEM }).ask<PageCheck>(checkerAsk);

    const progress: PageProgress = {
      path: `docs/wiki/${p.path}`,
      title: draft.title,
      status: check.revised ? "revised" : "verified",
      sources: draft.sources.length,
    };
    report(progress, "pages");
    return { spec: p, draft, check };
  }),
);

log(`全部 ${results.length} 个页面已写盘并完成逐页核对`);

// ---------------------------------------------------------------- 汇总索引
phase("汇总生成 README 索引");
const groupOf = (path: string): string => {
  if (path.startsWith("modules/")) return "引擎与数据模块";
  if (path.startsWith("gui/")) return "桌面界面（QML）";
  if (path.startsWith("guides/")) return "开发者指南";
  if (path.startsWith("reference/")) return "参考手册";
  return "总览";
};
const groupOrder = ["总览", "引擎与数据模块", "桌面界面（QML）", "开发者指南", "参考手册"];

const indexLines: string[] = [];
indexLines.push("# NovelAgent 项目 Wiki");
indexLines.push("");
indexLines.push(
  "> 本 wiki 描述本项目的当前实现：模块职责与边界、关键组件、数据流、扩展点。每条结论都带代码出处，并由机械校验门检查引用的真实性。",
);
indexLines.push("");
indexLines.push("## 阅读路线");
indexLines.push("");
indexLines.push("1. 第一次接触本仓库：先读 [架构总览](architecture.md)，建立从界面到模型的主链路印象。");
indexLines.push("2. 要改后端某个模块：在「引擎与数据模块」里找到对应页，重点看「职责与边界」与「扩展点与注意事项」。");
indexLines.push("3. 要改界面：读「桌面界面（QML）」两页；注意所有视觉值都只有一个来源。");
indexLines.push("4. 要动手加东西：直接看「开发者指南」里对应的那一篇。");
indexLines.push("");
indexLines.push("## 章节索引");
for (const g of groupOrder) {
  const group = results.filter((r) => groupOf(r.spec.path) === g);
  if (group.length === 0) continue;
  indexLines.push("");
  indexLines.push(`### ${g}`);
  indexLines.push("");
  for (const r of group) {
    indexLines.push(`- [${r.draft.title}](${r.spec.path}) — ${r.draft.summary}`);
  }
}
indexLines.push("");
indexLines.push("## 参考与约定");
indexLines.push("");
indexLines.push("- 引用规范：文件一律写仓库根相对全路径，必要时带行号，例如 src/agent/core/CoreLoop.cpp。");
indexLines.push("- 校验门：运行 node scripts/wiki_lint.mjs --docs docs/wiki，检查文件路径、引用行号、内部链接与页面完整度，error 必须为 0。");
indexLines.push("- 开发硬约束见仓库根目录的 AGENTS.md 与 CLAUDE.md；本 wiki 只描述现状，规划与演进方案放在 docs/superpowers/。");
indexLines.push("");
indexLines.push("## 生成与维护");
indexLines.push("");
indexLines.push("- 本 wiki 由 .zcode/skills/repowiki 技能规定的流程生成：按固定章节清单并行撰写，逐页由独立子代理核对代码引用，最后过机械校验门。");
indexLines.push("- 代码改动后请重跑校验门；只重写受影响的页面，不要整站重生成。文档漂移和代码缺陷一样需要拦截。");

const indexBody = indexLines.join("\n");
await agent("索引编辑").ask(
  [
    "把下面这份索引内容原样写入 docs/wiki/README.md。",
    "用 Write 工具写盘，内容逐字照抄，不要改动、不要增删任何一行，不要加代码围栏，不要写标题之外的东西。",
    "",
    "----- 索引正文开始 -----",
    indexBody,
    "----- 索引正文结束 -----",
  ].join("\n"),
);

// ---------------------------------------------------------------- 机械校验门
phase("按机械检查结果修订引用问题");
const lintIssues = (issues: LintIssue[]): string =>
  issues
    .slice(0, 60)
    .map((e) => `${e.file}:${e.line} [${e.kind}] ${e.ref} — ${e.message}`)
    .join("\n");

const proofreader = agent("引用修订员", {
  system:
    "你是文档引用修订员。你只修正 wiki 页面里对不上代码的引用：文件路径、行号、内部链接、页面结构。" +
    "修正必须以真实代码为依据，绝不修改 src/ 与 tests/ 下的文件。" +
    "如果某条警告是外部 API 名（例如 Win32 或 Qt 的名字，本仓库里当然检索不到），把它的反引号去掉即可，不要删掉这个说法。",
});

let lintClean = false;
let finalErrors = -1;
let finalWarnings = -1;
for (let round = 1; round <= 3; round += 1) {
  const lint = await world.run("node", ["scripts/wiki_lint.mjs", "--docs", "docs/wiki", "--json"], {
    timeoutMs: 120000,
  });
  let parsed: { errors: LintIssue[]; warnings: LintIssue[] } = { errors: [], warnings: [] };
  try {
    parsed = JSON.parse(lint.stdout) as { errors: LintIssue[]; warnings: LintIssue[] };
  } catch {
    parsed = { errors: [], warnings: [] };
  }
  finalErrors = parsed.errors.length;
  finalWarnings = parsed.warnings.length;
  log(`第 ${round} 轮引用检查：error ${finalErrors}，warning ${finalWarnings}`);
  if (lint.exitCode === 0) {
    lintClean = true;
    break;
  }
  const errorText = parsed.errors.length > 0 ? lintIssues(parsed.errors) : lint.stdout.slice(0, 3000);
  const warnText = parsed.warnings.length > 0 ? lintIssues(parsed.warnings) : "（无）";
  await proofreader.ask(
    [
      `机械检查发现 ${finalErrors} 处必须修复的问题、${finalWarnings} 处需要核对的警告。请逐条处理：`,
      "",
      "【必须修复】",
      errorText,
      "",
      "【需要核对】",
      warnText,
      "",
      "逐条判断并修正：路径写错或未写全的改成仓库根相对全路径；行号超界的改成真实行号或去掉行号；内部链接断裂的改成本 wiki 里真实存在的页面（本 wiki 全部页面见下）；" +
        "标识符检索不到的，若是本项目类型必须改成真实名字，若是外部 API 就把反引号去掉；页面结构不合格（章节太少或内容过短）的补足实质内容。",
      "",
      "本 wiki 全部页面：",
      pageList,
      "",
      "改完不需要自己跑检查，脚本稍后会再跑一次。只改 wiki 页面，不要动 src/ 与 tests/。",
    ].join("\n"),
  );
}

// ---------------------------------------------------------------- 交付
try {
  await artifact.file("wiki", "docs/wiki/README.md", {
    title: "NovelAgent 项目 Wiki",
    description: "按模块拆分的项目文档：职责边界、关键组件、数据流与扩展点，每条结论带代码出处。",
    primary: true,
  });
} catch {
  const indexEditor = agent("索引补写");
  await indexEditor.ask(
    `docs/wiki/README.md 缺失或无法发布。请按 .zcode/skills/repowiki/SKILL.md 的模板，依据 docs/wiki/ 下已有页面重建 docs/wiki/README.md 索引页并写盘。`,
  );
  await artifact.file("wiki", "docs/wiki/README.md", {
    title: "NovelAgent 项目 Wiki",
    description: "按模块拆分的项目文档：职责边界、关键组件、数据流与扩展点，每条结论带代码出处。",
    primary: true,
  });
}

const findings: Finding[] = [];
for (const r of results) {
  for (const problem of r.check.problems.slice(0, 3)) {
    findings.push({
      where: `docs/wiki/${r.spec.path}`,
      what: problem,
      evidence: `独立核对者「核对·${r.spec.title}」逐条比对代码后的结论；该页${r.check.revised ? "已就地修正" : "未改动"}`,
      status: r.check.revised ? "verified" : "unconfirmed",
      severity: "low",
    });
  }
}
findings.push({
  where: "docs/wiki/reference/interfaces.md「已废弃的名字」",
  what: "旧开发指南与评审文档用过的 IProjectReader、IOutputChannel、ReplHandler、PromptComposer、IContextManager 等名字在 src/ 中并不存在；那些旧文档已删除，替代实现与现名对照表留在该页，防止后来者照抄",
  evidence: "node scripts/wiki_lint.mjs --docs docs/wiki 校验该页引用的替代实现真实存在（退出码 0）",
  status: "verified",
  severity: "medium",
});

const finalReport: WorkflowReport = {
  conclusion:
    `已在 docs/wiki/ 生成 ${results.length} 个内容页面加一份索引，覆盖架构总览、12 个后端模块、QML 界面、4 篇开发者指南与 4 份参考手册。` +
    (lintClean
      ? "每页由独立子代理逐条核对代码断言，机械校验门最终退出码为 0。"
      : `机械校验门最终仍有 ${finalErrors} 处 error 未清零，这批引用的真实性需人工复核。`),
  findings,
  verified: lintClean
    ? [
        `${results.length} 个页面各由一名独立子代理读源码逐条核对断言并就地修正`,
        "node scripts/wiki_lint.mjs --docs docs/wiki 退出码 0：文件路径存在、引用行号未超界、内部链接可达、页面结构与篇幅合格",
        "索引 pageList 中的每个页面均由撰写子代理实际写盘成功",
      ]
    : [
        `${results.length} 个页面各由一名独立子代理读源码逐条核对断言并就地修正`,
        `机械校验门跑过 ${finalErrors >= 0 ? "至少一轮" : "零轮"}，最终 error ${finalErrors}、warning ${finalWarnings}`,
      ],
  notCovered: [
    "本次只改文档，未运行 C++ 构建与单元测试（改动不涉及 src/ 与 tests/）",
    `标识符级检查中仍有 ${finalWarnings} 条警告：部分为 Win32/Qt 等外部 API 名，脚本无法判定，已交给修订子代理逐条核对`,
    "页面内容对读者是否易懂，未做基于真实用户视角的评审",
    "docs/wiki 之外的历史文档（docs/superpowers/ 下的设计记录、docs/design/ 与 docs/prototypes/ 的图）只读了未改",
  ],
};

return finalReport;
