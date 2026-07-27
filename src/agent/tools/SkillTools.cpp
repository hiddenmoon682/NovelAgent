// SkillTools 实现 — use_skill（按需加载）与 save_skill（技能落盘）。

#include "agent/tools/SkillTools.h"

#include "agent/skill/SkillRegistry.h"
#include "project/Models/Project.h"
#include "utils/SchemaUtils.h"

#include <spdlog/spdlog.h>

#include <cctype>
#include <fstream>

namespace agent {

using json = nlohmann::json;

namespace {

// 技能名校验：^[a-z0-9]+(-[a-z0-9]+)*$（与 OpenCode 约定一致），
// 同时防止路径穿越（名称即目录名）。
bool isValidSkillName(const std::string& name) {
    if (name.empty() || name.size() > 64)
        return false;
    if (name.front() == '-' || name.back() == '-')
        return false;
    bool prev_dash = false;
    for (char c : name) {
        if (c == '-') {
            if (prev_dash) return false;
            prev_dash = true;
            continue;
        }
        prev_dash = false;
        if (!std::islower(static_cast<unsigned char>(c)) &&
            !std::isdigit(static_cast<unsigned char>(c)))
            return false;
    }
    return true;
}

// frontmatter 单行字段消毒：换行会注入任意元数据字段（如 always: true
// 提权常驻、bins 触发命令执行），"---" 行会提前终结 frontmatter。
std::string sanitizeFrontmatterValue(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == '\n' || c == '\r')
            out += ' ';
        else if (c != '"')  // 引号可逃逸带引号字段（如 emoji）
            out += c;
    }
    return out;
}

} // namespace

// ── use_skill ──

json UseSkillTool::parameters() const {
    return utils::schema::object({
        {"name", utils::schema::stringProp("技能名（取自 <available_skills> 目录）")}
    }, {"name"});
}

json UseSkillTool::execute(const json& args) {
    if (!registry_)
        return {{"error", "技能系统未初始化"}};

    const std::string name = args.value("name", "");
    auto content = registry_->loadContent(name);
    if (!content)
        return {{"error", "技能不存在、已被禁用或正文不可读: " + name}};

    spdlog::info("[use_skill] 加载技能 '{}' ({} 字节)", name, content->size());
    return {{"name", name}, {"content", *content}};
}

// ── save_skill ──

json SaveSkillTool::parameters() const {
    return utils::schema::object({
        {"name",        utils::schema::stringProp("技能名，小写字母数字加连字符，如 dialogue-polish")},
        {"description", utils::schema::stringProp("一句话描述技能用途与适用时机（LLM 依此决定是否加载）")},
        {"content",     utils::schema::stringProp("技能正文 Markdown：使用场景、执行步骤、注意事项等")},
        {"emoji",       utils::schema::stringProp("可选，展示用 emoji 图标")},
        {"always",      utils::schema::booleanProp("可选，是否全文常驻上下文（默认 false，按需加载）")}
    }, {"name", "description", "content"});
}

json SaveSkillTool::execute(const json& args) {
    if (!registry_)
        return {{"error", "技能系统未初始化"}};
    if (!project_ || project_->path.empty())
        return {{"error", "未打开项目，无法保存技能"}};

    const std::string name = args.value("name", "");
    const std::string description = sanitizeFrontmatterValue(args.value("description", ""));
    const std::string content = args.value("content", "");
    const std::string emoji = sanitizeFrontmatterValue(args.value("emoji", ""));
    const bool always = args.value("always", false);

    if (!isValidSkillName(name))
        return {{"error", "技能名不合法（要求小写字母数字加连字符）: " + name}};
    if (description.empty() || content.empty())
        return {{"error", "description 和 content 不能为空"}};

    std::filesystem::path dir =
        std::filesystem::path(project_->path) / "skills" / name;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return {{"error", "创建技能目录失败: " + ec.message()}};

    std::ofstream out(dir / "SKILL.md", std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        return {{"error", "无法写入 SKILL.md"}};

    out << "---\n";
    out << "name: " << name << "\n";
    out << "description: " << description << "\n";
    if (!emoji.empty())
        out << "emoji: \"" << emoji << "\"\n";
    if (always)
        out << "always: true\n";
    out << "---\n\n";
    out << content;
    if (!content.empty() && content.back() != '\n')
        out << "\n";
    out.close();

    // 刷新注册表，新技能立即出现在目录中；system prompt 的
    // <available_skills> 目录在下次新建/切换会话时由 prompt 提供者刷新
    registry_->discoverAll();

    const std::string path = (dir / "SKILL.md").string();
    spdlog::info("[save_skill] 技能 '{}' 已保存: {}", name, path);
    return {{"ok", true}, {"path", path},
            {"hint", "技能已保存并注册。用户可在技能面板中管理它。"}};
}

REGISTER_TOOL_DEPS(agent::UseSkillTool, "use_skill", use_skill)
REGISTER_TOOL_DEPS(agent::SaveSkillTool, "save_skill", save_skill)

} // namespace agent
