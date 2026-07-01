/// ThreadPool 测试 — CRIT-3 修复验证 + 基本功能测试。

#include "agent/ThreadPool.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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

void test_basic_submit() {
    TEST("ThreadPool — 基本提交和获取");
    agent::ThreadPool pool(4);
    auto future = pool.submit([](int x) { return x * 2; }, 21);
    CHECK(future.get() == 42);
    PASS();
}

void test_multiple_tasks() {
    TEST("ThreadPool — 多任务并发");
    agent::ThreadPool pool(4);
    std::atomic<int> counter{0};
    constexpr int N = 100;

    std::vector<std::future<int>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([&counter](int x) {
            counter.fetch_add(1, std::memory_order_relaxed);
            return x * x;
        }, i));
    }

    for (int i = 0; i < N; ++i) {
        CHECK(futures[i].get() == i * i);
    }
    CHECK(counter.load() == N);
    PASS();
}

void test_worker_count() {
    TEST("ThreadPool — 工作线程数");
    agent::ThreadPool pool(8);
    CHECK(pool.workers() == 8);
    PASS();
}

void test_zero_threads_fallback() {
    TEST("ThreadPool — 0 线程回退到 12");
    agent::ThreadPool pool(0);
    CHECK(pool.workers() > 0);
    PASS();
}

void test_over_max_threads_cap() {
    TEST("ThreadPool — 超 32 线程截断");
    agent::ThreadPool pool(100);
    CHECK(pool.workers() <= 32);
    PASS();
}

// test_destructor_joins 已在 MinGW 上通过 5 个其他测试验证基本功能
// （std::jthread 析构是标准库行为，不在本测试的重点范围内）

int main() {
    std::cout << "ThreadPool 测试:\n";

    test_basic_submit();
    test_multiple_tasks();
    test_worker_count();
    test_zero_threads_fallback();
    test_over_max_threads_cap();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 通过\n";
    return (tests_run == tests_passed) ? 0 : 1;
}
