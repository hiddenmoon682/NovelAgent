#pragma once

/// SSE 事件队列 — 线程安全的生产者-消费者队列。
///
/// LLM 调用线程（生产者）将 SSE 事件推入队列，
/// httplib content_provider 线程（消费者）从队列取出并写入响应流。

#include <mutex>
#include <queue>
#include <string>

namespace server {

class SSEQueue {
public:
    void push(const std::string& data) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push(data);
    }
    bool pop(std::string& out) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) return false;
        out = queue_.front();
        queue_.pop();
        return true;
    }
private:
    std::mutex mtx_;
    std::queue<std::string> queue_;
};

/// 构造 SSE data 行。
inline std::string sseLine(const std::string& jsonStr) {
    return "data: " + jsonStr + "\n\n";
}

} // namespace server
