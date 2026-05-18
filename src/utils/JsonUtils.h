#pragma once

// 更安全地访问 nlohmann::json 的辅助函数。
// getOpt 在键不存在或值为 null 时返回 nullopt，
// 可以避免直接访问缺失字段时抛异常。
// getOrDefault 在键缺失时返回默认值。

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace utils::json {

// 尝试读取指定键并转换为目标类型。
// 如果键不存在或值为 null，则返回 std::nullopt。
template<typename T>
std::optional<T> getOpt(const nlohmann::json& j, const std::string& key) {
    if (j.contains(key) && !j[key].is_null()) {
        return j[key].get<T>();
    }
    return std::nullopt;
}

// 读取指定键并转换为目标类型。
// 如果键不存在或值为 null，则返回调用方提供的默认值。
template<typename T>
T getOrDefault(const nlohmann::json& j, const std::string& key, const T& defaultValue) {
    if (j.contains(key) && !j[key].is_null()) {
        return j[key].get<T>();
    }
    return defaultValue;
}

// 读取对象字段；若字段缺失、为 null 或不是 object，则返回空对象。
// 适合处理 metadata 这类“可选但应当保持为 object”的字段。
inline nlohmann::json getObjectOrEmpty(const nlohmann::json& j, const std::string& key) {
    if (j.contains(key) && j[key].is_object()) {
        return j[key];
    }
    return nlohmann::json::object();
}

} // namespace utils::json
