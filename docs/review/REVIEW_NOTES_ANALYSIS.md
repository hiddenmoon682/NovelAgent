# REVIEW_NOTES 问题分析与解决方案

## Context

`docs/review/REVIEW_NOTES.md` 提出了 2 个待议设计问题：
1. Token 预算分配策略（50/30/20）可能过度设计
2. `context_window` 参数命名歧义

经过对全部相关代码的深入分析（ContextManager、DegradationPipeline、ChapterSummaryCache、ExecutionTracer、TuiStatusBar、Agent、SerialProcessor、AppConfig），以下是对每个问题的判断和推荐方案。

---

## 问题 1：Token 预算分配策略 — 判断：**审查意见基本正确**

### 代码验证结果

| 审查指出的问题 | 验证结果 | 证据 |
|---|---|---|
| 50/30/20 硬编码，无数据支撑 | ✅ 确认 | `ContextManager.cpp:25-27` 匿名命名空间常量，无配置入口 |
| 子预算实际未约束行为 | ✅ 确认 | `conversation_budget`/`summary_budget` 在 `assemble()` 中从未被读取；实际只用 `total_budget - sys_tokens` |
| 并非真正的会话级管理 | ✅ 确认 | 每次 `assemble()` 从零开始计算，无跨调用 token 消耗追踪 |
| ExecutionTracer 数据不回写 | ✅ 确认 | `ContextManager` 无 `ExecutionTracer` 引用 |
| ChapterSummaryCache 从未被读回 | ✅ 确认 | `getChapterSummary()`/`loadAllSummaries()` 在全部 `src/` 中零调用 |
| 降级策略几乎不触发 | ✅ 部分确认 | 默认 65536 窗口下，需极长对话或巨大 system prompt 才触发 |
| 本质等价于 `while(total>limit) pop_front()` | ✅ 确认 | `truncateMessages()` 就是从最新消息反向贪心保留 |

### 额外发现

- **`BudgetAllocation::degradation_level` 字段是废字段**：`allocateBudget()` 从不设置它（恒为 0），实际降级级别存在 `ContextAssembly` 上
- **L4 (TruncateConv) 策略是无操作**：`apply()` 原样返回 system prompt，注释说"实际截断在 truncateMessages 中处理"，但 `truncateMessages` 不受降级级别影响
- **摘要预算从未被检查**：`summary_budget` 算出后从未用于限制摘要大小

### 解决方案：简化为 `max_tokens + 尾部截断`

**推荐方案**：不再在输入侧做复杂的预算分配和降级，改为简单模型：

1. **移除组件**：
   - `BudgetAllocation` 结构体（`ContextManagerTypes.h:55-61`）
   - `allocateBudget()` 方法（`ContextManager.cpp:200-208`）
   - `calculateBudget()` 方法（`ContextManager.cpp:183-186`，仅被 `allocateBudget` 调用）
   - `DegradationPipeline` 整个类（头文件 + 实现 + 5 个策略类，~200 行）
   - `ChapterSummaryCache` 整个类（头文件 + 实现，~100 行，完全死代码）
   - `ConversationSummarizer` 类（`assemble()` 中的摘要逻辑，~150 行，基于规则而非 LLM，效果存疑）

2. **简化 `assemble()` 逻辑**：
   ```
   assemble(conversation, max_tokens, project, chapter_id):
       1. 构建 system prompt（保留现有 PromptContextBuilder）
       2. 计算 sys_tokens
       3. msg_budget = max(0, max_tokens - sys_tokens)
       4. 从最新消息反向截断到 msg_budget
       5. 返回 ContextAssembly
   ```
   移除的步骤：预算分配、降级触发/执行、对话摘要生成/注入

3. **简化 `ContextAssembly`**：移除 `budget`、`degradation_level` 字段，仅保留 `messages`、`system_prompt`、`total_tokens`、`truncated`、`truncated_count`

4. **保留项**：
   - `truncateMessages()` 方法（核心逻辑，从最新消息反向保留）
   - `buildSystemPrompt()` / `PromptContextBuilder`（有价值的功能）
   - `SessionPersistence`（会话持久化，独立功能）

### 风险评估

- **API 成本**：移除预算上限后，用户需自行管理 `max_tokens`。建议保留 `max_tokens` 参数并设合理默认值（如 128K），允许用户通过 `/config` 调整
- **长对话**：尾部截断对于写小说场景是合理的——最近对话最重要，旧内容可通过 project 结构（大纲/角色）保留在 system prompt 中
- **测试更新**：`test_context_manager.cpp` 中涉及 `allocateBudget`、`DegradationPipeline`、摘要的测试需要重写

---

## 问题 2：`context_window` 参数命名歧义 — 判断：**审查意见正确**

### 代码验证结果

| 审查指出的问题 | 验证结果 | 证据 |
|---|---|---|
| 命名易误解为模型窗口大小 | ✅ 确认 | `AppConfig.h:19` 注释写"模型支持的最大上下文窗口"，但实际是应用层预算上限 |
| 默认 64K 无依据 | ✅ 确认 | 3 处独立硬编码 65536（AppConfig/Agent/IMessageProcessor） |
| 所有调用方共享同一值 | ✅ 确认 | 无模型自适应机制 |
| 无代码查询模型真实窗口 | ✅ 确认 | `ILLMClient` 无模型能力查询接口 |

### 额外发现

- **运行时更新 bug**：`/config context_window <value>` 只更新 `Agent::context_window_`，不会传播到已构造的 `SerialProcessor::context_window_`（在 `useSerialProcessor()` 时拷贝的），导致配置修改不生效

### 解决方案：重命名 + 修复传播

1. **重命名**：`context_window` → `max_tokens`
   - `AppConfig.h:19`：`int max_tokens = 131072;`（改为 128K 默认，更新注释为"每次请求发送给 LLM 的最大 token 数"）
   - `Agent.h:95`：`int max_tokens_ = 131072;`
   - `IMessageProcessor.h:85`：`int max_tokens_ = 131072;`
   - `ContextManager::assemble()` 参数名
   - 所有调用方和测试

2. **修复传播 bug**：将 `max_tokens` 改为从 `Agent` 动态读取，而非在 `useSerialProcessor()` 时拷贝
   - 方案 A：`SerialProcessor` 持有 `Agent*` 指针，实时读取 `max_tokens_`
   - 方案 B：在 `/config max_tokens` 时同步更新 `SerialProcessor`
   - **推荐方案 B**：改动最小，不引入反向依赖

3. **更新注释**：明确语义为"应用层每次请求的 token 预算上限"，非"模型上下文窗口大小"

---

## 实施计划

### 第一步：简化 ContextManager（问题 1）

**修改文件**：
- `src/agent/ContextManager.h` — 移除 `allocateBudget()`/`calculateBudget()` 声明、`ConversationSummarizer`/`ChapterSummaryCache`/`DegradationPipeline` 成员
- `src/agent/ContextManager.cpp` — 重写 `assemble()` 为简化版，移除匿名命名空间常量
- `src/agent/ContextManagerTypes.h` — 移除 `BudgetAllocation`，简化 `ContextAssembly`
- `src/agent/ContextManagerTypes.cpp` — 如有实现则更新

**删除文件**：
- `src/agent/DegradationPipeline.h`
- `src/agent/DegradationPipeline.cpp`
- `src/agent/ConversationSummarizer.h`
- `src/agent/ConversationSummarizer.cpp`
- `src/agent/ChapterSummaryCache.h`
- `src/agent/ChapterSummaryCache.cpp`

**更新 CMake**：
- `cmake/Sources.cmake:68-70` — 移除 3 行（ConversationSummarizer、ChapterSummaryCache、DegradationPipeline）

**更新测试**：
- `tests/test_context_manager.cpp` — 重写测试覆盖简化后的 `assemble()`

### 第二步：重命名 context_window → max_tokens（问题 2）

**修改文件**（约 10 个文件）：
- `src/config/AppConfig.h:19` — 字段名 + 默认值 + 注释
- `src/agent/Agent.h:95` — 成员名 + 注释
- `src/agent/Agent.cpp:31,37,173` — setter/getter/使用处
- `src/agent/IMessageProcessor.h:58,85` — 成员名 + setter
- `src/agent/IMessageProcessor.cpp:181` — 使用处
- `src/agent/ContextManager.h:48-52` — 参数名
- `src/agent/ContextManager.cpp:68` — 参数名
- `src/NovelAgentApp.cpp:49` — 调用处
- `src/cli/ReplHandler.cpp:102,105,111-115,160` — CLI 显示和设置
- `tests/test_context_manager.cpp` — 测试参数
- `tests/test_agent.cpp` — 测试 fixture
- `tests/test_llm_client.cpp` — 测试 fixture

### 第三步：修复配置传播 bug

**修改文件**：
- `src/cli/ReplHandler.cpp:111-115` — `/config max_tokens` 命令同时更新 `Agent` 和当前 `SerialProcessor`
- 或通过 `Agent` 添加 `syncContextWindow()` 方法统一同步

### 第四步：编译验证 + 测试

```bash
cmake --build build --target novelagent_lib
cmake --build build --target test_context_manager
./build/tests/test_context_manager
ctest --test-dir build
```

---

## 影响评估

| 维度 | 影响 |
|---|---|
| 删除代码量 | ~500 行（6 个文件删除 + ContextManager 简化） |
| 修改代码量 | ~50 行（重命名 + assemble 重写） |
| 破坏性变更 | `ContextAssembly` 字段移除（`budget`、`degradation_level`），需检查所有消费者 |
| 功能保留 | system prompt 构建、消息截断、会话持久化均保留 |
| 测试影响 | `test_context_manager.cpp` 需重写，其他测试仅重命名 |
