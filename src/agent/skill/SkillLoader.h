#pragma once

#include "agent/skill/SkillMetadata.h"

#include <filesystem>
#include <string>
#include <vector>

namespace skill {

// 技能加载器 — 从磁盘读取 SKILL.md，解析 frontmatter 元数据、
// 按需加载正文，并做可用性门控（OS/依赖二进制/环境变量）。
// 无状态工具类：所有方法均为 const，缓存写入由调用方持有的 SkillMetadata 承载。
class SkillLoader {
public:
    // 递归扫描目录下的全部 SKILL.md，解析元数据并通过门控的技能列表。
    // 目录不存在/遍历异常时返回空列表，解析失败的技能跳过并告警。
    std::vector<SkillMetadata> discover(const std::filesystem::path& dir) const;
    // 确保技能正文已加载（幂等）：读取 SKILL.md 的 frontmatter 之后部分填入 content。
    // 读取失败不置位 content_loaded，以便文件恢复后可重试。
    void ensureLoaded(SkillMetadata& skill) const;
    // 门控检查：always 技能直接通过；否则校验 OS 限制、必需二进制与环境变量。
    bool checkGating(const SkillMetadata& skill) const;

private:
    // 解析单个 SKILL.md 的 YAML frontmatter（name/description/emoji/always/
    // bins/envs/os/commands），不读取正文（渐进式加载）。
    SkillMetadata parseFrontmatter(const std::filesystem::path& file) const;
    // 判断某可执行文件是否在 PATH 中（Windows 用 where，其余用 which）。
    bool isBinaryAvailable(const std::string& bin) const;
    // 判断某环境变量是否已设置且非空。
    bool isEnvAvailable(const std::string& env) const;
    // 返回当前操作系统标识："linux" / "darwin" / "win32" / "unknown"。
    std::string currentOS() const;
};

} // namespace skill
