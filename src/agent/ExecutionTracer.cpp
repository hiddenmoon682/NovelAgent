/// ExecutionTracer 实现。

#include "agent/ExecutionTracer.h"

#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>

using json = nlohmann::json;

namespace agent {

// ===========================================================================
// TraceEntry
// ===========================================================================

TraceEntry TraceEntry::make(const std::string& type, int tokens, int ms) {
    TraceEntry e;
    e.event_type = type;
    e.tokens_used = tokens;
    e.duration_ms = ms;
    return e;
}

// ===========================================================================
// ExecutionTracer
// ===========================================================================

void ExecutionTracer::record(const TraceEntry& entry) {
    TraceEntry e = entry;
    e.step_index = step_index_++;
    e.timestamp = nowTimestamp();
    entries_.push_back(std::move(e));
}

void ExecutionTracer::record(const std::string& event_type, int tokens,
                              int duration_ms, const json& payload) {
    TraceEntry e;
    e.event_type = event_type;
    e.tokens_used = tokens;
    e.duration_ms = duration_ms;
    e.payload = payload;
    e.step_index = step_index_++;
    e.timestamp = nowTimestamp();
    entries_.push_back(std::move(e));

    spdlog::debug("[ExecutionTracer] {} ({} tokens, {}ms)",
                  event_type, tokens, duration_ms);
}

std::string ExecutionTracer::dump(const std::string& dir_path) const {
    if (entries_.empty()) return {};

    utils::file::createDirs(dir_path);
    std::string session_id = generateSessionId();
    std::string path = utils::file::joinPath(dir_path, session_id + ".jsonl");

    std::ostringstream oss;
    for (const auto& e : entries_) {
        json j;
        j["timestamp"] = e.timestamp;
        j["step_index"] = e.step_index;
        j["event_type"] = e.event_type;
        j["payload"] = e.payload;
        j["tokens_used"] = e.tokens_used;
        j["duration_ms"] = e.duration_ms;
        oss << j.dump() << "\n";
    }

    utils::file::writeText(path, oss.str());
    spdlog::info("[ExecutionTracer] 轨迹已保存: {} ({} 条)", path, entries_.size());
    return path;
}

json ExecutionTracer::summary() const {
    json s;
    s["total_steps"] = entries_.size();

    int total_tokens = 0, total_ms = 0;
    int llm_calls = 0, tool_calls = 0, errors = 0;
    for (const auto& e : entries_) {
        total_tokens += e.tokens_used;
        total_ms += e.duration_ms;
        if (e.event_type == "llm_call") ++llm_calls;
        else if (e.event_type == "tool_call") ++tool_calls;
        else if (e.event_type == "error") ++errors;
    }

    s["total_tokens"] = total_tokens;
    s["total_duration_ms"] = total_ms;
    s["llm_calls"] = llm_calls;
    s["tool_calls"] = tool_calls;
    s["errors"] = errors;

    if (!entries_.empty()) {
        s["avg_tokens_per_call"] = llm_calls > 0 ? total_tokens / llm_calls : 0;
        s["avg_duration_ms"] = total_ms / static_cast<int>(entries_.size());
    }

    return s;
}

std::string ExecutionTracer::recentSummary(int n) const {
    if (entries_.empty()) return "暂无执行轨迹。\n";

    std::ostringstream ss;
    int start = std::max(0, static_cast<int>(entries_.size()) - n);

    ss << "最近 " << (static_cast<int>(entries_.size()) - start) << " 步:\n";
    for (int i = start; i < static_cast<int>(entries_.size()); ++i) {
        const auto& e = entries_[i];
        ss << "  " << e.step_index << ". [" << e.event_type << "]"
           << " tokens=" << e.tokens_used
           << " time=" << e.duration_ms << "ms\n";
    }

    auto s = summary();
    ss << "\n总计: " << s["total_steps"].get<int>() << " 步, "
       << s["total_tokens"].get<int>() << " tokens, "
       << s["total_duration_ms"].get<int>() << "ms";

    return ss.str();
}

// ===========================================================================
// 辅助
// ===========================================================================

std::string ExecutionTracer::nowTimestamp() {
    return ProjectIO::nowTimestamp();
}

std::string ExecutionTracer::generateSessionId() {
    auto now = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    // 加随机后缀避免同一秒内多次会话冲突（mutex 保护 RNG 线程安全）
    static std::mt19937 rng(static_cast<unsigned>(now));
    static std::mutex rng_mutex;
    {
        std::lock_guard<std::mutex> lock(rng_mutex);
        oss << "-" << (rng() % 10000);
    }
    return oss.str();
}

} // namespace agent
