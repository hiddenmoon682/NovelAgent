# 系统提示词所有权审查 — 2026-07-16

## 背景

系统提示词（System Prompt）在项目中存在三个副本，职责边界模糊，导致数据流混乱和潜在的不一致性。

## 现状

### 三个副本

| 位置 | 存储方式 | 用途 | 备注 |
|------|---------|------|------|
| `Agent::system_prompt_` | `std::string` | **权威源**，由构造函数/`setSystemPrompt()` 设置 | 创建 Processor 时值拷贝给 Processor |
| `SerialProcessor/ParallelProcessor::system_prompt_` | `std::string` | `buildEffectivePrompt()` 的输入源 | `Agent::setSystemPrompt()` 通过 `IMessageProcessor::setSystemPrompt()` 同步更新 |
| `Conversation::system_prompt_` | `std::string`，独立于 `messages_` | 通过 `setSystemPrompt()`/`addSystem()` 设置 | **基本未被 process 流程消费** |

### 当前数据流

```
Agent::setSystemPrompt(p)
    ├──→ Agent::system_prompt_ = p
    └──→ processor_->setSystemPrompt(p)    // 同步到 Processor

process() 时：
    Processor::buildEffectivePrompt()
        ├── Path A（无 ContextManager）:
        │     out_messages = conversation.messages()   // 不含 system
        │     return system_prompt_                     // 忽略 conversation.systemPrompt()
        │
        └── Path B（有 ContextManager）:
              assembly = context_manager_->assemble(...)
              out_messages = assembly.messages
              return PromptComposer::compose(system_prompt_, assembly.system_prompt)
              // 仍用 Processor 自己的 system_prompt_，忽略 conversation.systemPrompt()

    → loop.run(..., effective_prompt, ...)
        → client_.chat(messages, tools, effective_prompt, ...)
```

### 关键问题

1. **`Conversation::system_prompt_` 是死字段** — 它被 Conversation 类存储和管理，提供 `systemPrompt()` 访问器，但整条 process 链路从未读取它。
2. **Path A 潜在丢失对话级 system prompt** — 如果有人通过 `conversation.setSystemPrompt("新提示词")` 设置了系统提示词，`buildEffectivePrompt()` 的 Path A 会完全忽略它（只返回 `SerialProcessor::system_prompt_`）。
3. **三个副本同步成本** — `Agent::setSystemPrompt()` 需要手动调用 `processor_->setSystemPrompt()` 来同步，新增 Processor 类型时容易遗漏同步逻辑。
4. **注释与实现矛盾** — Conversation 的注释写 "System prompt 的实际来源是 Agent::system_prompt_" 说明它自己也承认不是权威源，那为什么还要提供 `setSystemPrompt()`/`systemPrompt()` 接口？

## 决议：由 Conversation 统一管理系统提示词

### 目标架构

```
Agent::setSystemPrompt(p)
    └──→ conversation.setSystemPrompt(p)     // 唯一副本存入 Conversation

process() 时：
    Processor::buildEffectivePrompt()
        ├── Path A:
        │     out_messages = conversation.messages()
        │     return conversation.systemPrompt()    // 从 Conversation 读取
        │
        └── Path B:
              assembly = context_manager_->assemble(conversation, ...)
              out_messages = assembly.messages
              return PromptComposer::compose(conversation.systemPrompt(), assembly.system_prompt)
              // ↑ 从 Conversation 读取

    → loop.run(..., effective_prompt, ...)
        → client_.chat(messages, tools, effective_prompt, ...)
```

### 需要修改的文件

| 文件 | 修改内容 |
|------|---------|
| `Agent.h` | 移除 `system_prompt_` 成员 |
| `Agent.cpp` | `setSystemPrompt()` 改为写入 `conversation_.setSystemPrompt(p)`；`useSerialProcessor()` 和 `useParallelProcessor()` 不再传 `system_prompt_` |
| `IMessageProcessor.h` | 移除 `SerialProcessor`/`ParallelProcessor` 的 `system_prompt_` 成员；移除 `setSystemPrompt()` 虚方法（或保留但改为透传给 Conversation） |
| `IMessageProcessor.cpp` | `buildEffectivePrompt()` 改为从 `conversation.systemPrompt()` 读取；`ParallelProcessor::process()` 同理；构造函数不再接收 `system_prompt` 参数 |
| 所有调用 `SerialProcessor(ctor, system_prompt)` 的地方 | 适配新构造函数签名 |

### 注意事项

1. **PromptComposer 的职责不变** — 仍负责将 personality（固定角色）和 context（动态上下文）拼接，只是 personality 的输入源从 Processor 成员变为 `conversation.systemPrompt()`。
2. **ContextManager::assemble() 的入参兼容性** — assemble() 内部可能读取 conversation 的 system prompt，统一后行为一致。
3. **序列化/反序列化** — 检查 `SessionPersistence` 是否依赖 `Agent::system_prompt_`，改为从 Conversation 存取。
4. **Processor 切换时的 system prompt 保持** — 当前 `setSystemPrompt()` 通过虚方法同步所有 Processor，统一后 Agent 只需管理 Conversation，切换 Processor 时自动继承。
