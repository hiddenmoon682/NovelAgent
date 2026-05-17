#pragma once

// 标准库里常用但分散的字符串辅助操作。
// 这些函数都足够轻量，直接内联即可。

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace utils::string {

inline void ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
        [](unsigned char c) { return !std::isspace(c); }));
}

inline void rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
        [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
}

inline void trim(std::string& s) {
    ltrim(s);
    rtrim(s);
}

inline std::string trimmed(std::string s) {
    trim(s);
    return s;
}

// 按分隔符拆分字符串，保留空片段。
inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::string token;
    std::istringstream stream(s);
    while (std::getline(stream, token, delim)) {
        result.push_back(token);
    }
    return result;
}

inline std::string join(const std::vector<std::string>& parts, const std::string& delim) {
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += delim;
        result += parts[i];
    }
    return result;
}

inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
        s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::toupper(c); });
    return s;
}

} // namespace utils::string
