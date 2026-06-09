# 暂缓问题

> 创建时间: 2026-05-31 | 最后更新: 2026-06-09
> 用途: 记录已知但当前阶段不修复的问题，留待后续评估

---

## 当前活跃暂缓项（1 个）

### Volume 与 Chapter 同级字段语义重叠

**文件**: `src/project/Models.h`

**创建时间**: 2026-05-29

**问题**: `Volume` 和 `Chapter` 中 `key_events`/`focus_characters`/`active_plot_threads`/`goal` 同名同类型，存在语义重叠。

**决定**: Phase 4 实现上下文管理时采用方案 A（Volume 内嵌 `chapter_ids` 列表关联）。当前注释已明确标注卷级/章级差异，运行时无歧义。

---

## 已解决的暂缓项

| 问题 | 解决方式 | 时间 |
|------|---------|------|
| PCH 优化 | 对象库+Ninja+PCH | 2026-06-08 |
| CreateChapter 部分保存 | 全量 ProjectIO::save() | 2026-06-09 |
| Project& 裸引用 | shared_ptr<Project> | 2026-06-09 |
| Update 风格不一致 | 指针到成员 map | 2026-06-09 |
| execute() 无 CM | --exec 集成 ContextManager | 2026-06-09 |
| ShellTools 无超时 | CreateProcess+WaitForSingleObject(30s) | 2026-06-09 |
| SchemaUtils allowExtra | 添加可选 bool allowExtra 参数 | 2026-06-09 |
| SubAgent/TemplateManager 无测试 | test_sub_agent.cpp (4 测试) | 2026-06-09 |
| FileUtils 等无测试 | 工具函数隐性覆盖足够 | 2026-06-09 |
