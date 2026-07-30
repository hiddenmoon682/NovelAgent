#pragma once

// TokenCounter — 启发式 Token 估算器 + 自校准修正（整合自 TokenCalibrator）。
//
// 两类接口：
//   静态方法（无状态） — 纯字符级启发式估算，任何地方可用。
//   实例方法（有状态） — 按模型维护 EMA 修正因子，校准启发式估算值。
//
// 使用示例：
//   // 静态估算
//   int t = TokenCounter::countTokens("你好 world");
//
//   // 带校准的估算（需要 TokenCounter 实例）
//   TokenCounter counter;
//   int estimated = TokenCounter::countTokensCalibrated("你好 world", "deepseek-v4-flash", &counter);
//   counter.calibrate("deepseek-v4-flash", estimated, actual_from_api);

#include "llm/Message.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace llm {

class TokenCounter {
public:
    TokenCounter() = default;

    // ===================================================================
    // 静态估算（纯启发式，无状态）
    // ===================================================================

    // 估算单段文本的 token 数（中文: 每字 0.75, 英文: 每词 1.3）。
    static int countTokens(const std::string& text);

    // 估算消息列表的总 token 数（含角色标记和工具调用的结构开销）。
    static int countMessages(const std::vector<Message>& messages);

    // 估算单条消息的 token 数（避免为截断循环构造临时 vector）。
    static int countSingleMessage(const Message& msg);

    // 统计文本中的中文字符数（CJK 统一表意文字）。
    static int estimateChineseChars(const std::string& text);

    // 统计文本中的英文单词数（连续拉丁字母序列）。
    static int estimateEnglishWords(const std::string& text);

    // ===================================================================
    // 校准（实例方法，按模型维护 EMA 修正因子）
    // ===================================================================

    // 注册一次校准观测值。
    // model      模型名（如 "deepseek-v4-flash", "gpt-4o"）
    // estimated  请求前的启发式估算 token 数
    // actual     API 返回的真实 prompt_tokens
    void calibrate(const std::string& model, int estimated, int actual);

    // 将启发式估算值乘以修正因子。
    // estimated  启发式估算值
    // model      模型名（空字符串时返回 original 原值）
    // returns 修正后的估算值
    int apply(const std::string& model, int estimated) const;

    // countTokens + 校准一步完成（静态版本，calibrator 为 nullptr 时不做校准）。
    // model      模型名（空字符串时不校准）
    // calibrator TokenCounter 实例指针（nullptr 时退化到纯估算）
    static int countTokensCalibrated(const std::string& text, const std::string& model,
                                     const TokenCounter* calibrator = nullptr);
    // countMessages + 校准一步完成（静态版本）。
    static int countMessagesCalibrated(const std::vector<Message>& messages, const std::string& model,
                                       const TokenCounter* calibrator = nullptr);

    // 查询指定模型的当前修正因子。
    // 返回 [0.1, 3.0] 范围内的值，默认 1.0（不做修正）。
    double getCorrection(const std::string& model) const;

    // 重置指定模型的校准数据。
    void reset(const std::string& model);

    // 重置所有模型的校准数据。
    void resetAll();

    // 返回已校准的模型列表（用于调试/诊断）。
    std::vector<std::string> calibratedModels() const;

    // 校准统计摘要（用于调试/诊断）。
    struct CalibrationStats {
        double correction = 1.0;
        int observations = 0;
        int total_estimated = 0;
        int total_actual = 0;
    };
    CalibrationStats stats(const std::string& model) const;

private:
    // 单模型的 EMA 校准状态。
    struct ModelCalibration {
        double correction = 1.0;      // EMA 修正因子
        int observations = 0;         // 校准次数
        int total_estimated = 0;      // 累计估算值（调试用）
        int total_actual = 0;         // 累计实际值（调试用）
    };

    // 自适应 EMA：前 kFastObservations 次用较大 α 快速逼近，之后用小 α 稳定跟踪。
    static constexpr int kFastObservations = 5;
    static constexpr double kAlphaFast = 0.5;
    static constexpr double kAlphaSlow = 0.1;

    // 修正因子安全钳位 [min, max]。
    // 正常情况下 ratio 通常在 [0.5, 2.0] 之间，极端值直接截断以防污染 EMA。
    static constexpr double kMinCorrection = 0.1;
    static constexpr double kMaxCorrection = 3.0;

    std::unordered_map<std::string, ModelCalibration> models_;
};

} // namespace llm
