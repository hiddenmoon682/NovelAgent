#pragma once

// ProjectAccess — Project 的受控访问层（P2/P3 定版落地）。
//
// Project 是纯数据模型（POJO），多会话并行下的并发保护由本层承担：
// - 本层持有跨会话共享的锁与事务方法；工具/GUI/索引服务一律经本层访问。
// - withReadLock / withWriteLock：lambda 接收 Project&（锁已持有），锁内直接操作公开字段；
//   锁边界由本层声明，外部无法在无锁状态下安全拿到 Project 引用。
// - 事务方法（addXxx/updateXxx/removeXxx）：独占锁内一次"读-改-写"，自动 markDirty 对应脏位。
// - 快照读（getXxx）：共享锁内拷贝返回，供"锁外计算"。
// - save()：锁内快照拷贝 + 脏标记 test-and-clear，锁外序列化写盘（文件 IO 不持锁）。
// - path() / title() 等只读 getter：装配期不变的运行期/元数据字段，无锁读。
//
// 约定：
// - 一个 Project 只对应一个 ProjectAccess（生产路径由 NovelAgentApp 装配唯一实例，
//   所有工具/GUI/索引共享）；多实例包同一 Project 且并发访问会失去保护。
// - withLock 的 lambda 内不得再调用本层/事务方法（shared_mutex 非递归，会死锁）；
//   锁内只做裸字段访问，简单操作用事务方法。

#include "project/Models/Project.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

class ProjectAccess {
public:
    explicit ProjectAccess(std::shared_ptr<Project> project);
    explicit ProjectAccess(Project& project);  // 测试用：包栈上对象（不持有所有权）

    std::shared_ptr<Project> project() const { return project_; }

    // ── 只读 getter（装配期不可变字段，无锁）──
    const std::string& path() const { return project_->path; }
    const std::string& title() const { return project_->title; }
    bool hasProject() const { return static_cast<bool>(project_); }

    // ── 锁内访问（lambda 接收 Project&，锁已持有；禁止嵌套调用事务方法）──
    template <typename Fn>
    auto withReadLock(Fn&& fn) const {
        std::shared_lock<std::shared_mutex> lk(lock_);
        return fn(*project_);
    }
    template <typename Fn>
    auto withWriteLock(Fn&& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        return fn(*project_);
    }

    // ── 快照读（共享锁内返回拷贝，供"锁外计算"）──
    std::vector<Character> getCharacters() const {
        std::shared_lock<std::shared_mutex> lk(lock_);
        return project_->characters;
    }
    std::vector<Setting> getSettings() const {
        std::shared_lock<std::shared_mutex> lk(lock_);
        return project_->settings;
    }
    std::vector<WorldRule> getWorldRules() const {
        std::shared_lock<std::shared_mutex> lk(lock_);
        return project_->world_rules;
    }
    Outline getOutline() const {
        std::shared_lock<std::shared_mutex> lk(lock_);
        return project_->outline;
    }
    Style getStyle() const {
        std::shared_lock<std::shared_mutex> lk(lock_);
        return project_->style;
    }

    // ── 事务修改（独占锁内"读-改-写"一次完成，无撕裂；自动 markDirty 对应位）──
    void addCharacter(Character c) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        project_->characters.push_back(std::move(c));
        project_->markDirty(Project::DIRTY_CHARACTERS);
    }
    bool updateCharacter(const std::string& id, const std::function<void(Character&)>& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::find_if(project_->characters.begin(), project_->characters.end(),
                               [&](const Character& x) { return x.id == id; });
        if (it == project_->characters.end()) return false;
        fn(*it);
        project_->markDirty(Project::DIRTY_CHARACTERS);
        return true;
    }
    bool removeCharacter(const std::string& id) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::remove_if(project_->characters.begin(), project_->characters.end(),
                                 [&](const Character& x) { return x.id == id; });
        if (it == project_->characters.end()) return false;
        project_->characters.erase(it, project_->characters.end());
        project_->markDirty(Project::DIRTY_CHARACTERS);
        return true;
    }
    void addSetting(Setting s) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        project_->settings.push_back(std::move(s));
        project_->markDirty(Project::DIRTY_SETTINGS);
    }
    bool updateSetting(const std::string& id, const std::function<void(Setting&)>& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::find_if(project_->settings.begin(), project_->settings.end(),
                               [&](const Setting& x) { return x.id == id; });
        if (it == project_->settings.end()) return false;
        fn(*it);
        project_->markDirty(Project::DIRTY_SETTINGS);
        return true;
    }
    bool removeSetting(const std::string& id) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::remove_if(project_->settings.begin(), project_->settings.end(),
                                 [&](const Setting& x) { return x.id == id; });
        if (it == project_->settings.end()) return false;
        project_->settings.erase(it, project_->settings.end());
        project_->markDirty(Project::DIRTY_SETTINGS);
        return true;
    }
    void addWorldRule(WorldRule r) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        project_->world_rules.push_back(std::move(r));
        project_->markDirty(Project::DIRTY_WORLD_RULES);
    }
    bool updateWorldRule(const std::string& id, const std::function<void(WorldRule&)>& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::find_if(project_->world_rules.begin(), project_->world_rules.end(),
                               [&](const WorldRule& x) { return x.id == id; });
        if (it == project_->world_rules.end()) return false;
        fn(*it);
        project_->markDirty(Project::DIRTY_WORLD_RULES);
        return true;
    }
    bool removeWorldRule(const std::string& id) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::remove_if(project_->world_rules.begin(), project_->world_rules.end(),
                                 [&](const WorldRule& x) { return x.id == id; });
        if (it == project_->world_rules.end()) return false;
        project_->world_rules.erase(it, project_->world_rules.end());
        project_->markDirty(Project::DIRTY_WORLD_RULES);
        return true;
    }

    // ── 大纲域（chapters / volumes / plot_threads 同属 DIRTY_OUTLINE）──
    void addChapter(Chapter c) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        project_->outline.chapters.push_back(std::move(c));
        project_->markDirty(Project::DIRTY_OUTLINE);
    }
    bool updateChapter(const std::string& id, const std::function<void(Chapter&)>& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::find_if(project_->outline.chapters.begin(), project_->outline.chapters.end(),
                               [&](const Chapter& x) { return x.id == id; });
        if (it == project_->outline.chapters.end()) return false;
        fn(*it);
        project_->markDirty(Project::DIRTY_OUTLINE);
        return true;
    }
    bool removeChapter(const std::string& id) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::remove_if(project_->outline.chapters.begin(), project_->outline.chapters.end(),
                                 [&](const Chapter& x) { return x.id == id; });
        if (it == project_->outline.chapters.end()) return false;
        project_->outline.chapters.erase(it, project_->outline.chapters.end());
        project_->markDirty(Project::DIRTY_OUTLINE);
        return true;
    }
    void addVolume(Volume v) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        project_->outline.volumes.push_back(std::move(v));
        project_->markDirty(Project::DIRTY_OUTLINE);
    }
    bool updateVolume(const std::string& id, const std::function<void(Volume&)>& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::find_if(project_->outline.volumes.begin(), project_->outline.volumes.end(),
                               [&](const Volume& x) { return x.id == id; });
        if (it == project_->outline.volumes.end()) return false;
        fn(*it);
        project_->markDirty(Project::DIRTY_OUTLINE);
        return true;
    }
    bool removeVolume(const std::string& id) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::remove_if(project_->outline.volumes.begin(), project_->outline.volumes.end(),
                                 [&](const Volume& x) { return x.id == id; });
        if (it == project_->outline.volumes.end()) return false;
        project_->outline.volumes.erase(it, project_->outline.volumes.end());
        project_->markDirty(Project::DIRTY_OUTLINE);
        return true;
    }
    void addPlotThread(PlotThread pt) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        project_->outline.plot_threads.push_back(std::move(pt));
        project_->markDirty(Project::DIRTY_OUTLINE);
    }
    bool updatePlotThread(const std::string& id, const std::function<void(PlotThread&)>& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::find_if(project_->outline.plot_threads.begin(), project_->outline.plot_threads.end(),
                               [&](const PlotThread& x) { return x.id == id; });
        if (it == project_->outline.plot_threads.end()) return false;
        fn(*it);
        project_->markDirty(Project::DIRTY_OUTLINE);
        return true;
    }
    bool removePlotThread(const std::string& id) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        auto it = std::remove_if(project_->outline.plot_threads.begin(), project_->outline.plot_threads.end(),
                                 [&](const PlotThread& x) { return x.id == id; });
        if (it == project_->outline.plot_threads.end()) return false;
        project_->outline.plot_threads.erase(it, project_->outline.plot_threads.end());
        project_->markDirty(Project::DIRTY_OUTLINE);
        return true;
    }
    void updateStyle(const std::function<void(Style&)>& fn) {
        std::unique_lock<std::shared_mutex> lk(lock_);
        fn(project_->style);
        project_->markDirty(Project::DIRTY_STYLE);
    }

    // ── 落盘（锁内快照 + 脏标记 test-and-clear；文件 IO 在锁外）──
    void save();

private:
    std::shared_ptr<Project> project_;
    mutable std::shared_mutex lock_;  // 跨会话共享的项目锁（P3：锁在访问层，非数据层）
};