# 设计审查误判与夸大记录

> "AI 审代码，看着像问题，其实不是问题。"

审查工具（subagent 逐文件通读）在生成 `DESIGN_REVIEW_CODE.md` 时，由于缺乏运行时行为推演和跨文件调用链追踪，产生了若干**论述偏差**和**过度诊断**。本文档记录这些误判，供后续审查校准参考。

---

## 1. 问题 11：`rewindTo()` 后 `pinnedIndices()` 索引不准确

### 审查报告原文

> "`pinnedIndices()` 返回的索引在回滚后就不再准确（因为列表长度变了，但 preserved 标记还在被保留的消息上）。"

### 实际情况

**此论断不成立。** `pinnedIndices()` 的实现是：

```cpp
// Conversation.h:146-152
std::vector<size_t> pinnedIndices() const {
    std::vector<size_t> result;
    for (size_t i = 0; i < messages_.size(); ++i) {  // 遍历当前消息列表
        if (messages_[i].preserved) result.push_back(i);
    }
    return result;
}
```

每次调用都从**当前** `messages_` 重新计算索引，不存在"过期索引"问题。`truncateTo()` 缩短列表后，下次 `pinnedIndices()` 返回的是新列表中的位置——完全自洽。

### 底层关切（有效）

回滚到 pinned 消息的索引之前时，该消息被静默截断——这一点属实。已在 `rewindTo()` 中添加 warn 日志提示用户。

### 教训

审查时看到"索引"就条件反射想到"过期/悬空"，未读实际实现即下定论。应先 grep 函数体再判断。

---

## 2. 问题 17：错误消息中英混用

### 审查报告原文

> "工具返回给 LLM 的 error 消息...混用意味着 LLM 可能看到'工具 xxx 不存在'（中文）同时看到 'ToolCallLoop timeout'（英文），体验不一致。"

### 实际情况

**程度被严重夸大。** grep 全量 `"error"` 消息后，仅发现 **1 处**英文技术词混入：

```cpp
// ToolCallLoop.cpp:178（修复前）
result.error = "Tool call 循环超时 (" + std::to_string(config.timeout.count()) + "s)";
```

其余 30+ 处错误消息全部是**纯中文**。此处 "Tool call" 是内部技术术语，LLM 完全能理解，且出现在面向开发者的日志级别，非面向 LLM 的工具返回内容。

### 教训

从一个样本泛化到全局。审查时看到一处就推断"到处都是"，未做定量 grep。

---

## 3. 问题 21：ToolCallLoop 超时后的"悬挂线程风险"

### 审查报告原文

> "超时后直接 return result，future 对象析构...异步任务仍在后台运行，可能继续修改 conversation_。可能产生数据竞争（如果调用方立即开始下一轮对话）。"

### 实际情况

**数据竞争论断不成立。** `std::async(std::launch::async)` 返回的 `std::future` 在析构时**会阻塞等待**异步任务完成（C++ 标准 [futures.async]/5 保证）。调用链是：

```
ToolCallLoop::run()
  ├─ future.wait_for(timeout) → timeout
  ├─ result.timed_out = true
  ├─ return result;           // ← 此时 future 析构，阻塞等待 async 任务退出
  └─ (调用方收到 result 时，async 任务已完全退出)
```

所以不存在"调用方收到 result 时 async 任务还在跑"的情况，更不存在数据竞争。

### 底层关切（有效但不同）

真正的架构问题是：
1. **取消机制缺失**：超时后虽阻塞等待，但没有通知 async 任务"尽快退出"，导致在 HTTP 调用中途仍需等 `read_timeout`（180s）
2. **假超时**：`future.wait_for(timeout)` 触发后，实际返回给调用方的时间 = timeout + 剩余 HTTP 调用时长

Issue 21/26 的修复（`cancelled_` 穿透）解决的是问题 1，问题 4 的修复（`ThreadPool`）解决的是问题 2 的线程复用。

### 教训

审查时看到 `std::async` + `timeout` + `return` 的组合，想当然认为存在 use-after-free / data race，忽略了 C++ 标准对 `std::future` 析构行为的保证。

---

## 4. 问题 23：FileStorageBackend 路径语义不一致

### 审查报告原文

> "`loadJson("conversation.json")` → 尝试读取当前工作目录下的 conversation.json（错误）。调用方必须知道哪些方法需要绝对路径、哪些需要相对路径，否则产生 bug。"

### 实际情况

**设计不一致存在，但 bug 不成立。** 所有调用方都通过 `agentDir()` 构造绝对路径：

```cpp
// ContextManager::saveSessionState() → persistence_.save(conv)
// → SessionPersistence::save() → storage_.saveJson(agentDir() + "/conversation.json", ...)
```

`agentDir()` 调用 `ProjectIO::agentDir(project_path_)` 返回绝对路径（如 `/home/user/novel/.novelagent`），因此传给 `saveJson` 的始终是绝对路径。**不存在任何以相对路径调用 `saveJson/loadJson` 的实际代码路径。**

### 修复

虽然无实际 bug，但仍统一了路径解析规则（相对路径自动以 `project_path_` 为基准），作为防御性编程改进。

### 教训

审查时看到了"A 方法用绝对路径、B 方法用相对路径"的设计不一致，就推断"一定有调用方会搞错"——但未追踪实际调用链验证。设计不一致 ≠ 运行时 bug。

---

## 5. 问题 8：`Conversation::systemPrompt()` 与 `Agent::system_prompt_` 双重管理

### 审查报告原文

> "`Conversation::addSystem()` 添加的消息实际上永远不会被发送给 LLM。两个 system prompt 源共存，语义模糊。"

### 实际情况

**不是 bug，是有意为之的职责分离：**

| 位置 | 用途 | 消费者 |
|------|------|--------|
| `Conversation::systemPrompt()` | 只读提取器，供调试/日志查询 | 人类开发者 |
| `Agent::system_prompt_` | 实际 LLM system prompt 来源 | `LLMClient::chat()` |

`Conversation::messages()` **显式过滤** System 角色消息（因为 OpenAI API 要求 system prompt 单独通过参数传递）。这不是 bug——这是适配 OpenAI API 协议的正确实现。

`Conversation::addSystem()` 的存在是为了**完整的对话序列化/反序列化**（保存到 `conversation.json` 时需要保留 system 消息以供调试回溯）。发送给 LLM 时不使用它，但持久化时需要它。

### 教训

审查时看到"两个地方都有 system prompt"就认为是重复/冲突，未理解它们服务于不同的消费者（LLM vs 持久化/调试）。

---

## 6. 问题 9：流式回调仅首轮生效

### 审查报告原文

> "后续工具调用的 LLM 响应用户完全看不到。"

### 实际情况

**是设计决策，不是缺陷。** `all_rounds_streaming = false` 是默认值，因为：

1. 中间轮次的 LLM 输出通常是工具调用决策（如"我需要调用 read_chapter"），用户不关心
2. SSE 长连接在中间轮次断开的概率更高（DeepSeek API 超时）
3. 首轮 + 末轮已经有流式输出覆盖关键信息

用户如果需要中间轮次可见，设置 `all_rounds_streaming = true` 即可。

### 教训

审查时看到"有功能开关但默认关闭"就认为"功能缺失"，未分析默认值的设计意图。

---

## ~~7. 问题 20：ListChaptersTool 排序不稳定~~（已移除）

> 已被 Issue 20 修复前的 commit（A17 fix）解决。审查时未读取最新代码即下定论。此条在报告最终版中已移除，此处记录以示审查过程透明。

---

## 总结

| 问题 | 类型 | 误判原因 |
|------|------|----------|
| 11 | 论述偏差 | 看到"索引"条件反射为"悬空"，未读实现 |
| 17 | 程度夸大 | 从 1 个样本泛化到全局，未定量 grep |
| 21 | 论述偏差 | 忽略 C++ 标准对 `std::future` 析构的阻塞保证 |
| 23 | 设计≠bug | 设计不一致存在但无实际调用路径触发 |
| 8 | 误读设计 | 未理解职责分离（LLM 消费 vs 持久化消费） |
| 9 | 设计决策 | 看到开关默认关闭就认为功能缺失 |
| 20 | 代码过期 | 未读最新代码即下定论 |

**模式**：AI 审查倾向于 **高估风险、夸大程度、泛化样本、忽略设计意图**。人类审查者应：
- 对每个"严重"问题要求 grep/追踪调用链证据
- 区分"设计不一致"和"运行时 bug"
- 区分"功能开关默认关闭"和"功能缺失"
- 对定性判断（"混用""不稳定"）要求量化数据
