#pragma once

// 内置技能 — 以嵌入字符串形式随应用分发。
// 应用启动时调用 installBuiltinSkills() 将其落盘到全局技能目录
// （~/.novelagent/skills/<name>/SKILL.md），已存在时不覆盖，
// 用户可自行修改或删除（删除后下次启动会恢复出厂内容）。

#include <string>

namespace skill {

// 将全部内置技能安装到指定技能根目录（不存在才写入）。
void installBuiltinSkills(const std::string& skills_dir);

} // namespace skill
