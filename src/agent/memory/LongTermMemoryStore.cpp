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
    // 取当前系统时间距 Unix 纪元（1970-01-01）的秒数，作为记忆创建时间戳。
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

// 追加一条记忆并立即持久化；返回生成的 id，写入失败时返回空串。
std::string LongTermMemoryStore::append(const std::string& text, const std::string& kind)
{
    if (text.empty()) return {};      // 空文本无意义，直接拒绝

    // 加锁后再检查初始化，保证与写操作互斥
    std::lock_guard lock(mutex_);
    if (!initialized_) {              // 未 init() 时没有可写的目标文件，拒绝写入
        spdlog::warn("[LongTermMemory] 未初始化，忽略写入");
        return {};
    }

    MemoryEntry entry;
    entry.created_at = nowEpochSeconds();   // 记录当前时间戳，作为 id 组成部分与排序依据

    // 生成唯一 id：格式为 mem-<时间戳>-<序号>。
    // 重启后 seq_ 归零，同一秒内可能与已加载条目撞 id，故循环递增直到唯一。
    // 每次循环都可能撞上，所以循环内自增 seq_ 并与已有条目逐条比对，命中后重新生成。
    do {
        entry.id = "mem-" + std::to_string(entry.created_at)
                 + "-" + std::to_string(seq_++);
    } while (std::any_of(entries_.begin(), entries_.end(),
                         [&](const MemoryEntry& e) { return e.id == entry.id; }));

    entry.text = text;                          // 填入记忆内容
    entry.kind = kind.empty() ? "fact" : kind; // 未指定类型时默认为 fact

    entries_.push_back(entry);   // 先写入内存，供本次会话内读取
    saveToFile();                // 再落盘到日志文件，保证跨会话持久化

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

// 从日志文件加载并重建条目（init 时调用）。
// 文件不存在或格式无效时清空并返回（从空开始，不抛异常）；
// 仅接受数组格式，逐项解析字段，跳过缺 id 或 text 的无效条目。
void LongTermMemoryStore::loadFromFile()
{
    entries_.clear();

    if (!utils::file::exists(path_)) {
        spdlog::debug("[LongTermMemory] 日志文件不存在，从空开始: {}", path_);
        return;
    }

    // 读取并解析日志文件；失败（文件损坏/非 JSON）时返回空指针。
    auto j = ProjectIO::loadJsonFile(path_);
    // 日志必须是 JSON 数组（每项一条记忆）；空指针或非数组都视为格式无效，从空开始。
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

// 将当前条目写回日志文件（调用方需持有锁）。
// 步骤：把 entries_ 序列化为 JSON 数组 → 确保目标目录存在 → 落盘。
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
    // 首次写入前目录可能不存在（如 .novelagent/ 尚未创建），先建目录，
    // 否则 saveJsonFile 写文件会因目标目录缺失而失败。
    if (!dir.empty() && !utils::file::exists(dir)) {
        utils::file::createDirs(dir);
    }
    ProjectIO::saveJsonFile(path_, j);
}

} // namespace agent
