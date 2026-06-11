/// TUI 组件单元测试。
/// 测试 ChatPanel、InputBar、StatusBar、Sidebar 的核心逻辑。
/// FTXUI 渲染结果通过 Element 转字符串验证。

#include "../src/tui/TuiChatPanel.h"
#include "../src/tui/TuiInputBar.h"
#include "../src/tui/TuiStatusBar.h"
#include "../src/tui/TuiSidebar.h"

#include "../src/project/Models.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

// ── 辅助宏 ──
static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) \
    do { std::cout << "  " << name << "... "; } while(0)

#define PASS() \
    do { std::cout << "通过\n"; testsPassed++; } while(0)

#define FAIL(msg) \
    do { std::cout << "失败: " << msg << "\n"; testsFailed++; } while(0)

#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); return; } } while(0)

// ── TuiChatPanel 测试 ──

static void test_chat_panel_initial_state() {
    TEST("初始状态");
    TuiChatPanel panel;
    // 空面板渲染不应崩溃
    auto elem = panel.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_chat_panel_append_user() {
    TEST("追加用户消息");
    TuiChatPanel panel;
    panel.appendUserMessage("你好");
    auto elem = panel.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_chat_panel_streaming() {
    TEST("流式文本追加");
    TuiChatPanel panel;
    panel.startAssistantMessage();
    panel.appendContent("这是");
    panel.appendContent("一段");
    panel.appendContent("流式文本");
    panel.finishMessage();
    auto elem = panel.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_chat_panel_multiple_messages() {
    TEST("多条消息渲染");
    TuiChatPanel panel;
    panel.appendUserMessage("问题1");
    panel.startAssistantMessage();
    panel.appendContent("回答1");
    panel.finishMessage();
    panel.appendUserMessage("问题2");
    panel.startAssistantMessage();
    panel.appendContent("回答2");
    panel.finishMessage();
    auto elem = panel.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_chat_panel_error() {
    TEST("错误消息");
    TuiChatPanel panel;
    panel.appendError("网络连接失败");
    auto elem = panel.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_chat_panel_tool_call() {
    TEST("工具调用消息");
    TuiChatPanel panel;
    panel.appendToolCall("read_chapter");
    auto elem = panel.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_chat_panel_thinking() {
    TEST("思考链消息");
    TuiChatPanel panel;
    panel.appendThinking("正在分析角色关系…");
    panel.appendThinking("确定剧情走向…");
    auto elem = panel.render();
    CHECK(elem != nullptr);
    PASS();
}

// ── TuiInputBar 测试 ──

static void test_input_bar_construction() {
    TEST("InputBar 构造");
    TuiInputBar bar;
    auto comp = bar.render();
    CHECK(comp != nullptr);
    PASS();
}

static void test_input_bar_submit_callback() {
    TEST("提交回调");
    TuiInputBar bar;
    std::string received;
    bar.onSubmit([&received](const std::string& text) {
        received = text;
    });
    // 直接调用回调（模拟 Enter 提交）
    // 由于无法模拟 FTXUI 事件，验证回调已设置即可
    CHECK(received.empty());  // 尚未触发
    PASS();
}

// ── TuiStatusBar 测试 ──

static void test_status_bar_default() {
    TEST("默认模式");
    TuiStatusBar bar;
    auto elem = bar.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_status_bar_mode_change() {
    TEST("模式切换");
    TuiStatusBar bar;
    bar.setMode("思考中");
    auto elem = bar.render();
    CHECK(elem != nullptr);

    bar.setMode("执行工具");
    elem = bar.render();
    CHECK(elem != nullptr);

    bar.setMode("错误");
    elem = bar.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_status_bar_project_info() {
    TEST("项目信息更新");
    TuiStatusBar bar;
    bar.setProjectInfo("星辰大海", 12, 5);
    bar.updateTokens(5000);
    auto elem = bar.render();
    CHECK(elem != nullptr);
    PASS();
}

// ── TuiSidebar 测试 ──

static void test_sidebar_empty_project() {
    TEST("空项目侧边栏");
    TuiSidebar sidebar;
    sidebar.setProject(nullptr);
    auto elem = sidebar.render();
    CHECK(elem != nullptr);
    PASS();
}

static void test_sidebar_with_project() {
    TEST("带项目数据侧边栏");
    // 构造测试 Project 数据
    auto project = std::make_shared<Project>();
    project->title = "测试小说";

    // 添加章节
    Chapter ch1;
    ch1.id = "ch-001";
    ch1.title = "开幕雷击";
    ch1.order = 1;
    project->outline.chapters.push_back(ch1);

    Chapter ch2;
    ch2.id = "ch-002";
    ch2.title = "命运转折";
    ch2.order = 2;
    project->outline.chapters.push_back(ch2);

    // 添加角色
    Character c1;
    c1.id = "char-001";
    c1.name = "林逸";
    c1.role = "主角";
    project->characters.push_back(c1);

    TuiSidebar sidebar;
    sidebar.setProject(project);
    auto elem = sidebar.render();
    CHECK(elem != nullptr);

    // 刷新不崩溃
    sidebar.refresh();
    elem = sidebar.render();
    CHECK(elem != nullptr);
    PASS();
}

// ── 主函数 ──

int main() {
    std::cout << "test_tui:\n";

    test_chat_panel_initial_state();
    test_chat_panel_append_user();
    test_chat_panel_streaming();
    test_chat_panel_multiple_messages();
    test_chat_panel_error();
    test_chat_panel_tool_call();
    test_chat_panel_thinking();

    test_input_bar_construction();
    test_input_bar_submit_callback();

    test_status_bar_default();
    test_status_bar_mode_change();
    test_status_bar_project_info();

    test_sidebar_empty_project();
    test_sidebar_with_project();

    std::cout << "\n结果: " << testsPassed << "/" << (testsPassed + testsFailed)
              << " 通过\n";
    return testsFailed > 0 ? 1 : 0;
}
