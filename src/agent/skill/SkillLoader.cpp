#include "agent/skill/SkillLoader.h"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace skill {

namespace {

// 剥离行首 UTF-8 BOM（Windows 记事本等编辑器保存的文件首行常见，
// 不处理会导致 "---" 精确比较失败，frontmatter 整体丢失）
void stripBom(std::string& line) {
    if (line.size() >= 3 && line[0] == '\xEF' && line[1] == '\xBB' && line[2] == '\xBF')
        line.erase(0, 3);
}

} // namespace

std::vector<SkillMetadata>
SkillLoader::discover(const std::filesystem::path& dir) const {
    std::vector<SkillMetadata> result;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec)
        return result;

    // 带 error_code 的迭代：权限拒绝/符号链接环等异常不得穿透到
    // setupAgent 炸掉整个 App 构造
    std::filesystem::recursive_directory_iterator it(
        dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        spdlog::warn("[SkillLoader] 无法遍历技能目录 {}: {}", dir.string(), ec.message());
        return result;
    }

    for (std::filesystem::recursive_directory_iterator end; it != end;
         it.increment(ec)) {
        if (ec) {
            spdlog::warn("[SkillLoader] 技能目录遍历中断 {}: {}", dir.string(), ec.message());
            break;
        }
        const auto& entry = *it;
        if (!entry.is_regular_file(ec) || ec || entry.path().filename() != "SKILL.md")
            continue;

        try {
            auto skill = parseFrontmatter(entry.path());
            if (checkGating(skill))
                result.push_back(std::move(skill));
        } catch (const std::exception& e) {
            // 解析失败的技能跳过，但必须留痕：静默丢弃会让用户无从排查
            spdlog::warn("[SkillLoader] 技能解析失败，已跳过 {}: {}",
                         entry.path().string(), e.what());
        }
    }
    return result;
}

void SkillLoader::ensureLoaded(SkillMetadata& skill) const {
    if (skill.content_loaded)
        return;

    std::ifstream file(skill.root_dir / "SKILL.md");
    if (!file.is_open()) {
        // 不置位 content_loaded：文件恢复后下次调用仍可重读，
        // 调用方可据此区分“读取失败”与“正文为空”
        spdlog::warn("[SkillLoader] 无法读取技能正文: {}",
                     (skill.root_dir / "SKILL.md").string());
        return;
    }

    std::string line;
    bool in_frontmatter = false;
    bool past_frontmatter = false;
    bool first_line = true;
    std::ostringstream body;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (first_line) {
            stripBom(line);
            first_line = false;
        }

        if (line == "---") {
            if (!in_frontmatter && !past_frontmatter) {
                in_frontmatter = true;
                continue;
            }
            if (in_frontmatter) {
                in_frontmatter = false;
                past_frontmatter = true;
                continue;
            }
        }

        if (past_frontmatter)
            body << line << "\n";
    }

    skill.content = body.str();
    skill.content_loaded = true;
}

bool SkillLoader::checkGating(const SkillMetadata& skill) const {
    if (skill.always)
        return true;

    if (!skill.os_restrict.empty()) {
        std::string os = currentOS();
        bool matched = false;
        for (const auto& allowed : skill.os_restrict) {
            if (allowed == os) {
                matched = true;
                break;
            }
        }
        if (!matched)
            return false;
    }

    for (const auto& bin : skill.required_bins) {
        if (!isBinaryAvailable(bin))
            return false;
    }

    for (const auto& env : skill.required_envs) {
        if (!isEnvAvailable(env))
            return false;
    }

    return true;
}

SkillMetadata
SkillLoader::parseFrontmatter(const std::filesystem::path& file) const {
    std::ifstream ifs(file);
    if (!ifs.is_open())
        throw std::runtime_error("Cannot open: " + file.string());

    SkillMetadata skill;
    skill.root_dir = file.parent_path();

    std::string line;
    bool in_frontmatter = false;
    bool first_line = true;

    // 当前正在解析的数组字段名
    std::string active_array;
    // 当前正在解析的 commands 数组中的对象
    SkillCommand pending_cmd;
    bool in_command_item = false;

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (first_line) {
            stripBom(line);
            first_line = false;
        }

        if (line == "---") {
            if (!in_frontmatter) {
                in_frontmatter = true;
                continue;
            }
            break; // frontmatter 结束，停止读取（渐进式：不读 body）
        }

        if (!in_frontmatter)
            continue;

        // 计算缩进
        int indent = 0;
        for (char c : line) {
            if (c == ' ') ++indent;
            else break;
        }
        // 空行/纯空白行直接跳过（find_first_not_of 返回 npos 时
        // substr(npos) 会抛异常，导致整个技能被丢弃）
        const auto first_char = line.find_first_not_of(" \t");
        if (first_char == std::string::npos)
            continue;
        std::string trimmed = line.substr(first_char);

        // 顶层字段（无缩进）
        if (indent == 0) {
            active_array.clear();
            if (in_command_item) {
                if (!pending_cmd.name.empty())
                    skill.commands.push_back(pending_cmd);
                pending_cmd = {};
                in_command_item = false;
            }

            auto colon = trimmed.find(':');
            if (colon == std::string::npos)
                continue;

            std::string key = trimmed.substr(0, colon);
            std::string val = trimmed.substr(colon + 1);
            // trim
            while (!val.empty() && val.front() == ' ') val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '"'))
                val.pop_back();
            if (!val.empty() && val.front() == '"')
                val.erase(val.begin());

            if (key == "name") skill.name = val;
            else if (key == "description") skill.description = val;
            else if (key == "emoji") skill.emoji = val;
            else if (key == "always") skill.always = (val == "true");
            else if (key == "required_bins" || key == "bins") active_array = "bins";
            else if (key == "required_envs" || key == "envs") active_array = "envs";
            else if (key == "os") active_array = "os";
            else if (key == "commands") active_array = "commands";
            continue;
        }

        // 缩进行：数组项
        if (trimmed[0] == '-') {
            std::string item = trimmed.substr(1);
            while (!item.empty() && item.front() == ' ') item.erase(item.begin());

            if (active_array == "commands") {
                // 新命令对象开始
                if (in_command_item && !pending_cmd.name.empty())
                    skill.commands.push_back(pending_cmd);
                pending_cmd = {};
                in_command_item = true;

                // 可能是 "- name: xxx" 格式
                auto colon = item.find(':');
                if (colon != std::string::npos) {
                    std::string k = item.substr(0, colon);
                    std::string v = item.substr(colon + 1);
                    while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                    while (!v.empty() && (v.back() == ' ' || v.back() == '"'))
                        v.pop_back();
                    if (!v.empty() && v.front() == '"') v.erase(v.begin());
                    if (k == "name") pending_cmd.name = v;
                    else if (k == "description") pending_cmd.description = v;
                }
            } else if (active_array == "bins") {
                skill.required_bins.push_back(item);
            } else if (active_array == "envs") {
                skill.required_envs.push_back(item);
            } else if (active_array == "os") {
                skill.os_restrict.push_back(item);
            }
            continue;
        }

        // 缩进的 key: value（commands 对象的属性）
        if (in_command_item && active_array == "commands") {
            auto colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string k = trimmed.substr(0, colon);
                std::string v = trimmed.substr(colon + 1);
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                while (!v.empty() && (v.back() == ' ' || v.back() == '"'))
                    v.pop_back();
                if (!v.empty() && v.front() == '"') v.erase(v.begin());
                if (k == "name") pending_cmd.name = v;
                else if (k == "description") pending_cmd.description = v;
            }
        }
    }

    //  flush 最后一个 command
    if (in_command_item && !pending_cmd.name.empty())
        skill.commands.push_back(pending_cmd);

    if (skill.name.empty())
        skill.name = skill.root_dir.filename().string();

    return skill;
}

bool SkillLoader::isBinaryAvailable(const std::string& bin) const {
#ifdef _WIN32
    std::string cmd = "where " + bin + " > nul 2>&1";
#else
    std::string cmd = "which " + bin + " > /dev/null 2>&1";
#endif
    return std::system(cmd.c_str()) == 0;
}

bool SkillLoader::isEnvAvailable(const std::string& env) const {
    const char* val = std::getenv(env.c_str());
    return val != nullptr && val[0] != '\0';
}

std::string SkillLoader::currentOS() const {
#ifdef __linux__
    return "linux";
#elif defined(__APPLE__)
    return "darwin";
#elif defined(_WIN32)
    return "win32";
#else
    return "unknown";
#endif
}

} // namespace skill
