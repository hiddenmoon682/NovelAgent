#include "agent/skill/SkillRegistry.h"

#include <mutex>
#include <sstream>
#include <unordered_set>

namespace skill {

SkillRegistry::SkillRegistry(SkillLoader loader)
    : loader_(std::move(loader)) {}

void SkillRegistry::addSearchPath(std::filesystem::path dir) {
    std::unique_lock lock(mutex_);
    search_paths_.push_back(std::move(dir));
}

// 扫描全部搜索路径并重建技能列表。
// 行为：先清空旧列表，遍历各路径发现技能，按名称去重（先发现者优先），
// 并依据禁用集合标记每个技能是否启用。仅供加载阶段调用，之后用 get/loadContent 查询。
void SkillRegistry::discoverAll() {
    std::unique_lock lock(mutex_);
    skills_.clear();
    std::unordered_set<std::string> seen;

    for (const auto& dir : search_paths_) {
        for (auto& skill : loader_.discover(dir)) {
            if (seen.count(skill.name))   // 同名技能只保留先发现的（项目级优先）
                continue;
            seen.insert(skill.name);
            skill.enabled = disabled_names_.count(skill.name) == 0;  // 禁用集中则停用
            skills_.push_back(std::move(skill));
        }
    }
}

const SkillMetadata* SkillRegistry::get(const std::string& name) const {
    // 注意：返回的指针仅在下次 discoverAll 前有效，且不受锁保护，
    // 跨线程场景请用 listSkills/loadContent 的值语义接口。
    std::unique_lock lock(mutex_);
    for (auto& s : skills_) {
        if (s.name == name) {
            loader_.ensureLoaded(s);
            return &s;
        }
    }
    return nullptr;
}

std::optional<std::string> SkillRegistry::loadContent(const std::string& name) const {
    std::unique_lock lock(mutex_);  // ensureLoaded 会写入缓存
    for (auto& s : skills_) {
        if (s.name != name)
            continue;
        if (!s.enabled)
            return std::nullopt; // 禁用技能对 LLM 不可见
        loader_.ensureLoaded(s);
        if (!s.content_loaded)
            return std::nullopt; // SKILL.md 读取失败（已被删除/移动）
        return s.content;
    }
    return std::nullopt;
}

bool SkillRegistry::setEnabled(const std::string& name, bool enabled) {
    std::unique_lock lock(mutex_);
    // 先查找再改集合：避免不存在的名字污染 disabled_names_
    //（否则会随下次合法 toggle 一起写进 skills.json）
    for (auto& s : skills_) {
        if (s.name == name) {
            s.enabled = enabled;
            if (enabled)
                disabled_names_.erase(name);
            else
                disabled_names_.insert(name);
            return true;
        }
    }
    return false;
}

void SkillRegistry::setDisabledSkills(const std::vector<std::string>& names) {
    std::unique_lock lock(mutex_);
    disabled_names_.clear();
    disabled_names_.insert(names.begin(), names.end());
    for (auto& s : skills_)
        s.enabled = disabled_names_.count(s.name) == 0;
}

std::vector<std::string> SkillRegistry::disabledSkills() const {
    std::shared_lock lock(mutex_);
    return {disabled_names_.begin(), disabled_names_.end()};
}

std::vector<SkillMetadata> SkillRegistry::listSkills() const {
    std::shared_lock lock(mutex_);
    return skills_;
}

// 渐进式披露：always 技能全文常驻；其余启用技能仅列入目录，
// 由 LLM 调用 use_skill 工具按需加载全文，避免技能增多时上下文膨胀。
std::string SkillRegistry::getSkillContext() const {
    std::unique_lock lock(mutex_);  // 常驻技能的 ensureLoaded 会写入缓存
    std::ostringstream catalog;  // 按需技能目录（仅名称+描述）
    std::ostringstream resident; // 常驻技能全文（完整 Markdown）

    // ── 遍历分流：按 always 把启用技能拆成“常驻全文”与“按需目录”两桶 ──
    for (auto& s : skills_) {
        if (!s.enabled)
            continue;  // 禁用技能对 LLM 完全隐藏

        if (s.always) {
            // 常驻技能：渲染完整内容（标题 + 描述 + 正文 + 命令列表）
            loader_.ensureLoaded(s);  // 懒加载正文到缓存
            resident << "### " << s.name << "\n";
            if (!s.description.empty())
                resident << s.description << "\n\n";
            resident << s.content << "\n";

            // 附带该技能提供的斜杠命令（渲染时补斜杠）
            if (!s.commands.empty()) {
                resident << "**Commands:**\n";
                for (const auto& cmd : s.commands) {
                    resident << "- `/" << cmd.name << "`";
                    if (!cmd.description.empty())
                        resident << " — " << cmd.description;
                    resident << "\n";
                }
            }
            resident << "\n";
        } else {
            // 按需技能：仅列一行“名称: 描述”，正文留给 use_skill 加载
            catalog << "- " << s.name;
            if (!s.description.empty())
                catalog << ": " << s.description;
            catalog << "\n";
        }
    }

    std::string catalog_str = catalog.str();
    std::string resident_str = resident.str();
    if (catalog_str.empty() && resident_str.empty())
        return {};  // 无任何可用技能时返回空串，prompt 不注入该段

    // ── 最终拼接：先按需目录（带使用指引），后常驻全文 ──
    std::ostringstream ctx;
    if (!catalog_str.empty()) {
        // 用 XML 标签包裹目录，并附一句引导：匹配时先调 use_skill 再执行
        ctx << "以下技能可按需使用：当任务与某技能描述匹配时，"
               "先调用 use_skill 工具加载其完整内容，再按内容指引执行。\n"
            << "<available_skills>\n" << catalog_str << "</available_skills>\n";
    }
    if (!resident_str.empty()) {
        if (!catalog_str.empty())
            ctx << "\n";  // 两段之间空行分隔
        ctx << resident_str;
    }
    return ctx.str();
}

bool SkillRegistry::hasSkill(const std::string& name) const {
    std::shared_lock lock(mutex_);
    for (const auto& s : skills_) {
        if (s.name == name)
            return true;
    }
    return false;
}

std::vector<SkillCommand> SkillRegistry::getAllCommands() const {
    std::shared_lock lock(mutex_);
    std::vector<SkillCommand> cmds;
    for (const auto& s : skills_) {
        if (!s.enabled)
            continue;
        for (const auto& c : s.commands)
            cmds.push_back(c);
    }
    return cmds;
}

} // namespace skill
