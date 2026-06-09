/// ChapterSummaryCache 实现。

#include "agent/ChapterSummaryCache.h"

#include "utils/FileUtils.h"
#include "utils/JsonUtils.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace agent {

std::optional<ChapterSummaryEntry> ChapterSummaryCache::get(const std::string& chapter_id)
{
    auto all = loadAll();
    auto it = all.find(chapter_id);
    if (it != all.end()) return it->second;
    return std::nullopt;
}

void ChapterSummaryCache::update(const ChapterSummaryEntry& entry)
{
    auto all = loadAll();
    all[entry.chapter_id] = entry;

    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, summary] : all) {
        nlohmann::json e;
        e["chapter_id"] = summary.chapter_id;
        e["summary"] = summary.summary;
        e["characters"] = summary.characters;
        e["settings"] = summary.settings;
        e["key_events"] = summary.key_events;
        e["updated_at"] = summary.updated_at;
        j[id] = e;
    }

    const std::string path = utils::file::joinPath(storage_.agentDir(), kSummariesFile);
    storage_.saveJson(path, j);
    spdlog::debug("[ChapterSummaryCache] 更新: {}", entry.chapter_id);
}

std::map<std::string, ChapterSummaryEntry> ChapterSummaryCache::loadAll()
{
    std::map<std::string, ChapterSummaryEntry> result;
    const std::string path = utils::file::joinPath(storage_.agentDir(), kSummariesFile);

    auto j = storage_.loadJson(path);
    if (j.is_null() || !j.is_object()) return result;

    for (auto it = j.begin(); it != j.end(); ++it) {
        ChapterSummaryEntry entry;
        entry.chapter_id = it.key();
        const auto& val = it.value();
        entry.summary = utils::json::getOrDefault(val, "summary", std::string{});
        entry.characters = utils::json::getOrDefault(val, "characters", std::vector<std::string>{});
        entry.settings = utils::json::getOrDefault(val, "settings", std::vector<std::string>{});
        entry.key_events = utils::json::getOrDefault(val, "key_events", std::vector<std::string>{});
        entry.updated_at = utils::json::getOrDefault(val, "updated_at", std::string{});
        result[entry.chapter_id] = entry;
    }
    return result;
}

} // namespace agent
