/// TokenCalibrator 测试 — EMA 平滑、多模型隔离、边界情况。

#include "llm/TokenCalibrator.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() \
    do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) \
    do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); } } while(0)
#define CHECK_NEAR(a, b, eps) \
    do { if (std::abs((a) - (b)) > (eps)) { \
        std::cout << "FAILED: " << #a << "=" << (a) << " vs " << #b << "=" << (b) << " (eps=" << (eps) << ")\n"; \
        return; } } while(0)

void test_no_data_returns_one() {
    TEST("无校准数据时返回 1.0");
    llm::TokenCalibrator cal;
    CHECK(cal.getCorrection("unknown-model") == 1.0);
    CHECK(cal.apply("unknown-model", 100) == 100);
    PASS();
}

void test_first_calibration() {
    TEST("首次校准直接使用 ratio");
    llm::TokenCalibrator cal;
    // 估算 100，实际 80 → ratio = 0.8
    cal.calibrate("test-model", 100, 80);
    CHECK_NEAR(cal.getCorrection("test-model"), 0.8, 0.001);
    CHECK(cal.apply("test-model", 100) == 80);
    PASS();
}

void test_ema_smoothing() {
    TEST("EMA 平滑：多次校准");
    llm::TokenCalibrator cal;

    // 第 1 次：ratio = 0.8，首次直接用 → correction = 0.8
    cal.calibrate("m", 100, 80);
    auto c1 = cal.getCorrection("m");

    // 第 2 次：ratio = 0.7
    // EMA: 0.3 * 0.7 + 0.7 * 0.8 = 0.21 + 0.56 = 0.77
    cal.calibrate("m", 100, 70);
    auto c2 = cal.getCorrection("m");

    // 验证 EMA 在朝向新 ratio 移动
    CHECK(c2 < c1);  // 0.77 < 0.8
    CHECK_NEAR(c2, 0.77, 0.001);

    // 第 3 次：ratio = 0.9
    // EMA: 0.3 * 0.9 + 0.7 * 0.77 = 0.27 + 0.539 = 0.809
    cal.calibrate("m", 100, 90);
    CHECK_NEAR(cal.getCorrection("m"), 0.809, 0.001);

    PASS();
}

void test_multi_model_isolation() {
    TEST("多模型独立校准，互不干扰");
    llm::TokenCalibrator cal;

    cal.calibrate("model-a", 100, 80);  // ratio = 0.8
    cal.calibrate("model-b", 100, 120); // ratio = 1.2

    CHECK_NEAR(cal.getCorrection("model-a"), 0.8, 0.001);
    CHECK_NEAR(cal.getCorrection("model-b"), 1.2, 0.001);

    // 模型隔离后各自的校准数据独立
    cal.calibrate("model-a", 100, 90);  // EMA: 0.3*0.9 + 0.7*0.8 = 0.83
    CHECK_NEAR(cal.getCorrection("model-a"), 0.83, 0.001);
    CHECK_NEAR(cal.getCorrection("model-b"), 1.2, 0.001); // model-b 不变

    PASS();
}

void test_zero_defense() {
    TEST("除零保护：estimated=0 或 actual=0 时无影响");
    llm::TokenCalibrator cal;

    // estimated = 0
    cal.calibrate("m", 0, 100);
    CHECK(cal.getCorrection("m") == 1.0);  // 未创建条目

    // actual = 0
    cal.calibrate("m", 100, 0);
    CHECK(cal.getCorrection("m") == 1.0);  // 仍未创建条目

    // estimated < 0（防御）
    cal.calibrate("m", -1, 100);
    CHECK(cal.getCorrection("m") == 1.0);

    PASS();
}

void test_extreme_ratio_clamp() {
    TEST("极端比值钳位：ratio < 0.1 或 > 3.0 被截断");
    llm::TokenCalibrator cal;

    // ratio = 0.05 → 钳位到 0.1
    cal.calibrate("m", 1000, 50);
    CHECK_NEAR(cal.getCorrection("m"), 0.1, 0.001);

    // ratio = 5.0 → 钳位到 3.0
    cal.calibrate("m2", 100, 500);
    CHECK_NEAR(cal.getCorrection("m2"), 3.0, 0.001);

    PASS();
}

void test_reset() {
    TEST("reset 后恢复默认 1.0");
    llm::TokenCalibrator cal;

    cal.calibrate("m", 100, 80);
    CHECK(cal.getCorrection("m") != 1.0);

    cal.reset("m");
    CHECK(cal.getCorrection("m") == 1.0);

    // resetAll
    cal.calibrate("a", 100, 80);
    cal.calibrate("b", 100, 120);
    cal.resetAll();
    CHECK(cal.getCorrection("a") == 1.0);
    CHECK(cal.getCorrection("b") == 1.0);

    PASS();
}

void test_apply_uses_correction() {
    TEST("apply 返回 estimated * correction 的整数截断");
    llm::TokenCalibrator cal;

    cal.calibrate("m", 100, 75);  // correction = 0.75
    CHECK(cal.apply("m", 200) == 150);  // 200 * 0.75 = 150

    cal.calibrate("m", 200, 150);  // EMA 后 correction 接近 0.75
    CHECK(cal.apply("m", 100) == static_cast<int>(100 * cal.getCorrection("m")));

    PASS();
}

void test_calibrated_models_list() {
    TEST("calibratedModels 返回已校准模型列表");
    llm::TokenCalibrator cal;

    CHECK(cal.calibratedModels().empty());

    cal.calibrate("a", 100, 80);
    cal.calibrate("b", 100, 120);

    auto models = cal.calibratedModels();
    CHECK(models.size() == 2);

    PASS();
}

void test_stats() {
    TEST("stats 返回校准统计");
    llm::TokenCalibrator cal;

    cal.calibrate("m", 100, 80);
    cal.calibrate("m", 200, 160);

    auto s = cal.stats("m");
    CHECK(s.observations == 2);
    CHECK(s.total_estimated == 300);
    CHECK(s.total_actual == 240);
    // correction 应接近 0.8（两次 ratio = 0.8，EMA 后仍为 0.8）
    CHECK_NEAR(s.correction, 0.8, 0.01);

    PASS();
}

int main() {
    std::cout << "TokenCalibrator 测试:\n";

    test_no_data_returns_one();
    test_first_calibration();
    test_ema_smoothing();
    test_multi_model_isolation();
    test_zero_defense();
    test_extreme_ratio_clamp();
    test_reset();
    test_apply_uses_correction();
    test_calibrated_models_list();
    test_stats();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 通过\n";
    return (tests_run == tests_passed) ? 0 : 1;
}
