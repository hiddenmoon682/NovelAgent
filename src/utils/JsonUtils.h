#pragma once

// Helpers for safer nlohmann::json access.
// getOpt returns nullopt when the key doesn't exist or is null —
// this avoids the exception nlohmann throws for missing keys.
// getOrDefault returns the default when the key is missing.

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
