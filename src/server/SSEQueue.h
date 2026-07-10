#pragma once

// SSE 事件队列 — 线程安全的有界生产者-消费者队列。
//
// Fix #2: 容量上限（5000条）防止客户端断开后内存无限增长。
// Fix #7: 条件变量（condition_variable）替代忙等轮询（sleep_for）。
//
// LLM 线程（生产者）将 SSE 事件推入队列，
// httplib content_provider 线程（消费者）从队列取出并写入响应流。

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

namespace server {

class SSEQueue {
public:
    static constexpr size_t kMaxSize = 5000;

    // 推送事件到队列。队列满时丢弃（背压保护）。
    // 返回 false 表示被丢弃。
    bool push(const std::string& data) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.size() >= kMaxSize) return false;
        queue_.push(data);
        cv_.notify_one();
        return true;
    }

    // 阻塞等待并弹出事件，超时返回 false。
    // timeout_ms 最大等待毫秒数
    bool pop_wait(std::string& out, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [this] { return !queue_.empty(); })) {
            return false;
        }
        out = queue_.front();
        queue_.pop();
        return true;
    }

    // 非阻塞弹出。
    bool pop(std::string& out) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) return false;
        out = queue_.front();
        queue_.pop();
        return true;
    }

    // 生产者检查：客户端是否仍在等待？
    std::atomic<bool> cancelled{false};

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<std::string> queue_;
};

// 构造 SSE data 行。
// Fix #6: delta 中的换行符转义，防止破坏 SSE 帧边界。
inline std::string sseLine(const std::string& jsonStr) {
    return "data: " + jsonStr + "\n\n";
}

// 转义 JSON 字符串值中的换行符（保护 SSE 帧边界）。
inline std::string jsonEscapeNewlines(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 8);
    for (char c : raw) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            default:   out += c;
        }
    }
    return out;
}

} // namespace server
