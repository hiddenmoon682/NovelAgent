#include "skill/SkillLoader.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace skill {

std::vector<SkillMetadata>
SkillLoader::discover(const std::filesystem::path& dir) const {
    std::vector<SkillMetadata> result;
    if (!std::filesystem::exists(dir))
        return result;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file() || entry.path().filename() != "SKILL.md")
            continue;

        try {
            auto skill = parseFrontmatter(entry.path());
            if (checkGating(skill))
                result.push_back(std::move(skill));
        } catch (...) {
            // 解析失败的技能静默跳过
        }
    }
    return result;
}

void SkillLoader::ensureLoaded(SkillMetadata& skill) const {
    if (skill.content_loaded)
        return;

    std::ifstream file(skill.root_dir / "SKILL.md");
    if (!file.is_open()) {
        skill.content_loaded = true;
        return;
    }

    std::string line;
    bool in_frontmatter = false;
    bool past_frontmatter = false;
    std::ostringstream body;

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

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

    // 当前正在解析的数组字段名
    std::string active_array;
    // 当前正在解析的 commands 数组中的对象
    SkillCommand pending_cmd;
    bool in_command_item = false;

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

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
        std::string trimmed = line.substr(line.find_first_not_of(" \t"));
        if (trimmed.empty())
            continue;

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
