#pragma once

// 索引服务抽象接口 — 解耦 ReplHandler 对 NovelAgentApp 的反向依赖（Issue 6）。
//
// NovelAgentApp 实现此接口，ReplHandler 通过此接口访问索引功能，
// 不再持有 NovelAgentApp* 反向指针。

#include <functional>
#include <string>

namespace agent {

// 索引操作结果。
struct IndexResult {
    int chapters = 0;
    int characters = 0;
    int settings = 0;
    int world_rules = 0;
    int total_chunks = 0;
    std::string error;           //  空 = 成功，非空 = 失败原因

    bool ok() const { return error.empty() && total_chunks > 0; }
};

// 索引服务抽象接口。
class IIndexService {
public:
    virtual ~IIndexService() = default;

    // 为当前项目建立向量索引（章节/角色/设定/世界规则）。
    // progress  可选进度回调，每完成一个阶段调用一次（参数为中文描述）。
    //                  为 nullptr 时不报告进度。
    // 索引结果统计，error 非空表示失败。
    virtual IndexResult indexAll(std::function<void(const std::string&)> progress = nullptr) = 0;
};

} // namespace agent
