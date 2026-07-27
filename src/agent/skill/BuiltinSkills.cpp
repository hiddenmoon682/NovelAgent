// 内置技能内容与安装逻辑。

#include "agent/skill/BuiltinSkills.h"

#include <filesystem>
#include <fstream>

namespace skill {

namespace {

// create-skill 元技能：引导用户创建符合规范的新技能，最终用 save_skill 落盘。
const char* kCreateSkillMd = R"MD(---
name: create-skill
emoji: "🛠"
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

struct BuiltinSkill {
    const char* dir_name;
    const char* content;
};

const BuiltinSkill kBuiltinSkills[] = {
    {"create-skill", kCreateSkillMd},
};

} // namespace

void installBuiltinSkills(const std::string& skills_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;

    for (const auto& s : kBuiltinSkills) {
        fs::path dir = fs::path(skills_dir) / s.dir_name;
        fs::path file = dir / "SKILL.md";
        if (fs::exists(file, ec))
            continue; // 尊重用户已有（可能被修改过的）版本

        fs::create_directories(dir, ec);
        if (ec)
            continue;

        std::ofstream out(file, std::ios::binary);
        if (out.is_open())
            out << s.content;
    }
}

} // namespace skill
