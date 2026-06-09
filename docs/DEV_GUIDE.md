# NovelAgent 开发指南

> 创建: 2026-06-09 | 基于 Phase 3 完整开发过程中积累的经验

本文档记录在 Phase 3 开发过程中建立的设计模式、优化策略和注意事项，
供后续 Phase 3.5 / Phase 4 / Phase 5 开发时参考。

---

## 一、架构原则

### 1.1 依赖倒置（Dependency Inversion）

**规则**: Agent/工具等核心组件依赖抽象接口，不依赖具体实现。

| 接口 | 实现 | 消费者 |
|------|------|--------|
| `llm::ILLMClient` | `llm::LLMClient` | `agent::Agent` |
| `IProjectReader` | `ProjectAccess(Project&)` | 只读工具（Get*, List*） |
| `IProjectWriter` | `ProjectAccess(Project&)` | 写入工具（Create*, Update*, Write*） |
| `IOutputChannel` | `ConsoleOutput` | `ReplHandler`, `CommandParser`, `StreamDisplay` |
| `IStorageBackend` | (Phase 4 实现) | 所有工具 |

**反模式**: 直接持有具体类引用、硬编码 `std::cout`、直接 `#include` 特定实现头文件。

### 1.2 门面模式（Facade）

- `NovelAgentApp` 封装全部组件装配逻辑，替代 main.cpp 中分散的初始化代码
- `ToolPipeline` 统一工具执行管线（校验→执行→截断→错误处理）
- `PromptComposer` 显式组装 system prompt（personality/context/task 三组件）
- `StreamingPipeline` 封装 SSEParser + StreamAccumulator 的管道装配

### 1.3 自注册模式（Self-Registration）

新增工具只需两步：
1. 创建工具类继承 `BuiltInTool`
2. 在 `.cpp` 中添加 `REGISTER_TOOL(ToolClass, "tool_name", var_suffix)`

工具自动被发现和注册，无需修改 `AgentSetup.cpp`。
ShellTools 等不需要 `Project&` 的工具使用手动注册。

---

## 二、封装规则

### 2.1 数据结构

- **DTO 使用 struct**: `ContextAssembly`, `ToolDefinition`, `StreamCallbacks` 等纯数据载体使用 public struct（配合 nlohmann ADL 序列化）
- **类使用 private 成员**: `Agent`, `ToolRegistry`, `ContextManager`, `LLMClient` 等有行为的类全部 private 成员
- **Conversation**: 移除所有非 const 访问器，修改只能通过 `add*()` 方法

### 2.2 工厂方法

- `Message::user()`, `Message::system()`, `Message::assistant()`, `Message::toolResult()` — 替代聚合初始化
- `SchemaUtils::object()`, `stringProp()`, `integerProp()` 等 — JSON Schema 构建

### 2.3 指针与生命周期

- Agent 持有外部对象的引用（`ILLMClient&`, `ToolRegistry&`），不管理生命周期
- 工具持有 `Project&`（Phase 3.5 改为 `shared_ptr<Project>` 以支持多 Agent）
- ContextManager 通过 `Agent::setContextManager()` 注入，未来改为 `IContextManager` 接口
- 所有裸指针到外部对象的依赖必须标注生命周期约束（注释）

---

## 三、算法与性能

### 3.1 已应用的优化

| 类别 | 优化 | 位置 |
|------|------|------|
| 复杂度 | `truncateMessages` O(n²) → O(n) | `ContextManager.cpp` |
| 复杂度 | `countTokens` 中英文单次遍历 | `TokenCounter.cpp` |
| 拷贝 | `countSingleMessage` 避免临时 vector | `TokenCounter.h` |
| 拷贝 | `buildEffectivePrompt` 消除双重消息拷贝 | `Agent.cpp` |
| I/O | `ListChaptersTool` 不读文件 | `ChapterTools.cpp` |
| I/O | `CreateChapterTool` 不写 6 个无用 JSON | `ChapterTools.cpp` |

### 3.2 后续注意事项

- 新增工具时避免 O(n²) 算法（如嵌套遍历 chapters × characters）
- 高频路径（Agent 循环、工具执行）避免不必要的 string/json 拷贝
- `countSingleMessage` 用于单条消息统计；批量统计用 `countMessages`

---

## 四、编译性能

### 4.1 头文件规则

| 规则 | 示例 |
|------|------|
| 用 `json_fwd.hpp` 替代 `json.hpp` | `BuiltInTool.h`, 工具声明头文件 |
| 前向声明替代 `#include` | `Agent.h` 中 `class ToolRegistry;` |
| 声明与实现分离 | `ToolPipeline.h` (声明) + `ToolPipeline.cpp` (实现) |
| 注册逻辑移入 .cpp | `AgentSetup.h` (前向声明) + `AgentSetup.cpp` (include 全部工具) |

### 4.2 已配置的加速

- **PCH**: `nlohmann/json.hpp`, `spdlog/spdlog.h`, STL (string, vector, map, algorithm, sstream, functional, optional)
- **对象库**: `novelagent_lib` — 所有业务 .cpp 编译一次，测试和 main 复用 .o
- **生成器**: Ninja（自动检测）

### 4.3 当前编译时间

| 场景 | 时间 |
|------|------|
| 全量构建（clean build） | ~45s |
| 修改一个 .cpp（增量） | ~3-5s |
| 修改一个工具头文件（增量） | ~5-10s |
| 修改 Models.h（增量） | ~30s（PCH 失效） |

---

## 五、测试策略

### 5.1 测试分层

| 层 | 目标 | 示例 |
|----|------|------|
| 单元测试 | 单个类/函数的逻辑 | `test_token_counter`, `test_tool_registry` |
| Mock 集成测试 | 模拟 HTTP 服务器测试 Agent 循环 | `test_agent`, `test_llm_client` |
| 文件 I/O 测试 | 真实临时目录测试工具 | `test_chapter_tools`, `test_character_tools` |
| 端到端测试 | 真实 API + Agent + 工具 | `test_e2e_chapter`（手动执行） |

### 5.2 测试模式

- 使用临时目录 + RAII 自动清理
- Mock HTTP 服务器用 `httplib::Server` + `bind_to_any_port`
- SSE chunk 构造用 `test_sse_helpers.h` 辅助函数
- 项目创建用 `ProjectIO::createProjectDir` + `ProjectIO::load`

---

## 六、安全规则

### 6.1 ShellTools

- **黑名单**: 40+ 关键词（含 PowerShell 缩写），拦截破坏性操作
- **输出截断**: 100KB 上限
- **管道和重定向**: 允许（`|`, `;`, `>`, `>>`）
- **后续加固**: Phase 3.5 改为 `CreateProcess` + `WaitForSingleObject` 实现超时

### 6.2 工具参数

- `SchemaUtils::object()` 默认 `additionalProperties: false` — 拒绝 LLM 传入额外字段
- Phase 5.5 将实现完整的参数 Schema 校验（required/type/enum 检查）

### 6.3 LLM 交互

- API 调用重试: 指数退避 3 次（1s/2s/4s），429/502/503 + 网络错误
- 工具结果截断: 32KB 安全上限

---

## 七、错误处理策略

| 层 | 策略 | 示例 |
|----|------|------|
| Infrastructure (LLMClient) | 抛异常 `std::runtime_error` | 网络错误、API 错误 |
| Domain (Agent) | 捕获异常 + 工具返回 JSON error | `executeTool()` 返回 `{"error":"..."}` |
| Domain (ToolPipeline) | 单个工具失败不阻断其他 | try-catch per tool_call |
| Presentation (ReplHandler) | 捕获所有异常，友好提示 | `catch (const std::exception&)` |

---

## 八、后续 Phase 需注意的关键点

### Phase 3.5（多 Agent 并行编排）

- ⚠️ `Project&` 裸引用 → 改为 `shared_ptr<Project>`（子 Agent 独立生命周期）
- ⚠️ ShellTools 超时 → `CreateProcess` + `WaitForSingleObject`
- ⚠️ SubAgent 只能获取 `IProjectReader&`（只读权限）

### Phase 4（上下文管理 + 语义检索）

- ⚠️ `IStorageBackend` 实现 FileStorageBackend（当前） + SqliteStorageBackend（Phase 4）
- ⚠️ ContextManager 多级降级（50/30/20 预算分配）
- ⚠️ 对话摘要压缩（`summarizeConversation`）

### Phase 5（打磨）

- ⚠️ Step 5.4: Agent 显式状态机（Idle/Thinking/AwaitingTool/WaitingUser/Error）
- ⚠️ Step 5.5: 工具参数 Schema 校验
- ⚠️ Step 5.6: Agent 执行轨迹记录（JSONL，`/trace show`）

---

## 九、编码惯例速查

- 注释语言: 中文
- 命名空间: `agent::`, `llm::`, `utils::file::`, `utils::schema::`
- 工具注册: `REGISTER_TOOL` 宏（`BuiltInTool.h`）
- 工具基类: `agent::BuiltInTool`（5 个纯虚方法）
- 工具类别: `agent::ToolCategory`（7 个枚举值）
- 工厂方法: `Message::user()` / `system()` / `assistant()` / `toolResult()`
- JSON Schema: `utils::schema::object()` / `stringProp()` / `integerProp()`
- SSE 测试: `llm::test::sseContentChunk()` / `sseFinishChunk()` / `sseToolCallChunk()`
- `#pragma once` 而非 include guards
- C++20，CMake 3.24+，GCC 15.2 (MinGW-w64)
