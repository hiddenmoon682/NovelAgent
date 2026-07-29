#pragma once

// 顺序 ID 的格式化与解析工具。
// 项目内的实体 ID 统一采用 "prefix-NNN" 补零格式（如 ch-001、char-012、setting-003），
// 此前各工具文件各自实现补零与 std::stoi 解析（含空 catch），逻辑重复且易漂移，
// 统一收口到这里（D1+D2 修复）。

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace utils::id {

// 生成补零 3 位的顺序 ID。
//
// 格式与磁盘上已有 ID 保持一致：number 补零到 3 位，
// 例如 formatSequentialId("ch-", 5) → "ch-005"。
// number >= 1000 时不截断，直接拼接完整数字（如 "ch-1000"）。
//
// Args:
//   prefix: ID 前缀，需包含分隔符，如 "ch-"、"setting-"。
//   number: 序号，应为正整数。
//
// Returns:
//   拼接后的 ID 字符串。
std::string formatSequentialId(const std::string& prefix, int number);

// 安全解析 "prefix-NNN" 形式 ID 的数字尾号。
//
// 前缀不匹配、尾号为空或含非数字字符时返回 std::nullopt，
// 数字超出 int 范围等异常情况会以 spdlog::debug 记录后返回 std::nullopt，
// 不会向调用方抛出异常。
//
// Args:
//   id: 待解析的完整 ID，如 "ch-005"。
//   prefix: 期望的前缀（含分隔符），如 "ch-"。
//
// Returns:
//   解析成功时返回尾号数值（如 5），否则返回 std::nullopt。
std::optional<int> tryParseIdNumber(const std::string& id, const std::string& prefix);

// 在容器中按 ID 查找元素。
//
// 约定：元素类型 T 需含可与 std::string 比较的 id 成员（项目内实体均满足）。
// 各工具文件此前各自实现 findChapter/findCharacter 等同构查找，
// 统一收口到这里（D1 修复）。
//
// Args:
//   items: 待查找的实体容器。
//   id: 目标实体 ID，如 "ch-005"。
//
// Returns:
//   命中时返回指向容器内元素的指针（非拷贝），未命中返回 nullptr。
template <typename T>
T* findById(std::vector<T>& items, const std::string& id) {
    auto it = std::find_if(items.begin(), items.end(),
        [&](const T& e) { return e.id == id; });
    return (it != items.end()) ? &(*it) : nullptr;
}

} // namespace utils::id
