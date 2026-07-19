#include "llm/TokenCounter.h"
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>

namespace llm {

// ---------------------------------------------------------------------------
// UTF-8 解码 — 从当前字节位置解码一个 Unicode 码点，并推进迭代器
// ---------------------------------------------------------------------------
static uint32_t decodeUtf8(const char*& it, const char* end)
{
    unsigned char c = static_cast<unsigned char>(*it);
    uint32_t codepoint = 0;
    int remaining = 0;

    if ((c & 0x80) == 0) {
        // 单字节 ASCII: 0xxxxxxx
        ++it;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        remaining = 1;
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        remaining = 2;
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        remaining = 3;
    } else {
        // 非法首字节，跳过
        ++it;
        return 0xFFFD; // Unicode 替换字符
    }

    ++it;
    while (remaining > 0 && it < end) {
        c = static_cast<unsigned char>(*it);
        if ((c & 0xC0) != 0x80) break; // 非法的后续字节
        codepoint = (codepoint << 6) | (c & 0x3F);
        ++it;
        --remaining;
    }

    // 截断序列（字符串在码点中间结束）或非法后续字节
    if (remaining > 0) {
        return 0xFFFD; // Unicode 替换字符
    }

    return codepoint;
}

// ---------------------------------------------------------------------------
// 判断码点是否属于 CJK 统一表意文字范围
// ---------------------------------------------------------------------------
static bool isCJK(uint32_t cp)
{
    return (cp >= 0x4E00 && cp <= 0x9FFF)     // CJK 统一表意文字（常用汉字）
        || (cp >= 0x3400 && cp <= 0x4DBF)      // CJK Ext-A（罕用字）
        || (cp >= 0xF900 && cp <= 0xFAFF)      // CJK 兼容表意文字
        || (cp >= 0x20000 && cp <= 0x2A6DF);   // CJK Ext-B（历史字、异体字）
}

// ===========================================================================
// 公开接口
// ===========================================================================

int TokenCounter::estimateChineseChars(const std::string& text)
{
    int count = 0;
    const char* it = text.data();
    const char* end = it + text.size();

    while (it < end) {
        unsigned char c = static_cast<unsigned char>(*it);
        if ((c & 0x80) == 0) {
            ++it; // ASCII，跳过
        } else {
            uint32_t cp = decodeUtf8(it, end);
            if (isCJK(cp)) {
                ++count;
            }
        }
    }

    return count;
}

int TokenCounter::estimateEnglishWords(const std::string& text)
{
    int count = 0;
    bool inWord = false;

    for (char c : text) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            if (!inWord) {
                ++count;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }

    return count;
}

int TokenCounter::countTokens(const std::string& text)
{
    // 单次遍历同时统计中文和英文，避免双重扫描
    int chineseChars = 0;
    int englishWords = 0;
    bool inWord = false;

    const char* it = text.data();
    const char* end = it + text.size();

    while (it < end) {
        unsigned char c = static_cast<unsigned char>(*it);
        if ((c & 0x80) == 0) {
            // ASCII
            if (std::isalpha(c)) {
                if (!inWord) { ++englishWords; inWord = true; }
            } else {
                inWord = false;
            }
            ++it;
        } else {
            // 多字节 UTF-8 → 解码并判断 CJK
            uint32_t cp = decodeUtf8(it, end);
            if (isCJK(cp)) {
                ++chineseChars;
            }
        }
    }

    // 中文: 每字符约 0.60~0.75 token（取 0.75 偏保守）
    // 英文: 每单词约 1.2~1.5 token（取 1.3）
    return static_cast<int>(chineseChars * 0.75 + englishWords * 1.3);
}

int TokenCounter::countSingleMessage(const Message& msg)
{
    int total = countTokens(msg.content);
    total += countTokens(msg.tool_call_id);
    total += countTokens(msg.name);
    total += countTokens(msg.reasoning_content);
    total += 4; // 消息角色等元数据开销

    for (const auto& tc : msg.tool_calls) {
        total += countTokens(tc.function_name);
        total += countTokens(tc.arguments);
        total += 8; // 工具调用 JSON 结构开销
    }
    return total;
}

int TokenCounter::countMessages(const std::vector<Message>& messages)
{
    int total = 0;

    for (const auto& msg : messages) {
        total += countTokens(msg.content);
        total += countTokens(msg.tool_call_id);
        total += countTokens(msg.name);
        total += countTokens(msg.reasoning_content);
        total += 4; // 消息角色等元数据开销（约 4 token/条）

        for (const auto& tc : msg.tool_calls) {
            total += countTokens(tc.function_name);
            total += countTokens(tc.arguments);
            total += 8; // 工具调用 JSON 结构开销（约 8 token/次）
        }
    }

    return total;
}

// ===========================================================================
// 静态校准辅助（一步完成 count + apply）
// ===========================================================================

int TokenCounter::countTokensCalibrated(
    const std::string& text, const std::string& model,
    const TokenCounter* calibrator)
{
    int raw = countTokens(text);
    if (calibrator && !model.empty())
        return calibrator->apply(model, raw);
    return raw;
}

int TokenCounter::countMessagesCalibrated(
    const std::vector<Message>& messages, const std::string& model,
    const TokenCounter* calibrator)
{
    int raw = countMessages(messages);
    if (calibrator && !model.empty())
        return calibrator->apply(model, raw);
    return raw;
}

// ===========================================================================
// 校准实现（从 TokenCalibrator 合并）
// ===========================================================================

void TokenCounter::calibrate(const std::string& model, int estimated, int actual) {
    if (estimated <= 0 || actual <= 0) return;

    auto& mc = models_[model];
    double ratio = static_cast<double>(actual) / estimated;
    ratio = std::clamp(ratio, kMinCorrection, kMaxCorrection);

    if (mc.observations == 0) {
        mc.correction = ratio;
    } else {
        double alpha = (mc.observations < kFastObservations) ? kAlphaFast : kAlphaSlow;
        mc.correction = alpha * ratio + (1.0 - alpha) * mc.correction;
    }

    mc.observations++;
    mc.total_estimated += estimated;
    mc.total_actual += actual;
}

double TokenCounter::getCorrection(const std::string& model) const {
    auto it = models_.find(model);
    if (it == models_.end()) return 1.0;
    return std::clamp(it->second.correction, kMinCorrection, kMaxCorrection);
}

int TokenCounter::apply(const std::string& model, int estimated) const {
    if (model.empty()) return estimated;
    return static_cast<int>(estimated * getCorrection(model));
}

void TokenCounter::reset(const std::string& model) {
    models_.erase(model);
}

void TokenCounter::resetAll() {
    models_.clear();
}

std::vector<std::string> TokenCounter::calibratedModels() const {
    std::vector<std::string> result;
    result.reserve(models_.size());
    for (const auto& pair : models_) {
        result.push_back(pair.first);
    }
    return result;
}

TokenCounter::CalibrationStats TokenCounter::stats(const std::string& model) const {
    CalibrationStats s;
    auto it = models_.find(model);
    if (it != models_.end()) {
        s.correction = it->second.correction;
        s.observations = it->second.observations;
        s.total_estimated = it->second.total_estimated;
        s.total_actual = it->second.total_actual;
    }
    return s;
}

} // namespace llm
