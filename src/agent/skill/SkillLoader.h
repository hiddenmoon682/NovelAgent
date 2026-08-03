#pragma once

// SkillLoader — 技能发现与渐进式加载。
//
// 技能以目录形式存在（<dir>/<name>/SKILL.md），文件由 YAML frontmatter
//（元数据）+ Markdown 正文（完整指引）组成。发现阶段只解析 frontmatter，
// 正文由 ensureLoaded 按需读取，避免启动时加载全部技能全文。

#include "agent/skill/SkillMetadata.h"

#include <filesystem>
#include <string>
#include <vector>

namespace skill {

// 无状态的技能加载器：扫描目录、解析 frontmatter、按需读正文。
// 所有方法均为 const，缓存写入由调用方持有的 SkillMetadata 承载。
class SkillLoader {
public:
    // 递归扫描目录，发现全部 SKILL.md 并解析其 frontmatter 元数据。
    // 解析失败的技能记日志后跳过，不读取正文（content 留空，交由 ensureLoaded 按需加载）。
    // @param dir 技能根目录；不存在或不可遍历时返回空列表，不抛异常。
    // @return 发现的技能元数据列表。
    std::vector<SkillMetadata> discover(const std::filesystem::path& dir) const;

    // 确保技能正文已加载（渐进式披露的第二级，幂等）。
    // 就地修改入参：将 SKILL.md 中 frontmatter 之后的正文读入 skill.content
    // 并置位 skill.content_loaded；已加载时直接返回，不重复读文件。
    // 读取失败时不置位 content_loaded（文件恢复后可重试），调用方可据此
    // 区分“读取失败”与“正文为空”。
    // @param skill 目标技能元数据，content / content_loaded 字段会被写入。
    void ensureLoaded(SkillMetadata& skill) const;

private:
    // 只读 frontmatter 构造元数据（name/description/always/commands，不读正文）；
    // 用 yaml-cpp 解析前以 “---” 分隔符提取文本块；文件无法打开或 YAML 非法时抛异常。
    SkillMetadata parseFrontmatter(const std::filesystem::path& file) const;
};

} // namespace skill
