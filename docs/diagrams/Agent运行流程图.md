# NovelAgent — Phase 3 Agent 运行流程详细图

> 生成日期: 2026-06-10  
> 覆盖范围: 从程序启动 → REPL 交互 → Agent 消息处理 → 上下文组装 → Tool Call 循环 → 工具执行 → LLM 流式调用 → 结果返回的完整链路  
> 图表格式: Mermaid（可在 VS Code、GitHub、Typora 中直接渲染）

---

## 一、系统启动总览

```mermaid
flowchart TD
    A["🚀 main() 入口"] --> B["Ansi::enableWindowsAnsi()"]
    B --> C["CLI11 解析命令行参数\n(-p 项目路径, -e 命令, --provider, -v)"]
    C --> D{"-v verbose?"}
    D -->|"是"| E["spdlog::set_level(debug)"]
    D -->|"否"| F["AppConfig::load()\n加载 config.json"]
    E --> F
    F --> G["环境变量覆盖 API Key\n(DEEPSEEK_API_KEY / KIMI_API_KEY / CLAUDE_API_KEY)"]
    G --> H{"-p 指定项目?"}
    H -->|"是"| I["ProjectManager::openOrCreate(path)\n打开或创建项目目录"]
    H -->|"否"| J["projectPtr = nullptr\n(无项目模式，进入欢迎页)"]
    I --> K["NovelAgentApp 构造"]
    J --> K

    K --> L{"-e 命令?"}
    L -->|"是"| M["novelAgent.runExec(command)\n单次命令 → 流式输出 → 退出"]
    L -->|"否"| N["novelAgent.runRepl()\n交互 REPL 循环"]

    M --> O["程序退出"]
    N --> O

    style A fill:#4a90d9,color:#fff
    style K fill:#e8a838,color:#fff
    style N fill:#50b86c,color:#fff
    style M fill:#50b86c,color:#fff
    style O fill:#d94a4a,color:#fff
```

---

## 二、NovelAgentApp 构造与装配

```mermaid
flowchart TD
    subgraph "NovelAgentApp 构造函数"
        A["接收参数: ProviderConfig, shared_ptr&lt;Project&gt;, IOutputChannel*, disabledTools"]

        A --> B["创建 ConsoleOutput\n(如未提供外部 IOutputChannel)"]
        A --> C["创建 LLMClient(provider)\n内部构造 HttpClient(HttpConfig)"]
        A --> D["创建 ToolRegistry（空注册中心）"]
        A --> E["创建 Agent(client, registry)\n默认使用 SerialProcessor"]
        A --> F["创建 FileStorageBackend(project.path)"]
        A --> G["创建 ContextManager(storage)\n内部初始化子模块:\n• ConversationSummarizer\n• ChapterSummaryCache\n• DegradationPipeline\n• SessionPersistence"]

        B & C & D & E & F & G --> H["setupAgent(disabledTools)"]
    end

    subgraph "setupAgent() 装配流程"
        H --> I{"project 有效?"}
        I -->|"是"| J["agent::registerAllTools(registry, project, disabled)\n→ BuiltInTool::registerAllTo()\n→ 遍历所有 REGISTER_TOOL 工厂\n→ 创建工具实例并注册到 ToolRegistry"]
        I -->|"否"| K["跳过工具注册"]

        J --> L["PromptComposer::compose()\n组装系统提示词:\n• personality: AI 人格定义\n• context: (空，由 ContextManager 动态注入)\n• task: (空)"]
        K --> L

        L --> M["agent.setSystemPrompt(prompt)"]
        M --> N["agent.setContextManager(&cm)"]
        N --> O["agent.setContextWindow(config.context_window)"]
        O --> P["agent.useParallelProcessor(&template_mgr)\n切换到 ParallelProcessor（可选）"]
    end

    style A fill:#4a90d9,color:#fff
    style H fill:#e8a838,color:#fff
    style J fill:#50b86c,color:#fff
```

---

## 三、REPL 交互主循环（ReplHandler::run）

```mermaid
flowchart TD
    A["ReplHandler::run() 开始"] --> B["清屏 + 显示标题\nNovelAgent v0.3.0"]
    B --> C{"project 有效?"}

    C -->|"无项目"| D["显示欢迎引导:\n• /new &lt;名称&gt; 创建新项目\n• /load &lt;路径&gt; 打开已有项目\n• /help 查看所有命令"]
    C -->|"有项目"| E["显示项目信息 + 提示"]

    D --> F["主循环开始"]
    E --> F

    F --> G["renderStatusBar()\n显示: 模式(Serial/Parallel) | 项目名"]
    G --> H["显示提示符 '> ' 等待输入"]
    H --> I["std::getline(std::cin, input)"]

    I --> J{"input 非空?"}
    J -->|"空"| F
    J -->|"非空"| K["gui_.addToHistory(input)"]

    K --> L{"以 '/' 开头?"}
    L -->|"是命令"| M["getCompletions() Tab 补全"]
    M --> N["CommandParser::execute(input)"]
    N --> O{"返回 false?"}
    O -->|"是 (/exit)"| P["显示 '再见' → 退出循环"]
    O -->|"否"| F

    L -->|"否 (用户消息)"| Q{"project 有效?"}
    Q -->|"无项目"| R["提示: 请先用 /new 或 /load 打开项目"]
    R --> F

    Q -->|"有项目"| S["gui_.startSpinner('思考')"]
    S --> T["StreamDisplay::create(out)\n构造流式回调 (ANSI 彩色输出)"]
    T --> U["agent_.processUserMessage(input, callbacks)\n→ 进入 Agent 核心处理流程"]

    U --> V["gui_.stopSpinner()"]
    V --> W{"finish_reason?"}
    W -->|"length"| X["警告: 回复因长度限制被截断"]
    W -->|"content_filter"| Y["警告: 部分内容因安全策略被过滤"]
    W -->|"stop / tool_calls"| Z["输出完成"]
    X --> F
    Y --> F
    Z --> F

    style A fill:#4a90d9,color:#fff
    style U fill:#e8a838,color:#fff
    style P fill:#d94a4a,color:#fff
```

---

## 四、Agent 消息处理核心流程（SerialProcessor::process）

这是 Phase 3 的**核心链路**，展示了从用户消息到 LLM 响应的完整数据流。

```mermaid
flowchart TD
    subgraph "Agent::processUserMessage() — 入口"
        A["用户输入: string"] --> B{"input 非空?"}
        B -->|"空"| B1["返回空 LLMResponse"]
        B -->|"非空"| C["processor_->process(input, conversation, callbacks)\n策略模式分发"]
    end

    subgraph "SerialProcessor::process() — Phase 3 核心"
        C --> D["conversation.addUser(input)\n追加用户消息到对话历史"]

        D --> E["registry_.getToolDefinitions()\n获取所有已注册工具定义"]
        D --> F["buildEffectivePrompt(conversation, out_messages)\n组装系统提示词 + 上下文"]

        E & F --> G["创建 ToolCallLoop(client, registry)"]
        G --> H["配置 ToolCallLoopConfig:\n• max_rounds = 10\n• first_round_streaming = true"]
        H --> I["loop.run(conversation, tools, prompt, callbacks, config)\n→ 进入 Tool Call 循环"]

        I --> J["从 result.response 提取内容"]
        J --> K["构造 Assistant 消息\n追加到 conversation"]
        K --> L["返回 Result{text, raw_response}"]
    end

    style A fill:#4a90d9,color:#fff
    style C fill:#4a90d9,color:#fff
    style I fill:#e8a838,color:#fff
    style L fill:#50b86c,color:#fff
```

---

## 五、上下文组装流程（ContextManager::assemble）

```mermaid
flowchart TD
    A["ContextManager::assemble(conversation, context_window, project, chapter_id)"] --> B["1. 预算分配 allocateBudget()"]

    subgraph "预算分配 (80/20 + 50/30/20 规则)"
        B --> B1["total_budget = context_window × 0.8"]
        B1 --> B2["chapter_budget = total × 50%\nconversation_budget = total × 30%\nsummary_budget = total × 20%"]
    end

    B2 --> C["2. 构建系统提示词 buildSystemPrompt()"]

    subgraph "系统提示词构建"
        C --> C1{"chapter_id 为空?"}
        C1 -->|"是"| C2["基础提示词:\n# 项目: title\nLogline + 主题"]
        C1 -->|"否"| C3["PromptContextBuilder::buildForChapter()\n构建章节上下文:\n• 当前章节信息\n• 前后章节摘要\n• 相关角色/设定\n• 大纲进度"]
    end

    C2 & C3 --> D["3. Token 开销计算"]
    D --> D1["sys_tokens = TokenCounter::countTokens(system_prompt)"]
    D1 --> D2["msg_budget = max(0, total_budget - sys_tokens)"]
    D2 --> D3["raw_msg_tokens = TokenCounter::countMessages(all_msgs)"]

    D3 --> E{"需降级?\nraw_msg_tokens > msg_budget\n或 sys_tokens > chapter_budget"}
    E -->|"是"| F["4. 触发降级策略 DegradationPipeline"]

    subgraph "降级策略 (四级)"
        F --> F1["L1: 压缩角色描述\n(保留名称+核心特征)"]
        F1 --> F2["L2: 删除次要角色\n(仅保留主角+关键配角)"]
        F2 --> F3["L3: 只保留章节 ID 列表\n(删除详细元数据)"]
        F3 --> F4["L4: 移除章节上下文\n(仅保留项目标题)"]
    end

    F4 --> G["重新计算 token → 更新 sys_tokens / msg_budget"]
    E -->|"否"| H["5. 对话摘要判断"]
    G --> H

    subgraph "对话摘要"
        H --> H1{"raw_msg_tokens > msg_budget\n且 消息数 > 10?"}
        H1 -->|"是"| H2["ConversationSummarizer::summarize()\n提取关键信息:\n• 当前任务\n• 关键决策\n• 重要发现\n• 待办事项"]
        H2 --> H3["render 为文本\n扣除 summary tokens 预算"]
        H1 -->|"否"| H4["跳过摘要"]
    end

    H3 & H4 --> I["6. 消息截断 truncateMessages()"]

    subgraph "消息截断 (O(n) 反向遍历)"
        I --> I1["从最新消息开始反向累加 token"]
        I1 --> I2{"累计 token <= budget?"}
        I2 -->|"是"| I3["保留该消息，继续"]
        I2 -->|"否"| I4["停止，丢弃更旧的消息"]
        I3 --> I1
        I4 --> I5["reverse → 恢复时间顺序"]
    end

    I5 --> J["7. 注入摘要"]
    J --> J1{"有摘要文本?"}
    J1 -->|"是"| J2["在消息列表头部插入:\n[上下文摘要 — 之前对话的关键信息]"]
    J1 -->|"否"| K["返回 ContextAssembly"]

    style A fill:#4a90d9,color:#fff
    style F fill:#e8a838,color:#fff
    style K fill:#50b86c,color:#fff
```

---

## 六、Tool Call 循环引擎（ToolCallLoop::run）

这是 Agent 与 LLM 交互的**核心循环**，负责首轮调用→检查工具调用→执行→回传→循环。

```mermaid
flowchart TD
    A["ToolCallLoop::run(conversation, tools, system_prompt, callbacks, config)"] --> B["创建 ToolPipeline(registry, conversation)"]

    B --> C{"config.timeout > 0?"}
    C -->|"是"| D["std::async 包装\n→ 超时控制线程"]
    C -->|"否"| E["直接执行 executeLoop()"]

    D --> E

    subgraph "executeLoop — 核心循环体"
        E --> F["═══ 首轮 (round=0) ═══"]

        F --> G{"config.first_round_streaming?"}
        G -->|"是 (默认)"| H["client_.chat(\n  conversation.messages(),\n  tools, system_prompt,\n  callbacks)\n→ 流式调用，逐 token 回调"]
        G -->|"否"| I["client_.chatNonStreaming(\n  conversation.messages(),\n  tools, system_prompt)\n→ 非流式，等待完整响应"]

        H & I --> J["获得 LLMResponse"]

        J --> K["═══ 进入循环 (round=0..max_rounds-1) ═══"]
        K --> L{"response.tool_calls\n为空?"}

        L -->|"是 (无工具调用)"| M["✅ 返回结果:\n• response (最终 LLM 响应)\n• rounds_executed (实际轮数)"]
        L -->|"否 (有工具调用)"| N["📋 记录日志:\n'N 个工具调用 (round=X)'"]

        N --> O["构造 Assistant 消息\n(含 content + tool_calls)\n→ conversation.add(assistant)"]

        O --> P["pipeline.executeAndAppend(tool_calls)\n→ 进入工具执行管线"]

        P --> Q["后续轮次: 非流式调用\nclient_.chatNonStreaming(\n  conversation.messages(),\n  tools, system_prompt)"]

        Q --> R{"round < max_rounds?"}
        R -->|"是"| L
        R -->|"否 (达到上限)"| S["⚠️ 警告: 达到最大轮数\n返回当前 response"]
    end

    D --> T{"超时?"}
    T -->|"是"| U["❌ 返回 timed_out=true"]
    T -->|"否"| V["返回 future.get()"]

    style A fill:#4a90d9,color:#fff
    style H fill:#50b86c,color:#fff
    style P fill:#e8a838,color:#fff
    style M fill:#50b86c,color:#fff
    style U fill:#d94a4a,color:#fff
```

---

## 七、工具执行管线（ToolPipeline::executeAndAppend）

```mermaid
flowchart TD
    A["ToolPipeline::executeAndAppend(tool_calls)"] --> B["遍历每个 ToolCall"]

    subgraph "executeOne — 单个工具执行"
        B --> C["解析 JSON 参数\nnlohmann::json::parse(tc.arguments)"]

        C --> D{"解析成功?"}
        D -->|"失败"| E["❌ 返回错误 JSON:\n{error: '参数 JSON 解析失败: ...'}"]

        D -->|"成功"| F["🔍 ParameterValidator::validate()\n对照工具 Schema 校验参数"]

        F --> G{"校验通过?"}
        G -->|"失败"| H["❌ 返回校验错误 JSON:\n{valid: false, errors: [...]}"]

        G -->|"通过"| I["registry_.executeTool(name, args)\n按名称查找工具条目 → 执行回调"]

        I --> J{"工具存在?"}
        J -->|"否"| K["❌ 返回: {error: '未知工具: ...'}"]

        J -->|"是"| L["执行工具回调 fn(args)"]
        L --> M{"执行抛异常?"}
        M -->|"是"| N["❌ 捕获异常:\n{error: '工具执行异常: ...'}"]

        M -->|"否"| O["✅ 获得工具结果 JSON"]
    end

    E & H & K & N & O --> P["truncateResult(result, 32KB)\n超出部分截断 + 标注"]
    P --> Q["conversation.addToolResult(tc.id, result)\n追加 tool 角色消息到对话历史"]

    Q --> R{"还有更多 ToolCall?"}
    R -->|"是"| B
    R -->|"否"| S["所有工具执行完毕\n控制返回 ToolCallLoop"]

    style A fill:#4a90d9,color:#fff
    style F fill:#e8a838,color:#fff
    style O fill:#50b86c,color:#fff
    style E fill:#d94a4a,color:#fff
```

---

## 八、LLM 流式调用详细流程（LLMClient::chat）

```mermaid
flowchart TD
    A["LLMClient::chat(messages, tools, system_prompt, callbacks)"] --> B["validateConfig()\n检查 api_key / base_url / model"]

    B --> C["buildRequestBody()\n构造 JSON 请求体:\n• model + messages\n• tools + temperature\n• max_tokens\n• stream = true"]

    C --> D["创建 StreamingPipeline"]
    D --> E["pipeline.setCallbacks(callbacks)\n注册外部回调"]
    E --> F["http_.postStreaming(path, body, content_receiver)"]

    subgraph "HTTP 传输 + SSE 流式处理"
        F --> G["httplib 发送 POST 请求\nAuthorization: Bearer xxx"]
        G --> H["API 返回 SSE 流:\ndata: {...}\n\ndata: {...}\n\ndata: [DONE]"]
        H --> I["content_receiver 逐块接收"]
        I --> J["pipeline.feed(raw_data)\n→ SSEParser 切分事件\n→ StreamChunk 解析\n→ StreamAccumulator 累积\n→ 回调转发"]
    end

    J --> K{"httplib::Result 有效?"}
    K -->|"网络错误"| L["❌ httpErrorToString()\n→ 中文错误描述\n→ callbacks.on_error()\n→ throw runtime_error"]

    K -->|"有效"| M{"HTTP status == 200?"}
    M -->|"≠ 200"| N["❌ parseApiError()\n→ 解析错误 JSON\n→ callbacks.on_error()\n→ throw runtime_error"]

    M -->|"200"| O{"pipeline.hasError()?"}
    O -->|"有 SSE 解析错误"| P["❌ pipeline.error()\n→ throw runtime_error"]

    O -->|"无错误"| Q{"pipeline.completed()?"}
    Q -->|"未完成"| R["❌ 流未正常结束\n→ throw runtime_error"]

    Q -->|"已完成"| S["✅ pipeline.response()\n返回完整 LLMResponse"]

    style A fill:#4a90d9,color:#fff
    style J fill:#e8a838,color:#fff
    style S fill:#50b86c,color:#fff
    style L fill:#d94a4a,color:#fff
```

---

## 九、StreamingPipeline 内部数据流

SSE 文本 → StreamChunk → LLMResponse 的完整转换管线。

```mermaid
flowchart LR
    subgraph "输入: 原始 SSE 文本"
        A["data: {&quot;id&quot;:&quot;chatcmpl-xxx&quot;,&quot;choices&quot;:[{&quot;delta&quot;:{&quot;content&quot;:&quot;你好&quot;}}]}..."]
    end

    subgraph "SSEParser — 协议层"
        B["按 \n\n 切分事件边界"]
        C["提取 'data:' 行内容"]
        D{"内容 == '[DONE]'?"}
        D -->|"是"| D1["StreamChunk{is_end=true}"]
        D -->|"否"| D2["JSON.parse() → processChunk()"]
        D2 --> D3["提取 choices[0].delta:\n• content → content_delta\n• reasoning_content → reasoning_delta\n• tool_calls[] → tool_call_deltas"]
    end

    subgraph "StreamChunk 中间表示"
        E["content_delta: string\nreasoning_delta: string\ntool_call_deltas: vector&lt;ToolCallDelta&gt;\nfinish_reason: string\nusage: UsageInfo\nis_end: bool"]
    end

    subgraph "StreamAccumulator — 聚合层"
        F["content 拼接: += delta"]
        G["reasoning 拼接: += delta"]
        H["tool_calls 按 index 合并\narguments 跨 chunk 拼接"]
        I["metadata 捕获:\nid / model / created"]
        J["usage 覆盖:\n末 chunk 覆盖前值"]
        K["checkComplete():\nfinish_reason 非空 或 is_end"]
    end

    subgraph "输出: LLMResponse"
        L["id, model, created\ncontent (完整文本)\nreasoning_content (思维链)\ntool_calls[] (完整调用列表)\nfinish_reason\nprompt/completion/total tokens"]
    end

    subgraph "实时回调转发"
        M["on_content(delta)\n→ 终端逐字绿色输出"]
        N["on_reasoning(delta)\n→ 终端灰色思维链"]
        O["on_tool_call_start()\n→ 终端 '[工具调用]' 标签"]
        P["on_complete(response)\n→ 终端 '(N tokens)' 统计"]
        Q["on_error(msg)\n→ 终端红色错误"]
    end

    A --> B --> C --> D
    D1 & D3 --> E
    E --> F & G & H & I & J
    F & G & H & I & J --> K
    K --> L
    E -.->|"触发回调"| M & N & O & P & Q

    style A fill:#e0e0e0
    style E fill:#fff3cd
    style L fill:#d4edda
```

---

## 十、完整时序图：从用户输入到 LLM 响应

```mermaid
sequenceDiagram
    autonumber

    participant User as 👤 用户
    participant CLI as 🖥️ ReplHandler
    participant Agent as 🤖 Agent
    participant Proc as 🔀 SerialProcessor
    participant CM as 📊 ContextManager
    participant Loop as 🔄 ToolCallLoop
    participant Pipe as 🔧 ToolPipeline
    participant Reg as 📋 ToolRegistry
    participant Client as 🌐 LLMClient
    participant SP as 📡 StreamingPipeline
    participant Conv as 💬 Conversation

    %% ═══ 阶段 1: 用户输入 ═══
    rect rgb(240, 248, 255)
        Note over User,Conv: ═══ 阶段 1: 用户输入 ═══
        User->>CLI: 输入 "帮我写第三章"
        CLI->>CLI: startSpinner("思考")
        CLI->>CLI: StreamDisplay::create(out)
        CLI->>Agent: processUserMessage(input, callbacks)
        Agent->>Proc: process(input, conversation, callbacks)
    end

    %% ═══ 阶段 2: 上下文组装 ═══
    rect rgb(255, 250, 240)
        Note over Proc,CM: ═══ 阶段 2: 上下文组装 ═══
        Proc->>Conv: addUser("帮我写第三章")
        Proc->>Reg: getToolDefinitions()
        Reg-->>Proc: tools[]
        Proc->>CM: assemble(conversation, context_window, project, chapter_id)
        CM->>CM: 预算分配 (50/30/20)
        CM->>CM: 构建系统提示词 (项目+章节上下文)
        CM->>CM: Token 计算 + 降级判断
        CM->>CM: 对话摘要 (可选)
        CM->>CM: 消息截断 (O(n) 反向遍历)
        CM-->>Proc: ContextAssembly {system_prompt, messages, budget}
        Proc->>Proc: PromptComposer::compose({personality, context})
    end

    %% ═══ 阶段 3: Tool Call 循环 ═══
    rect rgb(240, 255, 240)
        Note over Proc,Client: ═══ 阶段 3: Tool Call 循环 — 首轮流式调用 ═══
        Proc->>Loop: 创建 ToolCallLoop(client, registry)
        Proc->>Loop: run(conversation, tools, prompt, callbacks, config)
        Loop->>Client: chat(messages, tools, system_prompt, callbacks)
        Client->>Client: validateConfig() + buildRequestBody()
        Client->>SP: 创建 StreamingPipeline + setCallbacks(callbacks)
        Client->>Client: http_.postStreaming()

        loop SSE 流式数据
            Client->>SP: feed(raw_sse_data)
            SP->>SP: SSEParser → StreamChunk
            SP->>SP: StreamAccumulator 累积
            SP-->>CLI: on_content(delta) → 终端绿色输出
            SP-->>CLI: on_reasoning(delta) → 终端灰色输出
        end

        SP-->>Client: completed → pipeline.response()
        Client-->>Loop: LLMResponse{content, tool_calls[]}
    end

    %% ═══ 阶段 4: 工具执行 (如有) ═══
    rect rgb(255, 240, 255)
        Note over Loop,Conv: ═══ 阶段 4: 工具执行 (当 LLM 返回 tool_calls 时) ═══
        Loop->>Loop: 检查 response.tool_calls 非空
        Loop->>Conv: add(assistant_message + tool_calls)

        Loop->>Pipe: executeAndAppend(tool_calls)
        loop 每个 ToolCall
            Pipe->>Pipe: JSON 解析参数
            Pipe->>Pipe: ParameterValidator::validate()
            Pipe->>Reg: executeTool(name, args)
            Reg->>Reg: 查找工具条目
            Reg-->>Pipe: 工具执行结果 JSON
            Pipe->>Pipe: truncateResult(32KB)
            Pipe->>Conv: addToolResult(tc.id, result)
        end

        Pipe-->>Loop: 全部工具执行完毕

        Loop->>Client: chatNonStreaming(messages, tools, prompt)
        Client-->>Loop: LLMResponse (后续轮次)
        Note over Loop: 循环直到无 tool_calls 或达到 max_rounds
    end

    %% ═══ 阶段 5: 结果返回 ═══
    rect rgb(255, 248, 248)
        Note over Loop,User: ═══ 阶段 5: 结果返回 ═══
        Loop-->>Proc: ToolCallLoopResult{response, rounds_executed}
        Proc->>Conv: add(assistant_message)
        Proc-->>Agent: Result{text, raw_response}
        Agent-->>CLI: LLMResponse
        CLI->>CLI: stopSpinner()
        CLI->>User: 显示最终回复 + token 统计
    end
```

---

## 十一、并行处理器流程（ParallelProcessor — 可选模式）

```mermaid
flowchart TD
    A["ParallelProcessor::process(input, conversation, callbacks)"] --> B["orchestrator_->processMessage(input)\n→ AgentOrchestrator 编排"]

    subgraph "AgentOrchestrator 并行编排"
        B --> C["分析用户意图"]
        C --> D{"任务可拆分?"}
        D -->|"是"| E["拆分为子任务列表"]
        E --> F["创建 SubAgent 并行执行"]
        F --> F1["SubAgent 1: 读取上下文"]
        F --> F2["SubAgent 2: 撰写草稿"]
        F --> F3["SubAgent 3: 检查一致性"]
        F1 & F2 & F3 --> G["合并子 Agent 结果"]
        G --> H["主 Agent 审阅 + 润色"]
        D -->|"否"| I["直接调用 LLM 处理"]
    end

    B --> J["获得最终文本"]
    J --> K["conversation.addUser(input)"]
    K --> L["conversation.addAssistant(text)"]
    L --> M["返回 Result{text}"]

    style A fill:#4a90d9,color:#fff
    style B fill:#e8a838,color:#fff
    style M fill:#50b86c,color:#fff
```

---

## 十二、工具自注册流程

展示从 `REGISTER_TOOL` 宏到运行时工具可用的完整链路。

```mermaid
flowchart TD
    subgraph "编译时 — 静态注册"
        A["工具 .cpp 文件中调用\nREGISTER_TOOL(ReadChapterTool, 'read_chapter', read_chapter)"] --> B["宏展开:\nstatic const bool _reg_read_chapter =\n  BuiltInTool::registerFactory(\n    'read_chapter',\n    [](shared_ptr&lt;Project&gt; p) {\n      return make_unique&lt;ReadChapterTool&gt;(p);\n    });"]
        B --> C["程序启动时自动执行\n工厂函数存入 BuiltInTool::factories()"]
    end

    subgraph "运行时 — 实例化"
        C --> D["NovelAgentApp::setupAgent()"]
        D --> E["agent::registerAllTools(registry, project, disabled)"]
        E --> F["BuiltInTool::registerAllTo(registry, project, disabled)"]
        F --> G["遍历 factories()"]
        G --> H{"工具名在 disabled 中?"}
        H -->|"是"| I["跳过"]
        H -->|"否"| J["factory(project) → unique_ptr&lt;BuiltInTool&gt;"]
        J --> K["registry.registerBuiltInTool(tool)\n内部包装为 ToolEntry"]
    end

    subgraph "运行时 — 查询与执行"
        K --> L["Agent 调用 registry.getToolDefinitions()\n→ 返回所有工具的 ToolDefinition 列表"]
        L --> M["LLM 在 function calling 中\n选择工具并传参"]
        M --> N["ToolPipeline 调用 registry.executeTool(name, args)\n→ 查找 ToolEntry → 执行回调"]
    end

    style A fill:#4a90d9,color:#fff
    style C fill:#fff3cd
    style K fill:#50b86c,color:#fff
    style N fill:#e8a838,color:#fff
```

---

## 十三、关键设计模式汇总

| 模式 | 应用位置 | 说明 |
|------|----------|------|
| **策略模式** | `IMessageProcessor` → `SerialProcessor` / `ParallelProcessor` | Agent 的处理模式可插拔切换 |
| **门面模式** | `NovelAgentApp` | 封装全部组件装配 |
| **门面模式** | `ToolPipeline` | 统一工具执行管线（校验→执行→截断→错误处理） |
| **门面模式** | `StreamingPipeline` | 封装 SSEParser + StreamAccumulator |
| **模板方法** | `ToolCallLoop::run()` | 首轮调用→循环→执行→回传 的标准骨架 |
| **依赖倒置** | `ILLMClient` / `IOutputChannel` / `IStorageBackend` / `IProjectReader` | Agent 依赖抽象而非具体实现 |
| **工厂方法** | `Message::user()` / `Message::system()` 等 | 统一消息构造方式 |
| **自注册** | `REGISTER_TOOL` 宏 + `BuiltInTool::factories()` | 新增工具只需继承 + 宏调用 |
| **组合优于继承** | `ContextManager` 组合 4 个子模块 | 职责分离，各子模块可独立测试 |
| **RAII** | 所有资源管理 | 文件句柄、互斥锁、临时目录均通过类管理 |

---

## 十四、错误处理策略分布

```mermaid
flowchart TD
    subgraph "LLMClient 层 — 抛异常"
        A1["网络错误 → runtime_error"]
        A2["HTTP 非 200 → runtime_error"]
        A3["SSE 解析错误 → runtime_error"]
        A4["流未正常结束 → runtime_error"]
    end

    subgraph "Agent/ToolPipeline 层 — 返回 error JSON"
        B1["JSON 解析失败 → {error: '...'}"]
        B2["参数校验失败 → {valid: false, errors: [...]}"]
        B3["工具不存在 → {error: '未知工具'}"]
        B4["工具执行异常 → {error: '...'}"]
        B5["单个工具失败不阻断其他工具"]
    end

    subgraph "ReplHandler 层 — 捕获所有异常"
        C1["try-catch 包裹 processUserMessage()"]
        C2["显示友好错误提示"]
        C3["自动保存项目 (autoSaveOnError)"]
        C4["不退出 REPL 循环"]
    end

    A1 & A2 & A3 & A4 -->|"异常向上传播"| C1
    B1 & B2 & B3 & B4 -->|"错误 JSON 回传 LLM"| C1

    style A1 fill:#d94a4a,color:#fff
    style B1 fill:#e8a838,color:#fff
    style C1 fill:#50b86c,color:#fff
```

---

## 运行命令

在 VS Code 中打开此文件后，按 `Ctrl+Shift+V` 预览渲染效果；或安装 Mermaid 插件获得实时预览。

也可用命令行生成 SVG/PNG：

```bash
# 安装 mermaid-cli
npm install -g @mermaid-js/mermaid-cli

# 生成 SVG
mmdc -i docs/diagrams/Agent运行流程图.md -o docs/diagrams/Agent运行流程图.svg
```
