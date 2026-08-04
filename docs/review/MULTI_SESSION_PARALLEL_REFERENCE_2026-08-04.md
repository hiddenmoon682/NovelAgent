# 多会话并行架构参考文档（实施蓝图）

> 日期：2026-08-04
> 依据：`MULTI_SESSION_PARALLEL_REVIEW_2026-08-03.md` 及后续逐项决策（D1–D12、E1–E10）。
> 本文档是**整合后的最终实施蓝图**，替代原评审文档作为并行化改造的唯一依据。
> 原评审文档中与本文冲突的表述（如阶段 3 在 ToolPipeline 入口加锁）一律以本文为准。

---

## 一、目标与架构

### 1.1 目标

支持「**后台会话继续跑**」：会话 A 在后台生成时，用户可切到会话 B 发起新对话，A 继续运行。
多会话可同时处于运行中。**这是硬需求**（D12）。

### 1.2 核心架构：共享外壳 + 每会话运行时

把「会话状态」与「全局资源」分离。会话状态每会话独有，全局资源共享。

```
NovelAgentApp（共享外壳，全局一份）
  ├── client_factory_    LLMClientFactory·（线程安全，可共享）
  ├── registry_          ToolRegistry·（运行时只读）
  ├── project_           Project·（共享可变，自带锁，见 D7）
  ├── vector_store_ / embedding_gen_ / ltm_store_ / skill_registry_（共享，内置锁）
  ├── persistence_       SessionPersistence·（磁盘层，共享，索引内部加锁）
  ├── calibrator_        TokenCounter·（共享，内部加锁，见 D8）
  ├── index_service_     ProjectIndexService·（共享，runIndexUpdate 内部加锁，见 E8）
  └── agent_             Agent（门面 + 会话池，见 E2）

Agent（保留类名，改造成门面；全局一个）
  ├── pool_              SessionPool·（map<session_id, unique_ptr<SessionRuntime>>）
  ├── 共享装配配置        （system prompt provider / model limit / calibrator 指针，见 D11）
  └── 对外接口与现在一致    （process/switchSession/newSession/deleteSession/...，见 E6）

SessionRuntime（每会话一个，独立运行时）
  ├── session_id_        std::string（会话自身 id，见 D4/D10）
  ├── memory_            IMemory（每会话对话上下文）
  ├── client_            unique_ptr<ILLMClient>（每会话独立 LLM 客户端，见 D6）
  ├── state_             StateMachine（每会话状态机）
  ├── progressive_tools_  ProgressiveToolProvider（每会话工具加载状态）
  ├── pipeline_          ToolPipeline（每会话工具管道，num_threads=2，见 D9）
  ├── tracer_            ExecutionTracer（每会话执行轨迹）
  ├── usage_             ContextUsage（每会话用量快照）
  ├── cancel_requested_   std::atomic<bool>（每会话取消标志，见 D10）
  ├── persisted_         bool（未落盘标记，方案 C，见 D5）
  └── 持久化引用          SessionPersistence*（直接落盘，见 D4）
```

### 1.3 组件归属总表

| 组件 | 归属 | 理由 |
|------|------|------|
| memory_ / client_ / state_ / progressive_tools_ / pipeline_ / tracer_ / usage_ / cancel_requested_ | 每会话 | 会话状态，天然隔离 |
| factory_ / registry_ / persistence_ / vector_store_ / ltm_store_ / skill_registry_ / calibrator_ | 共享 | 全局资源 |
| project_ | 共享（自带锁） | 多会话共享语义，需并发保护 |
| budget_evaluator_ / compactor_ / budget_ | 共享（无状态） | 上下文管理组件，无会话状态 |
| index_service_ | 共享（内部锁） | 全局索引服务 |

---

## 二、最终决策清单

### 二.A 接口与归属（D1/D2/D5/D6/D10/E2/E3/E6/E10）

**A1. `process` 接口签名（D1）**
- `process(session_id, input, callbacks)`：前端显式传目标会话 id。
- 后端流程：`pool_.at(session_id)` 定位 SessionRuntime → 调 `rt.process(input, callbacks)`（E10，runtime 内部 process 不带 session_id）。
- `session_id` 不存在/已删除 → 返回 `finish_reason = "session_not_found"`，不抛异常。
- 变更面：QmlBridge.cpp:465 + 15 处测试（test_agent.cpp 13、test_e2e_chapter.cpp 2）补 session_id。

**A2. Agent 类名保留（E2）**
- 保留 `Agent` 类名，内部改造成「门面 + 会话池」，不新建 AgentFacade。QmlBridge 的 `app_->agent()` 及其调用不变。

**A3. SessionManager 去向（D2/E3）**
- `SessionManager` 解散、职责吸收进 `SessionPool`；Agent 直接持有 `pool_`（不再有 `session_manager_` 成员）。
- 生命周期/池操作（`newSession`/`switchSession`/`deleteSession`/`pendingNewSession`/`discardPendingNewSession`）归池。
- 消息级操作（`pin/unpin/edit/rewindTo/checkpointIndices`）与 `saveSessionState`/`loadSessionState` 移入 SessionRuntime。
- `currentSessionId()` 废弃。

**A4. pending 走方案 C（D1/D5）**
- `newSession()` 即用 `makeSessionId()` 生成 id 并建 SessionRuntime（有内存、有 id），**不写文件、不注册 index**；`persisted_ = false`。
- 首条消息 `process(session_id)` → `saveSessionState` 首次写文件 + 注册 index + 提取标题，`persisted_ = true`。
- 未发消息丢弃 → 从 pool 移除，index 无记录、无文件，不产生空会话。
- `pendingNewSession()` 仍用于前端占位；`discardPendingNewSession()` 语义变为「从 pool 移除未落盘会话」。

**A5. client 归属（D6）**
- client 迁入 SessionRuntime，Agent 不保留默认 client；`agent.client()` 访问器去掉，改 `runtime.client()`。
- **按需物化**：历史会话不预建 runtime，打开/使用才物化并 `factory.create()`。client 数 ≈ 活跃 runtime 数 ≤ 并发上限 4。
- **空闲休眠释放**：后台跑完且无人看时 runtime 保留（保内存/上下文）、释放 client 连接，切回重建。
- **删除/归档才销毁** runtime + client。

**A6. Agent 公有转发接口保留（E6）**
- `saveSessionState()/loadSessionState()/pendingNewSession()/discardPendingNewSession()/switchSession()/newSession()/deleteSession()/setPersistence()/setModelLimit()/setCalibrator()/setSummarySink()` 签名全部保留，内部转发到池/runtime/共享源。避免 GUI/测试大面积改动。

### 二.B 持久化（D3/D4/E4）

**B1. save/load 显式传 session_id（D3）**
- `save(session_id, memory)` / `load(session_id)`，按 id 写/读 `<id>.json`。
- `index.json` 从 `{active, sessions:[...]}` 改为 `{sessions:[...]}`。
- `activeSessionId()`、`switchSession` 写 active、`deleteSession` 切 active 全部移除。
- `makeSessionId` 查重不看 active，不变。

**B2. `updated_at` 排序移前端（D3）**
- 会话列表"最近使用"排序由前端维护：`sendMessage(sessionId)` 时把该会话移到列表最前（QmlBridge 维护最近顺序，`sessionList()` 按此前缀拼接）。
- 后端 `listSessions()` 不再按 `updated_at` 排序（返回存储/创建顺序）；字段保留写入但不作排序依据（兼容旧 index.json）。

**B3. 启动默认选中由前端维护（E4 修订）**
- 不设后端 `last_viewed` 字段（点开才物化，见 P8）。启动时前端用 D3 的"最近查看顺序"记一个当前项
  （或默认列表第一项）作为默认选中焦点；点开会话才物化并加载内容。

**B4. 完整历史层（D4）**
- 去掉全局 `history_sink_` 回调：SessionRuntime 直接持有 `SessionPersistence` 引用，压缩时自调 `appendHistory(own_id, compacted)`。`setSummarySink` 同理改为直接落 ltm。
- 并发安全：不同会话写不同 `.history` 文件无冲突；同一会话由同一 runtime 单线程独占执行，满足 `appendHistory` 单线程约束。
- **删除前 cancel+wait**：后台运行中删除会话，`deleteSession` 先 `cancel()` → wait 池任务退出 → 再归档 `.history` + 移除 runtime。

### 二.C 并发与锁（D7/D8/E7/E8）

**C1. Project 自带锁（D7）**
- `Project` 内置 `mutable std::shared_mutex`，提供事务粒度读/写方法（如 `updateChapter` 一次锁住整个读-改-写）。**不在 ToolPipeline 入口加锁**。
- 粒度 = 项目级单锁：读批次共享锁真并行；写批次独占锁串行化（写串行化在此合理——写低频、LLM 是瓶颈）。
- 写工具遵循「锁外计算、锁内小改」：读所需数据（共享锁）→ 做耗时计算（LLM/生成章节）**完全不持锁** → 最后独占锁只做小改动立即释放。**否决 copy-on-write 快照交换**。

**C2. `isReadOnly` 注册时标记（E7）**
- 工具注册时带 `is_readonly` 属性（`BuiltInTool::registerAllTo` 时标记），而非静态按 tool_name 判断。覆盖全部工具（含渐进式/延迟工具）。
- 混合读写批次按「含写则独占」处理。

**C3. 校准器加锁（D8）**
- `TokenCounter` 有状态实例方法（`calibrate`/`apply`/`getCorrection`/`reset`/`resetAll`/`calibratedModels`/`stats`）加 `std::lock_guard`；无状态静态方法不加锁。
- 实现时确认 `calibrate`/`apply` 不自相调用加锁方法（防死锁），必要时抽私有无锁方法。
- 校准必须跨会话共享（收敛必要条件）：`calibrate` 用原始估算（CoreLoop.cpp:109），无失稳回路；单会话凑不满 20-30 次观测，跨会话共享才收敛。按模型分桶，不同模型互不干扰。

**C4. `runIndexUpdate` 内部加锁（E8）**
- 锁放进 `ProjectIndexService` 内部，`runIndexUpdate` 加 `std::mutex` 串行化，与 D7"数据自带锁"一致。

### 二.D 线程与取消（D9/D10/E5/E9）

**D1. 线程模型**
- 共享调度池 `agent::ThreadPool` 线程数 = 4（并发会话上限）。
- 每会话 `ToolPipeline` 传 `num_threads = 2`；去掉 `num_threads = 0` 的"全串行"分支。
- 4 会话并发 = 8 工具线程 + 4 调度 = 12 线程上限。

**D2. 取消（D10）**
- 双通道取消：全局 `g_cancel_flag`（应用重建/退出）+ 每会话 `cancel_requested_`（`SessionRuntime::cancel()` 置本会话标志；`cancelAll()` 置全局并遍历 `cancel()`；`CoreLoop::setCancelled` 接收每会话标志指针）。
- **删除运行中会话 = 强制取消（B）**：`deleteSession` → `cancel()` → wait 池任务退出（带超时 2-5s）→ 归档 `.history` + 移除 runtime。超时后强制移除。
- **取消占位消息**：取消时注入一条 assistant 占位消息维持 user/assistant 角色交替（硬约束：vLLM/Mistral 报 "Conversation roles must alternate"）。**补增控制标记**（如 `content` 特殊前缀或 Message 控制字段），UI 据此过滤/特殊渲染为"已取消"提示；仍持久化以保证跨重启一致。

**D3. 全局取消复位（E5）**
- `cancelAll()` 后由调用方（rebuildApp/退出）显式复位全局标志，或每次 process 提交前检查并复位。

**D4. 状态栏 usage（E9）**
- 状态栏显示「当前查看会话」的 usage（QML 绑定 currentSessionId 对应 runtime 的 usage）。

### 二.E 装配（D11）

**E1. 装配仅创建时生效（D11）**
- 装配配置（system prompt / model limit / 校准器）作为共享源由 Agent 持有，`newSession` 创建 SessionRuntime 时拷贝注入；已存在会话不更新（保持创建时快照）。
- `setSystemPromptProvider` 始终保留（会话创建/切换时重建 prompt）；`setSystemPrompt` 直接写仅作启动时无 provider 兜底。

---

## 三、并发安全矩阵

| 资源 | 读（多会话并发） | 写（多会话并发） | 保护 |
|------|-----------------|-----------------|------|
| 会话自身 memory_ / client_ / state_ / tools_ | 仅本会话线程 | 仅本会话线程 | 无需锁（线程独占） |
| 共享 project_ | 多会话并发读 | 多会话并发写 | Project 自带 shared_mutex（读共享/写独占，D7） |
| 共享 registry_ | 多会话并发读 | 仅装配时写 | 初始化后只读 |
| 共享 persistence_ | 多会话并发读 | 多会话并发写 | 会话文件按 id 隔离；index 更新内部加锁（B1） |
| 共享 vector_store_ / ltm_store_ | 多会话并发读 | 多会话并发写 | 已内置锁，无需改 |
| 共享 calibrator_（TokenCounter） | 多会话并发读 | 多会话并发写 | 有状态方法内部 std::mutex（C3） |
| 共享 index_service_ | 多会话并发读 | 多会话并发写 | runIndexUpdate 内部 std::mutex（C4） |

**加锁顺序约定**（防死锁）：`project_ → calibrator_ → persistence_`，禁止逆序嵌套。

---

## 四、分步实施计划

> 渐进式改造，每阶段独立验证、可回滚。

### 阶段 0：准备（不改行为）
- [ ] 新建 `src/agent/core/SessionRuntime.h`：声明成员 `session_id_`、`memory_`、`client_`、`state_`、
      `progressive_tools_`、`pipeline_`、`tracer_`、`usage_`、`cancel_requested_`、`persisted_`、持久化引用。
- [ ] 确认 `LLMClientFactory::create()` 多线程并发安全（const，已验证）。

### 阶段 1：会话运行时容器化（核心改造）
- [ ] 实现 `SessionRuntime`：构造时 `factory.create()` 建独立 client；`ToolPipeline` 传 `num_threads = 2`（D9）。
- [ ] 将 Agent 的 `memory_`/`state_`/`progressive_tools_`/`pipeline_`/`tracer_`/`usage_`/`client_` 迁入 SessionRuntime。
- [ ] `SessionManager` 解散吸收进 `SessionPool`；Agent 持有 `pool_`（E3）。
- [ ] `process` 增加 `session_id` 参数：`pool_.at(session_id)` → `rt.process(input, callbacks)`（D1/E10）。
- [ ] 删除 `Agent::execute` 及其测试（附决策 11）。
- [ ] 验证：单会话行为与改造前完全一致（回归 test_agent.cpp 其余用例 + GUI 单会话手工）。

### 阶段 2：多会话并发执行
- [ ] 引入共享 `agent::ThreadPool`（线程数 = 4），`process` 提交为池任务。
- [ ] `switchSession` 仅切「当前查看焦点」，不再清空/重载共享内存；SessionRuntime 各自独立运行。
- [ ] 每会话独立取消标志 + 保留全局取消（D10/D2）。
- [ ] 验证：新增 `test_parallel_sessions`——两会话同时 process 各自独立回复/状态；一个会话取消不影响另一个。

### 阶段 3：共享资源并发保护
- [ ] `Project` 内置 `shared_mutex` + 事务级读写方法（D7，**非 ToolPipeline 入口**）；写工具"锁外计算/锁内小改"。
- [ ] `TokenCounter` 有状态方法加内部 `std::mutex`（C3）。
- [ ] `SessionPersistence`：`save(session_id, memory)`/`load(session_id)`，删 active，index 加锁（B1）。
- [ ] 工具注册带 `is_readonly` 属性（E7）。
- [ ] 验证：新增 `test_concurrent_project`——两会话并发写不同章节，断言无数据丢失/无崩溃；并发读不阻塞。

### 阶段 4：GUI 层改造
- [ ] `busy_` 从全局锁改为「按会话 busy」；`QmlBridge` 新增 `Q_PROPERTY(QString currentSessionId)` 与 `sessionBusy_`。
- [ ] 输入框按「当前查看会话」busy 禁用；`sendMessage` 校验目标会话自身 busy。
- [ ] 会话列表支持多个「运行中」状态展示（`SessionListModel` 增加 `running` 字段）。
- [ ] 流信号加 `sessionId` 参数（tokenReceived / reasoningReceived / toolCallStarted / toolCallFinished / responseComplete）。
- [ ] `runAgent` 改为提交进池（异步），新增 `on_complete(session_id, response)` 完成回调；`responseComplete` 带 sessionId（P1）。
- [ ] `AgentPanel.qml` 按当前查看会话过滤多通道流信号；取消占位消息加控制标记、特殊渲染（D10）。
- [ ] `updated_at` 排序移前端维护（B2）；状态栏显示当前查看会话 usage（E9）。
- [ ] 验证：会话 A 运行中切到 B 发消息，A 继续输出、切回 A 看到完整历史；输入框仅当前查看会话 busy 时禁用。

### 阶段 5：生命周期管理
- [ ] `deleteSession` 先 `cancel()` + wait 池任务（带超时）→ 归档 `.history` + 移除 runtime（D4/D10）。
- [ ] 关闭/归档会话时优雅终止后台线程（析构前 join/wait）。
- [ ] 空闲会话休眠释放 client（D6）。
- [ ] 验证：长时（>10 分钟）多会话并发 + 反复增删会话，无崩溃、无泄漏（`_CrtDumpMemoryLeaks` 或 ASAN）。

---

## 五、资源账

| 资源 | 单会话 | 4 会话上限 | 说明 |
|------|--------|-----------|------|
| 调度线程 | 0（共享池） | 4 | 共享线程池，不随会话增长 |
| 工具线程 | 2（pipeline 内） | 8 | 每会话 pipeline，num_threads=2 |
| LLM HTTP 连接 | 1 | 4 | 每会话独立 client，按需物化 + 空闲休眠释放 |
| 对话 memory | 1 份 | 4 份 | 每会话独立上下文 |
| 校准器 | 1 实例（共享） | 1 实例 | 加锁共享，EMA 跨会话累积 |

**总线程数上限**：4 调度 + 8 工具 = 12 线程。

---

## 六、风险与边界

| 风险 | 等级 | 缓解 |
|------|------|------|
| 改造范围大，涉及 Agent 核心对象模型 | 高 | 分阶段渐进式，每阶段独立回归 |
| LLM 并发调用 | 中 | 每会话独立 client，天然隔离 |
| 工具并发写 project | 高 | Project 内置锁（D7） |
| 后台运行中删除会话 | 高 | deleteSession 先 cancel+wait 再归档（D4/D10） |
| 内存/连接资源增长 | 中 | 会话生命周期管理 + 空闲休眠释放（D6） |
| GUI 复杂度上升 | 中 | 按会话 busy + 多通道流信号 |

---

## 七、与旧文档的关系

- 本文件为整合后的唯一实施依据；原 `MULTI_SESSION_PARALLEL_REVIEW_2026-08-03.md` 保留作决策过程存档。
- 原文档中与本文冲突的表述一律以本文为准（尤其：锁在 Project 内部而非 ToolPipeline 入口；Agent 保留类名；SessionManager 解散为 SessionPool；持久化删 active；不设 last_viewed）。

---

## 八、补充决策（P1–P10，待确认）

> 追审发现的决策点，每条附建议解法。确认后改 ☑ 并移入对应章节。

**P1. process 的同步/异步与返回值协调（最关键）→ 已定：异步回调（方案 A）**
- 现状：QmlBridge.cpp:465 同步 `auto response = agent.process(input, cb)`，随后用 `response.content` emit `responseComplete`。
- 冲突：阶段 2 把 process 提交为池任务后，无法同步返回 `LLMResponse`，QmlBridge 调用方式必须改。
- 定版：**process 异步回调**——`process(session_id, input, cb)` 提交进池立即返回（不阻塞 GUI），
  池工作线程完成时经新增 `on_complete(session_id, response)` 回调送回；流式回调与完成回调均走
  QueuedConnection 投递到 GUI 线程。`responseComplete` 信号带 sessionId，QML 按当前查看会话归属。
  `runAgent` 不再开 `worker_` 线程，改为提交进池；线程末尾的 `runIndexUpdate`/`busy_.store(false)`
  收尾移到完成回调（按 sessionId）。
- ☑ 已确认

**P2. ToolPipeline 批次锁 vs Project 方法锁 → 已定：不引入批次锁，靠方法级锁**
- D7 说"锁在 Project 内部"又写"混合读写批次按含写则独占"——锁在方法内部，pipeline 无法对整个批次加独占锁。
- 定版：**不引入批次锁**。工具批次靠 Project 方法级锁各自串行；"含写则独占"只在**单次方法调用**内体现
  （写方法独占锁、读方法共享锁），描述单次调用的读写性质，而非批次。
- 规则：**写 Project 内容走独占锁，读 Project 内容走共享锁**（读也要共享锁，防撕裂读）。
- 约束：工具/GUI 一律通过 Project 的加锁方法访问，**禁止直接碰字段**（绕过锁形同虚设）。
- 批次内多工具一致性靠"锁外计算、锁内小改"（D7）；跨方法原子性交给 P3 的事务方法。
- ☑ 已确认

**P3. Project 事务方法的粒度 → 已定：事务方法为主 + withLock 受控守卫兜底**
- Project 是 POJO 数据持有者（字段全公开，无 getter/setter），逐字段加锁会被拆成多次独立加锁、产生撕裂读/写。
- 定版：
  - **事务方法为主**：聚合数据（characters/settings/world_rules/chapters/outline）提供
    `getXxx`（读快照，共享锁）/`addXxx`/`updateXxx`/`removeXxx`（独占锁），每个方法内部锁住
    整个读-改-写，一次调用完成、无撕裂。
  - **withLock 受控守卫兜底**：`withWriteLock(fn)`/`withReadLock(fn)`，锁边界由 Project 声明，
    调用方传 lambda；跨聚合/非标准复合操作用它包住保证原子。
  - **禁止裸字段访问**：工具/GUI 一律走加锁方法或 withLock，禁止直接碰公开字段（P2）。
  - 工具代码（`BuiltInTool`）改造为调用 Project 加锁方法，是阶段 3 主要工作量。
- ☑ 已确认

**P4. `isReadOnly` 属性的字段与默认值 → 已定：放注册元数据，默认非只读（保守）**
- `ToolDefinition`（llm/Message.h）会被序列化发给 LLM，不能塞 is_readonly；工具注册元数据 `ToolEntry`（ToolRegistry.h）只有 name/category。
- 定版：
  - **`is_readonly` 放 `ToolEntry`（注册元数据）**，`bool is_readonly = false`；只做锁判断，不进
    `ToolDefinition`、不序列化给 LLM。
  - **默认非只读（false，保守）**：未标的一律走独占锁，只有显式标 `is_readonly` 的工具走共享锁。
    漏标只是"多等锁"（安全），不会"并发写冲突"（危险）。
  - `ToolRegistry::registerTool(...)` 加 `bool is_readonly = false` 参数；`BuiltInTool::registerAllTo`
    只读工具显式传 true。
  - 用途：工具级读写标记，供 ToolPipeline 批次判断（含写则独占）；与 P3 的 Project 方法级锁互补。
- ☑ 已确认

**P5. SessionRuntime 的构造参数/依赖注入方式 → 已定：核心依赖构造注入 + client 单独可替换**
- 阶段 1 说构造时 `factory.create()`，但完整参数未定。
- 定版（折中）：
  - **构造参数一次性注入核心依赖**（相对固定、runtime 生命周期内不变）：factory、registry、project、
    persistence、calibrator、共享装配配置（`SessionConfig` 结构体，含 prompt provider/model limit）、
    共享资源 vector_store/ltm_store/skill_registry。编译期保证 runtime 完整，避免 setter 漏设。
  - **client 单独可替换**（`setClient()`/`rebuildClient()`）：D6 的"空闲休眠释放 client、切回重建"
    只替换 client，不重造整个 runtime，其余核心依赖在构造时已固定。
  - 构造时即 `factory.create()` 建初始 client（按需物化时）。
- ☑ 已确认

**P6. 占位消息控制标记的实现 → 已定：Message 加 `bool is_control` 字段（方案 A）**
- D10 说"content 特殊前缀或 Message 控制字段"，未定。
- 定版（方案 A）：**Message 加 `bool is_control = false`**。取消占位消息设 `is_control = true`。
  - 目的：把"系统注入的占位消息"与"真实助手回复"区分开——UI 读到 `is_control` 时不当作普通回复
    显示，而是过滤或渲染成"已取消"提示，保持用户对话干净；历史层/导出也能据此排除。
  - 不用 content 前缀（用户/LLM 可能以同前缀开头，误判 + 前缀会显示）。
  - 与现有 `preserved` 字段同模式（Message.h 已有 preserved，风格一致）。
  - 连带：`serializeMessage`/`parseMessage`（SessionPersistence.cpp:19/54）加 `is_control` 序列化；
    UI（QmlBridge/QML）渲染时检查 `is_control`；完整历史层 `loadHistory` 同样可读。
- ☑ 已确认

**P7. switchSession 后端/前端分工 → 已定：纯前端焦点切换，不设 last_viewed**
- 多会话下 switchSession 不再清空/重载内存（SessionRuntime 各自独立），只是切「当前查看焦点」。
- 定版：**纯前端焦点切换**——后端 `switchSession(id)` 仅校验 id 存在（不写任何持久化字段），
  前端 `currentSessionId` 更新焦点（QML 过滤流信号、决定 busy/usage）。**不设后端 last_viewed**。
- 启动默认选中由前端"最近查看顺序"维护（B3/E4 修订）。
- ☑ 已确认

**P8. 会话物化时机 → 已定：用户点开才物化（懒，方案 B）**
- 历史会话是否启动即物化还是用户点开才物化？
- 定版（方案 B）：**用户点开才物化（懒）**——启动不建任何 runtime/client/内存；用户点开会话
  （或发消息）时才真正物化（建 runtime + client + 加载内容）。
  - 与 D6"按需物化 + 空闲休眠释放"同一原则。
  - 启动快、不浪费 client 连接（4 会话上限不被空会话占用）。
  - 前端 `currentSessionId` 初始化为"最近查看顺序"当前项（或列表第一项）；点开才物化。
- ☑ 已确认

**P9. 并发上限溢出体验 → 已定：拒绝（方案 B，报"并发已满"）**
- 4 个会话都在跑，第 5 个发消息：线程池排队，用户感知未定。
- 定版（方案 B）：**拒绝**——`process` 提交时若池已满（4 线程全忙），直接拒绝并返回
  `finish_reason = "concurrency_full"`（前端提示"当前并发已满，请稍后再试"），不排队。
  - 理由：实现简单；并发跑满 4 个会话的场景罕见，排队状态展示的复杂度不值得。
- 并发上限 4（线程池线程数）不受影响；排队≠并发，本方案不引入排队。
- ☑ 已确认

**P10. 测试补 session_id 的方式 → 已定：测试辅助返回会话 id（方案 C）**
- 15 处 process 调用补 session_id，测试怎么选 id？
- 定版（方案 C）：**测试文件里加 `newSession(agent)` 辅助**，返回新会话 id（`newSession()` +
  取 runtime 的 id）；15 处测试开头取 id 再 `agent.process(sid, "Hi")`。
  - Agent 加一个只读 `sessionId()` 访问器（返回当前 runtime 的 id）供测试取 id；不算污染，是合理
    只读访问器。
  - 不污染生产接口（helper 在测试文件里）、少重复。
- ☑ 已确认