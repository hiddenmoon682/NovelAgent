# 工具注册缺失记录

**发现日期**: 2026-07-14

---

## 问题描述

以下 **4 个工具** 已定义并加入编译（`cmake/Sources.cmake` 第 93-96 行），但 **缺少 `REGISTER_TOOL` 宏调用**，导致它们在运行时不会被注册到 `BuiltInTool::factories()` 中，LLM 无法调用这些工具。

| # | 工具名 | 类名 | 文件 |
|---|--------|------|------|
| 1 | `get_chapter_context` | `GetChapterContextTool` | `ChapterContextTools.h/.cpp` |
| 2 | `get_relevant_characters` | `GetRelevantCharactersTool` | `RelevantCharacterTools.h/.cpp` |
| 3 | `get_relevant_settings` | `GetRelevantSettingsTool` | `RelevantSettingTools.h/.cpp` |
| 4 | `get_relevant_world_rules` | `GetRelevantWorldRulesTool` | `RelevantWorldRuleTools.h/.cpp` |

## 影响

- 这 4 个工具都在 `PromptContextBuilder.cpp` 第 242-245、256-258 行被作为**可调用工具**列举给 LLM
- 也在 `NovelAgentApp.cpp` 第 51 行的欢迎信息中被提及
- 但实际运行时均不可用——LLM 调用时会收到"工具未找到"错误
- 属于**静默失效**：提示词中指导 LLM 使用这些工具，但 LLM 永远无法成功调用

## 对比正常注册的工具

所有其他工具在 `.cpp` 文件末尾都有注册宏，例如：

```cpp
// ChapterTools.cpp
REGISTER_TOOL(agent::ReadChapterTool, "read_chapter", read_chapter)

// OutlineTools.cpp
REGISTER_TOOL(agent::GetOutlineTool, "get_outline", get_outline)
```

而这 4 个 `.cpp` 文件末尾均缺失对应的注册宏。

## 修复方式

分别在各 `.cpp` 文件末尾（`namespace agent { ... }` 闭合后）添加：

**`ChapterContextTools.cpp`**:
```cpp
REGISTER_TOOL(agent::GetChapterContextTool, "get_chapter_context", get_chapter_context)
```

**`RelevantCharacterTools.cpp`**:
```cpp
REGISTER_TOOL(agent::GetRelevantCharactersTool, "get_relevant_characters", get_relevant_characters)
```

**`RelevantSettingTools.cpp`**:
```cpp
REGISTER_TOOL(agent::GetRelevantSettingsTool, "get_relevant_settings", get_relevant_settings)
```

**`RelevantWorldRuleTools.cpp`**:
```cpp
REGISTER_TOOL(agent::GetRelevantWorldRulesTool, "get_relevant_world_rules", get_relevant_world_rules)
```

## 状态

- [ ] 待修复（4 个工具）
