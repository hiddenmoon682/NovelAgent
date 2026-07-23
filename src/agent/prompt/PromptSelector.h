#pragma once

/// PromptSelector — 从 Project 中筛选与目标章节关联的对象的共享逻辑。
///
/// 职责：提供复用选择函数供 PromptContextBuilder 和新工具使用。
/// 原位于 PromptContextBuilder.cpp 的匿名命名空间中，现提取为公开 API。

#include "project/Models.h"
#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace prompt {
namespace selector {

// ============================================================================
// 通用辅助函数（模板）
// ============================================================================

/// 在 vector<T> 中按 id 查找对象。所有 Model 类型均有 id 字段。
template<typename T>
const T* findById(const std::vector<T>& values, const std::string& id) {
    for (const auto& value : values) {
        if (value.id == id) {
            return &value;
        }
    }
    return nullptr;
}

/// 判断 JSON 值是否"有意义"——非 null、非空字符串、非空数组/对象。
inline bool isMeaningfulValue(const nlohmann::json& value) {
    if (value.is_null()) return false;
    if (value.is_string()) return !value.get<std::string>().empty();
    if (value.is_array() || value.is_object()) return !value.empty();
    return true;
}

/// 对任意 Model 对象执行字段清理：
///   - 可选跳过 "metadata" 字段
///   - alwaysInclude 中的字段强制保留（即使为空值）
///   - 其余字段仅在有意义的值时才保留
template<typename T>
nlohmann::json filterObject(
    const T& object,
    bool includeMetadata,
    const std::set<std::string>& alwaysInclude = {})
{
    nlohmann::json raw = object;
    nlohmann::json filtered = nlohmann::json::object();

    for (auto it = raw.begin(); it != raw.end(); ++it) {
        const std::string key = it.key();
        if (key == "metadata" && !includeMetadata) continue;
        if (!alwaysInclude.count(key) && !isMeaningfulValue(it.value())) continue;
        filtered[key] = it.value();
    }
    return filtered;
}

/// 检查 id 是否出现在字符串列表中。
inline bool containsId(const std::vector<std::string>& values, const std::string& id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}

/// 向 vector 中追加元素，通过 unordered_set 去重。跳过空指针和空 id。
template<typename T>
void appendUnique(const T* value, std::vector<const T*>& out, std::unordered_set<std::string>& seen) {
    if (!value || value->id.empty()) return;
    if (seen.insert(value->id).second) {
        out.push_back(value);
    }
}

// ============================================================================
// 关联对象选择函数
// ============================================================================

/// 从 Project 中筛选与目标章节关联的剧情线。
std::vector<const PlotThread*> selectPlotThreads(
    const Project& project,
    const Chapter& chapter,
    std::size_t maxCount);

/// 从 Project 中筛选与目标章节关联的角色（按优先级：POV > 焦点 > 场景 > 剧情线 > 发展 > 出场）。
std::vector<const Character*> selectCharacters(
    const Project& project,
    const Chapter& chapter,
    const std::vector<const PlotThread*>& plotThreads,
    std::size_t maxCount);

/// 从 Project 中筛选与目标章节关联的场景/设定地点。
std::vector<const Setting*> selectSettings(
    const Project& project,
    const Chapter& chapter,
    const std::vector<const PlotThread*>& plotThreads,
    std::size_t maxCount);

/// 从 Project 中筛选与所选设定地点关联的世界观规则。
std::vector<const WorldRule*> selectWorldRules(
    const Project& project,
    const std::vector<const Setting*>& settings,
    std::size_t maxCount);

/// 将 JSON 数组渲染为 Markdown 列表段。
/// 每项取 name/title/id 作为标题，其余字段作为缩进键值对输出。
std::string renderSectionList(const nlohmann::json& values, const std::string& heading);

} // namespace selector
} // namespace prompt
