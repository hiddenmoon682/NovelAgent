// LongTermMemoryStore 实现 — JSON 日志的加载/追加/保存。

#include "agent/memory/LongTermMemoryStore.h"

#include "project/ProjectIO.h"
#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>

using json = nlohmann::json;

namespace agent {

namespace {

int64_t nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

void LongTermMemoryStore::init(const std::string& path)
{
    std::lock_guard lock(mutex_);
    path_ = path;
    loadFromFile();
    initialized_ = true;
    spdlog::info("[LongTermMemory] 已初始化: {} ({} 条记忆)", path_, entries_.size());
}

std::string LongTermMemoryStore::append(const std::string& text, const std::string& kind)
{
    if (text.empty()) return {};

    std::lock_guard lock(mutex_);
    if (!initialized_) {
        spdlog::warn("[LongTermMemory] 未初始化，忽略写入");
        return {};
    }

    MemoryEntry entry;
    entry.created_at = nowEpochSeconds();
    // 重启后 seq_ 归零，同一秒内可能与已加载条目撞 id，递增直到唯一
    do {
        entry.id = "mem-" + std::to_string(entry.created_at)
                 + "-" + std::to_string(seq_++);
    } while (std::any_of(entries_.begin(), entries_.end(),
                         [&](const MemoryEntry& e) { return e.id == entry.id; }));
    entry.text = text;
    entry.kind = kind.empty() ? "fact" : kind;

    entries_.push_back(entry);
    saveToFile();

    spdlog::info("[LongTermMemory] 追加记忆 {} (kind={}, {} 字)",
                 entry.id, entry.kind, text.size());
    return entry.id;
}

bool LongTermMemoryStore::remove(const std::string& id)
{
    std::lock_guard lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(),
        [&id](const MemoryEntry& e) { return e.id == id; });
    if (it == entries_.end()) return false;

    entries_.erase(it);
    saveToFile();
    return true;
}

std::vector<MemoryEntry> LongTermMemoryStore::entries() const
{
    std::lock_guard lock(mutex_);
    return entries_;
}

size_t LongTermMemoryStore::count() const
{
    std::lock_guard lock(mutex_);
    return entries_.size();
}

bool LongTermMemoryStore::initialized() const
{
    std::lock_guard lock(mutex_);
    return initialized_;
}

// ===========================================================================
// 文件 I/O
// ===========================================================================

void LongTermMemoryStore::loadFromFile()
{
    entries_.clear();

    if (!utils::file::exists(path_)) {
        spdlog::debug("[LongTermMemory] 日志文件不存在，从空开始: {}", path_);
        return;
    }

    auto j = ProjectIO::loadJsonFile(path_);
    if (!j || !j->is_array()) {
        spdlog::warn("[LongTermMemory] 日志文件格式无效，从空开始: {}", path_);
        return;
    }

    for (const auto& item : *j) {
        if (!item.is_object()) continue;
        MemoryEntry entry;
        entry.id = utils::json::getOrDefault(item, "id", std::string{});
        entry.text = utils::json::getOrDefault(item, "text", std::string{});
        entry.kind = utils::json::getOrDefault(item, "kind", std::string{"fact"});
        entry.created_at = utils::json::getOrDefault(item, "created_at", int64_t{0});
        if (!entry.id.empty() && !entry.text.empty()) {
            entries_.push_back(std::move(entry));
        }
    }
}

void LongTermMemoryStore::saveToFile() const
{
    json j = json::array();
    for (const auto& e : entries_) {
        j.push_back({
            {"id", e.id},
            {"text", e.text},
            {"kind", e.kind},
            {"created_at", e.created_at}
        });
    }

    const std::string dir = utils::file::dirName(path_);
    if (!dir.empty() && !utils::file::exists(dir)) {
        utils::file::createDirs(dir);
    }
    ProjectIO::saveJsonFile(path_, j);
}

} // namespace agent
