// EmojiStripFilter 实现 — 流式文本 emoji 剥离（纯 C++、无 Qt 依赖）。
//
// 处理流程：feed() 先拼接上次未完成的 UTF-8 尾字节与本次增量，再逐码位
// 解码处理；不完整的序列留在内部缓存等待下一次 feed()。键帽 emoji 通过
// "持有数字/#/* 单码位 + 等待 U+FE0F / U+20E3" 的轻量状态机识别，
// 归一化为纯文本，避免把裸数字误伤，也不产生"数字套残框"。

#include "llm/EmojiStripFilter.h"

namespace llm {

namespace {

// 键帽归一化输出："1️⃣"→"1."、"🔟"→"10."、"#️⃣"→"#"、"*️⃣"→"*"；
// 其余码位（不会走到这里）原样输出。
std::string keycapEmit(char32_t base) {
    if (base >= '0' && base <= '9') {
        std::string s;
        s += static_cast<char>(base);
        s += '.';
        return s;
    }
    std::string s;
    s += static_cast<char>(base);
    return s;
}

} // namespace

// 处理一段增量文本；返回过滤后的文本，内部状态（未完成 UTF-8 尾字节、
// 键帽持有状态）跨调用保留。
std::string EmojiStripFilter::feed(const std::string& chunk) {
    pending_ += chunk;

    std::string out;
    size_t i = 0;
    const size_t n = pending_.size();

    while (i < n) {
        const unsigned char b = static_cast<unsigned char>(pending_[i]);
        int len = 0;
        char32_t cp = 0;

        if (b < 0x80) {
            len = 1;
            cp = b;
        } else if (b >= 0xC2 && b <= 0xDF) {
            len = 2;
            cp = b & 0x1F;
        } else if (b >= 0xE0 && b <= 0xEF) {
            len = 3;
            cp = b & 0x0F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            len = 4;
            cp = b & 0x07;
        } else {
            // 无效首字节（游离续字节 / 0xC0、0xC1 / 0xF5+）：丢弃该字节
            ++i;
            continue;
        }

        if (i + len > n)
            break;  // 序列不完整：留在 pending_，等待下次 feed()

        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const unsigned char c = static_cast<unsigned char>(pending_[i + k]);
            if ((c & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (c & 0x3F);
        }
        if (!ok) {
            ++i;  // 续字节失效：丢弃该首字节，继续向后重试
            continue;
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)
            || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            ++i;  // 过长编码 / 越界 / UTF-16 代理区：无效序列，丢弃
            continue;
        }

        // ── 键帽持有状态机 ──
        if (hold_active_) {
            if (!hold_mark_seen_) {
                if (cp == 0xFE0F) {
                    hold_mark_seen_ = true;
                    i += len;
                    continue;
                }
                if (cp == 0x20E3) {
                    out += keycapEmit(hold_cp_);  // "1⃣"（无 FE0F 写法）同样归一化
                    hold_active_ = false;
                    i += len;
                    continue;
                }
                out += emitCp(hold_cp_);  // 普通数字/#/*：原样释放
                hold_active_ = false;
                // 非键帽装饰：释放持有后按常规处理当前码位（落入下方分支）
            } else {
                if (cp == 0x20E3) {
                    out += keycapEmit(hold_cp_);
                    hold_active_ = false;
                    i += len;
                    continue;
                }
                out += emitCp(hold_cp_);  // 只有 FE0F 无键帽框：留数字、丢装饰
                hold_active_ = false;
                // 落入下方常规处理
            }
        }

        // ── 常规码位处理 ──
        if (cp == 0xFE0F || cp == 0x20E3 || cp == 0x200D) {
            // 变体选择符 / 游离键帽框 / 零宽连接符：装饰码位，剥离
        } else if (cp == 0x1F51F) {
            out += "10.";  // 🔟 数字键
        } else if ((cp >= 0x1F000 && cp <= 0x1FAFF)
                   || (cp >= 0x2600 && cp <= 0x27BF)
                   || (cp >= 0x2B00 && cp <= 0x2BFF)) {
            // emoji 主区段（含旗帜 / 皮肤色 / ZWJ 子码位）→ 剥离
        } else if ((cp >= '0' && cp <= '9') || cp == '#' || cp == '*') {
            // 数字 / # / *：先持有，等待下一个码位判定是否键帽
            hold_active_ = true;
            hold_mark_seen_ = false;
            hold_cp_ = cp;
        } else {
            out += cp < 0x80
                ? std::string(1, static_cast<char>(cp))
                : emitCp(cp);
        }

        i += len;
    }

    pending_.erase(0, i);
    return out;
}

// 流结束冲刷：结尾孤立数字（如被截断的键帽）保留数字本身；
// 未完成的 UTF-8 尾字节按无效数据处理。
std::string EmojiStripFilter::finish() {
    std::string out;
    if (hold_active_)
        out += emitCp(hold_cp_);  // 孤立数字 / # / * 原样释放（不补 "."）
    hold_active_ = false;
    hold_mark_seen_ = false;
    hold_cp_ = 0;
    pending_.clear();
    return out;
}

// 单次处理完整文本（非流式路径）：内部构造临时实例，无状态、可并发。
std::string EmojiStripFilter::stripOnce(const std::string& text) {
    EmojiStripFilter f;
    std::string r = f.feed(text);
    r += f.finish();
    return r;
}

void EmojiStripFilter::reset() {
    pending_.clear();
    hold_active_ = false;
    hold_mark_seen_ = false;
    hold_cp_ = 0;
}

// 码位 → UTF-8 编码串。
std::string EmojiStripFilter::emitCp(char32_t cp) const {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

} // namespace llm
