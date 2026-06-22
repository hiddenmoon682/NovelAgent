#pragma once

/// FTXUI TUI 侧边栏 — 右侧项目信息面板。
///
/// 通过 IProjectReader 读取项目数据，显示：
/// - 大纲 Tab：章节列表（ID/标题/顺序）
/// - 角色 Tab：角色列表（ID/名称/身份）

#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <vector>

// 前向声明
struct Project;
namespace project { class IProjectReader; }

/// 侧边栏中显示的一行信息。
struct SidebarEntry {
    std::string id;
    std::string label;      ///< 主标题
    std::string subtitle;   ///< 副标题（如章节编号、角色身份）
};

/// 侧边栏组件，从 Project 读取数据显示大纲和角色列表。
class TuiSidebar {
public:
    TuiSidebar();

    /// 设置数据源（可为空，表示无项目）。
    void setProject(std::shared_ptr<Project> project);

    /// 刷新数据（重新从 Project 读取）。
    void refresh();

    /// 渲染侧边栏为 FTXUI Element。
    ftxui::Element render() const;

private:
    std::shared_ptr<Project> project_;
    std::vector<SidebarEntry> chapters_;
    std::vector<SidebarEntry> characters_;

    /// 从 Project 加载章节和角色数据。
    void loadData();
};
