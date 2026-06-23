#pragma once

/// Models.h 内部辅助函数 — 供各子头文件共享。
///
/// 包含：
///   - getMetadataWithUnknownKeys() — 读取 metadata 并吸收未知字段
///   - contains() — 向量字符串查找
///   - hasAnyTag()  — 标签交集判断

#include "utils/JsonUtils.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace project::model_detail {

using json = nlohmann::json;
using JsonMap = std::map<std::string, json>;

inline JsonMap getMetadataWithUnknownKeys(const json& j, const std::set<std::string>& knownKeys) {
    JsonMap metadata = utils::json::getOrDefault<JsonMap>(j, "metadata", {});
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!knownKeys.count(it.key())) {
            metadata[it.key()] = it.value();
        }
    }
    return metadata;
}

inline bool contains(const std::vector<std::string>& values, const std::string& target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

inline bool hasAnyTag(const std::vector<std::string>& left, const std::vector<std::string>& right) {
    for (const auto& value : left) {
        if (contains(right, value)) {
            return true;
        }
    }
    return false;
}

} // namespace project::model_detail
