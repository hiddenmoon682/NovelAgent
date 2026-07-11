# NovelAgent — C++20 CLI 写小说 Agent

## 项目规则

### 注释语言
- **所有注释、docstring、文档说明必须使用中文。**
- 包括：文件头注释、函数说明、行内注释、TODO/FIXME/HACK 标记、README 中的说明文字。
- 代码标识符（变量名、函数名、类型名）仍使用英文。
- Git 提交信息使用中文。

### 代码风格
- C++20, CMake 构建, `nlohmann/json` + `spdlog` + `CLI11`。
- 头文件使用 `#pragma once`。
- 命名空间：`utils::file`, `utils::string`, `agent::`, `llm::`。

### 工作流
- 每 Phase 结束后：更新 CHANGELOG → git commit。（不自动 push，需要时手动执行）
- 变更记录以增量添加方式写入根目录 `CHANGELOG.md`（按日期倒序，最新在最上），不创建额外的 changelog 文件。
- 完成阶段性工作后更新 `docs/DEV_GUIDE.md` 中的相关章节。

---

## 架构规则（Phase 3 建立）

### 依赖倒置 — 核心组件依赖抽象接口
- `agent::Agent` 依赖 `llm::ILLMClient`（非具体 `LLMClient`）
- 只读工具依赖 `IProjectReader&`，写入工具依赖 `Project&`（通过 `ProjectAccess` 适配器）
- CLI 层（`ReplHandler`, `CommandParser`, `StreamDisplay`）通过 `IOutputChannel&` 输出，不硬编码 `std::cout`
- 会话持久化通过 `FileStorageBackend`（封装项目路径 + 转发 `ProjectIO`）；向量存储扩展点在 `IVectorStore`（未来可选 sqlite-vec 后端，非 IStorageBackend）
- **禁止**：直接持有具体类引用、硬编码全局流、跨层 include 实现头文件

### 门面模式
- `NovelAgentApp` 封装全部组件装配（替代 main.cpp 手动初始化）
- `ToolPipeline` 统一工具执行管线（校验→执行→截断→错误处理）
- `PromptComposer` 显式组装 system prompt（personality/context/task）
- `StreamingPipeline` 封装 SSEParser + StreamAccumulator

### 工具自注册 — 新增工具只需两步
1. 创建类继承 `agent::BuiltInTool`（实现 name/description/parameters/execute/category）
2. 在 `.cpp` 中添加 `REGISTER_TOOL(ToolClass, "tool_name", unique_suffix)`
- ShellTools 等不接收 `Project&` 的工具使用手动注册（见 `ShellTools.cpp`）
- **禁止**：手动编辑 `AgentSetup.cpp` 添加注册代码

### 读写分离
- `IProjectReader` — 只读接口（Get*, List* 工具使用）
- `IProjectWriter` — 写入接口（Create*, Update*, Write* 工具使用）
- `ProjectAccess` 将 `Project&` 适配为两个接口
- Phase 3.5 子 Agent 只获取 `IProjectReader&`，确保并行安全

---

## 封装规则

### 数据结构
- **DTO 使用 struct 全 public**：`ContextAssembly`, `ToolDefinition`, `StreamCallbacks`（配合 nlohmann ADL 序列化）
- **有行为的类全 private 成员**：`Agent`, `ToolRegistry`, `LLMClient`
- `Conversation` 只提供 const 访问器，修改只能通过 `add*()` 方法
- **禁止**：类暴露非 const 引用到内部容器

### 工厂方法（优先使用，不直接聚合初始化）
- `Message::user(content)` → `Message::system()` / `assistant()` / `toolResult(id, content)`
- `utils::schema::object({...}, required)` → `stringProp()` / `integerProp()` / `booleanProp()` / `stringEnum()`
- `llm::test::sseContentChunk()` / `sseFinishChunk()` / `sseToolCallChunk()` — SSE 测试辅助

### 生命周期
- Agent 持有外部引用（`ILLMClient&`, `ToolRegistry&`），不管理生命周期
- 工具持有 `Project&`（Phase 3.5 改为 `shared_ptr<Project>`）
- 所有裸指针依赖必须注释标注生命周期约束

---

## 编译性能规则

### 头文件最小化
- **用 `nlohmann/json_fwd.hpp` 替代 `nlohmann/json.hpp`**（头文件中只需前向声明时）
- **前向声明优先于 `#include`**（`class ToolRegistry;` 足够声明引用/指针成员时）
- **声明与实现分离**：模板外的实现移入 `.cpp`，头文件不引入 `spdlog` / 重型依赖
- **注册/装配逻辑移入 `.cpp`**（如 `AgentSetup.cpp` 包含所有工具头文件）

### 已配置的加速（不要破坏）
- PCH: `nlohmann/json.hpp` + `spdlog/spdlog.h` + STL（string/vector/map/algorithm/sstream/functional/optional）
- 对象库 `novelagent_lib`：所有业务 .cpp 编译一次，测试复用 .o
- Ninja 生成器（自动检测）

### 修改文件后的预期增量编译时间
- 修改一个 `.cpp`：~3-5s
- 修改一个不涉及 Models.h 的 `.h`：~5-10s
- 修改 `Models.h`：~30s（PCH 失效）

---

## 安全规则

### ShellTools
- **黑名单** ~40 关键词（含 PowerShell 缩写），拦截删除/格式化/下载/提权
- **允许**管道 `|`、重定向 `>`、分隔符 `;` `&&`（正常 shell 操作）
- 输出上限 100KB
- 安全声明：`isDangerousCommand()` 不提供绝对安全

### 工具参数
- `SchemaUtils::object()` 默认 `additionalProperties: false` — 拒绝 LLM 传入未定义字段
- Phase 5 将添加完整参数 Schema 校验

### LLM 交互
- API 重试：指数退避 3 次（1s/2s/4s），429/502/503 + 网络错误
- 工具结果上限 32KB（~8000 中文字），保证 `read_chapter` 返回全文

---

## 算法规则

### 避免的反模式
- **禁止** O(n²) 算法在热路径（如嵌套遍历 chapters × characters）
- **禁止** 高频路径中不必要的 `std::string` / `nlohmann::json` 拷贝
- **禁止** 工具中重复的文件 I/O（如列表时逐文件读取）
- **禁止** 手工拼接 JSON 字符串（`R"({"key":")" + val + R"("})"` → 用 `nlohmann::json`）

### 已有的优化（不要退化）
- `TokenCounter::countSingleMessage()` — 单条消息统计，避免临时 vector
- `TokenCounter::countTokens()` — 中英文单次遍历
- `ListChaptersTool` — 只返回元数据，不读文件

---

## 错误处理策略

| 层 | 策略 |
|----|------|
| LLMClient | 抛 `std::runtime_error`（网络/API 错误） |
| Agent/ToolPipeline | 单个工具失败不阻断其他，返回 `{"error":"..."}` |
| ReplHandler | 捕获所有异常，友好提示 |

---

## 测试规范

- 新模块必须有测试（`tests/test_<module>.cpp`）
- 临时目录测试用 RAII 自动清理
- Mock HTTP 用 `httplib::Server` + `bind_to_any_port`
- SSE 测试数据用 `test_sse_helpers.h` 构造
- 端到端测试标记为手动执行（不在 CTest 中自动运行）

---

---

## C++ 专项规则

### RAII — 所有资源必须由类管理
- 文件句柄：`std::unique_ptr<FILE, decltype(&_pclose)>`（见 `ShellTools.cpp`）
- 互斥锁 + 容器：封装为 RAII 类（见 `ConnectionCache` in `LLMClient.cpp`）
- 临时目录/文件：构造创建、析构清理（见测试中的 `TestProject`）
- 输出通道所有权：`std::unique_ptr<IOutputChannel>`（见 `NovelAgentApp`）
- **禁止**：裸 `new`/`delete`、裸 `malloc`/`free`、全局 `std::mutex` + `std::unordered_map` 散落

### 虚接口 vs 模板
- **运行时多态用虚接口**：`ILLMClient`, `BuiltInTool`, `IOutputChannel`, `IProjectReader`, `IVectorStore`
- **回调用 `std::function`**（类型擦除）：`StreamCallbacks`, `ToolRegistry` 的 fn
- **不要为"未来可能的扩展"添加虚接口**（`ContextManager` 当前无虚方法，Phase 4 需要时再加）
- **模板在本项目中仅用于**：`SchemaUtils` builder、nlohmann 的 `to_json`/`from_json` ADL

### Pimpl — 判断标准
- **考虑 Pimpl**：类被 10+ 个翻译单元包含 且 有 5+ 个重型 include 且 频繁修改
- **当前不需要**：`NovelAgentApp`（仅 2 个消费者）、`Agent`（已通过前向声明优化）、`ToolRegistry`（`vector<unique_ptr>` 无法 Pimpl 不引入额外分配）
- **已通过其他方式优化**：声明/实现分离（`ToolPipeline.h`→`.cpp`）、前向声明替代 include（`Agent.h` 中 `ToolRegistry`）

### 编译依赖最小化
- `nlohmann/json_fwd.hpp` 可用于头文件（仅需前向声明时），`nlohmann/json.hpp` 限 `.cpp`
- 前向声明优先级：class/struct → `json_fwd.hpp` → 轻型 STL（`<string>`）→ 重型三方库 → PCH 中的头文件
- 构建时用 Ninja（自动检测），不要切换回 Makefiles
- 不要把实现细节（spdlog 调用、nlohmann 构造）放在头文件的 inline 函数中

---

## 参考文档

| 文档 | 内容 |
|------|------|
| `docs/DEV_GUIDE.md` | 完整开发指南（设计模式、优化策略、注意事项） |
| `docs/review/REVIEW_NOTES.md` | 当前暂缓问题 |
| `docs/review/RESOLVED.md` | 已修复问题记录 |
| `PLAN.md` | 项目整体计划 |
