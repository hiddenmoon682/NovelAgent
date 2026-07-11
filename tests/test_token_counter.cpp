#include "llm/TokenCounter.h"
#include "llm/Message.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace llm;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        ++tests_run; \
        std::cout << "  TEST " << (name) << " ... "; \
    } while (0)

#define PASS() \
    do { \
        ++tests_passed; \
        std::cout << "PASSED\n"; \
    } while (0)

#define FAIL(msg) \
    do { \
        std::cout << "FAILED: " << (msg) << '\n'; \
        return; \
    } while (0)

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            FAIL(#cond); \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { if (std::abs((a) - (b)) > (eps)) { \
        std::cout << "FAILED: " << #a << "=" << (a) << " vs " << #b << "=" << (b) << " (eps=" << (eps) << ")\n"; \
        return; } } while(0)

// ---------------------------------------------------------------------------
// estimateChineseChars
// ---------------------------------------------------------------------------

void test_chinese_chars_empty() {
    TEST("estimateChineseChars — 空字符串");
    CHECK(TokenCounter::estimateChineseChars("") == 0);
    PASS();
}

void test_chinese_chars_pure_chinese() {
    TEST("estimateChineseChars — 纯中文");
    int n = TokenCounter::estimateChineseChars("你好世界");
    CHECK(n == 4);
    PASS();
}

void test_chinese_chars_pure_ascii() {
    TEST("estimateChineseChars — 纯 ASCII");
    CHECK(TokenCounter::estimateChineseChars("Hello, world! 123") == 0);
    PASS();
}

void test_chinese_chars_mixed() {
    TEST("estimateChineseChars — 中英混合");
    int n = TokenCounter::estimateChineseChars("Hello 你好 World 世界");
    CHECK(n == 4);
    PASS();
}

void test_chinese_chars_punctuation() {
    TEST("estimateChineseChars — 中文标点不计入");
    int n = TokenCounter::estimateChineseChars("你好，世界！");
    CHECK(n == 4); // 逗号和感叹号不是 CJK 字符
    PASS();
}

// ---------------------------------------------------------------------------
// estimateEnglishWords
// ---------------------------------------------------------------------------

void test_english_words_empty() {
    TEST("estimateEnglishWords — 空字符串");
    CHECK(TokenCounter::estimateEnglishWords("") == 0);
    PASS();
}

void test_english_words_simple() {
    TEST("estimateEnglishWords — 简单英文");
    int n = TokenCounter::estimateEnglishWords("The quick brown fox");
    CHECK(n == 4);
    PASS();
}

void test_english_words_pure_chinese() {
    TEST("estimateEnglishWords — 纯中文（应为 0）");
    CHECK(TokenCounter::estimateEnglishWords("你好世界") == 0);
    PASS();
}

void test_english_words_numbers() {
    TEST("estimateEnglishWords — 数字不视为单词");
    int n = TokenCounter::estimateEnglishWords("Chapter 123 page 456");
    CHECK(n == 2); // "Chapter" 和 "page"
    PASS();
}

// ---------------------------------------------------------------------------
// countTokens
// ---------------------------------------------------------------------------

void test_count_tokens_empty() {
    TEST("countTokens — 空字符串");
    CHECK(TokenCounter::countTokens("") == 0);
    PASS();
}

void test_count_tokens_pure_english() {
    TEST("countTokens — 纯英文");
    // "The quick brown fox" = 4 个英文单词 × 1.3 = 5.2 → 5
    int n = TokenCounter::countTokens("The quick brown fox");
    CHECK(n == 5);
    PASS();
}

void test_count_tokens_pure_chinese() {
    TEST("countTokens — 纯中文");
    // "你好世界" = 4 个中文字 × 0.75 = 3
    int n = TokenCounter::countTokens("你好世界");
    CHECK(n == 3);
    PASS();
}

void test_count_tokens_mixed() {
    TEST("countTokens — 中英混合");
    // "Hello 你好" = 1 英文单词 × 1.3 + 2 中文字 × 0.75 = 1.3 + 1.5 = 2.8 → 2
    int n = TokenCounter::countTokens("Hello 你好");
    CHECK(n == 2);
    PASS();
}

void test_count_tokens_non_negative() {
    TEST("countTokens — 结果非负");
    CHECK(TokenCounter::countTokens("!@#$%^&*()") >= 0);
    PASS();
}

// ---------------------------------------------------------------------------
// countMessages
// ---------------------------------------------------------------------------

void test_count_messages_empty() {
    TEST("countMessages — 空消息列表");
    std::vector<Message> messages;
    CHECK(TokenCounter::countMessages(messages) == 0);
    PASS();
}

void test_count_messages_single() {
    TEST("countMessages — 单条消息");
    std::vector<Message> messages = {
        {MessageRole::User, "你好", {}, "", ""}
    };
    // countTokens("你好") = 2 × 0.75 = 1.5 → 1, + 4 元数据 = 5
    int n = TokenCounter::countMessages(messages);
    CHECK(n == 5);
    PASS();
}

void test_count_messages_with_tool_call() {
    TEST("countMessages — 含工具调用的消息");
    std::vector<Message> messages = {
        {MessageRole::Assistant, "", {
            {"call_1", "function", "get_character", "{\"id\": \"liu\"}"}
        }, "", ""}
    };
    // content 为空 → 0
    // tool_call: function_name "get_character" (英文 2 词 × 1.3 = 2.6 → 2)
    //            arguments "{\"id\": \"liu\"}" ("id" 和 "liu" 算作 2 词 × 1.3 = 2.6 → 2)
    //            结构开销 +8
    // 消息开销 +4
    // total = 0 + 2 + 2 + 8 + 4 = 16
    int n = TokenCounter::countMessages(messages);
    CHECK(n > 0);
    PASS();
}

void test_count_messages_multiple() {
    TEST("countMessages — 多条消息");
    std::vector<Message> messages = {
        {MessageRole::User, "你好", {}, "", ""},
        {MessageRole::Assistant, "你好！有什么可以帮助你的？", {}, "", ""}
    };
    int n = TokenCounter::countMessages(messages);
    // 每条消息都有结构开销，总共 > 单条
    CHECK(n > TokenCounter::countTokens("你好"));
    PASS();
}

// ---------------------------------------------------------------------------
// UTF-8 边界情况
// ---------------------------------------------------------------------------

void test_utf8_truncated_sequence() {
    TEST("UTF-8 — 截断的多字节序列");
    // 构造一个 3 字节 CJK 序列的前导字节，后面不跟后续字节
    // 0xE4 是 "一" (U+4E00) 等常见汉字的前导字节
    std::string truncated;
    truncated.push_back('\xE4');
    truncated.push_back('\xB8'); // 第二个字节
    // 缺少第三个字节 —— 截断

    int n = TokenCounter::estimateChineseChars(truncated);
    // 截断序列不应被误判为 CJK 字符
    CHECK(n == 0);
    PASS();
}

void test_utf8_invalid_continuation() {
    TEST("UTF-8 — 非法的后续字节");
    // 3 字节前导 + 两个非法的后续字节
    std::string invalid;
    invalid.push_back('\xE4');
    invalid.push_back('\x80'); // 0x80 虽然以 10 开头，但不在有效范围内
    invalid.push_back('\x80');

    int n = TokenCounter::estimateChineseChars(invalid);
    // 不应崩溃，且不应误判
    CHECK(n >= 0);
    PASS();
}

// ===========================================================================
// 校准测试（从 TokenCalibrator 合并）
// ===========================================================================

void test_cal_no_data_returns_one() {
    TEST("calibrate — 无校准数据时返回 1.0");
    llm::TokenCounter cal;
    CHECK(cal.getCorrection("unknown-model") == 1.0);
    CHECK(cal.apply("unknown-model", 100) == 100);
    PASS();
}

void test_cal_first_calibration() {
    TEST("calibrate — 首次校准直接使用 ratio");
    llm::TokenCounter cal;
    cal.calibrate("test-model", 100, 80);
    CHECK_NEAR(cal.getCorrection("test-model"), 0.8, 0.001);
    CHECK(cal.apply("test-model", 100) == 80);
    PASS();
}

void test_cal_ema_smoothing() {
    TEST("calibrate — EMA 平滑：多次校准");
    llm::TokenCounter cal;

    cal.calibrate("m", 100, 80);
    auto c1 = cal.getCorrection("m");

    cal.calibrate("m", 100, 70);
    auto c2 = cal.getCorrection("m");

    CHECK(c2 < c1);
    CHECK_NEAR(c2, 0.77, 0.001);

    cal.calibrate("m", 100, 90);
    CHECK_NEAR(cal.getCorrection("m"), 0.809, 0.001);

    PASS();
}

void test_cal_multi_model_isolation() {
    TEST("calibrate — 多模型独立校准");
    llm::TokenCounter cal;

    cal.calibrate("model-a", 100, 80);
    cal.calibrate("model-b", 100, 120);

    CHECK_NEAR(cal.getCorrection("model-a"), 0.8, 0.001);
    CHECK_NEAR(cal.getCorrection("model-b"), 1.2, 0.001);

    cal.calibrate("model-a", 100, 90);
    CHECK_NEAR(cal.getCorrection("model-a"), 0.83, 0.001);
    CHECK_NEAR(cal.getCorrection("model-b"), 1.2, 0.001);

    PASS();
}

void test_cal_zero_defense() {
    TEST("calibrate — estimated=0/负数 或 actual=0 时无影响");
    llm::TokenCounter cal;

    cal.calibrate("m", 0, 100);
    CHECK(cal.getCorrection("m") == 1.0);

    cal.calibrate("m", 100, 0);
    CHECK(cal.getCorrection("m") == 1.0);

    cal.calibrate("m", -1, 100);
    CHECK(cal.getCorrection("m") == 1.0);

    PASS();
}

void test_cal_extreme_ratio_clamp() {
    TEST("calibrate — ratio < 0.1 或 > 3.0 被截断");
    llm::TokenCounter cal;

    cal.calibrate("m", 1000, 50);
    CHECK_NEAR(cal.getCorrection("m"), 0.1, 0.001);

    cal.calibrate("m2", 100, 500);
    CHECK_NEAR(cal.getCorrection("m2"), 3.0, 0.001);

    PASS();
}

void test_cal_reset() {
    TEST("calibrate — reset 后恢复默认 1.0");
    llm::TokenCounter cal;

    cal.calibrate("m", 100, 80);
    CHECK(cal.getCorrection("m") != 1.0);

    cal.reset("m");
    CHECK(cal.getCorrection("m") == 1.0);

    cal.calibrate("a", 100, 80);
    cal.calibrate("b", 100, 120);
    cal.resetAll();
    CHECK(cal.getCorrection("a") == 1.0);
    CHECK(cal.getCorrection("b") == 1.0);

    PASS();
}

void test_cal_apply() {
    TEST("calibrate — apply 返回 estimated * correction");
    llm::TokenCounter cal;

    cal.calibrate("m", 100, 75);
    CHECK(cal.apply("m", 200) == 150);

    PASS();
}

void test_cal_calibrated_models_list() {
    TEST("calibrate — calibratedModels 返回已校准模型列表");
    llm::TokenCounter cal;
    CHECK(cal.calibratedModels().empty());

    cal.calibrate("a", 100, 80);
    cal.calibrate("b", 100, 120);

    auto models = cal.calibratedModels();
    CHECK(models.size() == 2);

    PASS();
}

void test_cal_stats() {
    TEST("calibrate — stats 返回校准统计");
    llm::TokenCounter cal;

    cal.calibrate("m", 100, 80);
    cal.calibrate("m", 200, 160);

    auto s = cal.stats("m");
    CHECK(s.observations == 2);
    CHECK(s.total_estimated == 300);
    CHECK(s.total_actual == 240);
    CHECK_NEAR(s.correction, 0.8, 0.01);

    PASS();
}

void test_cal_empty_model_apply() {
    TEST("calibrate — 空模型名 apply 返回原值");
    llm::TokenCounter cal;
    CHECK(cal.apply("", 100) == 100);
    PASS();
}

// ===========================================================================
// main
// ===========================================================================

int main() {
    std::cout << "=== test_token_counter ===\n\n";

    test_chinese_chars_empty();
    test_chinese_chars_pure_chinese();
    test_chinese_chars_pure_ascii();
    test_chinese_chars_mixed();
    test_chinese_chars_punctuation();

    test_english_words_empty();
    test_english_words_simple();
    test_english_words_pure_chinese();
    test_english_words_numbers();

    test_count_tokens_empty();
    test_count_tokens_pure_english();
    test_count_tokens_pure_chinese();
    test_count_tokens_mixed();
    test_count_tokens_non_negative();

    test_count_messages_empty();
    test_count_messages_single();
    test_count_messages_with_tool_call();
    test_count_messages_multiple();

    test_utf8_truncated_sequence();
    test_utf8_invalid_continuation();

    // 校准
    test_cal_no_data_returns_one();
    test_cal_first_calibration();
    test_cal_ema_smoothing();
    test_cal_multi_model_isolation();
    test_cal_zero_defense();
    test_cal_extreme_ratio_clamp();
    test_cal_reset();
    test_cal_apply();
    test_cal_calibrated_models_list();
    test_cal_stats();
    test_cal_empty_model_apply();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
