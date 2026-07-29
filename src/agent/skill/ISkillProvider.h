#pragma once

// ISkillProvider — 技能查询抽象接口，解耦技能消费方
//（system prompt 组装、use_skill 工具等）与 SkillRegistry 具体实现。

#include "agent/skill/SkillMetadata.h"

#include <string>
#include <vector>

namespace skill {

// 技能查询抽象接口。
class ISkillProvider {
public:
    virtual ~ISkillProvider() = default;

    // 全部已发现技能的元数据列表（含被禁用的；返回副本，线程安全）。
    virtual std::vector<SkillMetadata> listSkills() const = 0;
    // 生成注入 system prompt 的技能上下文文本：always 技能全文常驻，
    // 其余启用技能仅列入 <available_skills> 目录；无可用技能时返回空串。
    virtual std::string getSkillContext() const = 0;
    // 是否存在指定名称的技能（不区分启用/禁用）。
    virtual bool hasSkill(const std::string& name) const = 0;
    // 汇总全部启用技能声明的斜杠命令（frontmatter commands 字段）。
    virtual std::vector<SkillCommand> getAllCommands() const = 0;
};

} // namespace skill
