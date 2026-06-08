# NovelAgent — LLM 请求→响应 完整流程图

> 生成日期: 2026-06-08  
> 覆盖范围: 从用户输入到 LLMResponse 返回的完整数据流  
> 图表格式: Mermaid（可在 VS Code、GitHub、Typora 中直接渲染）

---

## 一、总览：非流式 vs 流式两条路径

```mermaid
flowchart TD
    A["👤 用户输入"] --> B["CLI / REPL 解析"]
    B --> C["Agent 构建对话上下文\n(Conversation + Message)"]
    C --> D{"调用模式?"}

    D -->|"--exec 单次命令\n或不需要实时输出"| E["LLMClient::chatNonStreaming()"]
    D -->|"REPL 交互模式\n需要逐 token 输出"| F["LLMClient::chat() 流式"]

    E --> G["HTTP POST → 等待完整 JSON"]
    G --> H["JSON 解析 → LLMResponse"]
    H --> Z["🎯 返回给 Agent"]

    F --> I["HTTP POST + content_receiver"]
    I --> J["StreamingPipeline::feed()"]
    J --> K["逐 chunk 解析 + 累积"]
    K --> Z
```

---

## 二、详细时序图：流式调用完整过程

```mermaid
sequenceDiagram
    autonumber

    participant User as 👤 用户/CLI
    participant Agent as 🤖 Agent
    participant Conv as 📋 Conversation
    participant Client as 🌐 LLMClient
    participant HTTP as 🔌 httplib::Client<br/>(长连接缓存)
    participant Pipe as 🔗 StreamingPipeline
    participant Parser as 📡 SSEParser
    participant Acc as 🧩 StreamAccumulator
    participant API as ☁️ LLM API<br/>(DeepSeek/Kimi/Claude)

    %% ── 阶段 1: 准备工作 ──
    rect rgb(240, 248, 255)
        Note over User,API: ═══ 阶段 1: 准备请求 ═══
        User->>Agent: 用户输入文本
        Agent->>Conv: addUser("帮我写第三章...")
        Agent->>Conv: messages() + systemPrompt()
        Conv-->>Agent: 消息列表 + system 提示词
        Agent->>Client: chat(messages, tools, system_prompt, callbacks)
    end

    %% ── 阶段 2: 请求构造 ──
    rect rgb(255, 250, 240)
        Note over Client,API: ═══ 阶段 2: 构造并发送请求 ═══
        Client->>Client: validateConfig()<br/>检查 api_key / base_url / model
        Client->>Client: buildRequestBody()<br/>组装 JSON: model + messages +<br/>tools + temperature + max_tokens + stream=true
        Client->>HTTP: getOrCreateClient(this, base_url)<br/>获取或创建长连接 (Keep-Alive)
        HTTP-->>Client: httplib::Client&
        Client->>Client: 构造 httplib::Request<br/>设置 Authorization / Content-Type 头
        Client->>Client: 绑定 content_receiver lambda
    end

    %% ── 阶段 3: HTTP 发送 ──
    rect rgb(240, 255, 240)
        Note over Client,API: ═══ 阶段 3: HTTP 传输 ═══
        Client->>HTTP: cli.send(req) — POST /v1/chat/completions
        HTTP->>API: TCP/TLS 发送请求体 (JSON)
        API-->>HTTP: HTTP 200 + 逐 chunk 返回 SSE 数据
        Note over API: 模型逐 token 生成，每生成若干 token<br/>即推送一个 SSE chunk
    end

    %% ── 阶段 4: SSE 解析 ──
    rect rgb(255, 240, 255)
        Note over HTTP,Acc: ═══ 阶段 4: SSE 流式解析 ═══
        loop 每个 SSE chunk
            HTTP->>Client: content_receiver(data, len)
            Client->>Pipe: feed(raw_sse_data)
            Pipe->>Parser: feed(data)

            Note over Parser: 按 \n\n 切分事件边界<br/>提取 "data:" 行内容

            alt data 行 == "[DONE]"
                Parser->>Parser: 构建 StreamChunk{is_end=true}
            else data 行 == JSON
                Parser->>Parser: processEvent() → 解析 JSON
                Parser->>Parser: processChunk(json)<br/>提取 choices[0].delta 中各字段<br/>→ StreamChunk
            end

            Parser-->>Pipe: on_chunk(StreamChunk)
        end
    end

    %% ── 阶段 5: 增量累积 ──
    rect rgb(255, 255, 240)
        Note over Pipe,Acc: ═══ 阶段 5: 跨 chunk 增量累积 ═══
        loop 每个 StreamChunk
            Pipe->>Acc: feed(StreamChunk)
            Acc->>Acc: 首个 chunk → 捕获 id/model/created
            Acc->>Acc: content_delta → response_.content += delta
            Acc->>Acc: reasoning_delta → response_.reasoning_content += delta
            Acc->>Acc: tool_call_deltas → 按 index 累积到<br/>pending_tool_calls_[index]<br/>arguments 跨 chunk 拼接

            opt usage 非空（末个 chunk）
                Acc->>Acc: 覆盖 prompt/completion/total tokens
            end

            Acc->>Acc: checkComplete(chunk)<br/>finish_reason 非空 或 is_end?
        end
    end

    %% ── 阶段 6: 回调转发 ──
    rect rgb(248, 248, 255)
        Note over Pipe,User: ═══ 阶段 6: 实时回调转发 ═══
        Pipe->>Agent: on_content(delta) — 每个 content chunk
        Pipe->>Agent: on_reasoning(delta) — 思维链增量
        Pipe->>Agent: on_tool_call_start() — 首次检测到 tool_call
        Pipe->>Agent: on_complete(LLMResponse) — 流结束
    end

    %% ── 阶段 7: 结果返回 ──
    rect rgb(255, 248, 248)
        Note over Client,User: ═══ 阶段 7: 完成与返回 ═══
        Pipe-->>Client: pipeline.completed() == true
        Client->>HTTP: 检查 res->status 是否为 200
        Client->>Client: pipeline.response() → LLMResponse
        Client-->>Agent: 返回完整 LLMResponse
        Agent-->>User: 显示最终回复
    end
```

---

## 三、内部组件数据流图

展示各组件之间的数据类型转换：

```mermaid
flowchart LR
    subgraph "HTTP 传输层"
        A["原始 SSE 文本流<br/>data: {...}\n\ndata: {...}\n\ndata: [DONE]"]
    end

    subgraph "SSEParser — 协议解析"
        B["按 \n\n 切分事件"]
        C["提取 data: 行"]
        D["JSON → StreamChunk"]
        A --> B --> C --> D
    end

    subgraph "StreamChunk 结构"
        E["content_delta: string<br/>reasoning_delta: string<br/>tool_call_deltas: vector<br/>finish_reason: string<br/>usage: UsageInfo<br/>is_end: bool"]
    end

    D --> E

    subgraph "StreamAccumulator — 跨 chunk 聚合"
        F["content 拼接: += delta"]
        G["reasoning 拼接: += delta"]
        H["tool_calls 按 index 合并<br/>arguments 跨 chunk 拼接"]
        I["metadata 捕获: id/model/created"]
        J["usage 覆盖: 末 chunk 覆盖前值"]
    end

    E --> F & G & H & I & J

    subgraph "LLMResponse 最终产物"
        K["content: 完整文本<br/>reasoning_content: 思维链<br/>tool_calls: 完整工具调用列表<br/>finish_reason: stop/length/tool_calls<br/>usage: {prompt, completion, total}"]
    end

    F & G & H & I & J --> K
```

---

## 四、错误处理路径

```mermaid
flowchart TD
    Start["LLMClient::chat() 开始"] --> Validate{"validateConfig()"}

    Validate -->|"api_key / base_url / model 缺失"| ErrConfig["❌ std::runtime_error<br/>详细中文错误描述"]
    Validate -->|"通过"| BuildReq["构造请求体 JSON"]

    BuildReq --> SendHTTP["cli.send(req)"]

    SendHTTP --> CheckTransport{"res 是否有效?"}
    CheckTransport -->|"res.error() != Success"| ErrNetwork["❌ httpErrorToString()<br/>连接失败/超时/SSL错误<br/>→ 中文描述"]

    CheckTransport -->|"有效"| CheckHTTP{"res->status == 200?"}
    CheckHTTP -->|"≠ 200"| ErrAPI["❌ parseApiError()<br/>优先解析 JSON error.message<br/>回退 HTTP 状态码描述"]

    CheckHTTP -->|"200"| CheckSSE{"pipeline.hasError()?"}
    CheckSSE -->|"有 SSE 解析错误"| ErrSSE["❌ JSON 解析失败<br/>pipeline.error()"]

    CheckSSE -->|"无错误"| CheckComplete{"pipeline.completed()?"}
    CheckComplete -->|"未收到 finish_reason"| ErrIncomplete["❌ 流式响应未正常结束"]

    CheckComplete -->|"已完成"| Success["✅ 返回 LLMResponse"]

    ErrConfig & ErrNetwork & ErrAPI & ErrSSE & ErrIncomplete --> Callback["同时触发 callbacks.on_error()"]
```

---

## 五、关键数据结构对照

| 阶段 | 数据类型 | 来源 | 去向 |
|------|----------|------|------|
| 请求构造 | `std::vector<Message>` | `Conversation::messages()` | `buildRequestBody()` → JSON |
| 请求体 | `nlohmann::json` (body) | `buildRequestBody()` | HTTP POST body |
| SSE 原始数据 | `std::string` (raw bytes) | `httplib::content_receiver` | `StreamingPipeline::feed()` |
| SSE 事件 | `std::string` (event text) | `SSEParser::feed()` 内部切分 | `SSEParser::processEvent()` |
| 解析单元 | `StreamChunk` | `SSEParser::processChunk()` | `StreamAccumulator::feed()` |
| 增量片段 | `ToolCallDelta` | `StreamChunk::tool_call_deltas` | `StreamAccumulator::pending_tool_calls_` |
| 最终产物 | `LLMResponse` | `StreamAccumulator::checkComplete()` | `LLMClient` → `Agent` |
| 回调通知 | `StreamCallbacks` 各字段 | `StreamingPipeline` 转发 | Agent / StreamDisplay |

---

## 六、非流式 vs 流式对比

| 特性 | `chatNonStreaming()` | `chat()` 流式 |
|------|---------------------|---------------|
| HTTP 方式 | `cli.Post()` — 等待完整响应 | `cli.send()` + `content_receiver` |
| 解析方式 | `nlohmann::json::parse(res.body)` 一次性 | SSEParser 逐 chunk 增量解析 |
| 用户体验 | 等待完成后一次性输出 | 逐 token 实时显示（打字机效果） |
| 超时配置 | ReadTimeout=180s | ReadTimeout=180s（模型慢时安全） |
| 适用场景 | `--exec` 单次命令、批量处理 | REPL 交互模式 |
| 连接复用 | ✅ 长连接缓存 (Keep-Alive) | ✅ 长连接缓存 (Keep-Alive) |
| 错误定位 | 一次 parse 失败即报错 | 可区分传输层/HTTP/SSE解析/流完整性 四层错误 |

---

## 七、运行命令

在 VS Code 中打开此文件后，按 `Ctrl+Shift+V` 预览渲染效果；或安装 Mermaid 插件获得实时预览。

也可用命令行生成 SVG/PNG：

```bash
# 安装 mermaid-cli
npm install -g @mermaid-js/mermaid-cli

# 生成 SVG
mmdc -i docs/diagrams/LLM请求响应流程图.md -o docs/diagrams/LLM请求响应流程图.svg
```
