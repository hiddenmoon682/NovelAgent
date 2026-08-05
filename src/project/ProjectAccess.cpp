// ProjectAccess 实现 — 锁内快照 + 脏标记 test-and-clear 落盘。

#include "project/ProjectAccess.h"
#include "project/ProjectIO.h"

#include <spdlog/spdlog.h>
#include <shared_mutex>
#include <stdexcept>

ProjectAccess::ProjectAccess(std::shared_ptr<Project> project)
    : project_(std::move(project))
{
}

ProjectAccess::ProjectAccess(Project& project)
    : project_(&project, [](Project*) {})  // 不持有所有权（测试用）
{
}

void ProjectAccess::save() {
    if (!project_) return;

    // 锁内：脏标记 test-and-clear + 快照拷贝（原子地取走本次要写的标记，
    // 锁外写盘期间新产生的标记保留在 dirty_flags，下次 save 再写，不丢数据）。
    Project copy;
    uint32_t flags = 0;
    {
        std::unique_lock<std::shared_mutex> lk(lock_);
        flags = project_->dirty_flags;
        project_->dirty_flags = 0;
        copy = *project_;
    }

    // 防漏 markDirty 守卫：脏标记为空但有子实体 → 工具可能漏标记，全量保存以防静默丢失。
    if (flags == 0) {
        bool has_entities = !copy.characters.empty()
                         || !copy.settings.empty()
                         || !copy.world_rules.empty();
        bool has_outline = !copy.outline.chapters.empty()
                        || !copy.outline.volumes.empty()
                        || !copy.outline.plot_threads.empty();
        if (has_entities || has_outline) {
            spdlog::warn("[ProjectAccess] 脏标记为空但有子实体——可能遗漏 markDirty()，已全量保存");
            flags = Project::DIRTY_ALL;
        }
    }

    // 锁外序列化 + 写盘（文件 IO 不持锁，避免阻塞其它会话的工具调用）。
    ProjectIO::saveSnapshot(copy, flags);
}
