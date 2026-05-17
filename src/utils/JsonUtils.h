#pragma once

// 更安全地访问 nlohmann::json 的辅助函数。
// getOpt 在键不存在或值为 null 时返回 nullopt，
// 可以避免直接访问缺失字段时抛异常。
// getOrDefault 在键缺失时返回默认值。

#include <nlohmann/json.hpp>
#include <string>
#include <optional>

namespace utils::json {

template<typename T>
std::optional<T> getOpt(const nlohmann::json& j, const std::string& key) {
    if (j.contains(key) && !j[key].is_null()) {
        return j[key].get<T>();
    }
    return std::nullopt;
}

template<typename T>
T getOrDefault(const nlohmann::json& j, const std::string& key, const T& defaultValue) {
    if (j.contains(key) && !j[key].is_null()) {
        return j[key].get<T>();
    }
    return defaultValue;
}

} // namespace utils::json
