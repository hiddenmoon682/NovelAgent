# 暂缓问题

> 创建时间: 2026-05-31 | 最后更新: 2026-06-09
> 用途: 记录已知但当前阶段不修复的问题，留待后续评估

---

## 当前活跃暂缓项（5 个）

### 1. ShellTools _popen 无超时控制

**来源**: REVIEW_NOTES #13 → 第六轮暂缓

**问题**: `_popen` 无超时，挂起命令可永久阻塞进程。

**决定**: Phase 5 迁移到 `CreateProcess` + `WaitForSingleObject`。当前 Agent tool call 循环有 10 轮上限间接限制。

---

### 2. SchemaUtils::object() 硬编码 additionalProperties: false

**来源**: REVIEW_NOTES #10

**问题**: 某些 LLM 可能在参数中附加额外字段，触发 API 校验拒绝。

**决定**: 保留安全默认值。遇到兼容性问题时添加可选参数 `allowExtra`。

---

### 3. Volume 与 Chapter 同级字段语义重叠

**来源**: DEFERRED.md 原始条目

**问题**: `Volume` 和 `Chapter` 中 `key_events`/`focus_characters`/`active_plot_threads`/`goal` 同名同类型，存在语义重叠。

**决定**: Phase 4 实现上下文管理时采用方案 A（Volume 内嵌 `chapter_ids` 列表关联）。

---

### 4. FileUtils/JsonUtils/StringUtils 无独立测试

**来源**: RESOLVED.md #10（来自第一轮审查）

**问题**: 三个工具模块无独立单元测试。

**决定**: 不阻塞开发，留待后续补充。

---

### 5. 测试覆盖率缺口 — SubAgent 和 AgentOrchestrator 无独立测试

**来源**: Phase 3.5 实现

**问题**: `SubAgent`、`AgentOrchestrator`、`TemplateManager` 无独立测试（仅通过编译验证）。

**决定**: Phase 5 补充集成测试。

---

## 已解决的暂缓项

| 问题 | 解决方式 | 解决时间 |
|------|---------|---------|
| PCH 优化 | 对象库 + Ninja + PCH 配置 | 2026-06-08 |
| CreateChapter 部分保存 | 恢复全量 `ProjectIO::save()` | 2026-06-09 |
| Project& 裸引用 | 改为 `shared_ptr<Project>` | 2026-06-09 |
| Update 风格不一致 | Setting/WorldRule 改为 map | 2026-06-09 |
| execute() 无 CM | --exec 也集成 ContextManager | 2026-06-09 |
