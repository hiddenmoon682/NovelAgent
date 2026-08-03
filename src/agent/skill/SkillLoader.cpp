#include "agent/skill/SkillLoader.h"

#include <spdlog/spdlog.h>

#include <yaml-cpp/yaml.h>

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

// 扫描一个技能目录（递归），发现其中所有 SKILL.md 并解析出元数据。
// 由 discoverAll 对每个搜索路径调用；全程 error_code 不抛异常，
// 单个技能解析失败跳过且留日志，不中断整体扫描。
std::vector<SkillMetadata>
SkillLoader::discover(const std::filesystem::path& dir) const {
    std::vector<SkillMetadata> result;  // 收集扫描到的技能
    std::error_code ec;                 // 文件系统错误码，全程不抛异常
    if (!std::filesystem::exists(dir, ec) || ec)
        return result;  // 目录不存在或检查失败，无需扫描，返回空

    // 带 error_code 的迭代：权限拒绝/符号链接环等异常不得穿透到
    // setupAgent 炸掉整个 App 构造
    std::filesystem::recursive_directory_iterator it(
        dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {  // 迭代器构造失败（如路径非法）
        spdlog::warn("[SkillLoader] 无法遍历技能目录 {}: {}", dir.string(), ec.message());
        return result;
    }

    // 递归遍历目录；increment(ec) 把推进错误写入 ec 而非抛异常
    for (std::filesystem::recursive_directory_iterator end; it != end;
         it.increment(ec)) {
        if (ec) {  // 遍历中途出错（如权限拒绝）则中断，保留已扫描结果
            spdlog::warn("[SkillLoader] 技能目录遍历中断 {}: {}", dir.string(), ec.message());
            break;
        }
        const auto& entry = *it;
        // 只处理普通文件且文件名恰为 SKILL.md（技能目录约定），其余忽略
        if (!entry.is_regular_file(ec) || ec || entry.path().filename() != "SKILL.md")
            continue;

        try {
            auto skill = parseFrontmatter(entry.path());  // 解析该技能元数据
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
    // 幂等：正文已加载过则直接返回，避免重复读盘
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
    bool in_frontmatter = false;    // 正处于首个 --- 与次个 --- 之间（元数据区）
    bool past_frontmatter = false;  // 已越过次个 ---，进入正文区
    bool first_line = true;         // 仅首行需要剥 BOM
    std::ostringstream body;        // 收集正文

    // 状态机：用两个 --- 分隔符界定 frontmatter，只取其后的正文
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();        // 兼容 Windows CRLF 行尾
        if (first_line) {
            stripBom(line);         // 剥 UTF-8 BOM，避免影响首个 --- 判定
            first_line = false;
        }

        if (line == "---") {
            if (!in_frontmatter && !past_frontmatter) {
                in_frontmatter = true;   // 首个 ---：进入 frontmatter
                continue;
            }
            if (in_frontmatter) {
                in_frontmatter = false;
                past_frontmatter = true;  // 次个 ---：frontmatter 结束，正文开始
                continue;
            }
            // 正文里再出现的 --- 不处理，落入下方按正文收集
        }

        if (past_frontmatter)
            body << line << "\n";   // 仅收集 frontmatter 之后的行
    }

    skill.content = body.str();
    skill.content_loaded = true;    // 标记已加载（即使正文为空也算成功）
}

// 解析 SKILL.md 的 frontmatter（--- 之间的元数据块）为 SkillMetadata。
// 流程：先用 --- 提取 frontmatter 文本块，交给 yaml-cpp 全量解析；
// 非法 YAML 直接抛异常（开发期不兼容存量，不符合协议即拒绝），
// 由 discover 捕获后跳过并告警。正文不读取，保持渐进式加载。
SkillMetadata
SkillLoader::parseFrontmatter(const std::filesystem::path& file) const {
    std::ifstream ifs(file);
    if (!ifs.is_open())
        throw std::runtime_error("Cannot open: " + file.string());

    SkillMetadata skill;
    skill.root_dir = file.parent_path();

    // 用 “---” 分隔符提取 frontmatter 文本块（首个到次个 --- 之间），
    // 交给 yaml-cpp 做全量 YAML 解析；正文不读，保持渐进式加载。
    std::string line;
    bool in_frontmatter = false;
    bool past_frontmatter = false;
    bool first_line = true;
    std::ostringstream fm;   // 收集 frontmatter 文本块

    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();   // 兼容 Windows CRLF 行尾
        if (first_line) {
            stripBom(line);    // 剥 UTF-8 BOM，避免影响首个 --- 判定
            first_line = false;
        }

        if (line == "---") {
            if (!in_frontmatter && !past_frontmatter) {
                in_frontmatter = true;   // 首个 ---：进入 frontmatter
                continue;
            }
            if (in_frontmatter) {
                in_frontmatter = false;
                past_frontmatter = true; // 次个 ---：frontmatter 结束，停止读取
                break;
            }
        }
        if (in_frontmatter)
            fm << line << "\n";
    }

    // 非法 YAML 抛异常，由 discover 捕获后跳过并告警；
    // 空 frontmatter（无字段）按空 YAML 处理，不抛错。
    YAML::Node node;
    try {
        node = YAML::Load(fm.str());
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("Invalid YAML frontmatter: " + std::string(e.what()));
    }

    if (node.IsMap()) {
        if (node["name"])
            skill.name = node["name"].as<std::string>("");
        if (node["description"])
            skill.description = node["description"].as<std::string>("");
        // always 兼容 bool 与字符串写法（true/True/yes/on/1），宽松语义不丢技能
        if (node["always"] && node["always"].IsScalar()) {
            const std::string v = node["always"].as<std::string>("");
            skill.always = (v == "true" || v == "True" || v == "TRUE" ||
                            v == "yes" || v == "Yes" || v == "YES" ||
                            v == "on" || v == "On" || v == "ON" || v == "1");
        }
        if (node["commands"] && node["commands"].IsSequence()) {
            for (const auto& item : node["commands"]) {
                if (!item.IsMap())
                    continue;
                // 丢弃无名命令，其余按 name/description 读取
                std::string cmd_name = item["name"] ? item["name"].as<std::string>("") : "";
                if (cmd_name.empty())
                    continue;
                SkillCommand cmd;
                cmd.name = cmd_name;
                cmd.description = item["description"] ? item["description"].as<std::string>("") : "";
                skill.commands.push_back(std::move(cmd));
            }
        }
    }

    if (skill.name.empty())
        skill.name = skill.root_dir.filename().string();

    return skill;
}

} // namespace skill
