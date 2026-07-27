#pragma once

// 索引服务抽象接口 — 解耦上层对 NovelAgentApp 的反向依赖。
//
// 调用方通过此接口访问索引功能，
// 不再持有 NovelAgentApp* 反向指针。

#include <functional>
#include <string>

namespace agent {

// 索引操作结果。
struct IndexResult {
    int chapters = 0;            // 参与索引的章节数（含未变更）
    int characters = 0;
    int settings = 0;
    int world_rules = 0;
    int memories = 0;            // 长期记忆条目数
    int total_chunks = 0;        // 本次实际重嵌入的 chunk 数
    int updated_sources = 0;     // 内容变更、重新嵌入的源数量
    int skipped_sources = 0;     // 哈希未变、跳过的源数量
    int removed_sources = 0;     // 源已删除、清理孤儿向量的源数量
    std::string error;           //  空 = 成功，非空 = 失败原因

    // 成功 = 无错误。增量模式下内容无变化时 total_chunks 可为 0。
    bool ok() const { return error.empty(); }
};

// 索引服务抽象接口。
class IIndexService {
public:
    virtual ~IIndexService() = default;

    // 为当前项目建立向量索引（章节/角色/设定/世界规则/长期记忆）。
    //
    // 默认增量模式：仅重嵌入内容哈希变化的源，清理已删除源的孤儿向量；
    // 嵌入模型或维度与清单指纹不符时自动整库重建。
    //
    // progress  可选进度回调，每完成一个阶段调用一次（参数为中文描述）。
    //                  为 nullptr 时不报告进度。
    // force     true = 忽略清单强制全量重建。
    // 索引结果统计，error 非空表示失败。
    virtual IndexResult indexAll(std::function<void(const std::string&)> progress = nullptr,
                                 bool force = false) = 0;
};

} // namespace agent
