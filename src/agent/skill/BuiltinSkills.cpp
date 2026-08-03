// 内置技能内容与安装逻辑。

#include "agent/skill/BuiltinSkills.h"

#include "utils/FileUtils.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

namespace skill {

namespace {

// create-skill 元技能：引导用户创建符合规范的新技能，最终用 save_skill 落盘。
const char* kCreateSkillMd = R"MD(---
name: create-skill
description: 当用户想创建、编写或定制一个新技能（Skill）时使用，引导用户明确需求并生成规范的 SKILL.md
---

## 目标

引导用户从模糊想法出发，创建一个高质量、可复用的技能，并用 save_skill 工具保存。

## 执行步骤

1. **澄清需求**：先向用户确认三件事（缺一则追问）：
   - 这个技能解决什么问题？在什么场景下应该被触发？
   - 期望的产出是什么（文风指导 / 结构模板 / 检查清单 / 工作流程）？
   - 是常驻技能（每次对话都需要）还是按需技能（特定任务才用）？

2. **设计元数据**：
   - name：小写字母数字加连字符（如 `dialogue-polish`、`foreshadow-check`），能望文生义
   - description：一句话说清「做什么 + 什么时候用」——这是按需加载的唯一判断依据，
     必须包含触发场景关键词，避免空泛（坏例：「帮助写作」；好例：「润色对话，
     使角色语气符合人设，在用户要求改写对话或对话生硬时使用」）
   - always：仅当技能内容极短且每次对话都需要时才设 true，默认 false

3. **撰写正文**（Markdown，建议 100~500 字，聚焦可执行指令）：
   - `## 使用场景`：何时触发本技能
   - `## 执行方法`：具体步骤或模板，写给 AI 看的祈使句，避免背景科普
   - `## 注意事项`：边界情况与禁忌（可选）

4. **确认并保存**：把设计好的 name/description/正文展示给用户确认，
   确认后调用 save_skill 工具保存。保存成功后告知用户：
   技能已生效，可在输入框上方的「技能」面板中管理它。

## 注意事项

- 一个技能只做一件事；用户需求过大时建议拆分为多个技能
- 正文是写给 AI 的操作指引，不是写给人看的文档，删除一切寒暄与解释性废话
- 若同名技能已存在，先提醒用户 save_skill 会覆盖原内容
)MD";

// 内置技能条目：目录名 + 嵌入的 SKILL.md 全文。
struct BuiltinSkill {
    const char* dir_name;   // 技能目录名（落盘时作为 <skills_dir>/<dir_name>/）
    const char* content;    // SKILL.md 完整内容（frontmatter + 正文）
};

// 内置技能清单：随应用二进制分发，启动时由 installBuiltinSkills 落盘。
const BuiltinSkill kBuiltinSkills[] = {
    {"create-skill", kCreateSkillMd},
};

// 默认全局规则：静态叙事结构参考（自 plot-structure 技能正文下沉，
// 经 RulesProvider 每次注入 system prompt，不再依赖常驻技能）。
const char* kDefaultRulesMd = R"MD(# 叙事结构参考

## 三幕式结构
- **第一幕（建置）**：介绍角色、世界观、核心冲突
- **第二幕（对抗）**：升级矛盾、midpoint 转折
- **第三幕（解决）**：高潮对决、收束伏笔

## 起承转合（网文常用）
- 起：开篇钩子，3 章内建立核心悬念
- 承：展开世界观，深化角色关系
- 转：重大反转或危机
- 合：阶段性收束 + 新钩子

## 节奏控制
- 每 3-5 章设置一个小高潮
- 每卷结尾设置悬念钩子
- 张弛有度：紧张情节后安排日常/感情戏缓冲
)MD";

} // namespace

// 将全部内置技能安装到指定技能根目录（不存在才写入）。
// 语义：已存在的 SKILL.md 一律跳过 —— 尊重用户修改，但删除后下次启动会恢复出厂内容。
void installBuiltinSkills(const std::string& skills_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;  // 全程用 error_code，文件系统异常不抛出、不中断启动

    for (const auto& s : kBuiltinSkills) {
        fs::path dir = fs::path(skills_dir) / s.dir_name;  // 拼技能目录：<skills_dir>/<dir_name>
        fs::path file = dir / "SKILL.md";                  // 拼文件路径：<skills_dir>/<dir_name>/SKILL.md
        if (fs::exists(file, ec))
            continue; // 尊重用户已有（可能被修改过的）版本

        fs::create_directories(dir, ec);
        if (ec) {
            // 建目录失败（权限等）则跳过该技能，记录便于诊断
            spdlog::warn("[Skills] 创建技能目录失败，跳过 {}: {}", dir.string(), ec.message());
            continue;
        }

        std::ofstream out(file, std::ios::binary);  // 二进制写，避免 CRLF 转换
        if (out.is_open())
            out << s.content;
        else
            spdlog::warn("[Skills] 无法打开技能文件，跳过 {}: {}", file.string(), s.dir_name);
    }
}

// 将默认全局规则安装到 <config_dir>/rules.md（不存在才写入）。
// 语义与 installBuiltinSkills 一致：尊重用户已有（可能被修改过）的版本，
// 删除后下次启动恢复出厂内容；写入失败（权限等）静默跳过，不中断启动。
void installDefaultRules(const std::string& config_dir) {
    const std::string path = utils::file::joinPath(config_dir, "rules.md");
    if (utils::file::exists(path))
        return; // 尊重用户已有版本

    try {
        utils::file::writeText(path, kDefaultRulesMd);  // 父目录缺失时自动创建
    } catch (const std::exception& e) {
        // 写入失败不中断启动，下次启动重试；记录原因便于诊断
        spdlog::warn("[Skills] 默认规则写入失败: {}: {}", path, e.what());
    } catch (...) {
        spdlog::warn("[Skills] 默认规则写入失败(未知异常): {}", path);
    }
}

} // namespace skill
