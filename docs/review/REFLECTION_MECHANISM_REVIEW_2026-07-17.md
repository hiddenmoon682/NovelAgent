# 反思（Reflection）机制名不副实问题 — 2026-07-17

## 问题

`ToolCallLoop` 中的 CRIT-2 反思机制**只保留了"形式"，没有实际的分析能力**。它仅仅向对话注入一条模板化的中文提醒，然后重新调一次 LLM，期望 LLM 自行纠错——没有任何实质性的上下文分析或错误诊断。

## 当前的实现

```cpp
// 反思路径 — 检测到重复且反思未耗尽
if (has_repeated) {
    if (reflection_rounds_ < config.max_reflection_rounds) {
        ++reflection_rounds_;

        llm::Message reflection;
        reflection.role = llm::MessageRole::Assistant;
        reflection.content = buildReflectionPrompt(
            repeated_tool_name, repeated_args, reflection_rounds_);
        conversation.add(std::move(reflection));

        // 跳过工具执行，直接调 LLM
        response = client_.chat(conversation.messages(), tools, system_prompt, callbacks);
        // ... token 统计 ...
        continue;
    }

    // 反思耗尽 → 终止
    r.loop_detected = true;
    // ... 返回错误 ...
    return r;
}
```

## 它做了 vs 没做什么

| 动作 | 状态 |
|------|------|
| 检测重复调用 | ✅ 有（`isRepeatedCall`） |
| 注入提示消息 | ✅ 有（`buildReflectionPrompt` 模板） |
| 重新调 LLM | ✅ 有 |
| **分析工具执行结果** | ❌ 跳过 `pipeline.execute()`，根本不知道工具返回了什么 |
| **检查对话历史** | ❌ 没有上下文推导 |
| **提供具体修正方向** | ❌ 模板消息泛泛而谈（"可能是参数不对/数据已存在"），不基于实际错误 |
| **对比前后轮次参数差异** | ❌ 只检查 `tool_name + args_json` 精确匹配，忽略了语义等价的不同参数 |

## 真正有效的反思应该做什么

一个可用的自修正机制至少应包括：

1. **检查工具返回结果** — 上一轮调用是成功还是失败？错误信息是什么？
2. **定位根因** — 是参数无效、数据已存在、还是 LLM 理解错了工具用途？
3. **提供具体修正指导** — "write_chapter 返回了'章节已存在'，下一次应该先调用 read_chapter 查看现有内容"
4. **选择性重试** — 只重试有修复可能的那次调用，而不是无差别跳过所有工具后重调 LLM

## 相关文档

- **重复调用导致的编码冗余**：详见 `TOOL_CALL_LOOP_DUPLICATE_CALL_REVIEW_2026-07-17.md`（反思路径中的 `client_.chat()` 与首轮/循环底部重复的问题）
- **反思相关内容已从该文档剥离至此文档**

## 解决方案方案

**不修复，直接删除整个反思机制。**

原因：目前的反思机制本质上是占位代码，没有实际的分析/纠错能力。如果真的出现工具重复调用、反复循环的情况，计划使用其他方法解决（例如更好的 prompt 设计、更合理的工具定义、或外层 Agent 的监督机制），而不是在 ToolCallLoop 内部维护一个名不副实的自修正模块。

### 需要删除的内容

- `ToolCallLoop::reflection_rounds_` 成员变量
- `ToolCallLoop::buildReflectionPrompt()` 方法
- `ToolCallLoopConfig::max_reflection_rounds` 配置项
- `ToolCallLoop::run()` 中的 `has_repeated` 检测 → 反思路径分支
- 所有 `// CRIT-2:` 相关注释
- `isRepeatedCall()` 方法仍然保留（重复检测本身有用，用于 `loop_detected` 终止）
