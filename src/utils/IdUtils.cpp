#include "utils/IdUtils.h"

#include <cctype>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace utils::id {

std::string formatSequentialId(const std::string& prefix, int number) {
    std::string num = std::to_string(number);
    // 补零到 3 位；超过 3 位（>=1000）时不截断，保留完整数字
    if (num.size() < 3) {
        num.insert(0, 3 - num.size(), '0');
    }
    return prefix + num;
}

std::optional<int> tryParseIdNumber(const std::string& id, const std::string& prefix) {
    // 前缀不匹配（含 id 短于前缀的情况）直接返回 nullopt
    if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0) {
        return std::nullopt;
    }

    const std::string suffix = id.substr(prefix.size());
    // WHY: 先做纯数字校验再 stoi——std::stoi 会接受 "5x" 这类部分数字并解析出 5，
    // 非标准 ID 应整体跳过而不是取部分值参与编号统计
    for (char c : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return std::nullopt;
        }
    }

    try {
        return std::stoi(suffix);
    } catch (const std::exception& e) {
        // 纯数字但超出 int 范围（out_of_range）等情况：记录后按解析失败处理
        spdlog::debug("[IdUtils] 解析 ID 尾号失败: id='{}' prefix='{}': {}", id, prefix, e.what());
        return std::nullopt;
    }
}

} // namespace utils::id
