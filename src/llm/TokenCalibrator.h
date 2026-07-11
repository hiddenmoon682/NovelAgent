#pragma once

/// 自校准 Token 估算修正器。
///
/// 利用每次 API 调用返回的真实 prompt_tokens 作为 ground truth，
/// 按模型名独立维护 EMA（指数移动平均）修正因子。
///
/// 轻量级设计：不引入外部依赖，仅使用 STL。

#include <string>
#include <unordered_map>
#include <vector>

namespace llm {

class TokenCalibrator {
public:
    /// 默认构造，修正因子初始化为 1.0（不做修正）。
    TokenCalibrator() = default;

    /// 注册一次校准观测值。
    /// @param model      模型名（如 "deepseek-chat", "gpt-4o"）
    /// @param estimated  请求前的启发式估算 token 数
    /// @param actual     API 返回的真实 prompt_tokens
    void calibrate(const std::string& model, int estimated, int actual);

    /// 查询指定模型的当前修正因子。
    /// 返回 [0.1, 3.0] 范围内的值，默认 1.0（不做修正）。
    double getCorrection(const std::string& model) const;

    /// 将启发式估算值乘以修正因子。
    /// @param estimated  启发式估算值
    /// @param model      模型名
    /// @returns 修正后的估算值
    int apply(const std::string& model, int estimated) const {
        return static_cast<int>(estimated * getCorrection(model));
    }

    /// 重置指定模型的校准数据。
    void reset(const std::string& model);

    /// 重置所有模型的校准数据。
    void resetAll();

    /// 返回已校准的模型列表（用于调试/诊断）。
    std::vector<std::string> calibratedModels() const;

    /// 校准统计摘要（用于调试/诊断）。
    struct CalibrationStats {
        double correction = 1.0;
        int observations = 0;
        int total_estimated = 0;
        int total_actual = 0;
    };
    CalibrationStats stats(const std::string& model) const;

private:
    /// 单模型的 EMA 校准状态。
    struct ModelCalibration {
        double correction = 1.0;      ///< EMA 修正因子
        double smoothed_ratio = 1.0;  ///< EMA 内部状态（等同于 correction）
        int observations = 0;         ///< 校准次数
        int total_estimated = 0;      ///< 累计估算值（调试用）
        int total_actual = 0;         ///< 累计实际值（调试用）
    };

    /// EMA 平滑系数（α=0.3，即新观测值权重 30%，历史权重 70%）。
    /// 选择原因：约 7 次观测后收敛到接近真实值，
    /// 对单次异常值不敏感，响应速度和稳定性之间取得平衡。
    static constexpr double kAlpha = 0.3;

    /// 修正因子安全钳位 [min, max]。
    /// 正常情况下 ratio 通常在 [0.5, 2.0] 之间，极端值直接截断以防污染 EMA。
    static constexpr double kMinCorrection = 0.1;
    static constexpr double kMaxCorrection = 3.0;

    std::unordered_map<std::string, ModelCalibration> models_;
};

} // namespace llm
