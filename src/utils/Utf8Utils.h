#pragma once

// UTF-8 / UTF-32（码点）转换与计量工具 — 基于 simdutf 的薄封装。
//
// 编码校验与转换全部交给 simdutf（third_party/simdutf，single-header
// amalgamation，运行时指令分派 + fuzz 打磨），本文件只负责
// std::string/std::u32string 的适配。
//
// 为什么需要这套转换：std::string 的下标是字节而非字符，中文 1 字在
// UTF-8 下占 3 字节，直接按下标索引/切分容易切在多字节字符中间产生乱码。
// 先转成 std::u32string（每个元素 1 个码点，中文占 1 位）再按"字"处理，
// 最后转回 UTF-8 输出。

#include <cstdint>
#include <string>

#include <simdutf.h>

namespace utils::utf8 {

// UTF-8 → UTF-32（码点）。
//
// 正常路径：simdutf 校验 + SIMD 转换，输入应为合法 UTF-8（正文经文件读取）。
// 兜底路径：仅非法/截断输入触发，坏字节以 U+FFFD 代替并继续，
// 保证调用方不因异常数据中断（语义与旧手写实现一致）。
inline std::u32string utf8ToU32(const std::string& s)
{
    if (simdutf::validate_utf8(s.data(), s.size())) {
        std::u32string out(
            simdutf::utf32_length_from_utf8(s.data(), s.size()), U'\0');
        // 转换输出长度由 utf32_length_from_utf8 预先精确计算，返回值无需校验
        [[maybe_unused]] const size_t written =
            simdutf::convert_valid_utf8_to_utf32(s.data(), s.size(), out.data());
        return out;
    }

    // 兜底：逐码点扫描，任何非法情况都整体替换为 U+FFFD 并只前进 1 字节
    std::u32string out;
    out.reserve(s.size());
    const char* it = s.data();
    const char* end = it + s.size();
    while (it < end) {
        const unsigned char lead = static_cast<unsigned char>(*it);
        if ((lead & 0x80) == 0) {
            out.push_back(lead);
            ++it;
            continue;
        }
        uint32_t cp = 0xFFFD;
        const char* seq = it;
        if ((lead & 0xE0) == 0xC0) cp = lead & 0x1F;
        else if ((lead & 0xF0) == 0xE0) cp = lead & 0x0F;
        else if ((lead & 0xF8) == 0xF0) cp = lead & 0x07;

        // 尝试解析当前引导字节指示的变长序列；失败则退化为 1 个 U+FFFD
        bool valid = cp != 0xFFFD;
        int len = 1;
        if (valid) {
            len = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
            valid = it + len <= end;
            for (int k = 1; valid && k < len; ++k) {
                const unsigned char cc =
                    static_cast<unsigned char>(it[k]);
                if ((cc & 0xC0) != 0x80) { valid = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            // 剔除 overlong、代理区与超出 U+10FFFF 的码点
            if (valid && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800)
                          || (len == 4 && cp < 0x10000) || cp > 0x10FFFF
                          || (cp >= 0xD800 && cp <= 0xDFFF))) {
                valid = false;
            }
        }
        out.push_back(valid ? cp : 0xFFFD);
        it = valid ? seq + len : seq + 1;
    }
    return out;
}

// UTF-32（码点）→ UTF-8（中文 1 码点 3 字节）。
// 输入来自本工具的 utf8ToU32（保证为合法码点序列），直走 simdutf 转换。
inline std::string u32ToUtf8(const std::u32string& s)
{
    if (s.empty()) return {};
    std::string out;
    out.resize(simdutf::utf8_length_from_utf32(s.data(), s.size()));
    // 转换输出长度由 utf8_length_from_utf32 预先精确计算，返回值无需校验
    [[maybe_unused]] const size_t written =
        simdutf::convert_valid_utf32_to_utf8(s.data(), s.size(), out.data());
    return out;
}

// 单个码点的 UTF-8 字节长度（1/2/3/4）。
// 纯长度算术，非编码解析，无需引库。
inline int utf8ByteLength(char32_t cp)
{
    if (cp < 0x80) return 1;
    if (cp < 0x800) return 2;
    if (cp < 0x10000) return 3;
    return 4;
}

// 码点串对应的 UTF-8 字节数（按字节口径计量时无需为统计做完整转换）。
inline size_t utf8ByteCount(const std::u32string& s)
{
    size_t bytes = 0;
    for (char32_t cp : s) bytes += utf8ByteLength(cp);
    return bytes;
}

} // namespace utils::utf8