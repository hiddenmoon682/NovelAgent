#pragma once

// 固定大小线程池 — Issue 4：替代 std::async 的 SubAgent 并发执行。
//
// 特性：
// - 固定 N 个 std::jthread 工作线程，任务队列无界
// - submit() 返回 std::future<T>，接口与 std::async 对齐
// - 析构时自动等待所有任务完成 + 关闭线程
//
// 使用示例:
//   ThreadPool pool(4);
//   auto future = pool.submit([](int x) { return x * 2; }, 21);
//   int result = future.get();  // 42

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace agent {

class ThreadPool {
public:
    // num_threads  工作线程数（默认 12，上限 32）
    explicit ThreadPool(size_t num_threads = 12)
        : stop_(false)
    {
        if (num_threads > 32) num_threads = 32;

        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        // std::jthread 析构自动 join
    }

    // 提交任务，返回 future。
    // 接口与 std::async 对齐：submit(fn, args...)
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            [fn = std::forward<F>(f),
             ... args = std::forward<Args>(args)]() mutable -> ReturnType {
                return fn(std::forward<Args>(args)...);
            });

        std::future<ReturnType> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) {
                // 线程池已停止，直接在当前线程执行避免任务丢失
                (*task)();
                return future;
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    // 当前排队任务数。
    size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    // 当前工作线程数。
    size_t workers() const { return workers_.size(); }

private:
    std::vector<std::jthread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_;

    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }
};

} // namespace agent
