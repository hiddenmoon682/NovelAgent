# 多会话并行架构评审：每会话一个 Agent 运行时

> 日期：2026-08-03
> 审核范围：Agent 对象模型、会话管理、并发安全、GUI 层
> 设计目标：支持「后台会话继续运行」，即多个会话可并发执行各自的 LLM 对话循环

---

## 一、背景与目标

### 1.1 现状

当前 `C++NovelAgent` 采用**单实例 Agent** 模型：整个应用只有一个 `Agent` 对象，内部持有
**唯一一份** `IMemory`（对话上下文）、`StateMachine`（状态机）、`ProgressiveToolProvider`
（渐进工具）、`ToolPipeline`（工具管道）、`ExecutionTracer`（执行轨迹）。

切换会话时，`SessionManager::switchSession` 对这份唯一的内存做「清空 → 重载」：

```cpp
void SessionManager::reloadActiveSession() {
    resetRuntimeState();   // 清空 memory_，重建 prompt
    loadSessionState();    // 从新会话重载进去
}
```

因此当前「多会话」是**时间上的串行复用**（同一份内存切来切去），不是**空间上的并行**。

### 1.2 目标

用户明确需求：**后台会话可继续跑**。即：
- 会话 A 正在生成（LLM 对话循环进行中）时，用户切换到会话 B 发起新对话；
- 会话 A 的生成在后台**继续**，完成后可回看；
- 多个会话可同时处于「运行中」状态。

### 1.3 约束

- 不破坏现有项目数据（人物/设定/大纲/章节）的多会话共享语义。
- 不引入新的外部二进制依赖（项目无外部二进制依赖）。
- 保持现有职责边界（SessionManager 管生命周期、Agent 管 LLM 编排）。

---

## 二、现状深入剖析

### 2.1 对象所有权全景

```
NovelAgentApp（全局一份，职责：装配）
  ├── client_           LLMClientFactory（LLM 客户端工厂，构造后不可变）
  ├── registry_         ToolRegistry（工具注册表）
  ├── memory_           Memory（唯一对话记忆）      ← 矛盾核心
  ├── agent_            Agent（唯一对话代理）        ← 矛盾核心
  ├── project_          shared_ptr<Project>（项目数据，共享可变）
  ├── vector_store_     VectorStore（向量库）
  ├── embedding_gen_    EmbeddingGenerator（嵌入生成器）
  ├── ltm_store_        LongTermMemoryStore（长期记忆）
  ├── skill_registry_   SkillRegistry（技能注册表）
  ├── persistence_      SessionPersistence（会话持久化，磁盘层）
  └── index_service_    ProjectIndexService（索引服务）

Agent（单一实例，职责：LLM 编排门面）
  ├── factory_          LLMClientFactory&（引用）
  ├── client_           unique_ptr<ILLMClient>（唯一 LLM 客户端）
  ├── registry_         ToolRegistry&（引用）
  ├── memory_           IMemory&（引用，与 NovelAgentApp 共享唯一实例）
  ├── progressive_tools_  ProgressiveToolProvider（每 Agent 一份）
  ├── pipeline_         ToolPipeline（每 Agent 一份）
  ├── budget_evaluator_ / compactor_ / budget_ / calibrator_（上下文管理）
  ├── tracer_           ExecutionTracer（执行轨迹）
  ├── state_            StateMachine（状态机）
  └── session_manager_  SessionManager（会话管理，共享同一 memory_）
```

### 2.2 关键结论

**矛盾核心**：`memory_`、`state_`、`progressive_tools_`、`pipeline_`、`tracer_` 都是
**Agent 单例的成员**。一个 `process` 循环正占用它们时，另一个循环再来会**互相覆盖**。

因此要让「一个 Agent 并发跑多个对话循环」，**单个 Agent 实例在架构上做不到**——
必须把「会话运行时状态」从 Agent 单例中拆出去，改成**每会话一份**。

### 2.3 线程安全现状（关键约束）

| 组件 | 线程安全 | 证据 |
|------|---------|------|
| `LLMClient` | **单实例不安全** | `LLMClient.h:24` 明确「httplib 内部状态不可共享」 |
| `HttpClient` | **单实例不安全** | `HttpClient.h:8` 明确「httplib::Client 内部状态不可并发」 |
| `LLMClientFactory` | ✅ 线程安全 | `LLMClientFactory.h:6`「构造后不可变，可在多线程间共享」 |
| `ProgressiveToolProvider` | ✅ 内部 `shared_mutex` 保护 | `ProgressiveToolProvider.h:25` |
| `ToolRegistry` | ⚠️ 读安全，写需锁 | 工具注册表，运行时只读 |
| `Project` | ⚠️ **共享可变**，无锁 | 工具读写项目数据，多会话并发写会冲突 |
| `Memory` | ⚠️ 无锁 | 每会话独立后无冲突 |
| `TokenCounter`（校准器） | ⚠️ **有状态且非线程安全** | `TokenCounter.h:152` 的 `models_` 是**无锁 unordered_map**；`calibrate()`（写）与 `apply()`/`getCorrection()`（读）并发访问同一 map，**数据竞争** |
| `ToolPipeline` | ⚠️ **内部自带 ThreadPool** | `ToolPipeline.h:30` 默认 `pool_(num_threads > 0 ? ... : nullptr)`，默认 **4 线程**；每会话一个 pipeline 则每会话自带 4 线程池 |
| `VectorStore` | ✅ **已内置锁** | `VectorStore.h:124` 有 `mutable std::shared_mutex mutex_`；读共享锁、写独占锁 |
| `LongTermMemoryStore` | ✅ **已内置锁** | `LongTermMemoryStore.h` 所有公开方法内部 `mutable std::mutex` 保护 |

**最关键的发现**：`LLMClient` 和 `HttpClient` 都**单实例不安全**，且代码注释**明确建议**
「多线程场景请使用 `LLMClientFactory` 为每个执行上下文创建独立实例」（`LLMClient.h:25`）。

这意味着**多会话并行时，每个会话需要一个独立的 `LLMClient` 实例**（通过 `factory.create()` 创建），
而不是共享同一个 `client_`。这恰好与「每会话一个运行时」的方向一致——每会话运行时自带一个 client。

**次关键发现（本深评审补充）**：
1. **校准器 `calibrator_`（`TokenCounter`）有状态且非线程安全**——文档此前误标「共享无状态」。`models_` 是无锁 unordered_map，多会话并发调用 `calibrate()`/`apply()` 会数据竞争，**必须加内部 mutex**（见决策 8）。
2. **`ToolPipeline` 内部自带线程池**——默认 4 线程。若每会话独立 pipeline，则 4 会话 × 4 = 16 工具线程 + 4 调度线程 = 20 线程，资源偏高（见决策 9）。
3. **好消息**：`VectorStore` 与 `LongTermMemoryStore` 已内置锁，**并发安全已解决**，无需在 4.2 矩阵中再标「需锁」（见 4.2 更新）。

---

## 三、目标架构设计

### 3.1 核心拆分：共享外壳 + 每会话运行时

**原则**：把「会话状态」与「全局资源」分离。会话状态每会话独有，全局资源共享。

```
NovelAgentApp（共享外壳，全局一份）
  ├── client_factory_   LLMClientFactory（工厂本身线程安全，可共享）
  ├── registry_         ToolRegistry（工具注册表，运行时只读）
  ├── project_          shared_ptr<Project>（共享可变，需加锁）
  ├── vector_store_ / embedding_gen_ / ltm_store_ / skill_registry_（共享）
  ├── persistence_      SessionPersistence（磁盘层，共享）
  └── agent_            AgentFacade（门面，持有会话池）

AgentFacade（门面，全局一个）
  ├── 持有 SessionPool（容器，map<session_id, SessionRuntime>）
  ├── 负责调度：创建/销毁/切换会话运行时
  └── 对外暴露与现在一致的接口（process/switchSession/newSession...）

SessionRuntime（每会话一个，独立运行时）
  ├── memory_           IMemory（每会话一份对话上下文）
  ├── client_           unique_ptr<ILLMClient>（每会话独立 LLM 客户端，经 factory.create()）
  ├── state_            StateMachine（每会话独立状态机）
  ├── progressive_tools_  ProgressiveToolProvider（每会话独立工具加载状态）
  ├── pipeline_         ToolPipeline（每会话独立工具管道）
  ├── tracer_           ExecutionTracer（每会话独立执行轨迹）
  ├── usage_            ContextUsage（每会话独立用量快照）
  └── cancel_requested_   std::atomic<bool>（每会话独立取消标志）
```

### 3.2 每会话独有 vs 全局共享 划分

| 组件 | 归属 | 理由 |
|------|------|------|
| `memory_`（对话上下文） | 每会话 | 会话状态，天然隔离 |
| `client_`（LLM 客户端） | **每会话** | **LLMClient 单实例不安全**，须独立实例 |
| `state_`（状态机） | 每会话 | 会话状态 |
| `progressive_tools_` | 每会话 | 会话级工具加载状态（`reset()` 在会话边界调用） |
| `pipeline_` | 每会话 | 内含 ToolPipeline，会话级 |
| `tracer_` / `usage_` | 每会话 | 会话级观测 |
| `cancel_requested_` | 每会话 | 会话级取消 |
| `factory_`（工厂） | 共享 | 线程安全，构造后不可变 |
| `registry_`（工具表） | 共享 | 运行时只读 |
| `project_`（项目数据） | 共享（加锁） | 多会话共享语义，需并发保护 |
| `persistence_`（持久化） | 共享 | 磁盘层，天然串行化 |
| `vector_store_` / `ltm_store_` / `skill_registry_` | 共享 | 全局资源 |
| `budget_evaluator_` / `compactor_` / `budget_` | 共享（无状态） | 上下文管理组件，无会话状态 |
| `calibrator_`（TokenCounter） | **共享（内部加锁）** | 有状态（`models_` 无序锁 map），多会话共享须加内部 mutex |

### 3.3 关键设计决策

**决策 1：LLM 客户端必须每会话独立实例**
- 原因：`LLMClient` / `HttpClient` 单实例不安全（httplib 内部状态不可共享）。
- 方案：`SessionRuntime` 构造函数内调用 `factory.create()` 创建独立 client。
- 影响：每个会话的 LLM 连接独立，互不干扰，天然支持并发。代价是每个会话多一个 HTTP 连接（合理）。

**决策 2：Agent 由「单一执行体」改为「门面 + 会话池」**
- Agent 保留 `switchSession`/`newSession`/`deleteSession` 等对外接口（GUI 无需改动接口签名）。
- ✅ 已确认：`process` 增加 `session_id` 参数，改为 `process(session_id, input, callbacks)`——显式指定目标会话的 SessionRuntime，而非依赖「当前 active 会话」。
- 内部 `process(session_id, ...)` 改为「定位 session_id 对应的 SessionRuntime → 在其上执行」。

**决策 3：共享线程池调度（已确认）**
- 复用现有 `agent::ThreadPool`（线程数设为 4），会话的 `process` 作为任务提交到池中执行。
- 并发上限 = 线程池线程数 = 4；线程数即并发会话上限，天然防止资源无限增长。
- 会话自身状态（memory/state/tools）只被执行该会话任务的工作线程访问，无需锁。
- 取消/停止：`SessionRuntime` 持有独立 `cancel_requested_` 标志，任务可随时响应取消。

**决策 4：共享 `Project` 的工具执行层统一加锁（已确认）**
- 多会话工具并发读写 project 数据（人物/剧本/大纲）会冲突。
- ✅ 已确认方案 B：在 `ToolPipeline::execute()` 统一入口加锁——本次调用含写工具则加独占锁，否则加共享锁。
- 复用 `ToolPipeline` 已有的 `isReadOnly()` 区分，只改入口一处，工具代码零改动。
- 补充：GUI 直接写 project（导入/手动编辑）不经过 pipeline，需另行约定（GUI 仅在无会话运行时改 project）。

**决策 5：GUI 层 `busy_` 从「全局单锁」改为「按会话 busy」**
- 当前 `busy_` 是全局原子锁，运行时拒绝所有会话操作。
- 改造后：每个会话独立 busy 状态，切换会话不再被全局锁阻塞。
- ✅ 已确认（输入框禁用规则）：输入框按「当前查看会话」的 busy 状态禁用——后台会话运行不影响查看/操作其他会话。`sendMessage` 仍校验目标会话自身的 busy，避免同一会话重复提交。

**决策 6：流信号改造成会话多通道（已确认，方案 A：后台实时输出）**
- 当前 `tokenReceived`/`reasoningReceived`/`toolCallStarted`/`toolCallFinished` 是全局单通道，无会话 id 关联，后台会话 token 无处归属。
- ✅ 已确认方案 A：所有流信号增加 `sessionId` 参数，QML 端按「当前查看会话」过滤接收，切换会话时无缝衔接后台已输出的内容。
- 改造点：`QmlBridge.h` 信号签名加 sessionId；`QmlBridge.cpp` 回调从 `SessionRuntime` 取会话 id 后 emit；`AgentPanel.qml` 的 Connections 处理器按当前会话 id 决定是否渲染。
- 新增 `Q_PROPERTY(QString currentSessionId)` 供 QML 过滤「当前查看会话」；`switchSession` 时同步更新该属性。
- 代价：UI 改动量较大（信号签名 + 多通道过滤），换来后台会话实时可见的体验。

**决策 7：持久化层 save 改为显式传会话 id（已确认）**
- 当前 `SessionPersistence` 的 `index.json` 用**单一 active 字段**标识当前会话，`save(memory)`/`load()` 都读写该 active 会话——多会话并发时 save 会互相覆盖。
- ✅ 已确认：`save` 改为 `save(session_id, memory)` 显式传 id，弃用单一 active 字段；每个会话独立落盘，按 id 隔离。
- 索引更新（`index.json`）在多会话下仍需加锁串行化（磁盘 IO 天然串行）。

**决策 8：校准器归属 = 共享 + 内部加锁（已确认）**
- 问题：`TokenCounter` 的 `models_` 是无锁 unordered_map，多会话并发 `calibrate()`（写）/ `apply()`（读）会数据竞争。
- ✅ 已确认：**共享一个校准器实例，但给 `TokenCounter` 加内部 `std::mutex`**（锁住 `calibrate`/`apply`/`getCorrection`/`reset`/`stats` 等读写 `models_` 的方法）。
- 实现：`TokenCounter.h` 增加 `mutable std::mutex mutex_;` 成员，每个公开实例方法加 `std::lock_guard`。静态方法（`countTokens` 等）无状态，无需锁。
- 优点：多会话共享同一套校准数据（EMA 修正因子跨会话累积，更准确）；加锁成本极低（校准只在每轮 LLM 请求后调用一次）。

**决策 9：工具线程池 = 每会话 pipeline + 线程数调小（已确认）**
- 问题：`ToolPipeline` 内部自带 4 线程池，若每会话独立 pipeline，则 4 会话 × 4 = 16 工具线程 + 4 调度线程 = 20 线程，资源偏高。
- ✅ 已确认：**保持每会话独立 pipeline，但把工具线程数调整为 1-2**（`ToolPipeline` 构造函数支持 `num_threads` 参数）。
- 实现：`SessionRuntime` 创建 pipeline 时传 `num_threads = 2`（或 1）。4 会话 × 2 = 8 工具线程 + 4 调度线程 = 12 线程，可控。
- 权衡：工具线程数调小会影响单会话内「多个只读工具并发执行」的吞吐，但本项目工具调用量不大，1-2 线程足够。

**决策 10：取消语义 = 保留全局 + 每会话取消（双通道）**
- 问题：`rebuildApp` 用全局 `bootstrap::g_cancel_flag` 取消所有会话；单会话取消需独立标志。
- ✅ 已确认：**双通道取消**——保留全局 `g_cancel_flag`（用于应用重建/退出时取消所有),同时 `SessionRuntime` 持有独立 `cancel_requested_`（`std::atomic<bool>`）用于单会话取消。
- 实现：`SessionRuntime::cancel()` 只置本会话 `cancel_requested_`；`AgentFacade::cancelAll()` 置全局标志并遍历所有会话 `cancel()`。`CoreLoop::setCancelled` 接受每会话标志指针。

**决策 11：`Agent::execute` 归属 = 已删除（已确认）**
- 调研结论：`Agent::execute`（原 `Agent.h:97`，单命令模式，不携带历史、不修改对话历史）**仅被 `tests/test_agent.cpp` 的 `test_execute_mode()` 一个测试用例使用**；CLI 目录 0 处、GUI（`QmlBridge`）0 处调用。
- 所有其他 `execute` 均为工具类 / `IToolProvider` / `ToolPipeline` 的接口（不同实体，与 `Agent::execute` 无关）。
- ✅ 已确认：**已删除 `Agent::execute` 声明、定义及 `test_execute_mode()` 测试**——它未接入任何实际 UI，且在多会话架构下需为每个会话维护一套独立路径，纯属负担。
- ⚠️ 待用户最终确认后再删除。

**决策 12：装配广播 = 共享源 + 会话创建时注入（已确认）**
- 问题：装配期方法（`setSystemPrompt`/`setModelLimit`/`setCalibrator`/`setPersistence`/`setSummarySink`）操作 `memory_`/`budget_`/`calibrator_`，会话化后这些成员迁移到 SessionRuntime。
- ✅ 已确认：**共享源 + 会话创建时注入**——保留全局装配配置（system prompt、model limit、calibrator 指针等）作为「共享源」，在 `SessionRuntime` 创建时把共享源注入到每个新会话。
- 实现：`AgentFacade` 维护一份「装配配置」结构体；`newSession` 创建 SessionRuntime 时从配置拷贝注入；`setSystemPrompt` 等先更新共享配置，再广播到所有已存在会话（或仅在会话创建时生效）。

---

## 四、并发安全设计

### 4.1 线程模型

```
主线程（GUI）：
  - 调用 agent.process(session_id, input) → 提交该会话的 process 任务到共享线程池
  - 调用 agent.switchSession(id) → 仅切换「当前查看焦点」，不阻塞任何运行中的会话

共享线程池（agent::ThreadPool，4 线程）：
  - 每个工作线程按需执行一个会话的 process 循环（LLM ↔ 工具）
  - 会话只被一个工作线程访问自己的 memory_ / state_ / tools_ / client_
  - 访问共享 project_ 时通过锁保护
  - 线程数即并发上限（4 个并发会话），会话数超出时排队执行
```

### 4.2 并发访问矩阵

| 资源 | 读（多会话并发） | 写（多会话并发） | 保护 |
|------|-----------------|-----------------|------|
| 会话自身 memory_ | 仅本会话线程 | 仅本会话线程 | 无需锁（线程独占） |
| 会话自身 client_ | 仅本会话线程 | 仅本会话线程 | 无需锁（线程独占） |
| 共享 project_ | 多会话并发读 | 多会话并发写 | ToolPipeline 入口 shared_mutex（读共享/写独占） |
| 共享 registry_ | 多会话并发读 | 仅装配时写 | 初始化后只读 |
| 共享 `persistence_` | 多会话并发读 | 多会话并发写 | 需串行化（磁盘 IO）；会话文件按 id 隔离，索引更新加锁 |
| 共享 `vector_store_` / `ltm_store_` | 多会话并发读 | 多会话并发写 | ✅ **已内置锁**（`VectorStore` 用 `shared_mutex`，`LongTermMemoryStore` 用 `mutex`），无需额外改造 |
| 共享 `calibrator_`（TokenCounter） | 多会话并发读 | 多会话并发写 | 需加内部 `std::mutex`（见决策 8） |

### 4.3 潜在风险与对策

| 风险 | 说明 | 对策 |
|------|------|------|
| project 并发写冲突 | 会话 A、B 同时写章节 | ToolPipeline 入口按读写工具加 shared_mutex |
| 持久化并发写 | 多会话同时 save 会话文件 | 会话文件按 id 隔离；索引更新需锁 |
| 工具副作用 | 工具读 project 时被其他会话修改 | 工具执行时持有 project 读/写锁 |
| 内存增长 | 每个会话一份 memory+client | 会话生命周期管理（关闭/归档时释放） |
| 取消语义 | 单个 `cancel_requested_` 不够 | 每会话独立取消标志 + 保留全局取消（双通道，见决策 10） |
| 校准器数据竞争 | `calibrate()`/`apply()` 并发访问 `models_` | TokenCounter 加内部 mutex（见决策 8） |
| 工具线程资源膨胀 | 每会话 pipeline 自带 4 线程池，4 会话 = 20 线程 | pipeline 线程数调小到 1-2（见决策 9） |
| `Agent::execute` 幽灵代码 | 仅测试覆盖，GUI/CLI 未接入 | 建议删除（见决策 11） |

---

## 五、深度评审补充（组件交互 / 并发方案 / 资源评估）

> 本章为深评审新增，用于把「决策点」落到「组件间怎么协作、数据怎么流、资源怎么用」的层面。

### 5.1 组件交互关系与数据流向

**主流程（发送消息 → 后台生成）**：
```
GUI AgentPanel.qml
  └─ sendMessage(sessionId, input)
      └─ QmlBridge::sendMessage
          └─ AgentFacade::process(sessionId, input, callbacks)
              └─ 提交到共享线程池 (agent::ThreadPool, 4 线程)
                  └─ 工作线程执行 SessionRuntime::process
                      ├─ LLMClient（本会话独立实例）→ 流式响应
                      ├─ CoreLoop（本会话）→ 工具调用
                      │    └─ ToolPipeline::execute（入口加 shared_mutex 保护 project）
                      │         └─ 只读工具（共享锁）/ 写工具（独占锁）
                      └─ SessionPersistence::save(session_id, memory)（按 id 落盘）
                            └─ ProjectIndexService::runIndexUpdate（全局串行化）
```

**流信号反向（SessionRuntime → QML）**：
```
SessionRuntime::process 回调
  └─ QmlBridge 的 StreamCallbacks（经 QMetaObject::invokeMethod 投递到主线程）
      └─ emit tokenReceived(sessionId, token) / reasoningReceived(sessionId, ...)
           / toolCallStarted(sessionId, ...) / toolCallFinished(sessionId, ...)
           / responseComplete(sessionId, msg)
      └─ AgentPanel.qml 按 currentSessionId 过滤，仅渲染当前查看会话的信号
```

**关键点**：
1. `process` 的调度在**共享线程池**，但每个会话的 `SessionRuntime` 状态只被**执行该会话的任务**访问，天然线程独占。
2. 跨会话共享的只有：`factory_`（共享）、`registry_`（只读）、`project_`（锁保护）、`calibrator_`（内部锁）、`persistence_`（锁/串行）、`vector_store_`/`ltm_store_`（内置锁）、`skill_registry_`（共享）。
3. 流信号必须携带 `sessionId`，否则后台会话的 token 无处归属（决策 5）。

### 5.2 并发安全具体实现方案（落地清单）

| 竞争点 | 方案 | 落点 |
|--------|------|------|
| 会话状态（memory/state/tools/client） | 每会话独立 + 单线程独占 | SessionRuntime 成员，无需锁 |
| 共享 `project_` | ToolPipeline 入口 shared_mutex（按 isReadOnly 区分） | `ToolPipeline::execute()` |
| 共享 `calibrator_` | 内部 std::mutex | `TokenCounter` 各实例方法 |
| 共享 `persistence_` | 会话文件按 id 隔离 + 索引更新加锁 | `SessionPersistence::save(session_id, ...)` |
| `vector_store_`/`ltm_store_` | 已内置锁，无需改 | 无需改 |
| 全局取消 | 全局 `g_cancel_flag` + 每会话 `cancel_requested_` | `SessionRuntime::cancel()` + `AgentFacade::cancelAll()` |
| 流信号跨线程 | `QMetaObject::invokeMethod(Qt::QueuedConnection)` 投递到主线程 | `QmlBridge` 回调 |

**加锁顺序约定**（防死锁）：所有会话统一按 `project_ → calibrator_ → persistence_` 顺序加锁；禁止逆序嵌套。

### 5.3 性能影响与资源消耗评估

| 资源 | 单会话 | 4 会话上限 | 说明 |
|------|--------|-----------|------|
| 调度线程 | 0（共享池） | 4 | 共享线程池，不随会话增长 |
| 工具线程 | 1-2（pipeline 内） | 8-32 | 每会话 pipeline 自带，调小到 1-2（决策 9） |
| LLM HTTP 连接 | 1 | 4 | 每会话独立 client，互不干扰 |
| 对话 memory | 1 份 | 4 份 | 每会话独立上下文 |
| 校准器 | 1 实例（共享） | 1 实例 | 加锁共享，EMA 跨会话累积 |

**总线程数估算**：4 会话 × 2 工具线程 + 4 调度线程 = **12 线程**（上限），远低于原来的 20 线程，可控。

**内存评估**：每个会话一份对话上下文（几百 KB ~ 数 MB，取决于 token 用量）+ 独立 client（无大内存）。4 会话内存占用可接受。

**性能瓶颈**：LLM 网络 IO 是主要瓶颈（每会话独立连接，天然并发隔离）；工具线程调到 1-2 不影响关键路径（工具调用量小）。

---

## 六、分步实施计划

> 采用渐进式改造，每步可独立验证、可回滚。

### 阶段 0：准备（不改行为）
- [ ] 抽取 `SessionRuntime` 结构定义（纯数据声明，不接入）。
      - 关键修改点：新建 `src/agent/core/SessionRuntime.h`，声明成员：`memory_`（IMemory）、`client_`（unique_ptr<ILLMClient>）、`state_`（StateMachine）、`progressive_tools_`（ProgressiveToolProvider）、`pipeline_`（ToolPipeline）、`tracer_`（ExecutionTracer）、`usage_`（ContextUsage）、`cancel_requested_`（std::atomic<bool>）。
- [ ] 确认 `LLMClientFactory` 在多线程下 create() 的并发安全性（`create()` 为 const，已验证）。

### 阶段 1：会话运行时容器化（核心改造）
- [ ] 新建 `SessionRuntime` 类：持有 memory / client / state / progressive_tools / pipeline / tracer / usage / cancel。
      - 关键修改点：实现 `SessionRuntime::SessionRuntime(LLMClientFactory&, ...)`，构造函数内 `factory.create()` 创建独立 client；`ToolPipeline` 构造传入 `num_threads = 2`（决策 9）。
- [ ] 将 `Agent` 的 `memory_`、`state_`、`progressive_tools_`、`pipeline_`、`tracer_`、`usage_`、`client_` 迁移到 `SessionRuntime`。
      - 关键修改点：`Agent.h` 删除这些成员，改为持有 `SessionPool`；`Agent::process` / `processSerial` 内部改从 `SessionRuntime` 取成员。
- [ ] `Agent` 改为持有 `SessionPool`（`map<session_id, unique_ptr<SessionRuntime>>`）。
      - 关键修改点：`Agent.h` 新增 `SessionPool pool_;`；`SessionManager` 的 `newSession`/`switchSession`/`deleteSession` 改为操作 pool。
- [ ] `process` 增加 `session_id` 参数，定位 session_id 对应的 SessionRuntime 并执行。
      - 关键修改点：`process(session_id, input, callbacks)` 内 `auto& rt = pool_.at(session_id);` 后执行 `rt.process(...)`。
- [ ] 删除 `Agent::execute` 及其测试（决策 11，已确认）。
      - 关键修改点：已删除 `Agent.h` 声明、`Agent.cpp` 定义、`tests/test_agent.cpp` 的 `test_execute_mode()` 及 main 调用。
- [ ] 验证：单会话行为与改造前完全一致（回归测试）。
      - 验收：`tests/test_agent.cpp` 其余用例全部通过；GUI 单会话手工操作正常。

### 阶段 2：多会话并发执行
- [ ] 引入共享 `agent::ThreadPool`（线程数设为 4），会话的 `process` 提交为池任务。
      - 关键修改点：`AgentFacade::process` 改为 `pool_.submit([rt, input, callbacks]{ rt->process(input, callbacks); })`；注意 `ThreadPool` 默认 12 线程，需显式设为 4。
- [ ] 支持「会话 A 运行中切换会话 B」——B 的 process 提交到池中，A 继续运行。
      - 关键修改点：`switchSession` 不再清空/重载共享 memory，仅切换「当前查看焦点」；SessionRuntime 各自独立运行。
- [ ] 实现每会话独立取消标志（`SessionRuntime::cancel_requested_`）+ 保留全局取消（决策 10）。
      - 关键修改点：`SessionRuntime::cancel()` 置本会话标志；`AgentFacade::cancelAll()` 置全局 `g_cancel_flag` 并遍历 `cancel()`；`CoreLoop::setCancelled` 接收每会话标志指针。
- [ ] 验证：并发 test（test_agent.cpp 扩展多会话场景）。
      - 验收：新增 `test_parallel_sessions`——两个会话同时 process，各自独立回复、独立状态；一个会话取消不影响另一个。

### 阶段 3：共享资源并发保护
- [ ] `ToolPipeline::execute()` 入口加 `shared_mutex`（含写工具加独占锁，否则共享锁）。
      - 关键修改点：`ToolPipeline::execute` 内先判断本次 `tool_calls` 是否含写工具（复用 `isReadOnly`），含写则 `unique_lock`，否则 `shared_lock`；成员 `mutable std::shared_mutex project_mutex_;`。
- [ ] `TokenCounter` 加内部 `std::mutex`（决策 8）。
      - 关键修改点：`TokenCounter.h` 加 `mutable std::mutex mutex_;`，`calibrate`/`apply`/`getCorrection`/`reset`/`resetAll`/`calibratedModels`/`stats` 各加 `std::lock_guard`。
- [ ] `SessionPersistence` 索引更新加锁；`save` 改为 `save(session_id, memory)` 显式传 id，弃用单一 active 字段，会话按 id 隔离落盘。
      - 关键修改点：`SessionPersistence.h` 改签名；`save` 内部按 session_id 写 `<id>.json`；`index.json` 更新加 `std::mutex`。
- [ ] 验证：并发读写 project 的测试。
      - 验收：新增 `test_concurrent_project`——两个会话并发写不同章节，用 `std::thread` + 延迟模拟竞争，断言无数据丢失/无崩溃；`test_parallel_sessions` 中并发读 project 不阻塞。

### 阶段 4：GUI 层改造
- [ ] `busy_` 从全局锁改为「按会话 busy」。
      - 关键修改点：`QmlBridge.h` 新增 `Q_PROPERTY(QString currentSessionId)` 与 `QHash<QString,bool> sessionBusy_`；`runAgent` 不再设全局 `busy_`，改设目标会话 busy。
- [ ] 输入框按「当前查看会话」的 busy 状态禁用；`sendMessage` 校验目标会话自身 busy。
      - 关键修改点：`AgentPanel.qml` 绑定 `currentSessionId` 对应会话的 busy；`sendMessage` 内校验目标会话 busy。
- [ ] 会话列表支持多个「运行中」状态展示。
      - 关键修改点：`SessionListModel` 增加 `running` 字段，随流信号更新。
- [ ] 切换会话不阻塞运行中的会话。
      - 关键修改点：`switchSession` 仅更新 `currentSessionId`，不再等待/取消其他会话。
- [ ] 流信号加 `sessionId` 参数（tokenReceived / reasoningReceived / toolCallStarted / toolCallFinished / responseComplete）。
      - 关键修改点：`QmlBridge.h` 信号签名加 `QString sessionId`；`runAgent` 回调从 SessionRuntime 取 id 后 emit。
- [ ] `AgentPanel.qml` 按当前查看会话过滤多通道流信号。
      - 关键修改点：Connections 处理器内 `if (sessionId !== currentSessionId) return;`
- [ ] 验证：GUI 手工测试（多会话并行 + 切换查看实时输出）。
      - 验收：会话 A 运行中切到 B 发消息，A 继续输出、切换回 A 看到完整历史；输入框仅当前查看会话 busy 时禁用。

### 阶段 5：生命周期管理
- [ ] 会话销毁时释放 SessionRuntime（memory/client/线程）。
      - 关键修改点：`deleteSession` 先 `cancel()` + 等待池任务结束（`future.wait()`），再移除 pool 节点释放。
- [ ] 关闭/归档会话时优雅终止其后台线程。
      - 关键修改点：析构前 `join`/`wait`，避免任务仍在跑时销毁成员。
- [ ] 验证：长时运行稳定性。
      - 验收：长时间（>10 分钟）多会话并发 + 反复增删会话，无崩溃、无泄漏（可用 `_CrtDumpMemoryLeaks` 或 ASAN 验证）。

---

## 七、风险与权衡

### 7.1 主要风险

| 风险 | 等级 | 缓解 |
|------|------|------|
| 改造范围大，涉及 Agent 核心对象模型 | 高 | 分阶段渐进式，每阶段独立回归 |
| LLM 并发调用（多会话同时请求） | 中 | 每会话独立 client 实例，天然隔离 |
| 工具并发写 project 数据 | 高 | ToolPipeline 入口统一加锁 |
| 内存/连接资源增长 | 中 | 会话生命周期管理 |
| GUI 复杂度上升 | 中 | 按会话 busy 状态 |

### 7.2 权衡

**优点**：
- 真正支持「后台会话继续跑」。
- 每会话独立 client，天然并发隔离，无 client 竞争。
- 会话状态彻底隔离，无交叉污染。

**代价**：
- 改造量大（Agent 对象模型重构）。
- 每会话一个 client 实例（每会话一个 HTTP 连接）。
- 共享 project 需引入锁，增加复杂度。
- 需要新增并发测试。

---

## 八、结论与建议

### 8.1 结论

你的方向**正确**：要支持「后台会话继续跑」，**必须把「会话运行时状态」从 Agent 单例中拆出，
改为每会话一个独立的运行时**（memory / state / tools / client），而不是翻转「会话持有 Agent」。

**关键架构约束**：`LLMClient` 单实例不安全，代码注释明确要求多线程用 `LLMClientFactory`
为每个执行上下文创建独立实例。这恰好支持「每会话一个 client」的设计——每个会话的 LLM
连接天然独立，无需共享、无需加锁。

### 8.2 建议

1. **建议采用上述「共享外壳 + 每会话运行时」方案**，而非简单复制整个 Agent。
2. **强烈建议分阶段实施**（阶段 0→5），每阶段独立回归，避免一次性大改的风险。
3. **优先做阶段 1（会话运行时容器化）**——这是核心，且单会话行为应保持不变，风险可控。
4. 并发测试需新增（多会话并发、工具并发写 project）。

---

## 附：待用户确认的决策点

1. **工作线程模型**：✅ 已确认 — 共享线程池（复用现有 `agent::ThreadPool`，线程数设为 4）。并发上限可控，会话数超出时自然排队，无需每会话建线程。
2. **project 加锁粒度**：✅ 已确认 — 方案 B：工具执行层统一加锁（`ToolPipeline::execute()` 入口，按读写工具加 shared_mutex）。GUI 直接写 project 需另行约定。
3. **会话数量的上限**：✅ 已确认 — 最大并发会话数 = 4（与线程池线程数对齐）。
4. **UI 交互**：✅ 已确认 — 方案 A：允许看到后台会话的实时输出。流信号加 `sessionId` 参数，QML 按当前查看会话过滤。
5. **process 接口签名**：✅ 已确认 — `process` 增加 `session_id` 参数（`process(session_id, input, callbacks)`），明确指定目标会话的 SessionRuntime，不依赖「当前 active 会话」。
6. **持久化 save 区分会话**：✅ 已确认 — `save` 改为 `save(session_id, memory)` 显式传 id，弃用单一 active 字段，会话按 id 隔离落盘；索引更新加锁串行化。
7. **输入框禁用规则**：✅ 已确认 — 按「当前查看会话」的 busy 状态禁用输入框；后台会话运行不影响查看/操作其他会话。`sendMessage` 仍校验目标会话自身 busy。
8. **校准器归属**：✅ 已确认 — 共享一个校准器实例，但给 `TokenCounter` 加内部 `std::mutex`（锁住 `models_` 的读写）。EMA 修正因子跨会话累积，更准确。
9. **工具线程池**：✅ 已确认 — 每会话独立 pipeline，但工具线程数调小到 1-2（`ToolPipeline` 构造传 `num_threads`）。4 会话 × 2 + 4 调度 = 12 线程上限。
10. **取消语义**：✅ 已确认 — 双通道：保留全局 `g_cancel_flag`（应用重建/退出）+ 每会话 `cancel_requested_`（单会话取消）。`cancelAll()` 置全局并遍历各会话 `cancel()`。
11. **`Agent::execute` 归属**：✅ 已确认 — 已删除 `Agent::execute` 声明、定义及 `test_execute_mode()` 测试。仅被该测试使用，CLI/`QmlBridge` 均未调用，避免多会话下维护独立路径。
12. **装配广播**：✅ 已确认 — 共享源 + 会话创建时注入：`AgentFacade` 维护全局装配配置（system prompt/model limit/calibrator 指针），`newSession` 创建 SessionRuntime 时注入。

> 补充说明（无需决策，仅需实现时注意）：
> - 装配期方法（`setSystemPrompt`/`setModelLimit`/`setCalibrator`）操作 `memory_`/`budget_`/`calibrator_`，会话化后改为操作对应 SessionRuntime 的成员，调用时机在会话创建后、提交前（决策 12）。
> - `runIndexUpdate` 全局索引服务在多会话下需串行化（同一时刻仅一个会话触发索引更新），避免索引竞争。
> - 加锁顺序约定：`project_ → calibrator_ → persistence_`，禁止逆序嵌套（防死锁）。

---

## 九、实现前待澄清清单（2026-08-04 补充审读）

> 基于对当前实现（双层持久化、`pending_new_` 延迟创建、`currentSessionId()`、单 Agent 基线）的
> 代码审读，以下决策点在进入阶段 0 前需逐项确认，否则实现会与预期设计偏离。
> 文档前述「决策 1-12」的 ✅ 代表"方向已确认"，不代表"细节已定"——本清单把其中
> 自相矛盾、与已落地代码冲突、以及尚未定义的点显式列出来。

### 9.1 文档内部矛盾（须先钉死）

**D1. `process()` 签名"对外不用改"自相矛盾 → 定版：前端把 sessionId 显式传给后端**
- 文档表述：决策 2「GUI 无需改动接口签名」，但附 5 又确认 `process` 增加 `session_id` 参数。
- 冲突：`process` 签名变化必然波及 `QmlBridge`/CLI/全部测试调用点，属接口变更。
- 澄清：文档前述「决策 1-12」的 ✅ 仅代表方向已确认；单看 `process`，「GUI 无需改动」与
  「加 session_id 参数」不可能同时成立——`process` 恰是 GUI 唯一实际调用的执行接口。
- **定版设计（把 sessionID 传给后端）**：
  1. **契约**：`process` 签名改为 `process(session_id, input, callbacks)`。`session_id` 由调用方
     （前端）提供，后端假定它已存在；不存在时返回 `finish_reason = "session_not_found"`，不抛异常。
  2. **调用链**：`AgentPanel.qml → QmlBridge::sendMessage(sessionId, input) =>
     agent().process(sessionId, input, cb) => pool_.at(sessionId) => SessionRuntime::process`。
     QmlBridge.cpp:465 由 `process(input, cb)` 改为 `process(sessionId, input, cb)`，
     `sessionId` 来自前端"当前查看会话"（决策 6 的 `currentSessionId` Q_PROPERTY）。
  3. **回调归属**：流信号（token/reasoning/tool 事件/responseComplete）在回调里带上同一
     `sessionId`，前端按"当前查看会话"过滤渲染——后台会话的增量输出也能实时回传。
  4. **收益**：执行目标由调用方显式声明，消除"当前 active 会话"这个共享可变中间态，后端
     `process` 无状态化、天然支持并发提交；测试可直连 `agent.process(sid, ...)` 免先 switch。
- **连带待决（牵连 D5）**：
  - `pool_.at(session_id)` 要求会话已存在，与 `pending_new_` 延迟创建冲突。**已定方案 C**：
    **id 提前生成、文件延迟落盘**——`newSession()` 时即用 `makeSessionId()` 生成 id 并建
    SessionRuntime（有内存、有 id），但**不写 `<id>.json`、不注册进 index**；首条消息
    `process(session_id)` 时 `saveSessionState` 才首次写文件 + 注册 index + 提取标题；
    未发消息就丢弃 → 从 pool 移除，index 无记录、无文件，不产生空会话文件。
    实现上把 `createSession()`（SessionPersistence.cpp:394）拆成「生成 id」与「首次落盘」两步。
  - 会话不存在/已删除的过期 id → 返回错误而非断言崩溃（见上 `session_not_found`）。
  - 变更面：仅 `switchSession`/`newSession`/`deleteSession` 等会话管理接口保持签名不变；
    QmlBridge.cpp:465 + 15 处测试（test_agent.cpp 13、test_e2e_chapter.cpp 2）同步补会话 id。

### 9.2 与已落地实现冲突（改造前需先定迁移方案）

**D2. `SessionManager` 的未来归宿（最大结构空白）→ 已定：收敛为 SessionPool，消息操作下放 SessionRuntime**
- 现状：`SessionManager` 持有单一 `memory_`，`reloadActiveSession()` 做「清空→重载」；
  还是 `pending_new_`、`currentSessionId()`、`saveSessionState`/`loadSessionState` 的唯一持有者。
  方法分两族：**族 1 生命周期/持久化**（newSession/switchSession/deleteSession/pending/
  saveSessionState/loadSessionState/currentSessionId）与**族 2 消息级操作**（pin/unpin/edit/
  rewindTo/checkpointIndices）。两族都因"共享内存"才捏在一起——每会话一个 SessionRuntime 后，
  共享内存消失，两族各归其位。
- **已定方案 C**：
  - **SessionManager 收敛为 SessionPool**（保留类名/职责为"池"，管存在性）：`newSession`（生成
    id + 建 runtime，方案 C 不落盘）、`switchSession`（仅切查看焦点）、`deleteSession`（移除
    runtime + 归档 + cancel/wait）、`pendingNewSession`/`discardPendingNewSession`（池级占位）。
  - **族 2 消息操作整体移入 SessionRuntime**：`pin/unpin/edit/rewindTo/checkpointIndices` 直接
    操作自身内存，无需 session_id（runtime 即该会话）。
  - **`saveSessionState`/`loadSessionState` 移入 SessionRuntime**：按自身 session_id 落盘，
    process 末尾由 runtime 自调。
  - **`currentSessionId()` 废弃**（D1 前端显式传 id）；pending 标志从 SessionManager 单布尔
    移到"池中未落盘 runtime 的 `persisted` 位"（方案 C/D5）。
  - **边界钩子**（`boundary_reset_hook_`/`usage_refresh_hook_`）本就是"每会话"的（清 tracer、
    重算用量），随 runtime 迁入 SessionRuntime，不再由池持有。
- 收益：池管"存在性"、runtime 管"自己的内存"，职责彻底分离；AgentFacade 不膨胀；与 D1/D5/D4
  既定方向完全咬合。

**D3. 双层持久化与 `active` 字段剥离 → 已定：save/load 显式传 session_id，active 与 updated_at 排序移出后端**
- 现状：`SessionPersistence` 深度依赖单一 `active` 字段——`save(memory)/load()`、
  `index.json` 的 `active`、`deleteSession` 切 active、`currentSessionId()`、`pending_new_` 全部围绕它。
- **定版**：
  - **`save(session_id, memory)` / `load(session_id)` 显式传 id**（决策 7），按 id 写/读 `<id>.json`；
    `saveSessionState`/`loadSessionState` 移入 SessionRuntime（D2）后按自身 id 落盘。
  - **`active` 字段彻底删除**：`index.json` 从 `{active, sessions:[...]}` 改为 `{sessions:[...]}`；
    `activeSessionId()`、`switchSession` 写 active、`deleteSession` 切 active 全部移除。
  - **`updated_at` 排序移出后端**：会话列表的"最近使用"排序由**前端维护**——`sendMessage(sessionId)`
    时把该会话移到列表最前（QmlBridge 维护最近顺序，`sessionList()` 按此前缀拼接）。后端
    `listSessions()` 不再按 `updated_at` 排序（返回存储/创建顺序）；`updated_at` 字段保留写入
    但不作排序依据（向后兼容旧 index.json）。
  - **持久化按会话是必然前提，非决策**：多会话并行下后端无法靠单一 active 定位，`save(session_id)`
    是 SessionRuntime 落盘的前提，**先于并行化完成**（同阶段 0/1 一并落地）。
  - **索引并发**：索引写入（首落盘/删除）需 `std::mutex` 串行化；会话文件 `<id>.json` 按 id 隔离无冲突。
  - **`currentSessionId()` 废弃**（D2/D1）；`makeSessionId` 查重不看 active，不变。

**D4. 完整历史层（`.history`）在文档中零覆盖 → 已定：去 sink，SessionRuntime 直接落盘 + 删除前 cancel+wait**
- 现状：`appendHistory`/`loadHistory`/`deleteSession` 归档 archive 均已实现（含单线程约束注释）。
  写入链：`Agent::applyCompaction → history_sink_（AppAssembly setHistorySink）→
  persistence_.appendHistory(session_id, messages)`。
- **已定方案 B（去 sink）**：
  - **去掉全局 `history_sink_` 回调**：SessionRuntime 直接持有 `SessionPersistence` 引用，
    压缩时自调 `appendHistory(own_id, compacted)`。归属天然正确（runtime 即该会话），
    无全局单点；D2 已把 `saveSessionState` 移入 runtime，runtime 本就持 persistence 做落盘，
    顺带做历史归档，耦合合理。`setSummarySink` 同理改为直接落 ltm。
  - **并发安全**：不同会话写不同 `.history` 文件无冲突；同一会话由同一 runtime 单线程独占执行，
    满足 `appendHistory` 的单线程约束，无需额外锁（写明前提）。
  - **关键风险——后台运行中删除会话**：会话 A 在后台生成时删除 A，`deleteSession` 必须先
    `cancel` 该会话 → `wait` 池任务结束 → **再**归档 `.history` + 移除 runtime，否则正在跑的
    process 会向已归档/已删除的文件 appendHistory，历史丢失或竞态。此约束补进生命周期章节（阶段 5）。
  - **`currentSessionId()` 不再用于归档归属**（D2/D5 废弃），由 runtime 自身会话 id 取代。

**D5. `pending_new_` 延迟创建与多会话模型冲突 → 已定：方案 C 下保留 pending 但改 id 生成时机**
- 现状：`pending_new_` 是「单一共享内存」的显示层概念（未落盘会话占位）。
- 已定方案 C（见 D1）：`newSession()` 即生成真实 id 并建 SessionRuntime，但**不写文件、不注册
  index**；首条消息才首次落盘。pending 机制**保留**，只是「id 何时生成」从「落盘时」改为「创建时」。
- 连带调整：
  - `pendingNewSession()` 仍用于前端占位（QML 置顶"新会话"）。
  - `discardPendingNewSession()` 语义变为「从 pool 移除未落盘会话」（index 无记录、无文件，不产生空会话）。
  - `currentSessionId()` 不再需要 pending 空值分支（pending 也有 id），压缩归档归属来源随之
    改为 SessionRuntime 自身持有（见 D4），无需再按「当前会话」解析。

### 9.3 实施细节未定（需补全）

**D6. 每会话 `client` 的迁移 → 已定：client 迁入 SessionRuntime，按需物化 + 空闲休眠释放**
- 现状：Agent 构造时 `factory.create()` 创建唯一 `client_`（Agent.cpp:39），`process`/`refreshUsage`
  全用它；`LLMClient`/`HttpClient` 单实例不安全，多会话并发必须每会话独立 client。
- **定版**：
  - **client 迁入 SessionRuntime，Agent 不再保留默认 client（方案 A）**：`process(session_id, ...)`
    定位 runtime 后，一切 LLM 调用（chat/chatNonStreaming/config）从该 runtime 的 client 取。
  - **`client()` 访问器去掉**：`agent.client()` 语义不再成立，改为 `runtime.client()`；测试/装配
    改用 pool 内 runtime。
  - **按需物化（lazy materialization）**：历史会话不预建 runtime，只作 index/磁盘记录；会话被
    打开/使用（点进或发消息）才物化 runtime 并 `factory.create()` 建 client。**client 数 ≈ 活跃
    runtime 数 ≤ 并发上限（4）**，不是历史会话总数。
  - **空闲休眠释放**：后台会话跑完且暂时无人看时，runtime 保留（保内存/上下文）但**释放 client
    连接**，切回时重建（`factory.create()` 全新连接）。后台会话"切走不销毁"（否则无法继续跑），
    真正销毁仅发生在删除/归档时（先 cancel+wait，见 D4）。
  - **`create()` 并发安全**：`LLMClientFactory::create()` 为 const 线程安全，多会话并行物化安全。
  - **装配期 refreshUsage**：因无默认 client，改为对活跃 runtime 广播或注入校准专用 client（见 D12）。

**D7. 工具锁粒度与 `isReadOnly` 完备性 → 已定：Project 自带 shared_mutex + 锁外计算/锁内小改**
- 现状：`ToolPipeline::execute` 无锁；`isReadOnly` 是 static 按 tool_name 判断（ToolPipeline.h:49）。
- **定版**：
  - **锁放进 `Project` 内部（数据自身持有锁）**：`Project` 自带 `mutable std::shared_mutex` 与
    事务粒度读/写方法（如 `updateChapter` 一次锁住整个读-改-写），而非在 `ToolPipeline::execute`
    入口加锁。理由：入口锁盖不住 GUI 直接写（导入/手动编辑不经过 pipeline）与间接路径
    （`ProjectIndexService` 等），且属"调用方加锁"易漏/双重锁；数据自带锁与 `VectorStore`/
    `LongTermMemoryStore` 现有模式一致。**pipeline 工具、GUI 导入/编辑、所有写路径共享同一把锁**。
  - **粒度 = 项目级单锁**：读批次共享锁真并行；写批次独占锁串行化。写串行化在此合理——写低频
    短时、瓶颈是 LLM 网络 IO、读批次已享并行。暂不做按聚合分锁或乐观并发（工具调用量小冲突低，
    遇同文档高频冲突再细化）。
  - **写工具结构约定「锁外计算、锁内小改」**：读所需数据（共享锁）→ 做耗时计算（LLM/生成章节）
    **完全不持锁** → 最后独占锁只做那一下小改动立即释放。避免写锁占用久；**否决 copy-on-write
    快照交换**（深拷贝成本高，且并发写下回换会覆盖他人改动，lost-update）。
  - **`isReadOnly` 覆盖集**：须覆盖全部已注册工具（含渐进式延迟工具），映射完整稳定；混合读写
    批次按「含写则独占」处理。

**D8. 校准器加锁范围 & 共享语义 → 已定：有状态方法加内部 mutex；共享是收敛的必要条件**
- 现状：`TokenCounter` 无锁（TokenCounter.h:26），`models_` 是无锁 unordered_map；多会话并发
  `calibrate()`（写）/`apply()`（读）会数据竞争。
- **定版**：
  - **加锁范围**：给**有状态实例方法**（`calibrate`/`apply`/`getCorrection`/`reset`/`resetAll`/
    `calibratedModels`/`stats`）加 `std::lock_guard`；**无状态静态方法**（`countTokens`/
    `countMessages`/`countSingleMessage` 等）不加锁（不碰 `models_`，加锁拖慢热路径）。
  - **重入确认**：实现时确认 `calibrate`/`apply` 内部不自相调用加锁方法（否则非递归 mutex 死锁）；
    若必要，把锁内核心逻辑抽成私有无锁方法。
  - **共享语义（收敛支点）**：校准**必须跨会话共享**——`ratio = actual/estimated` 用的是**原始估算**
    （CoreLoop.cpp:109 传 `countMessages` 原始值，非已校准值），无失稳回路，EMA 稳定收敛到真实偏差；
    收敛约需 20-30 次观测（α=0.5×5 次后 α=0.1，半衰期≈6.6 次），**单会话凑不满轮次，跨会话共享
    才收敛**。校准按模型分桶（`models_` 按 model key），不同模型互不干扰；同一模型跨会话累积是
    设计意图（EMA 跟踪近内容混合的均值），非污染。
  - **锁代价**：校准只在每轮 LLM 请求后触发一次，非热路径，加锁成本极低。

**D9. 线程模型参数 → 已定：每会话工具线程 = 2，去掉 num_threads=0 串行分支**
- 现状：`ToolPipeline` 默认 4 线程（ToolPipeline.h:30），多会话下每会话一个 pipeline 会线程膨胀。
- **定版**：
  - **每会话 `ToolPipeline` 传 `num_threads = 2`**（SessionRuntime 构造参数传入）：4 会话并发
    = 8 工具线程 + 4 调度 = **12 线程上限**。2 线程让单会话内少量只读工具并行（如一次调 2-3 个
    只读工具），成本可控；工具调用量小，1 线程会串行化只读批次，2 线程收益明显。
  - **去掉 `num_threads = 0` 的"全串行"分支**：`pool_` 不再为 nullptr 特例，恒为 2（或组合配置）。
  - **两层线程区分**：共享调度池（4 线程）决定并发会话数；每会话工具线程（2）决定单会话内工具
    并行度，两者独立，D9 只调后者。
  - **与 D7 关联**：工具线程数决定读批次（共享锁下）能并行到什么程度——2 线程已够，不为追求
    读并行加大线程数。

**D10. 取消与生命周期 → 已定：双通道取消 + 删除走强制取消（B）+ 占位消息加控制标记**
- 现状：`cancel_requested_` 是 Agent 成员（Agent.h:213），配合全局 `g_cancel_flag`。
- **定版**：
  - **双通道取消**：保留全局 `g_cancel_flag`（应用重建/退出）+ 每会话 `cancel_requested_`
    （`SessionRuntime::cancel()` 置本会话标志；`cancelAll()` 置全局并遍历 `cancel()`；
    `CoreLoop::setCancelled` 接收每会话标志指针）。
  - **删除运行中会话 = 强制取消（方案 B）**：`deleteSession` → `cancel()`（置标志、流式连接尽快
    关闭）→ **wait 池任务退出（带超时，如 2-5s）** → 再归档 `.history` + 移除 runtime。超时后记
    日志并强制移除（此时 runtime 状态可能不一致，但删除即"不要了"）。必须等池任务真正退场再移除
    runtime，否则任务的析构/后续 appendHistory 会访问已销毁 runtime（D4）。已归档的完整轮数据不受
    强制取消影响。
  - **取消占位消息（用户已验证的"虚拟对话"）**：取消时注入一条 assistant 占位消息维持 user/assistant
    角色交替（硬约束：vLLM/Mistral 报 "Conversation roles must alternate"）。**补增控制标记**：占位
    消息加标记（如 `content` 特殊前缀或 Message 控制字段），UI 据此过滤/特殊渲染为"已取消"提示，
    避免污染用户看到的对话；同时它仍会持久化以保证会话跨重启一致。

**D11. 装配广播（决策 12 内部"或"未定）→ 已定：仅创建时生效（方案 B）**
- 文档表述：「更新共享配置后广播到所有已存在会话（或仅在会话创建时生效）」——"或"即未定。
- **定版（方案 B）**：装配配置（system prompt / model limit / 校准器）作为**共享源**由装配层持有，
  `newSession` 创建 SessionRuntime 时**从共享源拷贝注入**；**已存在会话不更新**（保持创建时快照）。
- 理由：
  1. 与现状一致——`setSystemPromptProvider`（AppAssembly.cpp:72）本就是"会话边界重建、会话中途
     冻结"（保 KV cache 稳定），方案 B 是顺理成章的延续。
  2. 运行中改 prompt 会破坏 KV cache 稳定 + 会话上下文漂移，违背既定约束。
  3. 简单，无需遍历广播。
- 边界：`setSystemPromptProvider` 始终保留（会话创建/切换时重建 prompt）；`setSystemPrompt` 的
  "直接写"仅作启动时无 provider 的兜底。

### 9.4 建议（防过度设计）

**D12. 是否值得全量并行 → 已定：全量并行**
- 文档结论"必须每会话运行时"方向正确，但这是大改造。
- **定版：全量并行**——用户确认「后台会话继续跑」是硬需求（切到别的会话时当前会话继续后台生成），
  必须每会话一个 SessionRuntime。D1–D11 全部为此设计，非过度设计，作为实施蓝图。
- 实施按阶段 0→5 执行；每阶段独立回归，避免一次性大改。

### 9.5 确认记录

> 全部 12 项决策已定（D1–D12），作为本架构评审的最终实施蓝图。

| 编号 | 决策要点 | 状态 |
|------|---------|------|
| D1 | 前端把 sessionId 显式传给后端（`process(session_id, ...)`）；pending 走方案 C（id 提前、文件延迟） | ☑ 已确认 |
| D2 | SessionManager 收敛为 SessionPool；消息操作下放 SessionRuntime（方案 C） | ☑ 已确认 |
| D3 | save/load 显式传 session_id；删 active；updated_at 排序移前端维护 | ☑ 已确认 |
| D4 | 去 sink，SessionRuntime 直接落盘历史；删除前 cancel+wait；归属用 runtime 自身 id | ☑ 已确认 |
| D5 | pending 保留，id 生成时机改到创建时（方案 C） | ☑ 已确认 |
| D6 | client 迁入 SessionRuntime；按需物化 + 空闲休眠释放；去 agent.client() | ☑ 已确认 |
| D7 | Project 自带锁（项目级单锁）+ 锁外计算/锁内小改；isReadOnly 覆盖全部工具 | ☑ 已确认 |
| D8 | 校准器有状态方法加内部 mutex；跨会话共享是收敛必要条件（原始估算无失稳） | ☑ 已确认 |
| D9 | 每会话工具线程 = 2；去掉 num_threads=0 串行分支 | ☑ 已确认 |
| D10 | 删除走强制取消（B）+ wait 超时；占位消息加控制标记、UI 特殊渲染 | ☑ 已确认 |
| D11 | 装配仅创建时生效（共享源 + 创建时注入，方案 B）；setSystemPromptProvider 保留 | ☑ 已确认 |
| D12 | 全量并行（后台会话继续跑为硬需求）；实施按阶段 0→5 | ☑ 已确认 |