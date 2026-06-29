# 架构问题解决方案（Issues 2,3,4,5,6,12,13）

> 撰写日期：2026-06-29
> 针对：`docs/review/DESIGN_REVIEW_CODE.md` 中标记为"架构级备查"的 7 个问题

---

## 总览

| Issue | 问题 | 难度 | 预估工时 | 风险 | 建议 Phase |
|-------|------|------|----------|------|-----------|
| 6 | ReplHandler→NovelAgentApp* 反向引用 | ⭐ 低 | 1h | 极低 | 立即 |
| 13 | SubAgent 锁粒度 | ⭐ 低 | 2h | 低 | 立即 |
| 5 | Project 全量保存 | ⭐⭐ 中 | 4h | 中（26 处调用点） | Phase 5.5 |
| 12 | 模型内联序列化膨胀 | ⭐⭐ 中 | 3h | 中（影响所有 TU） | Phase 5.5 |
| 3 | ContextManager 职责拆分 | ⭐⭐⭐ 高 | 8h | 中高（重构核心类） | Phase 6 |
| 2 | Conversation 可变状态收敛 | ⭐⭐⭐ 高 | 12h | 高（影响所有调用链） | Phase 6 |
| 4 | SubAgent 线程池 | ⭐⭐⭐ 高 | 6h | 中高（并发模型变更） | Phase 6 |

---

## Issue 6: ReplHandler→NovelAgentApp* 反向引用

### 现状

```cpp
// ReplHandler.h:35,46
class NovelAgentApp;  // 前向声明
class ReplHandler {
    NovelAgentApp* app_ = nullptr;
    void setApp(NovelAgentApp* app) { app_ = app; }
};

// ReplHandler.cpp:163-171  — /index 命令
auto& store = app_->vectorStore();       // 绕过 IOutputChannel 直接访问 app
auto& emb = app_->embeddingGenerator();  // 同上
```

### 根因

`/index` 命令需要访问检索组件（`IVectorStore` + `IEmbeddingGenerator`），这些组件由 `NovelAgentApp` 持有。当前通过反向指针直接访问。

### 方案

新增 `IIndexService` 接口，`NovelAgentApp` 实现它，`ReplHandler` 依赖接口：

```cpp
// 新文件: src/agent/IIndexService.h
#pragma once
namespace agent {
/// 索引服务接口 — 解耦 ReplHandler 对 NovelAgentApp 的反向依赖（Issue 6）。
class IIndexService {
public:
    virtual ~IIndexService() = default;
    /// 为当前项目建立向量索引，返回成功与否。
    virtual bool indexAll() = 0;
};
}
```

```cpp
// NovelAgentApp.h 改动
#include "agent/IIndexService.h"
class NovelAgentApp : public agent::IIndexService {
    bool indexAll() override;  // 把 /index 核心逻辑移到这里
};
```

```cpp
// ReplHandler.h 改动 — NovelAgentApp* → IIndexService*
class ReplHandler {
    agent::IIndexService* index_service_ = nullptr;
    void setIndexService(agent::IIndexService* svc) { index_service_ = svc; }
    // 删除: NovelAgentApp* app_
};
```

```cpp
// ReplHandler.cpp — /index 命令
parser_.registerCommand("index", "/index — 为项目内容建立向量索引", [this](const auto&) {
    if (!project_ || project_->path.empty()) { /* 错误处理 */ return true; }
    if (!index_service_) { /* 错误处理 */ return true; }
    bool ok = index_service_->indexAll();
    // ... 输出结果
    return true;
});
```

### 改动范围

| 文件 | 改动 |
|------|------|
| `src/agent/IIndexService.h` | **新建** — 定义接口 |
| `src/NovelAgentApp.h` | 继承 `IIndexService`，声明 `indexAll()` |
| `src/NovelAgentApp.cpp` | 将 `/index` 核心逻辑从 ReplHandler.cpp 移入 `indexAll()` |
| `src/cli/ReplHandler.h` | `NovelAgentApp*` → `IIndexService*`，`setApp()` → `setIndexService()` |
| `src/cli/ReplHandler.cpp` | `/index` 改为调用 `index_service_->indexAll()` |
| `src/main.cpp` | `repl.setApp(&app)` → `repl.setIndexService(&app)` |

### 收益
- 消除反向依赖，CLI 层只依赖抽象接口
- `/index` 逻辑内聚到 `NovelAgentApp`，可被 REPL 和 REST API 复用
- 测试可 Mock `IIndexService`

---

## Issue 13: SubAgent 锁粒度细化

### 现状

```cpp
// SubAgent.cpp:36-75
auto future = std::async(std::launch::async, [this, config, tool_defs]() {
    {
        std::lock_guard<std::mutex> lock(conv_mutex_);
        conversation_.addUser(config.task);    // 锁仅保护这一句
    }                                            // ← 锁释放

    ToolCallLoop loop(...);                      // ← 无锁，安全（无 shared state）
    loop.setCancelled(&cancelled_);

    std::lock_guard<std::mutex> lock2(conv_mutex_);  // ← 第二个锁
    auto loop_result = loop.run(conversation_, ...);  // 锁持有整个 run() 期间！
});
```

**问题**：第二个 `lock_guard` 持有 `conv_mutex_` 跨越整个 `loop.run()`（可能数分钟）。如果外部线程在此期间调用 `conversation()`（const 访问器），会阻塞直到 `loop.run()` 完成。

### 方案

将 `loop.run()` 中对 `conversation_` 的修改改为"本地执行 + 批量合并"，锁只保护合并操作：

```cpp
// SubAgent.cpp — 修改后的 lambda
auto future = std::async(std::launch::async, [this, config, tool_defs]() -> SubAgentResult {
    SubAgentResult r;
    try {
        if (cancelled_) return r;

        // 步骤 1: 本地构造初始消息（不锁 conversation_）
        llm::Conversation localConv;           // 栈上临时 Conversation
        localConv.addUser(config.task);

        if (cancelled_) return r;

        // 步骤 2: 在本地 Conversation 上执行 tool_call 循环（完全无锁）
        ToolCallLoop loop(*client_, tools_);
        loop.setCancelled(&cancelled_);
        ToolCallLoopConfig cfg;
        cfg.max_rounds = config.max_tool_rounds;
        cfg.first_round_streaming = false;
        cfg.max_repeated_calls = 3;

        auto loop_result = loop.run(
            localConv, tool_defs, config.system_prompt, {}, cfg);

        // 步骤 3: 仅在最后，将结果原子合并到共享 conversation_（短暂持锁）
        {
            std::lock_guard<std::mutex> lock(conv_mutex_);
            // 批量移动消息：localConv → conversation_
            for (const auto& msg : localConv.all()) {
                conversation_.add(msg);  // 值拷贝，安全
            }
        }

        r.output = loop_result.response.content;
        r.input_tokens = loop_result.input_tokens;
        r.output_tokens = loop_result.output_tokens;
        // ...
    }
    // ...
});
```

### 关键洞察

当前 `lock_guard` 跨 `loop.run()` 持锁的唯一理由是为了"防止并发访问 `conversation_`"。但 `conversation_` 只在 SubAgent 内部被修改——没有其他线程同时写入。将修改隔离到本地 `Conversation` 后，锁只保护最终合并步骤（O(n) 消息拷贝），不再跨越 HTTP 调用。

### 改动范围

| 文件 | 改动 |
|------|------|
| `src/agent/SubAgent.cpp` | ~20 行：本地 Conversation + 末尾批量合并 |

### 收益
- `conv_mutex_` 持锁时间从数分钟降至毫秒级
- 外部线程调用 `conversation()` 不再可能长时间阻塞
- 代码语义更清晰：本地隔离执行 → 最终原子提交

---

## Issue 5: Project 大聚合根全量保存

### 现状

```cpp
// ProjectIO.cpp:209-230
void ProjectIO::save(const Project& project) {
    saveJsonFile(..., novelJson);        // novel.json
    saveJsonFile(..., outlineJson);      // outline.json
    saveJsonFile(..., charactersJson);   // characters.json
    saveJsonFile(..., settingsJson);     // settings.json
    saveJsonFile(..., worldRulesJson);   // world_rules.json
    saveJsonFile(..., styleJson);        // style.json
}
```

`create_character` → 6 个文件全部写入（实际上只有 `characters.json` 变了）。

**26 处调用点**分布在 9 个文件中，全部调用无差别的 `ProjectIO::save(*project_)`。

### 方案：脏标记位图

在 `Project` 中增加 `uint32_t dirty_flags`（位图）标记哪些子实体发生了变化。`save()` 只写脏文件。

```cpp
// project/Models/Project.h 改动
struct Project {
    // ... 现有字段 ...

    // Issue 5: 增量保存脏标记
    enum DirtyBit : uint32_t {
        DIRTY_NOVEL       = 1 << 0,  // novel.json（标题/状态/元数据）
        DIRTY_OUTLINE     = 1 << 1,  // outline.json（大纲/章节/卷/剧情线）
        DIRTY_CHARACTERS  = 1 << 2,  // characters.json
        DIRTY_SETTINGS    = 1 << 3,  // settings.json
        DIRTY_WORLD_RULES = 1 << 4,  // world_rules.json
        DIRTY_STYLE       = 1 << 5,  // style.json
        DIRTY_ALL         = 0x3F,    // 全量保存（向后兼容）
    };
    uint32_t dirty_flags = DIRTY_ALL;  // 首次保存默认全量

    void markDirty(DirtyBit bit) { dirty_flags |= bit; }
    void markClean() { dirty_flags = 0; }
    bool isDirty(DirtyBit bit) const { return (dirty_flags & bit) != 0; }
};
```

```cpp
// ProjectIO.cpp — save() 改为增量
void ProjectIO::save(const Project& project) {
    if (project.path.empty()) throw ...;

    Project mutableCopy = project;
    mutableCopy.format_version = kCurrentFormatVersion;
    mutableCopy.modified = nowTimestamp();
    migrateProject(mutableCopy);

    // 仅写脏文件（dirty_flags == DIRTY_ALL 时全量写入）
    if (project.isDirty(Project::DIRTY_NOVEL))
        saveJsonFile(joinPath(p, kNovelJson), mutableCopy);
    if (project.isDirty(Project::DIRTY_OUTLINE))
        saveJsonFile(joinPath(p, kOutlineJson), mutableCopy.outline);
    if (project.isDirty(Project::DIRTY_CHARACTERS))
        saveJsonFile(joinPath(p, kCharactersJson), mutableCopy.characters);
    if (project.isDirty(Project::DIRTY_SETTINGS))
        saveJsonFile(joinPath(p, kSettingsJson), mutableCopy.settings);
    if (project.isDirty(Project::DIRTY_WORLD_RULES))
        saveJsonFile(joinPath(p, kWorldRulesJson), mutableCopy.world_rules);
    if (project.isDirty(Project::DIRTY_STYLE))
        saveJsonFile(joinPath(p, kStyleJson), mutableCopy.style);
}
```

### 工具改动

每个工具在修改前标记对应的脏位。例如 `CharacterTools.cpp`：

```cpp
// CreateCharacterTool::execute()
project_->characters.push_back(new_char);
project_->markDirty(Project::DIRTY_CHARACTERS);  // ← 新增这一行
ProjectIO::save(*project_);                       // 只写 characters.json

// DeleteChapterTool::execute() — 影响 outline + characters（级联清理 chapter_appearances）
project_->outline.chapters.erase(...);
project_->markDirty(Project::DIRTY_OUTLINE);
project_->markDirty(Project::DIRTY_CHARACTERS);   // 级联清理了角色的 chapter_appearances
ProjectIO::save(*project_);
```

### 改动范围

| 文件 | 改动 |
|------|------|
| `src/project/Models/Project.h` | 添加 `DirtyBit` 枚举 + `dirty_flags` 字段 + 3 个 helper |
| `src/project/ProjectIO.cpp` | `save()` 按位检查脏标记 |
| `src/agent/tools/ChapterTools.cpp` | 6 处 save 前 add `markDirty()` |
| `src/agent/tools/CharacterTools.cpp` | 5 处 |
| `src/agent/tools/OutlineTools.cpp` | 4 处 |
| `src/agent/tools/SettingTools.cpp` | 3 处 |
| `src/agent/tools/WorldRuleTools.cpp` | 3 处 |
| `src/agent/tools/StyleTools.cpp` | 1 处 |
| `src/cli/ReplHandler.cpp` | 2 处标记 `DIRTY_ALL`（手动保存和自动保存） |

### 收益
- 长篇小说（100 章 + 50 角色 + 30 设定）中，`create_character` 从 6 文件写入降为 1 文件写入
- 向后兼容：默认 `DIRTY_ALL` 保证首次保存全量
- 脏标记自然支持未来的部分加载/部分同步

### 风险
- 26 处调用点都需要标记正确的脏位。**遗漏**导致修改不保存（数据丢失）。
- 缓解：`save()` 在 debug 构建中检查 dirty_flags == 0 时 warn；或保留 `saveAll()` 作为显式全量保存入口

---

## Issue 12: 模型内联序列化函数膨胀头文件

### 现状

以 `Chapter.h` 为例：`to_json`（~20 行） + `from_json`（~35 行）全部 `inline` 在头文件中。`Models.h` 包含全部模型头文件，所有使用 `Models.h` 的 `.cpp` 都会实例化这些模板。

### 方案：序列化分离到 .cpp

```cpp
// Chapter.h — 声明保留，实现移除
struct Chapter { /* 字段定义不变 */ };

// 仅声明，不再 inline 实现
void to_json(nlohmann::json& j, const Chapter& c);
void from_json(const nlohmann::json& j, Chapter& c);
```

```cpp
// 新文件: src/project/ModelSerialization.cpp
// 集中定义所有模型的 to_json/from_json
#include "project/Models.h"  // 所有模型声明
#include <nlohmann/json.hpp>

void to_json(nlohmann::json& j, const Chapter& c) { /* 原有实现 */ }
void from_json(const nlohmann::json& j, Chapter& c) { /* 原有实现 */ }
// ... Scene, Character, Setting, Outline, Style, WorldRule, Project 同理
```

### 改动范围

| 文件 | 改动 |
|------|------|
| `src/project/Models/Chapter.h` | 移除 inline 实现，保留声明 |
| `src/project/Models/Scene.h` | 同上 |
| `src/project/Models/Character.h` | 同上 |
| `src/project/Models/Setting.h` | 同上 |
| `src/project/Models/Outline.h` | 同上 |
| `src/project/Models/Style.h` | 同上 |
| `src/project/Models/WorldRule.h` | 同上 |
| `src/project/Models/Project.h` | 同上 |
| `src/project/ModelSerialization.cpp` | **新建** — 集中定义所有序列化函数 |
| `src/CMakeLists.txt` | 添加新 .cpp 到 novelagent_lib |

### 收益
- 修改任一模型字段 → 仅 `ModelSerialization.cpp` 重编译（而非所有包含 `Models.h` 的 .cpp）
- 估计节省 60-80% 的重编译时间（模型变更场景）
- 头文件更简洁，阅读体验更好

### 注意
- `nlohmann::json` 的 ADL 序列化（`to_json`/`from_json`）必须在调用方的命名空间或 `nlohmann` 命名空间中可见。分离到 .cpp 后需要显式 `#include <nlohmann/json.hpp>`（而非仅 `json_fwd.hpp`）
- `Message.h` 中的 `to_json`/`from_json`（`Message`, `ToolCall`, `LLMResponse`）也可同样处理，但它们是 llm 层而非 project 层，可以分开处理

---

## Issue 3: ContextManager 职责拆分

### 现状

`ContextManager`（~210 行声明 + ~350 行实现）集合了 6 种职责：

| 职责 | 方法 | 行数（估） | 外部依赖 |
|------|------|-----------|----------|
| ① 上下文组装 | `assemble`, `buildSystemPrompt` | ~150 | Project, ILLMClient |
| ② 对话压缩 | `compact`, `clearCompactedSummary` | ~80 | ILLMClient |
| ③ Token 追踪 | `recordUsage`, `checkThresholds`, `sessionStats`, `usagePercent` | ~60 | 无 |
| ④ 向量检索协调 | `setRetrievalBackend`, `isVectorStoreStale` | ~30 | IVectorStore, IEmbeddingGenerator |
| ⑤ 自动压缩策略 | `setAutoCompact`, `shouldAutoCompact` | ~15 | 无（自含） |
| ⑥ 会话持久化 | `saveSessionState`, `loadSessionState` | ~100 | FileStorageBackend, SessionPersistence |

### 方案：拆为 3 个类

```
Before:                          After:
┌─────────────────────┐          ┌─────────────────────┐
│   ContextManager    │          │   ContextManager    │ ← 门面（保留 ① ④ ⑥）
│  ① ② ③ ④ ⑤ ⑥      │          │  ├ TokenTracker     │ ← 新类（③）
└─────────────────────┘          │  ├ Compactor        │ ← 新类（② ⑤）
                                 │  └ SessionPersistence│ ← 已有（⑥）
                                 └─────────────────────┘
```

#### TokenTracker（纯计算类，无外部依赖）

```cpp
// src/agent/TokenTracker.h
namespace agent {

struct TokenSnapshot {
    int total_input = 0;
    int total_output = 0;
    int request_count = 0;
    int model_context_limit = 131072;
};

class TokenTracker {
public:
    void setModelLimit(int limit) { model_limit_ = limit; }

    void record(int input, int output) {
        snapshot_.total_input += input;
        snapshot_.total_output += output;
        snapshot_.request_count++;
    }

    /// 当前用量百分比 [0, 100]
    int usagePercent() const {
        if (model_limit_ <= 0) return 0;
        return (snapshot_.total_input * 100) / model_limit_;
    }

    /// 请求前检查：是否需要触发压缩/告警
    struct Threshold {
        bool should_warn = false;     // 用量 > 70%
        bool should_compact = false;  // 用量 > 85%
        bool critical = false;        // 用量 > 95%
    };
    Threshold check(int warning_pct = 70, int compact_pct = 85, int critical_pct = 95) const;

    const TokenSnapshot& snapshot() const { return snapshot_; }
    void reset() { snapshot_ = TokenSnapshot{}; }

private:
    TokenSnapshot snapshot_;
    int model_limit_ = 131072;
};

} // namespace agent
```

#### Compactor（LLM 驱动压缩）

```cpp
// src/agent/Compactor.h
namespace agent {

class Compactor {
public:
    /// 执行 LLM 驱动的对话压缩。
    /// @returns 压缩结果（摘要文本 + 压缩消息数 + 压缩后 token）
    CompactResult compact(
        llm::Conversation& conv,
        llm::ILLMClient& client,
        std::optional<std::string> focus = std::nullopt);

    bool hasSummary() const { return !summary_.empty(); }
    const std::string& summary() const { return summary_; }
    void clearSummary() { summary_.clear(); marker_ = 0; }
    int compactionMarker() const { return marker_; }

    void setAutoCompact(bool on, int threshold_pct = 70);
    bool shouldAutoCompact(int usage_percent) const;

private:
    std::string summary_;
    int marker_ = 0;
    bool auto_compact_ = false;
    int threshold_pct_ = 70;
};

} // namespace agent
```

#### ContextManager 门面（简化后）

```cpp
// ContextManager.h — 简化版
class ContextManager {
public:
    ContextManager(FileStorageBackend& storage);

    // ── 核心入口 ──
    ContextAssembly assemble(const llm::Conversation& conv, int max_tokens);
    std::string buildSystemPrompt(const Project& project, const std::string& chapter_id = "");

    // ── Project 注入 ──
    void setProject(const Project* p);
    void setCurrentChapter(const std::string& id);

    // ── 委托: TokenTracker ──
    void recordUsage(int in, int out) { tracker_.record(in, out); }
    int usagePercent() const { return tracker_.usagePercent(); }
    SessionTokenState sessionStats() const;
    void resetSession();

    // ── 委托: Compactor ──
    CompactResult compact(llm::Conversation& conv, llm::ILLMClient& client,
                          std::optional<std::string> focus = std::nullopt) {
        return compactor_.compact(conv, client, std::move(focus));
    }
    bool hasCompactedSummary() const { return compactor_.hasSummary(); }
    void clearCompactedSummary() { compactor_.clearSummary(); }
    int compactionMarker() const { return compactor_.compactionMarker(); }
    void setAutoCompact(bool on, int pct = 70) { compactor_.setAutoCompact(on, pct); }
    bool shouldAutoCompact() const { return compactor_.shouldAutoCompact(tracker_.usagePercent()); }

    // ── 向量检索 ──
    void setRetrievalBackend(...);
    bool isVectorStoreStale() const;
    void clearVectorStore();

    // ── 会话持久化 ──
    void saveSessionState(...);
    void loadSessionState(...);
    SessionPersistence& persistence() { return persistence_; }

    // 公开子组件（只读访问）
    const TokenTracker& tracker() const { return tracker_; }
    const Compactor& compactor() const { return compactor_; }

private:
    FileStorageBackend& storage_;
    SessionPersistence persistence_;
    TokenTracker tracker_;               // ← 新成员
    Compactor compactor_;                // ← 新成员
    const Project* project_ = nullptr;
    std::string current_chapter_id_;
    // ... 向量检索、警告缓存等剩余字段（~6 个）
};
```

### 收益
- `TokenTracker` 可独立单元测试（无需构造 ContextManager / FileStorageBackend / Project）
- `Compactor` 可独立测试（Mock ILLMClient 即可）
- `ContextManager` 从 210 行声明缩至 ~120 行
- 新增 Token 追踪需求（如按章节统计、累计预警）只需改 `TokenTracker`

### 接口兼容性
`Agent` 和 `SerialProcessor` 通过 `ContextManager*` 指针使用。拆分后 `ContextManager` 仍保留所有公开方法（内部委托），调用方零改动。

---

## Issue 2: Conversation 作为全局可变状态 — 写时拷贝收敛

### 现状回顾

```
Agent::conversation_ (private member)
  → IMessageProcessor::process(Conversation&)               [非 const 引用]
    → ToolCallLoop::run(Conversation&, ...)                  [非 const 引用]
      → ToolPipeline::executeAndAppend(tool_calls)           [Conversation& 成员]
        → conversation.add(...)                               [直接修改]
      → conversation.add(assistant)                          [直接修改]
    → conversation.addAssistant(text)                         [直接修改]
```

5 层调用链，每层都在修改同一个对象。任何一层持有引用悬空都会导致 use-after-free。

### 方案：增量式收敛（分三步走）

#### 第一步：ConversationDiff（改动最小）

```cpp
// llm/Conversation.h 新增
struct ConversationDiff {
    std::vector<Message> added;      // 追加的消息
    std::optional<size_t> truncate_to;  // 可选截断
    bool clear_preserved = false;    // 清除所有 preserved 标记
};

class Conversation {
public:
    // 现有方法保持不变...

    /// 批量应用一组修改（原子操作）。
    /// Issue 2 第一步：提供显式的批量修改点，减少散落的单条 add() 调用。
    void apply(const ConversationDiff& diff) {
        if (diff.clear_preserved) {
            for (auto& m : messages_) m.preserved = false;
        }
        if (diff.truncate_to) {
            truncateTo(*diff.truncate_to);
        }
        for (auto& m : diff.added) {
            messages_.push_back(std::move(m));
        }
    }
};
```

`ToolPipeline::executeAndAppend()` 改为返回 `ConversationDiff` 而非直接修改：

```cpp
// ToolPipeline.h — 现有接口改为返回 diff
class ToolPipeline {
public:
    /// 执行工具调用并返回对话修改，不直接修改 Conversation。
    ConversationDiff execute(const std::vector<llm::ToolCall>& tool_calls);

    /// [已废弃] 直接修改版本 — 内部调用 execute() + conv.apply()
    /// 保留以兼容 SubAgent（Issue 13 修改后也会迁移）
    void executeAndAppend(const std::vector<llm::ToolCall>& tool_calls);
};
```

#### 第二步：ConversationEditor（写权限收敛）

```cpp
// llm/Conversation.h — 新增写权限守卫
class ConversationEditor {
public:
    explicit ConversationEditor(Conversation& conv) : conv_(conv) {}
    ~ConversationEditor() = default;

    // 所有写操作通过 Editor，Conversation 自身移除公开的 add/truncateTo 等写方法
    void addUser(std::string content) { conv_.messages_.push_back(Message::user(std::move(content))); }
    void addAssistant(std::string content) { conv_.messages_.push_back(Message::assistant(std::move(content))); }
    void apply(const ConversationDiff& diff) { conv_.apply(diff); }

    Conversation& conv_;  // 不隐藏，方便传给 ToolCallLoop
};

class Conversation {
    friend class ConversationEditor;  // Editor 是唯一写入口
public:
    // 只读接口完全不变
    std::vector<Message> messages() const;
    const std::vector<Message>& all() const;
    // ...

    /// 创建写权限守卫。同一时刻应只有一个活跃的 Editor。
    ConversationEditor edit() { return ConversationEditor(*this); }

private:
    std::vector<Message> messages_;
    // 删除公开的 add/addUser/addSystem/addAssistant/addToolResult/truncateTo/removeOldest
};
```

#### 第三步：Copy-on-Write（并发安全）

```cpp
// llm/Conversation.h — CoW 包装
class Conversation {
public:
    // ... 只读接口 ...

    /// 创建 CoW 副本。修改副本不影响原对象，直到 commit()。
    Conversation fork() const;
    /// 将 fork 的修改合并回原对象。
    void merge(Conversation&& forked);

private:
    struct State {
        std::vector<Message> messages;
        int compaction_marker = 0;
    };
    std::shared_ptr<const State> state_;  // 不可变共享状态
};
```

`SubAgent` 并发场景中：每个子 Agent `fork()` 得到独立副本，执行完毕后 `merge()` 回主 Conversation。无锁并发。

### 实施建议

**第一步（立即做）**：`ConversationDiff` + `ToolPipeline` 改为返回 diff。改动范围约 30 行，风险极低。

**第二步（Phase 6）**：`ConversationEditor` 写权限收敛。需调整所有 `conversation.add*()` 调用点为 `conversation.edit().add*()` 或通过 Editor 操作。

**第三步（Phase 7+）**：CoW 仅在真正需要并发安全的场景（多 SubAgent 共享 Conversation）时实施。当前并行模式下 SubAgent 各自拥有独立 `conversation_`，不存在共享写入。

---

## Issue 4: SubAgent 异步线程模型

### 现状

```cpp
// AgentOrchestrator::executeParallel() — 每个任务创建一个 std::async
for (size_t i = 0; i < tasks.size(); ++i) {
    futures[i] = std::async(std::launch::async, [this, task = tasks[i]]() {
        // ... 创建 SubAgent → execute → 可能阻塞 180s HTTP 调用
    });
}
```

问题：
- `std::async` 不保证使用线程池（具体行为依赖于实现）
- MSVC 使用 `Concurrency::task` 线程池（OK），但 libstdc++ 可能每次创建新线程
- 所有 SubAgent 共享一个 HTTP 连接的 `read_timeout`（180s）——真正的取消做不到

### 方案：固定大小线程池 + jthread

```cpp
// 新文件: src/agent/ThreadPool.h
#pragma once
#include <atomic>
#include <functional>
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace agent {

/// 固定大小线程池 — 替代 std::async 的 SubAgent 并发执行（Issue 4）。
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 4);
    ~ThreadPool();

    /// 提交任务，返回 future。
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    size_t active_count() const { return active_.load(); }
    size_t queue_size() const;

private:
    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<size_t> active_{0};
    bool stop_ = false;
};

} // namespace agent
```

`AgentOrchestrator` 持有线程池：

```cpp
// AgentOrchestrator.h — 新增
class AgentOrchestrator {
public:
    void setThreadPool(ThreadPool* pool) { thread_pool_ = pool; }
private:
    ThreadPool* thread_pool_ = nullptr;  // 如果不设置，退化为 std::async
};
```

```cpp
// AgentOrchestrator.cpp — executeParallel() 改为使用线程池
for (size_t i = 0; i < tasks.size(); ++i) {
    if (thread_pool_) {
        futures[i] = thread_pool_->submit([this, task = tasks[i]]() { /* 同上 */ });
    } else {
        futures[i] = std::async(std::launch::async, [this, task = tasks[i]]() { /* 同上 */ });
    }
}
```

### HTTP 层取消

当前 httplib 的 `set_read_timeout(180s)` 是全局的。SubAgent 需要一个更短的超时：

```cpp
// LLMClientFactory 新增
std::unique_ptr<ILLMClient> createWithTimeout(std::chrono::seconds read_timeout);
```

SubAgent 构造时传入短超时（如 30s）的 LLMClient。配合 Issue 21+26 的 `cancelled_` 取消机制，超时后的等待时间从 180s 降至 30s。

### 收益
- 线程数量有界（默认 4），不会因并行子任务数量而无限增长
- `ThreadPool` 可复用于其他并发场景（如批量嵌入生成）
- 配合短 HTTP 超时，SubAgent 取消延迟从 180s → 30s

---

## 实施路线图

```
Week 1（Phase 5.5）:
  Day 1: Issue 6  (IIndexService)        — 1h, 极低风险
  Day 1: Issue 13 (SubAgent 锁粒度)      — 2h, 低风险
  Day 2: Issue 5  (Project 增量保存)     — 4h, 中风险（26 处调用点需仔细标记）
  Day 3: Issue 12 (模型序列化分离)       — 3h, 中风险（编译依赖变更）
  Day 3: 全面测试

Week 2（Phase 6）:
  Day 1-2: Issue 3  (ContextManager 拆分) — 8h, 中高风险
  Day 3-4: Issue 2  (Conversation Diff + Editor) — 第一步 + 第二步, 8h
  Day 5: Issue 4  (SubAgent 线程池)      — 6h, 中高风险

Week 3+（Phase 7+）:
  Issue 2 第三步: Conversation CoW        — 按需实施
```

---

## 每个 Issue 的风险缓解

| Issue | 主要风险 | 缓解措施 |
|-------|----------|----------|
| 6 | 无 | — |
| 13 | 本地 Conversation 内存开销 | 批量移动使用 `std::move` 避免拷贝 |
| 5 | 脏标记遗漏导致数据不保存 | debug 构建 assert dirty_flags != 0；CI 增加全量保存 E2E 测试 |
| 12 | 模板实例化可见性 | ADL 要求 `to_json`/`from_json` 对 nlohmann 可见，分离后在 .cpp 中显式包含 json.hpp |
| 3 | 接口兼容性 | ContextManager 保留所有现有公开方法（内部委托），零调用方改动 |
| 2 | 调用链变更范围大 | 分三步走，每步独立可验证；第一步（Diff）零调用方改动 |
| 4 | 并发模型变更多线程 bug | ThreadPool 独立单元测试（100+ 任务提交/取消/析构安全）；保留 std::async 回退路径 |
