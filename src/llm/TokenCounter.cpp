#include "llm/TokenCounter.h"
#include "utils/Utf8Utils.h"
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>

namespace llm {

// ---------------------------------------------------------------------------
// 判断码点是否属于 CJK 统一表意文字范围
//
// WHY 选这四个区段：基本区 + Ext-A + 兼容区 + Ext-B 已覆盖中文小说
// 文本中几乎全部汉字（含罕用字与异体字）。有意不含 CJK 标点（U+3000-303F）
// 与全角符号（U+FF00-FFEF）：它们的实际 token 开销与汉字不同，且占比低，
// 计入会系统性高估；日文假名/韩文同理不计（本项目面向中文创作场景）。
// 估算偏差由 TokenCounter 的 EMA 校准机制兜底，无需追求穷举全部区段。
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
    // 整串转 UTF-32（simdutf 校验 + SIMD 转换）后按码点统计，避免手写逐字节解析
    const std::u32string u = utils::utf8::utf8ToU32(text);
    int count = 0;
    for (char32_t cp : u) {
        if (isCJK(cp)) {
            ++count;
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
    // 单次 UTF-32 转换后统计中文和英文，避免双重扫描
    int chineseChars = 0;
    int englishWords = 0;
    bool inWord = false;

    const std::u32string u = utils::utf8::utf8ToU32(text);
    for (char32_t c : u) {
        if (c < 0x80) {
            // ASCII
            if (std::isalpha(static_cast<unsigned char>(c))) {
                if (!inWord) { ++englishWords; inWord = true; }
            } else {
                inWord = false;
            }
        }
        // 多字节字符不打断单词计数（与旧实现逐字节分支行为一致）
        if (isCJK(c)) {
            ++chineseChars;
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
    std::lock_guard<std::mutex> lock(mutex_);

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
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = models_.find(model);
    if (it == models_.end()) return 1.0;
    return std::clamp(it->second.correction, kMinCorrection, kMaxCorrection);
}

int TokenCounter::apply(const std::string& model, int estimated) const {
    if (model.empty()) return estimated;
    return static_cast<int>(estimated * getCorrection(model));
}

void TokenCounter::reset(const std::string& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    models_.erase(model);
}

void TokenCounter::resetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    models_.clear();
}

std::vector<std::string> TokenCounter::calibratedModels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(models_.size());
    for (const auto& pair : models_) {
        result.push_back(pair.first);
    }
    return result;
}

TokenCounter::CalibrationStats TokenCounter::stats(const std::string& model) const {
    std::lock_guard<std::mutex> lock(mutex_);
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
