#pragma once

// RulesProvider — “全局规则 + 项目规则”双层 Markdown 叠加（无状态、无缓存）。
//
// 上下文分层约定（见 CLAUDE.md）：
//   - 全局规则：<config_dir>/rules.md — 跨项目、每次对话都需要的硬约束/静态参考
//   - 项目规则：<project>/.novelagent/rules.md — 单项目定制，叠加在全局之后
// 每次 buildSystemPrompt（会话边界）时重新读盘，用户修改文件后下个会话即生效。

#include <string>

namespace agent::prompt {

class RulesProvider {
public:
    // @param config_dir 应用配置目录（通常传 utils::file::configDir()）。
    explicit RulesProvider(std::string config_dir);

    // 叠加读取全局与项目两层规则：各自非空时分别以「## 全局规则」/「## 项目规则」
    // H2 标题包裹，顺序固定全局在前、项目在后；两者皆空返回 ""。
    // @param project_path 项目根目录；为空时跳过项目层，只读全局规则。
    std::string combined(const std::string& project_path) const;

private:
    // 读取文件全文；打不开（不存在/权限）时返回空串，不抛异常。
    static std::string readFile(const std::string& path);

    std::string config_dir_;  // 全局规则所在配置目录
};

} // namespace agent::prompt
