# 待修复问题

> 创建时间: 2026-05-28 | 最后更新: 2026-06-09
> 用途: 记录代码审查中发现的、尚未修复或暂缓的问题
>
> 已修复的问题请记录在 `RESOLVED.md` 中。

---

## 当前状态

Phase 3 完整审查（3.1-3.12）共发现 23 个问题：
- **19 个已修复** → 记录在 `RESOLVED.md`
- **4 个暂缓** → 记录在此文件

---

## 暂缓问题

### 1. [🟡] Project& 裸引用生命周期 (#5)

**文件**: 所有工具类（ChapterTools/CharacterTools/SettingTools 等）

**问题**: 工具类持有 `Project&` 裸引用。如果 Project 对象先于 ToolRegistry 销毁，访问 `project_` 就是 use-after-free。

**决定**: 当前生命周期一致，无触发路径。Phase 3.5（多 Agent 并行编排）引入时改为 `std::shared_ptr<Project>`。

---

### 2. [🟡] ShellTools 无超时控制 (#13)

**文件**: `src/agent/tools/ShellTools.cpp`

**问题**: `_popen` 无超时，挂起命令可永久阻塞进程。

**决定**: Phase 3.5 迁移到 `CreateProcess` + `WaitForSingleObject` 实现超时。

---

### 3. [🟢] Update 工具字段更新方式不一致 (#21)

**文件**: `CharacterTools.cpp` vs `SettingTools.cpp` vs `WorldRuleTools.cpp`

**问题**: CharacterTools 用指针到成员 map，Setting/WorldRule 用 if-else 链。

**决定**: 后续统一重构时改为 map 模式。当前不影响功能。

---

### 4. [🟢] execute() 路径不使用 ContextManager (#23)

**文件**: `src/agent/Agent.cpp:64-72`

**问题**: `--exec` 模式不经过 ContextManager，不受 token 预算管理和上下文组装。

**决定**: 当前 `--exec` 是简化的单次查询模式，保持现状。后续如有需要再集成。

---

### 5. [🟢] SchemaUtils::object() 硬编码 additionalProperties: false (#10)

**文件**: `src/utils/SchemaUtils.h:41`

**问题**: 某些 LLM 可能在参数中附加额外字段，触发 API 校验拒绝。

**决定**: 保留安全默认值。遇到兼容性问题时添加可选参数 `allowExtra`。

---

## 附录

### 审查历史

| 轮次 | 日期 | 发现 | 修复 | 暂缓 |
|------|------|------|------|------|
| 第一轮 | 2026-05-28 | 10 | 9 | 1 |
| 第二轮 | 2026-05-29 | 1 | 1 | 0 |
| 第三轮 | 2026-06-09 | 11 | 9 | 2 |
| 第四轮 | 2026-06-09 | 4 | 3 | 1 |
| 第五轮 | 2026-06-09 | 8 | 6 | 2 |
| **合计** | | **34** | **28** | **6→5** |

> 注: 部分暂缓项已通过后续审查修复或合并，最终活跃暂缓项为 5 个。
