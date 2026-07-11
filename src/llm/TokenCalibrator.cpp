/// TokenCalibrator 实现 — 自校准 Token 估算修正器。

#include "llm/TokenCalibrator.h"

#include <algorithm>
#include <cmath>

namespace llm {

void TokenCalibrator::calibrate(const std::string& model, int estimated, int actual) {
    // 防御：估算值或真实值无效时不记录
    if (estimated <= 0 || actual <= 0) return;

    auto& mc = models_[model];
    double ratio = static_cast<double>(actual) / estimated;

    // 钳制 ratio 到合理范围，防止单次异常值污染 EMA
    ratio = std::clamp(ratio, kMinCorrection, kMaxCorrection);

    if (mc.observations == 0) {
        // 首次观测：直接用 ratio 作为初始值（不用默认 1.0 平滑，加速收敛）
        mc.correction = ratio;
        mc.smoothed_ratio = ratio;
    } else {
        // EMA: new = α × current_ratio + (1-α) × previous
        mc.smoothed_ratio = kAlpha * ratio + (1.0 - kAlpha) * mc.smoothed_ratio;
        mc.correction = mc.smoothed_ratio;
    }

    mc.observations++;
    mc.total_estimated += estimated;
    mc.total_actual += actual;
}

double TokenCalibrator::getCorrection(const std::string& model) const {
    auto it = models_.find(model);
    if (it == models_.end()) return 1.0; // 无校准数据，不做修正
    return std::clamp(it->second.correction, kMinCorrection, kMaxCorrection);
}

void TokenCalibrator::reset(const std::string& model) {
    models_.erase(model);
}

void TokenCalibrator::resetAll() {
    models_.clear();
}

std::vector<std::string> TokenCalibrator::calibratedModels() const {
    std::vector<std::string> result;
    result.reserve(models_.size());
    for (const auto& pair : models_) {
        result.push_back(pair.first);
    }
    return result;
}

TokenCalibrator::CalibrationStats TokenCalibrator::stats(const std::string& model) const {
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
