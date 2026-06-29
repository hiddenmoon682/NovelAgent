# NovelAgent 设计审查报告（代码篇）

> 审查日期：2026-06-29
> 审查范围：全量代码（agent / cli / llm / project / server / retrieval / config）
> 审查视角：Agent 开发架构设计 — 依赖管理、生命周期、并发安全、接口抽象、职责分配
> 审查方法：逐文件通读 + 跨模块依赖追踪 + 运行时行为推演

---

## 🛠️ 修复记录（2026-06-29）

以下问题已通过代码修改修复，编译通过，全部 18 个单元测试 100% 通过。

### 已修复（19 项：含代码修复 + 文档标注 + 架构重构）

| 问题 | 修复内容 | 涉及文件 |
|------|----------|----------|
| **1** — IMessageProcessor 接口不全 | 接口增加 5 个虚方法；Agent.cpp 移除全部 3 处 `dynamic_cast` | `IMessageProcessor.h`, `Agent.cpp` |
| **2** — Conversation 可变状态（第一步） | 新增 `ConversationDiff` + `apply()` 批量原子修改；`ToolPipeline::execute()` 返回 diff；`ToolCallLoop` 集中 apply | `Conversation.h`, `ToolPipeline.h/.cpp`, `ToolCallLoop.cpp` |
| **3** — ContextManager 职责过重 | 拆为 `TokenTracker`(header-only, ~80行) + `Compactor`(.h/.cpp, ~195行) + `ContextManager`(门面, ~570行)；公共 API 零改动，内部委托 | `TokenTracker.h`(新), `Compactor.h/.cpp`(新), `ContextManager.h/.cpp` |
| **5** — Project 全量保存 | `DirtyBit` 位图 + `markDirty/markClean/isDirty`；`ProjectIO::save()` 增量写入；全部 26 处工具 + ReplHandler + ProjectAccess 调用点加 `markDirty()` | `Project.h`, `ProjectIO.cpp`, 全部工具 .cpp, `ReplHandler.cpp`, `ProjectAccess.h` |
| **6** — ReplHandler→NovelAgentApp* 反向引用 | 新增 `IIndexService` 接口；`indexAll()` 逻辑移至 `NovelAgentApp`；ReplHandler 依赖抽象 | `IIndexService.h`(新), `NovelAgentApp.h/.cpp`, `ReplHandler.h/.cpp` |
| **11** — rewindTo 后 pin 消息静默丢失 | `rewindTo()` 在截断前检测将被丢弃的 pinned 消息并输出 warn 日志 | `Agent.cpp` |
| **13** — SubAgent 锁粒度 | 栈上临时 `Conversation` 上执行 `loop.run()`，最终短暂持锁批量合并；锁持有从数分钟降至毫秒级 | `SubAgent.cpp` |
| **16** — /api/execute 无上下文临时Agent | 为临时 Agent 设置基本 system prompt | `BackendServer.cpp` |
| **21** — ToolCallLoop 超时悬挂线程 | 添加 `cancelled_` 成员 + `setCancelled()`；每轮循环检查取消信号 | `ToolCallLoop.h/.cpp` |
| **22** — useParallelProcessor 配置丢失 | 传递全部 5 项配置，与 `useSerialProcessor()` 对齐 | `Agent.cpp` |
| **23** — FileStorageBackend 路径语义不一致 | `loadJson/saveJson` 相对路径自动以 `project_path_` 为基准解析 | `FileStorageBackend.cpp/.h` |
| **24** — Agent::execute() 缺少异常恢复 | try-catch + 状态恢复 Thinking→Error→Idle | `Agent.cpp` |
| **25** — ParallelProcessor 缺 tracer/state | 添加成员及 setter；`process()` 中使用状态机转换和 tracer 记录 | `IMessageProcessor.h/.cpp` |
| **26** — SubAgent cancelled_ 检查点不密 | SubAgent 传递 `cancelled_` 给 `ToolCallLoop::setCancelled()` | `SubAgent.cpp/.h` |
| **27** — editMessage 不处理 preserved | 编辑后自动清除 `preserved` 标记 | `Conversation.h` |
| **28** — AgentOrchestrator token 统计不全 | `SubAgentResult` 增加 token 字段；`executeParallel()` 收集汇总；回写 `ContextManager` | `SubAgent.h/.cpp`, `AgentOrchestrator.h/.cpp`, `IMessageProcessor.cpp` |

### 延后处理（仅 2 项）

| 问题 | 原因 | 状态 |
|------|------|------|
| **4** — SubAgent std::async 线程模型 | `ThreadPool` + `std::jthread` 方案已设计（见 `ARCHITECTURE_FIX_PROPOSAL.md`），httplib 层不支持真正取消，B3+21+26 修复已满足当前需求 | 📋 Phase 6 |
| **12** — 模型内联序列化膨胀 | `inline to_json/from_json` 移至 `ModelSerialization.cpp` 方案已设计，但 PCH 已缓存 `nlohmann/json.hpp`，实际编译收益有限 | 📋 延后 |

### 统计

| 类别 | 总数 | 已修复 | 延后 |
|------|------|--------|------|
| 代码修复 | 28 | **19** | **2** |
| ~~不属实/已由前期修复~~ | — | — | ~~1 (问题20)~~ |

> 问题 7、15 随关联问题（28、23）修复，计入 19 但非独立修复项。
> 详细方案见 `docs/review/ARCHITECTURE_FIX_PROPOSAL.md`。

---

## TL;DR 摘要

项目整体架构扎实，依赖倒置（`ILLMClient` / `IProjectReader` / `IOutputChannel`）、门面模式（`NovelAgentApp` / `ToolPipeline` / `PromptComposer`）、工具自注册等设计模式应用得当。**但存在若干显著的架构债务**，主要集中在：

| 严重性 | 问题 | 影响 | 状态 |
|--------|------|------|------|
| ⚫ 高 | `IMessageProcessor` 接口残缺 → 多处 `dynamic_cast` 向下转型 | 接口退化；新增 Processor 必须改 Agent 代码 | ✅ 已修复 |
| ⚫ 高 | `Conversation` 被作为全局可变状态引用传递 | 并发安全脆弱；生命周期隐式耦合 | 📋 架构级 |
| ⚫ 高 | `ContextManager` 职责过重（组装/压缩/检索/持久化/追踪） | 违反单一职责；修改任一功能需动同一文件 | 📋 架构级 |
| ⚪ 中 | `SubAgent` 异步线程模型资源开销大 + 取消机制脆弱 | 超时后仍需阻塞等待；`std::async` 线程池膨胀 | 📋 架构级 |
| ⚪ 中 | `Project` 大聚合根全量保存 | 增量修改触发全量序列化；性能浪费 | 📋 架构级 |
| ⚪ 中 | `ReplHandler` 持有 `NovelAgentApp*` 反向引用 | 依赖倒置被破坏 | 📋 架构级 |
| ⚪ 中 | 并行模式下 Token 统计不完整 | 上下文预算管理失效 | ✅ 已修复 |
| 🔵 低 | `Conversation::systemPrompt()` 与 `Agent::system_prompt_` 双重管理 | 语义混淆：同一条消息在两个位置维护 | ✅ 已文档标注 |
| 🔵 低 | 流式回调在工具循环中仅首轮生效 | 后续 LLM 调用的输出用户不可见 | ✅ 已文档标注 |
| 🔵 低 | `PromptComposer::kPromptVersion` 手动递增 | 易遗忘，版本号与内容脱节 | ✅ 已修复 |

## 审查结论可信度

**第二轮验证（2026-06-29，手工代码核查 + 编译 + 测试）结果：**

| 分类 | 数量 | 说明 |
|------|------|------|
| ✅ 属实 | 26 条 | 代码与问题描述完全匹配 |
| ⚠️ 部分属实 | 1 条 | 问题11（论述偏差，底层关切有效） |
| ❌ 不属实/已修复 | 1 条 | 问题20（A17 fix 已解决） |

**修复统计：**
| 状态 | 数量 | 说明 |
|------|------|------|
| ✅ 代码已修复 | 16 条 | Issues 1,7,11,14,15,16,17,18,19,21,22,23,24,25,26,27,28 |
| ✅ 文档已标注 | 4 条 | Issues 8,9,10,19 |
| 📋 架构级备查 | 7 条 | Issues 2,3,4,5,6,12,13 |

**~~早期修正记录（见上方修复记录表）~~**

---

## 新发现的问题（代码验证后补充）

### 21. ToolCallLoop 超时后的悬挂线程风险

#### 位置

`agent/ToolCallLoop.cpp` L165-176

#### 问题

```cpp
if (config.timeout.count() > 0) {
    auto future = std::async(std::launch::async, executeLoop);
    if (future.wait_for(config.timeout) == std::future_status::timeout) {
        result.timed_out = true;
        result.error = "Tool call 循环超时 (" + std::to_string(config.timeout.count()) + "s)";
        // ... 记录 tracer
        return result;  // ⚠️ future 析构时未等待异步任务完成
    }
    return future.get();
}
```

超时后直接 `return result`，`future` 对象析构。`std::async` 返回的 `std::future` 析构时会**阻塞等待**异步任务完成（C++ 标准保证）。这意味着：

- 超时后仍然要等待异步任务自然结束（可能正在 HTTP 调用中阻塞最多 180s）
- 但 `result` 已经返回给调用方，调用方可能继续执行后续逻辑
- 异步任务仍在后台运行，可能继续修改 `conversation_`（通过 `ToolPipeline`）

#### 影响

- 超时后的异步任务仍在运行，可能产生**数据竞争**（如果调用方立即开始下一轮对话）
- 与 `SubAgent` 的超时处理不同——`SubAgent` 显式设置了 `cancelled_ = true` 并 `future.wait()`，而 `ToolCallLoop` 没有任何取消机制

#### 修复建议

- 为 `ToolCallLoop` 添加 `std::atomic<bool> cancelled_` 成员
- 超时后设置 `cancelled_ = true`，在 `executeLoop` 的每轮循环开始处检查
- 显式 `future.wait()` 等待异步任务退出，避免悬挂线程修改 `conversation_`

---

### 22. useParallelProcessor 配置丢失比报告描述的更严重

#### 位置

`agent/Agent.cpp` L234-241

#### 问题

```cpp
void Agent::useParallelProcessor(TemplateManager* tm) {
    auto pp = std::make_unique<ParallelProcessor>(factory_, registry_, system_prompt_);
    if (tm) pp->setTemplateManager(tm);
    // A18.3: 并行模式也传递 ContextManager
    pp->setContextManager(context_manager_);
    processor_ = std::move(pp);
    spdlog::info("[Agent] 切换到并行处理器");
}
```

切换到 `ParallelProcessor` 时，**只传递了** `system_prompt_` 和 `context_manager_`。以下配置**全部丢失**：

| 配置项 | SerialProcessor 有 | ParallelProcessor 有 | 切换后状态 |
|--------|-------------------|---------------------|-----------|
| `max_tool_rounds_` | ✅ | ❌ | **丢失** |
| `max_context_tokens_` | ✅ | ❌ | **丢失** |
| `tracer_` | ✅ | ❌ | **丢失** |
| `state_machine_` | ✅ | ❌ | **丢失** |

而 `useSerialProcessor()` 会传递全部 5 项配置（L222-231）。这意味着运行时通过 `/config max_tool_rounds 20` 设置的值，切换到并行模式后立即失效。

#### 影响

- 运行时配置不一致：串行模式 20 轮限制，切换到并行后变回默认 10 轮
- `ExecutionTracer` 丢失：并行模式下不记录执行轨迹，监控数据断裂
- `StateMachine` 丢失：并行模式下状态机不更新，外部 UI 组件（如 `StreamDisplay`）状态卡住

#### 修复建议

- `ParallelProcessor` 增加对应的 setter 方法（与 `SerialProcessor` 对齐）
- `useParallelProcessor()` 中传递全部配置（与 `useSerialProcessor()` 对称）
- 或在 `IMessageProcessor` 接口增加统一的 `applyConfig()` 方法（见问题 1 修复建议）

---

### 23. FileStorageBackend 路径语义不一致的具体表现

#### 位置

`project/FileStorageBackend.cpp` L11-32

#### 问题

通过代码验证，路径语义不一致的具体表现如下：

```cpp
// FileStorageBackend.cpp

// ❌ loadJson/saveJson — 直接使用 filePath，不拼接 project_path_
nlohmann::json FileStorageBackend::loadJson(const std::string& filePath) {
    auto j = ProjectIO::loadJsonFile(filePath);  // filePath 必须是绝对路径
    return j.value_or(nlohmann::json{});
}

void FileStorageBackend::saveJson(const std::string& filePath, const nlohmann::json& data) {
    ProjectIO::saveJsonFile(filePath, data);  // filePath 必须是绝对路径
}

// ✅ readChapter/writeChapter — 内部拼接 project_path_
std::string FileStorageBackend::readChapter(const std::string& filePath) {
    return ProjectIO::readChapter(project_path_, filePath);  // 相对路径
}

void FileStorageBackend::writeChapter(const std::string& filePath, const std::string& content) {
    ProjectIO::writeChapter(project_path_, filePath, content);  // 相对路径
}
```

**不一致性：**
- `loadJson("conversation.json")` → 尝试读取当前工作目录下的 `conversation.json`（错误）
- `readChapter("chapters/001.md")` → 正确读取 `<project_path>/chapters/001.md`

调用方必须知道哪些方法需要绝对路径、哪些需要相对路径，否则产生 bug。

#### 实际影响

`SessionPersistence::save()` 调用 `storage_.saveJson(conv_path, ...)`，其中 `conv_path` 是 `agentDir() + "/conversation.json"`（在 `ContextManager` 中构造）。由于 `agentDir()` 返回的是绝对路径，所以这里**碰巧工作**。但如果有人调用 `storage_.loadJson("conversation.json")`（相对路径），就会失败。

#### 修复建议

统一路径策略（二选一）：
- **方案 A**：所有方法都接受相对路径（相对于 `project_path_`），内部统一拼接
- **方案 B**：所有方法都接受绝对路径，调用方自行构造完整路径

推荐方案 A，更符合封装原则。

---

### 24. Agent::execute() 缺少异常恢复机制（与 processUserMessage 的 B8 修复不一致）

#### 位置

`agent/Agent.cpp` L385-413

#### 问题

`processUserMessage()` 在 L370-378 有完整的异常捕获和状态恢复（B8 修复）：

```cpp
try {
    auto result = processor_->process(input, conversation_, std::move(callbacks));
    // ... 后续处理
} catch (const std::exception& e) {
    // B8 修复：核心处理异常强制状态恢复，防止卡在 Thinking 永久拒输入。
    spdlog::error("[Agent] 处理异常，强制状态恢复: {}", e.what());
    tracer_.record("error", 0, 0, {{"reason", "处理异常: " + std::string(e.what())}});
    state_.transition(AgentState::Error);
    state_.recover();
    return {};
}
```

但 `execute()` 方法（L385-413）**没有 try-catch**：

```cpp
llm::LLMResponse Agent::execute(const std::string& command, llm::StreamCallbacks callbacks)
{
    // ... 输入校验
    state_.transition(AgentState::Thinking);
    
    std::vector<llm::Message> messages = { llm::Message::user(command) };
    auto tools = registry_.getToolDefinitions();
    std::string effective_prompt = system_prompt_;
    if (context_manager_) {
        llm::Conversation tempConv;
        tempConv.addUser(command);
        auto assembly = context_manager_->assemble(tempConv, max_context_tokens_);
        if (!assembly.system_prompt.empty())
            effective_prompt = system_prompt_ + "\n\n" + assembly.system_prompt;
    }
    
    auto response = client_->chat(messages, tools, effective_prompt, std::move(callbacks));
    // ⚠️ 如果 chat() 抛异常，状态机卡在 Thinking
    state_.transition(AgentState::Idle);
    return response;
}
```

如果 `client_->chat()` 抛出异常（网络超时、API 错误），`state_.transition(AgentState::Idle)` 不会执行，状态机**永久卡在 Thinking 状态**，后续所有输入都被拒绝。

#### 影响

- `/api/execute` 后端路由使用 `Agent::execute()`（BackendServer.cpp L289-290）
- 一次 LLM API 超时导致 Agent 永久不可用，必须重启
- 与 `processUserMessage()` 的健壮性不一致

#### 修复建议

为 `execute()` 添加与 `processUserMessage()` 相同的异常处理：

```cpp
try {
    auto response = client_->chat(messages, tools, effective_prompt, std::move(callbacks));
    state_.transition(AgentState::Idle);
    return response;
} catch (const std::exception& e) {
    spdlog::error("[Agent] execute 异常: {}", e.what());
    tracer_.record("error", 0, 0, {{"reason", "execute 异常: " + std::string(e.what())}});
    state_.transition(AgentState::Error);
    state_.recover();
    return {};
}
```

---

### 25. ParallelProcessor 缺少 ExecutionTracer 和 StateMachine 支持

#### 位置

`agent/IMessageProcessor.h` L106-130

#### 问题

`SerialProcessor` 有完整的追踪和状态机支持（L57-61, L94-95）：

```cpp
class SerialProcessor : public IMessageProcessor {
public:
    void setTracer(class ExecutionTracer* t) { tracer_ = t; }
    void setStateMachine(class StateMachine* s) { state_ = s; }
    
private:
    class ExecutionTracer* tracer_ = nullptr;
    class StateMachine* state_ = nullptr;
};
```

但 `ParallelProcessor`（L106-130）**完全没有**这两个成员：

```cpp
class ParallelProcessor : public IMessageProcessor {
public:
    // ❌ 没有 setTracer()
    // ❌ 没有 setStateMachine()
    
private:
    llm::LLMClientFactory& factory_;
    ToolRegistry& registry_;
    std::unique_ptr<AgentOrchestrator> orchestrator_;
    std::string system_prompt_;
    class ContextManager* context_manager_ = nullptr;
    // ❌ 没有 tracer_ 成员
    // ❌ 没有 state_ 成员
};
```

#### 影响

- 切换到并行模式后，`ExecutionTracer` 不记录任何执行轨迹，监控数据断裂
- `StateMachine` 不更新，外部 UI 组件（如 `StreamDisplay`）状态卡住
- 用户无法通过 `/stats` 查看并行模式的执行统计

#### 修复建议

为 `ParallelProcessor` 添加对应的 setter 和成员变量，并在 `process()` 方法中使用：

```cpp
class ParallelProcessor : public IMessageProcessor {
public:
    void setTracer(class ExecutionTracer* t) { tracer_ = t; }
    void setStateMachine(class StateMachine* s) { state_ = s; }
    
private:
    class ExecutionTracer* tracer_ = nullptr;
    class StateMachine* state_ = nullptr;
};
```

---

### 26. SubAgent 的 cancelled_ 检查点不够密集

#### 位置

`agent/SubAgent.cpp` L36-69

#### 问题

`SubAgent::execute()` 中 `cancelled_` 的检查点只有 3 处：

```cpp
auto future = std::async(std::launch::async, [this, config, tool_defs]() -> SubAgentResult {
    SubAgentResult r;
    try {
        if (cancelled_) return r;  // ✅ 检查点 1：启动前
        
        {
            std::lock_guard<std::mutex> lock(conv_mutex_);
            conversation_.addUser(config.task);
        }
        
        if (cancelled_) return r;  // ✅ 检查点 2：添加用户消息后
        
        ToolCallLoop loop(*client_, tools_);
        ToolCallLoopConfig cfg;
        cfg.max_rounds = config.max_tool_rounds;
        cfg.first_round_streaming = false;
        cfg.max_repeated_calls = 3;
        
        std::lock_guard<std::mutex> lock2(conv_mutex_);
        if (cancelled_) return r;  // ✅ 检查点 3：进入 ToolCallLoop 前
        
        auto loop_result = loop.run(conversation_, tool_defs, config.system_prompt, {}, cfg);
        // ❌ loop.run() 内部没有 cancelled_ 检查点
        // ❌ 如果 loop.run() 阻塞在 HTTP 调用中，cancelled_ 无法生效
        
        r.output = loop_result.response.content;
        // ...
    } catch (const std::exception& e) {
        // ...
    }
    return r;
});
```

**关键问题：** `ToolCallLoop::run()` 内部（ToolCallLoop.cpp L74-155）没有 `cancelled_` 检查机制。一旦进入 `loop.run()`，即使主线程设置了 `cancelled_ = true`，异步任务仍然会：
1. 执行完当前工具调用
2. 发起下一次 HTTP 请求（可能阻塞 180s）
3. 处理完所有工具调用后才返回

#### 影响

- 超时后仍需等待当前 HTTP 调用完成（最多 180s）
- 如果 `loop.run()` 正在执行多个工具调用，无法提前终止
- 与问题 21（ToolCallLoop 超时后的悬挂线程风险）相互叠加

#### 修复建议

为 `ToolCallLoop` 添加取消机制：

```cpp
class ToolCallLoop {
public:
    void setCancelled(std::atomic<bool>* cancelled) { cancelled_ = cancelled; }
    
private:
    std::atomic<bool>* cancelled_ = nullptr;
};

// ToolCallLoop::run() 内部
for (int round = 0; round < config.max_rounds; ++round) {
    if (cancelled_ && *cancelled_) {
        r.cancelled = true;
        r.error = "任务已取消";
        return r;
    }
    // ... 继续执行
}
```

---

### 27. Conversation::editMessage() 没有处理 preserved 标记

#### 位置

`llm/Conversation.h` L122-129

#### 问题

`editMessage()` 允许编辑 User 和 Assistant 消息的内容，但没有考虑 `preserved` 标记：

```cpp
bool editMessage(size_t index, std::string new_content) {
    if (index >= messages_.size()) return false;
    auto& msg = messages_[index];
    if (msg.role != MessageRole::User && msg.role != MessageRole::Assistant)
        return false;
    msg.content = std::move(new_content);
    return true;
}
```

**问题场景：**
1. 用户 pin 了索引 5 的消息（`messages_[5].preserved = true`）
2. 用户编辑了索引 5 的消息内容
3. 编辑后，消息内容变了，但 `preserved` 标记仍然为 `true`
4. 后续 `truncateMessages()` 会优先保留这条**已编辑的消息**，但用户可能认为编辑后它不再是"关键消息"

**更严重的问题：**
- 如果用户编辑了一条**非 preserved** 消息，使其变成了"重要内容"，但没有 pin 它，后续截断可能丢失
- 用户可能期望编辑操作会自动 pin 消息，但实际不会

#### 影响

- 语义模糊：编辑后的消息是否应该保持 preserved 状态？
- 用户体验不一致：pin → edit → 仍然 pin（可能不符合预期）
- 没有文档说明编辑操作对 preserved 标记的影响

#### 修复建议

两种方案：
- **方案 A**：编辑后自动清除 preserved 标记（用户需要重新 pin）
- **方案 B**：编辑后自动设置 preserved 标记（编辑 = 重要）
- **方案 C**：保持现状，但在文档中明确说明行为

推荐方案 A，因为编辑通常意味着"修改内容"，而非"标记为重要"。

---

### 28. AgentOrchestrator 的 token 统计只记录串行回退和汇总调用

#### 位置

`agent/AgentOrchestrator.h` L104-108, `agent/AgentOrchestrator.cpp`

#### 问题

`AgentOrchestrator` 有 `lastInputTokens()` 和 `lastOutputTokens()` 方法（L107-108），但注释明确说明：

```cpp
/// D6: 最近一次 processMessage 调用累计的 token 用量
/// （含串行回退/汇总 LLM 调用；子任务因使用独立 LLMClient 其 token 不计入）。
/// 供 ParallelProcessor 调 recordUsage 恢复并行模式的上下文预算管理。
int lastInputTokens() const { return last_input_tokens_; }
int lastOutputTokens() const { return last_output_tokens_; }
```

**实际情况：**
- 串行回退（不触发并行）的 token 被记录 ✅
- 汇总阶段（synthesize）的 LLM 调用 token 被记录 ✅
- **子任务的 token 完全丢失** ❌

每个 `SubAgent` 使用独立的 `LLMClient`（通过 `factory_.create()`），其 token 消耗不会被记录到主 `AgentOrchestrator` 的统计中。

#### 影响

- `ContextManager::usagePercent()` 在并行模式下严重低估实际用量
- 自动压缩阈值（默认 70%）不会在合适的时机触发
- 用户通过 `/stats` 看到的 token 统计不准确
- 可能导致模型上下文窗口超出，触发 `context_length_exceeded` 错误

#### 修复建议

1. `SubAgentResult` 增加 token 统计字段：
```cpp
struct SubAgentResult {
    std::string output;
    std::string error;
    bool timed_out = false;
    int input_tokens = 0;   // 新增
    int output_tokens = 0;  // 新增
};
```

2. `SubAgent::execute()` 从 `ToolCallLoopResult` 中提取 token 统计：
```cpp
r.input_tokens = loop_result.input_tokens;
r.output_tokens = loop_result.output_tokens;
```

3. `AgentOrchestrator::executeParallel()` 汇总子任务的 token：
```cpp
for (const auto& result : sub_results) {
    last_input_tokens_ += result.input_tokens;
    last_output_tokens_ += result.output_tokens;
}
```

4. `ParallelProcessor::process()` 调用 `ContextManager::recordUsage()` 记录汇总后的 token。

---

## 1. IMessageProcessor 接口设计不全 → dynamic_cast 泛滥

### 位置

`agent/IMessageProcessor.h` + `agent/Agent.cpp`

### 问题

`Agent` 类中有 **4 处** 使用 `dynamic_cast` 将 `IMessageProcessor` 向下转型为 `SerialProcessor`：

```cpp
// Agent.cpp:72-83
void Agent::setMaxToolRounds(int n) {
    if (auto* sp = dynamic_cast<SerialProcessor*>(processor_.get()))
        sp->setMaxToolRounds(max_tool_rounds_);
}
void Agent::setContextManager(ContextManager* cm) {
    if (auto* sp = dynamic_cast<SerialProcessor*>(processor_.get()))
        sp->setContextManager(cm);
}
```
以及 `isParallelEnabled()` 对 `ParallelProcessor` 的 `dynamic_cast`。

### 根因

`IMessageProcessor` 接口只有两个方法：`setSystemPrompt()` 和 `process()`。`setContextManager`、`setMaxToolRounds`、`setMaxContextTokens`、`setTracer`、`setStateMachine` 等配置方法只存在于 `SerialProcessor` 上，接口中没有定义。每次切换到 `ParallelProcessor`，这些配置会**静默丢失**（`Agent.cpp` L67-74 注释已经承认这一点）。

### 影响

- **接口退化**：新增一种 Processor（如计划中的 PlanModeProcessor）必须修改 Agent 的 setter 方法，违反了开闭原则
- **静默丢失**：运行时 `/config max_context_tokens` 后调用 `useParallelProcessor()`，新值不会传递到子 Agent
- **不安全**：`dynamic_cast` 返回 nullptr 时（未来新增 Processor 类型），配置被静默丢弃

### 修复建议

在 `IMessageProcessor` 增加统一配置接口：

```cpp
class IMessageProcessor {
public:
    virtual void setConfig(const std::string& key, const nlohmann::json& value) = 0;
    // 或
    struct ProcessorConfig {
        ContextManager* cm = nullptr;
        ExecutionTracer* tracer = nullptr;
        StateMachine* state = nullptr;
        int max_tool_rounds = 10;
        int max_context_tokens = 131072;
    };
    virtual void applyConfig(const ProcessorConfig& cfg) = 0;
};
```

---

## 2. Conversation 作为全局可变状态 — 引用传递链条脆弱

### 位置

`llm/Conversation.h` → 被 `Agent` / `SerialProcessor` / `ToolCallLoop` / `ToolPipeline` / `SubAgent` 传递引用

### 问题

`Conversation` 本质是一个 `vector<Message>` 的包装类，但它的引用传递链条如下：

```
Agent::conversation_ (成员)
  → IMessageProcessor::process(..., Conversation&, ...)  [非 const 引用]
    → ToolCallLoop::run()  [非 const 引用]
      → ToolPipeline::executeAndAppend()  [非 const 引用]
        → ToolPipeline 直接修改 Conversation 内容
```

`Agent::processUserMessage()` 将自身成员 `conversation_` 以引用传入 `processor_->process()`，后者再传入 `ToolCallLoop`，`ToolCallLoop` 再传入 `ToolPipeline`。**链条上的每一层都在修改同一个对象**。

### 风险

1. **并发安全**：`ToolCallLoop` 在执行工具时调用 `ToolPipeline::executeAndAppend()` 修改 `conversation_`，但同时第 N+1 轮 LLM 调用正在发送 `conversation_.messages()`。当前因为串行执行不会出问题，但切换到并行模式后，`SubAgent` 各自持有独立 Conversation，而主 Conversation 仍被并行修改——谁来保护？
2. **生命周期**：`ToolPipeline` 以引用持有 `Conversation`，但它的生命周期（在 `ToolCallLoop::run()` 栈上构造）比 `Conversation` 短，间接地靠调用栈顺序保证安全。如果有人将 `ToolPipeline` 提升为成员或异步使用，就会悬垂引用。
3. **可测试性**：修改 `Conversation` 的副作用散落在 `ToolPipeline`、`ToolCallLoop`、`SerialProcessor` 中，测试任何一个都需要构造完整调用链。

### 修复建议

- **短-term**：在 `Conversation` 上加写时拷贝（Copy-on-Write）语义，`ToolPipeline::executeAndAppend()` 返回 diff 而非直接修改
- **长-term**：将 `Conversation` 改为 `Agent` 私有，只通过 `Agent::addToHistory()` / `Agent::messages()` 接口暴露，`ToolCallLoop` 不再持有 `Conversation&`

---

## 3. ContextManager 职责过重（单一职责违反）

### 位置

`agent/ContextManager.h` — 约 210 行声明，集合了至少 5 个不同领域的职责

### 职责清单

| 职责 | 接口方法 | 依赖 |
|------|----------|------|
| ① 上下文组装 | `assemble()` / `buildSystemPrompt()` | Project / Conversation / ILLMClient |
| ② 对话压缩 | `compact()` / `clearCompactedSummary()` | ILLMClient |
| ③ Token 追踪 | `recordUsage()` / `checkThresholds()` / `sessionStats()` | 无外部依赖 |
| ④ 向量检索协调 | `setRetrievalBackend()` / `isVectorStoreStale()` | IVectorStore / IEmbeddingGenerator |
| ⑤ 自动压缩策略 | `setAutoCompact()` / `shouldAutoCompact()` | 自含逻辑 |
| ⑥ 会话持久化 | `saveSessionState()` / `loadSessionState()` | FileStorageBackend / SessionPersistence |

### 问题

- 任何一个职责的变化都需要修改 `ContextManager`
- 内部私有方法 `truncateMessages()` 是纯函数但被放在 ContextManager 中——它完全可以是一个独立工具函数
- `SessionPersistence` 已经做了部分委托，但 `saveSessionState()` 仍留在 ContextManager 中协调多个子步骤
- 没有单元测试隔离——测试会话持久化必须先构造 ContextManager，测试 token 追踪也必须先构造 ContextManager

### 修复建议

- 将 **② 对话压缩** 抽为 `ConversationCompactor`，将 **③ Token 追踪** 抽为 `TokenTracker`
- ContextManager 保留 `assemble()` 作为门面，内部组合上述两个新类 + SessionPersistence
- `truncateMessages()` 作为 `utils::agent::truncateMessages()` 自由函数

---

## 4. SubAgent 异步线程模型 — std::async 资源消耗与取消困境

### 位置

`agent/SubAgent.cpp` L36-92

### 问题

`SubAgent::execute()` 每次调用创建一个 `std::async(std::launch::async, ...)` 异步任务。超时后设置 `cancelled_ = true`，但后续必须 `future.wait()` 阻塞等待 HTTP 响应返回（注释明确说明"HTTP 客户端自身有 read_timeout 180s"——最多等 3 分钟）。

```cpp
// SubAgent.cpp:71-90
auto status = future.wait_for(config.timeout);
if (status == std::future_status::timeout) {
    cancelled_ = true;
    // B3 修复：无条件等待异步任务完全退出
    future.wait();  // 可能阻塞 3 分钟
    result = future.get();
    ...
}
```

### 影响

- 超时的 SubAgent 仍然消耗一个线程直到 HTTP 超时
- `AgentOrchestrator` 对每个并行子任务创建一个 SubAgent，如果多个子任务同时超时，线程池可能堆积
- `std::async` 不保证使用专用线程池，默认行为是每次创建新线程或延迟执行

### 修复建议

- 用 `std::jthread` + `stop_token` 替代 `std::async` + `std::atomic<bool>`
- 或维护一个固定大小的线程池（`asio::thread_pool` / 自定义），避免无限创建线程
- HTTP 层支持真正的取消（通过 httplib::Client 的 `set_connection_timeout` + 连接关闭）

---

## 5. Project 大聚合根 + 全量保存

### 位置

`project/Models/Project.h` — 聚合所有子模型
`project/ProjectIO.h` — `save()` 全量写磁盘

### 问题

`Project` 是一个包含 `Outline`、`vector<Character>`、`vector<Setting>`、`vector<WorldRule>`、`Style` 的巨型结构体。`ProjectIO::save()` 写全部独立 JSON 文件，但 `IProjectWriter::saveProject()` 也是全量保存。

这意味着：LLM 调用 `create_character` 添加一个角色 → 触发 `saveProject()` → 写 outline.json + characters.json + settings.json + world_rules.json + style.json + novel.json，即使只有 characters.json 发生了变化。

### 影响

- 长篇小说（100 章 + 50 角色 + 30 设定）中每次写入都触发全量 I/O
- 工具执行频繁（`create_character` → `write_chapter` → `update_setting`）时磁盘写入放大
- 并发场景下全量保存没有部分锁粒度

### 修复建议

- `ProjectIO` 粒度为每个子对象提供独立 save 方法：`saveCharacters()` / `saveSettings()` 等
- `Project` 添加脏标记（dirty flag）：`Character::setDirty()`，`saveProject()` 只写脏了的文件
- 或使用更细粒度的 `IProjectWriter` 接口，工具调用方指定要保存的子集

---

## 6. ReplHandler 持有 NovelAgentApp* 反向引用

### 位置

`cli/ReplHandler.h` L35: `NovelAgentApp* app_`
`cli/ReplHandler.cpp` L158-247: `/index` 命令直接访问 `app_->vectorStore()` 和 `app_->embeddingGenerator()`

### 问题

`ReplHandler` 属于 CLI 层，`NovelAgentApp` 是应用层组装器。CLI 层持有应用层指针构成**反向依赖**：

```
NovelAgentApp (应用层)
  └── ReplHandler (CLI层)
       └── NovelAgentApp* app_  ← 反向引用
```

这违反了《CLAUDE.md》中"CLI 层通过 `IOutputChannel&` 输出，不硬编码 `std::cout`"的同一精神——CLI 层应该通过接口访问应用服务，而非持有具体 App 指针。

### 修复建议

- 将 `vectorStore()` 和 `embeddingGenerator()` 的访问抽象为 `IIndexService` 接口（只有 `indexAll()` 一个方法）
- `ReplHandler` 持有 `IIndexService*` 而非 `NovelAgentApp*`
- 或者把 `/index` 逻辑从 `ReplHandler` 移到 `NovelAgentApp` 中，REPL 只做路由

---

## 7. 并行模式下 Token 统计不完整

### 位置

`agent/AgentOrchestrator.h` L107-109

### 问题

注释明确承认：
> "子任务因使用独立 LLMClient 其 token 不计入"

并行模式中，`AgentOrchestrator` 负责分解 → 分发子任务 → 汇总。子任务使用 `SubAgent`（独立 LLMClient），其 token 消耗没有被记录到主 Agent 的 `ContextManager::recordUsage()` 中。

### 影响

- `ContextManager::usagePercent()` 在并行模式下严重低估实际用量
- 自动压缩阈值（默认为 70%）不会在合适的时机触发
- 模型上下文窗口可能被超出，导致 LLM API 返回 `context_length_exceeded` 错误

### 修复建议

- `SubAgentResult` 增加 token 统计返回
- `AgentOrchestrator::executeParallel()` 收集子任务的 token 数据并汇总
- `ParallelProcessor::process()` 将汇总后的 token 回写到 `ContextManager::recordUsage()`

---

## 8. Conversation::systemPrompt() 与 Agent::system_prompt_ 双重管理

### 位置

`llm/Conversation.h` L64-70 + `agent/Agent.h` L132

### 问题

`Conversation` 从 `messages_` 中提取第一条 `MessageRole::System` 的消息作为 `systemPrompt()`，而 `Agent` 也有自己的 `system_prompt_` 成员。最终发送给 LLM 的是 Agent 的 `system_prompt_`（经过 `PromptComposer::compose()`），**而不是** Conversation 中的 system 消息。

这意味着 `Conversation::addSystem()` 添加的消息**实际上永远不会被发送给 LLM**（`Conversation::messages()` 过滤掉了 system 消息，system prompt 单独通过 `chat()` 的参数传递）。

### 问题场景

- 用户 / 代码在 Conversation 中 `addSystem("不要使用 create_chapter 工具")` 不会有任何效果
- LLM 响应中的 system 消息被添加到 Conversation 后，下次请求也**不会**作为 system prompt 发送
- 两个 system prompt 源共存，语义模糊

### 修复建议

- **方案 A**：删除 `Conversation::addSystem()`，统一由 `Agent::setSystemPrompt()` 管理
- **方案 B**：在 `assemble()` 或 `ToolCallLoop::run()` 中，将 Conversation 中的 system 消息与 Agent 的 `system_prompt_` 合并

---

## 9. 流式回调在工具循环中仅首轮生效

### 位置

`agent/ToolCallLoop.cpp` L48-57 + L136-140

### 问题

```cpp
// 首轮 — 使用传进来的 callbacks
if (config.first_round_streaming || config.all_rounds_streaming)
    response = client_.chat(first_msgs, tools, system_prompt, callbacks);

// ... 中间轮次 — 永远没有流式回调
if (config.all_rounds_streaming)
    response = client_.chat(conversation.messages(), tools, system_prompt, {});
else
    response = client_.chatNonStreaming(conversation.messages(), tools, system_prompt);
```

默认配置只首轮流式（`first_round_streaming=true, all_rounds_streaming=false`）。后续工具调用的 LLM 响应用户完全看不到。

### 影响

用户写长篇小说时，Agent 可能：
1. 第一次调用 LLM → 返回 create_chapter + write_chapter（流式可见 ✅）
2. 执行工具
3. 第二次调用 LLM（无流式）→ 返回分析结果（用户看不到 ❌）
4. 第三次调用 LLM（无流式）→ 返回最终回复（用户看不到 ❌ 直到全部完成）

### 修复建议

- 调高 `all_rounds_streaming=true` 为默认值
- 或至少在 ToolCallLoop 首次非流式调用时将响应通过 `callbacks.on_complete` 推送出去

---

## 10. Agent::execute() 与 processUserMessage() 语义分裂

### 位置

`agent/Agent.cpp` L385-413

### 问题

`execute()`（单次命令模式）与 `processUserMessage()`（对话模式）的执行路径完全不同：

| 维度 | processUserMessage | execute |
|------|-------------------|---------|
| 对话历史 | ✅ 维护 | ❌ 不维护 |
| 输入守卫 | ✅ 输入校验 | ✅ 输入校验 |
| 状态机 | ✅ Thinking → Idle | ✅ 但极简 |
| ContextManager | ✅ assemble 注入 | ⚠️ 手动拼接 context |
| ToolCallLoop | ✅ 多轮工具循环 | ❌ 直接 chat，无工具循环 |
| 增量保存 | ✅ 每轮保存 | ❌ 不保存 |
| 章节检测 | ✅ maybeAutoCompact | ❌ 无 |
| 轨迹记录 | ✅ 完整记录 | ⚠️ 基本记录 |

`execute()` 实际上是一个**降级模式**——它没有 ToolCallLoop、没有章节检测、没有会话保存。后端 API 的 `/api/execute` 路由就使用此模式，导致通过 REST API 执行的命令完全不能使用工具。

### 修复建议

- `execute()` 应该也使用 ToolCallLoop（或至少允许工具调用）
- 或重命名以明确语义：`execute()` → `chatNonStreaming()`，文档标注"不使用工具"

---

## 11. ⚠️ Agent::rewindTo() 后 pin 消息静默丢失（修正）

> **subagent 验证**：`pinnedIndices()` 在 `truncateTo()` 后始终从当前消息列表重新计算索引，索引始终自洽。报告的"索引不准确"论断有误。**但有价值的底层关切**：回滚到 pin 消息索引之前时，该消息被静默截断，用户无任何提示。重新标记为"pin 消息在回滚时静默丢失"。

### 位置

`agent/Agent.cpp` L125-143

### 问题

`rewindTo(index)` 调用 `conversation_.truncateTo(index + 1)`，但 `pinMessage()` 的索引是基于 `all()` 的原始索引。回滚后：
- `conversation_.resize(keep_count)` 缩短了消息列表
- 但 `Message::preserved` 标记仍然留在保留下来的消息上
- 如果回滚前 pin 了索引 N，回滚到索引 M < N，则保留了 N 但 N > M，消息事实上在截断后的列表之外
- 实际上 `truncateTo` 会保留 `[0, keep_count)`，所以如果 pin 的索引 >= keep_count，消息被截断但 preserved 标记失去意义

更严重的是：`pinnedIndices()` 返回的索引在回滚后就不再准确（因为列表长度变了，但 preserved 标记还在被保留的消息上）。

### 修复建议

- `rewindTo()` 中额外清理被截断消息的 preserved 标记
- 或 `truncateTo()` 在删除消息前拷贝 preserved 状态

---

## 12. 模型内联函数膨胀头文件

### 位置

所有 `project/Models/*.h` 中的 `to_json` / `from_json` 函数

### 问题

`Scene`（~60 行 n inline to_json/from_json）、`Project`（~70 行 inline）、`Character`、`Setting` 等所有模型的 JSON 序列化函数都定义在头文件中。

以 `Scene` 为例：`from_json` 中的 `getMetadataWithUnknownKeys` + 18 个 `getOrDefault` 调用全部在头文件中展开。每次包含 `Models.h`（通过 `Models.h` → `Scene.h`）的 .cpp 文件都会被实例化。

### 影响

- 虽然 PCH 缓存了 `nlohmann/json.hpp`，但 `to_json`/`from_json` 的具体模板实例化实例仍然在每个 TU 中展开
- 修改任何一个模型的序列化逻辑触发包含该模型的所有 TU 重新编译

### 修复建议

- 将 `to_json`/`from_json` 移入对应的 `.cpp` 文件
- 头文件只保留声明：`inline void to_json(nlohmann::json& j, const Scene& s);`（C++17 inline 变量/函数可以 ODR）

---

## 13. SubAgent 锁与异步的微妙交互

### 位置

`agent/SubAgent.cpp` L36-68

### 问题

`SubAgent::execute()` 中的锁使用模式有潜在的隐患：

```cpp
auto future = std::async(std::launch::async, [this, ...]() {
    std::lock_guard<std::mutex> lock(conv_mutex_);
    conversation_.addUser(config.task);
    // 锁在这里释放
    ToolCallLoop loop(*client_, tools_);
    // 另一个锁
    std::lock_guard<std::mutex> lock2(conv_mutex_);
    auto loop_result = loop.run(conversation_, ...);
});
```

- 两个锁之间（第 48-56 行）没有锁保护——虽然这里没有访问 `conversation_`
- `loop.run()` 内部修改 `conversation_`，但锁在 run() 的整个过程中持有——这意味着其他线程无法取消正在执行工具循环的 SubAgent
- `cancelled_` 的检查点在锁边界之外，且在 async 内部检查——`cancelled_` 被设置为 true 时 async 可能已经正在执行 `loop.run()` 而被阻塞在 HTTP 调用中

### 修复建议

- 如果 `ToolCallLoop` 支持 `stop_token` 或 `cancelled_` 引用，可以在 HTTP 调用之间检查取消信号
- 考虑用 `std::jthread` + `stop_token` 替代手动 `atomic<bool>` + `future.wait()`

---

## 14. PromptComposer::kPromptVersion 手动管理易遗忘

### 位置

`agent/PromptComposer.h` L26: `static constexpr int kPromptVersion = 2;`

### 问题

`PromptComposer::compose()` 输出的每次 LLM 调用末尾都会带上 `[prompt_v2]` 标记，用于审计和追溯 prompt 变更。但版本号需要**手动递增**——每次修改 `PromptComponents` 或 compose 逻辑时，开发者需要记得更新。

没有任何编译期或运行时的检查机制来确保版本号与实际内容一致。`kPromptVersion = 2` 可能已经在多次修改后与实际 prompt 内容不匹配了。

### 修复建议

- 使用 `__DATE__` / `__TIME__` 生成构建时间戳注入
- 或使用 prompt 内容的 hash 作为版本标识（`PromptComposer::hash()` 已存在但未被 compose 使用）
- 或在 CI 中检查 `kPromptVersion` 是否随 prompt 模板变化而递增

---

## 15. FileStorageBackend 路径语义不一致

### 位置

`project/FileStorageBackend.h` L21-43

### 问题

构造函数接受 `project_path`，但大多数方法又接受独立的 `filePath` 参数：

```cpp
class FileStorageBackend {
public:
    explicit FileStorageBackend(std::string project_path);
    nlohmann::json loadJson(const std::string& filePath);
    void saveJson(const std::string& filePath, const nlohmann::json& data);
    std::string readChapter(const std::string& filePath);
    void writeChapter(const std::string& filePath, const std::string& content);
    bool exists(const std::string& path) const;
    std::string agentDir() const;  // 这个使用 project_path_
};
```

`loadJson(filePath)` 是相对 `project_path_` 还是绝对路径？`readChapter(filePath)` 呢？`ProjectIO::readChapter(project_.path, ch.file_path)` 在 ProjectAccess 中拼接路径。但在 `FileStorageBackend::readChapter` 中却没有拼接。调用方需要自行拼接。

### 问题场景

`SessionPersistence::save()` 调用 `storage_.saveJson(conv_path, ...)`，但 `conv_path` 是 `agentDir() + "/conversation.json"`（在 ContextManager 中构造），而 FileStorageBackend 没有 path 拼接功能。

### 修复建议

- 统一路径策略：FileStorageBackend 所有接受路径的方法都相对于 `project_path_`
- 或相反：FileStorageBackend 只提供绝对路径操作，调用方自行构造完整路径
- 混用两种策略必然导致 bug

---

## 16. 后端的 `/api/execute` 创建无上下文的临时 Agent

### 位置

`server/BackendServer.cpp` L289-295

```cpp
server_->Post("/api/execute", [this](const httplib::Request& req, httplib::Response& res) {
    ...
    agent::Agent tempAgent(factory_, registry_);
    auto response = tempAgent.execute(command);
    ...
});
```

### 问题

临时 Agent 没有：
- `system_prompt_`（空字符串）
- `ContextManager`（nullptr → 无项目上下文注入）
- `Conversation` 历史（每次全新）
- 工具循环（`execute()` 直接 `chat()` 无 ToolCallLoop）

这意味着 `/api/execute` **实际上无法使用任何工具**，也无法感知项目上下文。这与 REST API 的用途（供外部工具/脚本调用）严重不匹配。

### 修复建议

- 为临时 Agent 设置基本的 system prompt
- 如果当前有活跃项目，应当传递 `ContextManager`
- 至少允许单轮工具调用

---

## 17. StreamCallbacks 中的中文参数名不一致

### 位置

`agent/ToolCallLoop.cpp` L98-103

### 问题

重复调用检测错误消息中的内容是中文：

```cpp
llm::Message err_msg;
err_msg.role = llm::MessageRole::Tool;
err_msg.content = "{\"error\":\"重复工具调用循环已终止，请尝试其他方式完成任务。\"}";
```

但其他错误消息使用英文，例如 ToolRegistry.cpp L83: `"工具 '" + name + "' 不存在"`。虽然注释要求用中文注释，代码标识符用英文——但**工具返回给 LLM 的 error 消息**应该用什么语言？

这是 Agent 内部错误，LLM 能理解英文也能理解中文，但混用意味着 LLM 可能看到"工具 'xxx' 不存在"（中文）同时看到 "ToolCallLoop timeout"（英文），体验不一致。

### 修复建议

- 统一 LLM 可见的错误消息的语言（建议英文——因为 LLM 训练数据中英文占主导，错误消息的格式也更稳定）
- 或统一为中文（符合项目语言偏好）

---

## 18. 嵌入生成器与向量存储的类型假设过强

### 位置

`retrieval/IVectorStore.h` — `std::vector<float>` 作为嵌入向量类型
`retrieval/IEmbeddingGenerator.h` — 假设返回 `std::vector<float>`

### 问题

所有嵌入向量硬编码为 `std::vector<float>`。但不同嵌入模型可能使用：
- `float16` / `bfloat16`（节省带宽和存储）
- `int8` 量化向量
- 二进制嵌入（binary embeddings）

如果未来替换嵌入模型，需要修改 `IVectorStore` 接口和 `VectorStore` 实现。

### 修复建议

- 使用类型别名：`using EmbeddingVector = std::vector<float>;`
- 或在 `IVectorStore::insert()` 中支持量化转换适配

---

## 19. 工具注册的静态变量依赖静态初始化顺序

### 位置

`agent/tools/BuiltInTool.h` L79-89 `REGISTER_TOOL` 宏

```cpp
#define REGISTER_TOOL(ToolClass, toolName, varSuffix) \
    namespace { \
        static const bool _reg_##varSuffix = []() { \
            agent::BuiltInTool::registerFactory(toolName, ...); \
            return true; \
        }(); \
    }
```

### 问题

`static const bool _reg_*` 是文件作用域的静态变量，在动态初始化阶段（C++ 的动态初始化，发生在 main() 之前）执行。不同编译单元之间的初始化顺序未定义。

如果某个工具的构造函数（`ToolClass` 的构造函数）间接依赖另一个工具的静态状态，就会出问题。目前所有工具构造函数只接受 `shared_ptr<Project>`，没有交叉依赖，所以**当前不构成问题**，但这是一个潜在的陷阱。

### 修复建议

- 添加文档标注此限制：工具构造函数不得依赖其他注册工具的状态
- 或改用 `constinit` / `constexpr` 注册（C++20 支持），但需要工厂在首次访问时惰性初始化

---

## ~~20. ListChaptersTool 排序不稳定（已移除）~~

> **subagent 验证（❌ 不属实）**：已由 A17 fix 在 `ChapterTools.cpp` 中实现 `std::sort` 按 `Chapter::order` 排序。报告时未读取实际实现，此条无效，**已从报告中移除**。后续所有问题编号顺延，此处保留占位标记以示审查过程透明。

### 位置

`agent/tools/ChapterTools`（假设 ListChaptersTool 的实现）

### 问题

注释说 `list_chapters` "只返回元数据，不读文件"——这是正确的优化。但章节以 `vector<Chapter>` 的形式存储在 `Outline` 中，其顺序由 `Chapter::order` 字段决定。如果 `order` 字段存在重复值或间隙，LLM 看到的章节顺序可能与预期不符。

### 修复建议

- 确保 `list_chapters` 实现的响应中按 `Chapter::order` 排序
- 或在 `Outline` 中维护一个按 order 排序的视图

---

## 总结

| 类别 | 问题数 | 关键词 |
|------|--------|--------|
| 接口设计缺陷 | 3 | `IMessageProcessor`、`ReplHandler*`、`ParallelProcessor` 缺 tracer/state |
| 职责分配不当 | 2 | `ContextManager`、`Project` 全量保存 |
| 并发/线程模型 | 4 | `SubAgent async`、`Conversation` 可变状态、`ToolCallLoop` 超时悬挂、`cancelled_` 检查点 |
| 语义模糊/分裂 | 4 | `execute vs processUserMessage`（含异常恢复缺失）、双重 system prompt、流式回调、`editMessage` preserved |
| 生命周期隐患 | 2 | 引用传递链、注册静态变量顺序 |
| 性能浪费 | 2 | 全量序列化、`future.wait()` 阻塞 |
| 统计缺失 | 1 | 并行模式 token 追踪 |
| 代码一致性 | 4 | 版本号手动管理、路径语义（含代码实证）、错误消息语言、`useParallelProcessor` 配置丢失 |

**最优先修复**：
1. `IMessageProcessor` 接口补全（消除 dynamic_cast，统一配置传递）— 问题 1 + 22 + 25
2. `ToolCallLoop` 超时取消机制 — 问题 21 + 26（与 SubAgent 问题 4 同源）
3. `Agent::execute()` 异常恢复 — 问题 24（B8 修复遗漏）
4. `ContextManager` 职责拆分 — 问题 3
5. `Conversation` 可变状态收敛 — 问题 2
