# 待修复问题

> 创建时间: 2026-05-28
> 用途: 记录代码审查中发现的、尚未修复的问题
>
> 已修复的问题请记录在 `RESOLVED.md` 中。

---

> 以下为 2026-06-09 Phase 3.1-3.4 代码审查发现的问题。
>
> **状态**: 11 个问题中 9 个已修复（2026-06-09），2 个暂缓（#5 #10）。
>
> ---
>
> 以下为 2026-06-09 Phase 3.6-3.12 代码审查发现的问题。

## 12. [严重] ShellTools — PowerShell 命令注入漏洞

**文件**: `src/agent/tools/ShellTools.cpp`

**问题**: LLM 传入的 `command` 参数直接拼接到 `powershell.exe -NoProfile -Command` 中执行，完全没有校验。LLM 可能被 prompt injection 攻击诱导执行危险系统命令。

**修复**: 添加 `isDangerousCommand()` 黑名单（24 个危险关键词）+ 输出 100KB 截断。✅ 已修复

## 13. [中等] ShellTools — 无超时控制

**问题**: `_popen` 无超时。挂起命令（如 `while($true){}`）会导致进程永久阻塞。

**决定**: 暂缓。`_popen` 不直接支持超时，需改用 `CreateProcess` + `WaitForSingleObject`。当前通过 Agent 的 tool call 间接限流。

## 14. [轻微] CommandParser — 命令名大小写敏感

**文件**: `src/cli/CommandParser.cpp`

**问题**: `/Help` 和 `/help` 不视为同一命令。

**修复**: 命令名转小写后比较。✅ 已修复

## 15. [轻微] ShellTools — 输出无大小限制

**问题**: 命令输出可能无限增长（如 `dir C:\Windows\System32`）。

**修复**: 添加 100KB 输出截断。✅ 已修复

---

## 1. [严重] ContextManager 的 system_prompt 永久覆盖用户设置的 system_prompt_

**文件**: `src/agent/Agent.cpp` — `runToolLoop()`

**问题**: 在 tool call 循环中，ContextManager 产出的上下文 system prompt（项目设定、章节信息）通过 `system_prompt_ = assembly.system_prompt` 直接覆盖了成员变量。用户通过 `setSystemPrompt()` 设置的「AI 人格提示词」（如"你是一个网文写作助手"）被永久替换，第一次调用后即丢失。

此外，ContextManager 的上下文 prompt 被存入成员变量后，会在后续不相关的对话轮次中继续使用（除非重新调用 `setSystemPrompt()` 恢复），造成上下文污染。

**根本原因**: 两类 system prompt 语义不同——「人格提示词」描述 AI 行为方式，「上下文提示词」描述项目信息——但当前把它们存入了同一个变量、且后者覆盖前者。

**建议**:
```cpp
// runToolLoop() 中改为局部变量组合，不修改成员
std::string effective_prompt = system_prompt_;
if (context_manager_) {
    auto assembly = context_manager_->assemble(...);
    if (!assembly.system_prompt.empty()) {
        effective_prompt = system_prompt_ + "\n\n" + assembly.system_prompt;
    }
}
// 所有 LLM 调用使用 effective_prompt
```
**影响范围**: 所有启用了 ContextManager 的场景，每次 LLM 调用的 system prompt 都可能不正确。

**严重度**: 🔴 严重

---

## 2. [严重] truncateMessages() token 减法公式与初始估值不一致

**文件**: `src/agent/ContextManager.cpp` — `truncateMessages()`

**问题**: 截断循环中逐条减去 token 时，只统计了 `content` 字段 + 硬编码的 `4`（结构开销），但 `total` 的初始值来自 `TokenCounter::countMessages()`，后者统计了完整的消息 token（含 role 标记、tool_calls 结构、name 字段等）。

两条公式的差值会累积：如果被移除的消息中有携带 `tool_calls` 的 assistant 消息（每次工具调用循环都有），`total` 下降量少于实际消耗量，可能导致截断不够、总 token 仍超出预算。

另一方面，`tool_calls` 的 `arguments` 可能很长（如 `write_chapter` 的全文内容），扣除 4 token 完全无法代表真实开销。

**建议**: 每次移除后重新计算，避免手工同步两套公式：
```cpp
while (!result.empty() && llm::TokenCounter::countMessages(result) > budget) {
    result.erase(result.begin());
    ++truncated_count;
}
```
如果担心性能，可以在循环外层加一个最大迭代次数的保护。

**影响范围**: tool call 循环场景（assistant 消息含 tool_calls 时），截断准确性下降。

**严重度**: 🔴 严重

---

## 3. [严重] buildSystemPrompt() 对不存在的章节返回空字符串

**文件**: `src/agent/ContextManager.cpp` — `buildSystemPrompt()`

**问题**: 当传入一个不存在或无效的 `chapter_id` 时，`PromptContextBuilder::buildForChapter()` 返回 `std::nullopt`，当前代码直接 `return {}`（空字符串）。对比之下，不传 `chapter_id` 的路径会返回最小化的项目概述（标题、logline、主题）。

这意味着：传错误章节 ID 比不传章节 ID 更糟糕——前者 LLM 完全没有项目上下文，后者至少能知道项目名称和主题。

**建议**: `buildForChapter` 失败时 fallback 到无章节版本：
```cpp
if (!ctx) {
    spdlog::warn("[ContextManager] 无法为章节 '{}' 构建上下文，回退到项目概述", chapter_id);
    return buildSystemPrompt(project);  // 无章节版本，至少返回项目基本信息
}
```

**影响范围**: 当 LLM 调用工具时传入了已删除或拼写错误的章节 ID。

**严重度**: 🔴 严重

---

## 4. [中等] truncateMessages() 在 budget ≤ 0 时返回全部消息

**文件**: `src/agent/ContextManager.cpp` — `truncateMessages()`

**问题**: 当 `budget <= 0` 时（极端情况：context_window 极小，或 system_prompt 已用尽全部预算），当前代码直接返回全部消息列表。这些消息无法被 LLM 容纳，应返回空列表。

```cpp
// 当前代码
if (messages.empty() || budget <= 0) {
    return messages;  // ← 预算为 0 时不应返回消息
}
```

**建议**:
```cpp
if (messages.empty()) return messages;
if (budget <= 0) {
    truncated_count = static_cast<int>(messages.size());
    return {};
}
```

**影响范围**: 极端小窗口场景（如用 context_window=1024 的廉价模型做简单查询）。

**严重度**: 🟡 中等

---

## 5. [中等] ChapterTools 持有 Project& 裸引用 — 悬空引用风险

**文件**: `src/agent/tools/ChapterTools.h` — 全部 5 个工具类

**问题**: 所有 Chapter 工具在构造时接收 `Project&` 引用并存储为成员。如果 Project 对象被销毁、移动或用户切换项目，但 ToolRegistry 中的工具实例仍然存活（ToolRegistry 持有 `unique_ptr<BuiltInTool>`），访问 `project_` 就是 use-after-free。

当前使用场景中 tool 和 project 生命周期一致，不会触发，但这是隐式约定而非强制约束。随着 Phase 3.5 多 Agent 并行编排的引入，sub-agent 可能持有对已释放 Project 的引用。

**建议**:
- **短期**: 在 `BuiltInTool` 文档中明确标出生命周期约束："工具实例的生命周期不得长于构造时传入的外部引用"
- **长期**: 改为 `std::shared_ptr<Project>`，或让工具的 `execute()` 方法接收 project 引用参数而非在构造时持有

**影响范围**: 未来多 Agent 场景、项目切换场景。当前无实际触发路径。

**严重度**: 🟡 中等

---

## 6. [中等] CreateChapterTool 部分保存与全量保存的不一致风险

**文件**: `src/agent/tools/ChapterTools.cpp` — `CreateChapterTool::execute()`

**问题**: `CreateChapterTool` 只调用 `ProjectIO::saveJsonFile()` 保存 `outline.json`，而非 `ProjectIO::save()` 保存全部 6 个 JSON 文件。这是一个刻意的性能优化（避免覆盖用户对 characters/settings 等文件的并行修改）。

但存在隐式风险：如果内存中 `characters.json`、`settings.json` 等有尚未落盘的修改，后续某处调用 `ProjectIO::save()` 时，会用内存中的旧状态覆盖 `outline.json`，导致新创建的章节数据丢失。当前代码中不会有"未落盘修改"（工具都直接操作 project 内存 + 文件），但这是脆弱的隐式保证。

**建议**: 
- 方案 A: `ProjectIO` 新增 `saveOutline()` 方法，内部只在 `outline.json` 上加文件锁，与其他 JSON 文件独立
- 方案 B: 始终调用 `ProjectIO::save()` 全量保存（当前的瓶颈在 I/O 上是可忽略的）

**影响范围**: 如果未来某处代码对 project 做内存修改后不立即落盘，再调用 CreateChapter → 后续 save 时可能丢数据。

**严重度**: 🟡 中等

---

## 7. [中等] 工具调用结果无大小限制，可能导致单条消息超出 token 预算

**文件**: `src/agent/Agent.cpp` — `executeToolCallsAndAppend()`

**问题**: 工具执行结果直接 `result.dump()` 后完整加入对话历史。如果 `read_chapter` 读取了一个 10000+ 字的章节，或 `list_chapters` 有 200 个章节，单条 tool result 消息可能就有数万字符。即使 ContextManager 做截断，单条巨大消息也可能导致内存浪费和 LLM 处理困难。

另外，LLM 在后续轮次中不需要看到完整章节原文——它需要的是"我读了第 X 章，内容是……"的确认信息。

**建议**: 在结果加入对话前做截断：
```cpp
constexpr size_t kMaxToolResultChars = 4000;
std::string result_str = result.dump();
if (result_str.size() > kMaxResultChars) {
    result_str = result_str.substr(0, kMaxResultChars) + "\n...(已截断)";
}
conversation_.addToolResult(tc.id, result_str);
```

**影响范围**: 章节读写场景，尤其是长章节的 `read_chapter` 调用。

**严重度**: 🟡 中等

---

## 8. [轻微] 空的 try/catch 块 — 捕获后立即重新抛出

**文件**: `src/agent/Agent.cpp:51-56`

**问题**: `processUserMessage()` 中对 `runToolLoop()` 的调用被 `try { ... } catch (...) { throw; }` 包裹，这个 try/catch 块不做任何日志或清理，直接重新抛出，等价于不存在。

**建议**: 删除该 try/catch 块，或在 catch 中做有意义的操作（如 spdlog::error 记录异常信息）。

**严重度**: 🟢 轻微

---

## 9. [轻微] ListChaptersTool 描述与实际行为不一致

**文件**: `src/agent/tools/ChapterTools.h:67`

**问题**: 工具描述声称返回"ID、标题、顺序和字数"，但实现中不包含 `word_count` 字段。这是 CHANGELOG 中记录的刻意优化（避免 100 章时 100 次磁盘 I/O），但工具描述未同步更新。LLM 读取描述后可能会在回复中声称"第 X 章有 Y 字"，但实际上它没有这个数据。

**建议**: 更新 `description()` 为"列出当前项目所有章节的 ID、标题、顺序和摘要"，或添加不读文件的估算字数（如基于 title/synopsis 长度估算）。

**严重度**: 🟢 轻微

---

## 10. [轻微] SchemaUtils::object() 全局硬编码 additionalProperties: false

**文件**: `src/utils/SchemaUtils.h:41`

**问题**: `additionalProperties: false` 作为安全默认是好的，但不是所有 LLM function calling 实现都严格遵守——某些模型偶尔在参数中附加额外字段。如果遇到这种情况，API 端参数校验可能拒绝请求。

**建议**: 当前不做修改（保留此安全默认），但在注释中记录此设计决策。如果后续遇到兼容性问题，可通过 `SchemaUtils::object(..., bool allowExtra = false)` 添加可选参数。

**严重度**: 🟢 轻微

---

## 11. [轻微] ContextAssembly::total_tokens 精度说明缺失

**文件**: `src/agent/ContextManager.h:23`

**问题**: `total_tokens` 字段注释为"实际占用的 token 数"，但实际上 `TokenCounter` 是启发式估算（中文 × 0.75 + 英文 × 1.3），不是精确值。所有消费此字段的代码都应意识到这是估值。

**建议**: 更新注释为 `"实际占用的 token 数（估算值，非精确计数）"`。

**严重度**: 🟢 轻微

---

> 以下为 2026-06-09 Phase 3 完整审查（3.5-3.12）发现的新问题。

## 16. [严重] ShellTools 黑名单误拦截 `|` `>` `>>` `;` `&&` `||` — 管道和重定向全部不可用

**文件**: `src/agent/tools/ShellTools.cpp:23-41`

**问题**: 黑名单中包含了管道符 `|`、重定向 `>` `>>`、分隔符 `;` `&&` `||`。这些字符在 PowerShell 中极为常用：

- `Get-ChildItem | Where-Object Name -like "*.md"` → 被 `|` 拦截
- `Get-Process | Select-Object -First 5` → 被 `|` 拦截
- `$x = 3; $y = 5` → 被 `;` 拦截

这导致工具**几乎无法用于任何有用的 PowerShell 操作**。`RunPowerShellTool` 本质上变成了只能执行单个 cmdlet 的受限工具，LLM 无论如何构造命令都会触发拦截。

此外，`<` `>` `>>` 作为子串匹配会误伤字符串中的比较操作符。

**建议**:
- 短期: 从黑名单中移除 `|` `;` `&&` `||` `>` `>>` `<`，这些是正常的 shell 操作符
- 真正需要阻止的是 **cmdlet 滥用**（`Invoke-Expression`/`iex`、`Remove-Item -Recurse`、`Start-Process` 等），黑名单中的 cmdlet 相关关键词（`rm`/`del`/`format`/`diskpart`/`reg`/`shutdown`）应该保留并加强（加 PowerShell 别名变体）
- 长期: 改为沙箱执行（Windows Job Object 或容器）

**严重度**: 🔴 严重 — 工具在当前状态下几乎不可用

---

## 17. [中等] ShellTools 注释声称有白名单但代码中未实现

**文件**: `src/agent/tools/ShellTools.cpp:60-63`

**问题**: 注释写道"仅允许读取和查询类命令的白名单前缀 // 限制为 Get-*, echo, dir/ls, type/cat, Select-*, Where-*, ForEach-*, Write-*"，但代码中完全没有任何白名单检测逻辑。实际的防护仅靠黑名单（`isDangerousCommand()`）。

这个注释会误导未来的维护者以为存在白名单保护。

**建议**: 删除注释中的错误描述，或将白名单作为额外保护层实现（先检查白名单通过，再检查黑名单）。

**严重度**: 🟡 中等

---

## 18. [中等] ShellTools 关键词匹配使用末位空格 — 可被轻易绕过

**文件**: `src/agent/tools/ShellTools.cpp:24-28`

**问题**: 部分关键词使用末位空格来减少误匹配（如 `"rm "`, `"del "`, `"rd "`, `"reg "`），但：
- PowerShell 中 `rm -r`（`rm` 后跟空格）会被匹配，但 `rm.exe` 或 `r'm'`（引号转义）不会
- `Remove-Item` 和 `del` 被分别检查，但 PowerShell 支持 `Remove-Item` 的缩写 `ri`
- `net user` 和 `net localgroup` 被检查，但 `net group`（域环境）未被检查

黑名单本质上是不安全的——总有绕过的方法。

**建议**: 在文档中明确标注"黑名单仅提供最低限度的防护，不应在不可信环境中运行"。在黑名单中补充 PowerShell 常见缩写：`ri`（Remove-Item）、`rdr`（Remove-Directory）、`sasv`（Start-Service）等。

**严重度**: 🟡 中等

---

## 19. [中等] StreamDisplay — 缺少 Windows ANSI 终端初始化

**文件**: `src/cli/StreamDisplay.cpp:13、17、22、27`

**问题**: 代码使用 ANSI 转义序列（`\033[90m`、`\033[31m` 等）做彩色输出，但未调用 `SetConsoleMode()` 启用虚拟终端处理。

在 Windows 10 1809 之前的版本，或某些终端模拟器中，ANSI 序列不会被正确解析，用户会看到类似 `←[90m[工具调用...]←[0m` 的乱码，而非彩色文本。

**建议**: 在 `StreamDisplay` 或 `main()` 中添加终端初始化：
```cpp
#ifdef _WIN32
#include <windows.h>
void enableAnsiSupport() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#endif
```

**严重度**: 🟡 中等 — 影响 Windows 终端用户体验

---

## 20. [轻微] ChapterTools.h header 注释仍引用 `word_count`

**文件**: `src/agent/tools/ChapterTools.h:60`

**问题**: `ListChaptersTool` 的类级注释写道 `{ id, title, order, word_count }`，但 `description()` 方法（#9 修复后）和 `execute()` 实现都正确地去掉了 `word_count`。header 注释作为文档没有同步更新。

```cpp
/// 返回: { chapters: [{ id, title, order, word_count }] }  // ← 过期
```

**建议**: 改为 `{ id, title, order, file_path, synopsis }`。

**严重度**: 🟢 轻微

---

## 21. [轻微] UpdateCharacterTool vs UpdateSettingTool 字段更新方式不一致

**文件**: `CharacterTools.cpp:166-193` vs `SettingTools.cpp:46-58`

**问题**: `UpdateCharacterTool` 使用指针到成员的 map（优雅、类型安全、易扩展），而 `UpdateSettingTool` 和 `UpdateWorldRuleTool` 使用 if-else 链（冗长、修改字段时容易漏分支）。同一代码库中两种风格并存，增加维护成本。

**建议**: 统一为指针到成员 map 方式（如 CharacterTools 的实现），在 SettingTools 和 WorldRuleTools 中采用。或将指针到成员 map 抽象为 `BuiltInTool` 提供的辅助方法。

**严重度**: 🟢 轻微

---

## 22. [轻微] ReplHandler 未使用 LLMResponse 返回值

**文件**: `src/cli/ReplHandler.cpp:63-64`

**问题**: 
```cpp
auto response = agent_.processUserMessage(input, callbacks);
std::cout << "\n" << std::flush;
```
`response` 对象（含 token 统计、finish_reason）被获取但从未使用。StreamDisplay 的 `on_complete` 已经输出了 token 信息，但如果有异常情况（如 `finish_reason == "length"` 表示被截断），用户无法从界面得知。

**建议**: 检查 `response.finish_reason`，在非 `"stop"` 时给用户提示：
```cpp
if (response.finish_reason == "length") {
    std::cout << "\n  \033[33m[注意: 回复因长度限制被截断]\033[0m";
}
if (response.finish_reason == "content_filter") {
    std::cout << "\n  \033[33m[注意: 部分内容因安全策略被过滤]\033[0m";
}
```

**严重度**: 🟢 轻微

---

## 23. [轻微] execute() 路径不使用 ContextManager

**文件**: `src/agent/Agent.cpp:64-72`

**问题**: `execute()`（单次命令模式，`--exec` 参数）直接调用 `client_.chat()`，不经过 ContextManager。这意味着单次命令不受 token 预算管理，也不享受 system prompt 上下文组装。

如果用户通过 `--exec` 执行"帮我分析第5章的剧情"这类需要项目上下文的命令，LLM 收到的只是裸的用户消息 + 基础 system_prompt_，缺少章节/角色/设定上下文。

**建议**: 当 `context_manager_` 存在时，`execute()` 也应该组装上下文：
```cpp
if (context_manager_) {
    auto assembly = context_manager_->assemble(conversation_, context_window_);
    effective_prompt = system_prompt_ + "\n\n" + assembly.system_prompt;
}
```

**严重度**: 🟢 轻微 — 当前 `--exec` 模式可能是故意保持简单的

---

## 附加观察：已确认修复的上一轮 11 个问题

| 问题 | 状态 |
|------|------|
| #1 system_prompt 覆盖 | ✅ 修复 — 改为局部变量 `effective_prompt` |
| #2 truncate 减法不一致 | ✅ 修复 — 改为 `countMessages(result)` 实时计算 |
| #3 空 prompt 回退 | ✅ 修复 — `buildForChapter` 失败时 fallback |
| #4 budget≤0 不截断 | ✅ 修复 — 现在返回空列表 |
| #5 Project& 裸引用 | ⏸ 暂缓（已文档化风险） |
| #6 CreateChapter 部分保存 | ⏸ 暂缓（已文档化，且后续 Create 工具用全量 save） |
| #7 tool result 无大小限制 | ✅ 修复 — 添加 4000 字符截断 |
| #8 空 try/catch | ✅ 已删除 |
| #9 ListChapters 描述 | ✅ 修复 — description() 正确 |
| #10 additionalProperties | ⏸ 暂缓（已文档化） |
| #11 total_tokens 精度说明 | ✅ 修复 — 注释已更新 |

---

## 附录：Phase 3 完整审查覆盖范围

| 步骤 | 模块 | 文件 | 状态 |
|------|------|------|------|
| 3.1 | ToolRegistry + BuiltInTool | `ToolRegistry.h/.cpp`, `BuiltInTool.h` | ⚪ 无新问题 |
| 3.2 | Agent 核心循环 | `Agent.h/.cpp` | ⚪ 已修复上轮全部问题 |
| 3.3 | ContextManager | `ContextManager.h/.cpp` | ⚪ 已修复上轮全部问题 |
| 3.4 | Chapter 工具 | `ChapterTools.h/.cpp` | 🟢 #20 header 注释过期 |
| 3.5 | Character 工具 | `CharacterTools.h/.cpp` | ⚪ 设计良好 |
| 3.6 | Setting + WorldRule 工具 | `SettingTools.h/.cpp`, `WorldRuleTools.h/.cpp` | 🟢 #21 风格不一致 |
| 3.7 | Outline + Project 工具 | `OutlineTools.h/.cpp` | ⚪ 无问题 |
| 3.8 | Shell 工具 | `ShellTools.h/.cpp` | 🔴 #16 黑名单误拦截 + 🟡 #17 #18 |
| 3.9 | AgentSetup 注册 | `AgentSetup.h` | ⚪ 干净的内联函数 |
| 3.10 | ReplHandler | `ReplHandler.h/.cpp` | 🟢 #22 response 未使用 |
| 3.11 | CommandParser + StreamDisplay | `CommandParser.h/.cpp`, `StreamDisplay.h/.cpp` | 🟡 #19 ANSI 初始化缺失 |
| 3.12 | main.cpp 集成 | `main.cpp` | 🟢 #23 execute 路径无 CM |
| 测试 | 4 个新增测试 | `test_character_tools`, `test_e2e_chapter` | ⚪ 覆盖合理 |

审查日期: 2026-06-09 | 本轮新发现问题: 1 🔴 + 4 🟡 + 4 🟢
