#include "agent/tools/ChapterTools.h"
#include "agent/tool/ToolRegistry.h"
#include "project/ProjectIO.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() \
    do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) \
    do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); } } while(0)

namespace fs = std::filesystem;
using json = nlohmann::json;

// ── 辅助：创建测试项目 ──

struct TestProject {
    std::string path;
    Project project;

    TestProject() {
        // 使用临时目录
        path = (fs::temp_directory_path() / "novelagent_test_3_4").string();
        // 清理旧目录（如果存在）
        std::error_code ec;
        fs::remove_all(path, ec);

        ProjectIO::createProjectDir(path, "测试小说");

        // 加载项目
        project = ProjectIO::load(path);

        // 添加一个初始章节
        Chapter ch;
        ch.id = "ch-001";
        ch.title = "第一章";
        ch.order = 1;
        ch.file_path = "chapters/ch-001-chapter1.md";
        ch.synopsis = "初始章节";
        project.outline.chapters.push_back(ch);
        ProjectIO::save(project);

        // 写入初始章节内容
        ProjectIO::writeChapter(path, ch.file_path, "# 第一章\n\n这是第一章的内容。\n");
    }

    ~TestProject() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// =========================================================================
// 测试 1: list_chapters
// =========================================================================

void test_list_chapters() {
    TEST("list_chapters — 列出所有章节");

    TestProject tp;
    agent::ListChaptersTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({});
    CHECK(result.contains("chapters"));
    CHECK(result["chapters"].is_array());
    CHECK(result["chapters"].size() == 1);
    CHECK(result["chapters"][0]["id"] == "ch-001");
    CHECK(result["chapters"][0]["title"] == "第一章");

    PASS();
}

// =========================================================================
// 测试 2: read_chapter
// =========================================================================

void test_read_chapter() {
    TEST("read_chapter — 读取章节全文");

    TestProject tp;
    agent::ReadChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({{"chapter_id", "ch-001"}});
    CHECK(result.contains("content"));
    CHECK(result["title"] == "第一章");
    CHECK(result["content"].get<std::string>().find("第一章的内容") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 3: write_chapter
// =========================================================================

void test_write_chapter() {
    TEST("write_chapter — 覆写章节内容");

    TestProject tp;
    tp.project.allow_auto_overwrite = true;  // D1.2: 测试需跳过覆写确认
    agent::WriteChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({
        {"chapter_id", "ch-001"},
        {"content", "# 改写后的第一章\n\n新内容。\n"}
    });
    CHECK(result["success"] == true);

    // 验证写入
    agent::ReadChapterTool reader(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto readback = reader.execute({{"chapter_id", "ch-001"}});
    CHECK(readback["content"].get<std::string>().find("改写后") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 4: append_to_chapter
// =========================================================================

void test_append_to_chapter() {
    TEST("append_to_chapter — 追加内容");

    TestProject tp;
    agent::AppendChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({
        {"chapter_id", "ch-001"},
        {"content", "\n## 新段落\n\n追加的内容。\n"}
    });
    CHECK(result["success"] == true);

    // 验证原始内容仍在 + 新内容已追加
    agent::ReadChapterTool reader(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto readback = reader.execute({{"chapter_id", "ch-001"}});
    std::string content = readback["content"];
    CHECK(content.find("第一章的内容") != std::string::npos); // 原有内容
    CHECK(content.find("追加的内容") != std::string::npos);   // 新内容

    PASS();
}

// =========================================================================
// 测试 5: create_chapter
// =========================================================================

void test_create_chapter() {
    TEST("create_chapter — 创建新章节");

    TestProject tp;
    agent::CreateChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({{"title", "第二章"}, {"synopsis", "第二章节"}});
    CHECK(result["success"] == true);
    CHECK(result["chapter"]["id"] == "ch-002");
    CHECK(result["chapter"]["title"] == "第二章");

    // 验证 outline 已更新
    CHECK(tp.project.outline.chapters.size() == 2);

    // 验证文件已创建
    std::string file_path = result["chapter"]["file_path"];
    std::string content = ProjectIO::readChapter(tp.path, file_path);
    CHECK(!content.empty());
    CHECK(content.find("第二章") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 6: read_chapter 不存在的章节
// =========================================================================

void test_read_nonexistent() {
    TEST("read_chapter — 不存在的章节返回错误");

    TestProject tp;
    agent::ReadChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    auto result = tool.execute({{"chapter_id", "ch-999"}});
    CHECK(result.contains("error"));
    CHECK(result["error"].get<std::string>().find("不存在") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 7: 通过 ToolRegistry 注册并执行
// =========================================================================

void test_via_registry() {
    TEST("通过 ToolRegistry 注册并执行 Chapter 工具");

    TestProject tp;
    agent::ToolRegistry registry;

    registry.registerBuiltInTool(
        std::make_unique<agent::ListChaptersTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));
    registry.registerBuiltInTool(
        std::make_unique<agent::ReadChapterTool>(std::shared_ptr<Project>(&tp.project, [](Project*){})));

    CHECK(registry.hasTool("list_chapters"));
    CHECK(registry.hasTool("read_chapter"));

    auto defs = registry.getToolDefinitions();
    CHECK(defs.size() == 2);

    // 执行
    auto result = registry.executeTool("list_chapters", {});
    CHECK(result.contains("chapters"));
    CHECK(result["chapters"].size() == 1);

    PASS();
}

// ============================================================================
// A8 + A7 新工具测试
// ============================================================================

void test_delete_chapter() {
    TEST("delete_chapter — 删除章节并验证级联清理");
    TestProject tp;
    agent::CreateChapterTool create(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    create.execute(json{
        {"title", "待删除章"}, {"order", 2},
        {"goal", "测试"}, {"conflict", "测试"}
    });
    // TestProject 自带 ch-001，create 后 chapter 数 = 2
    CHECK(tp.project.outline.chapters.size() == 2);
    std::string ch_id = tp.project.outline.chapters.back().id;
    PlotThread pt;
    pt.id = "pt-test"; pt.name = "测试线"; pt.start_chapter_id = ch_id;
    tp.project.outline.plot_threads.push_back(pt);
    agent::DeleteChapterTool del(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    auto r = del.execute(json{{"chapter_id", ch_id}});
    CHECK(r.value("success", false) == true);
    CHECK(tp.project.outline.chapters.size() == 1);  // 回到只有 ch-001
    CHECK(tp.project.outline.plot_threads[0].start_chapter_id.empty());
    PASS();
}

void test_update_chapter_scenes() {
    TEST("update_chapter_scenes — 完整替换场景列表");
    TestProject tp;
    agent::CreateChapterTool create(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    create.execute(json{{"title", "场景章"}, {"order", 1}});
    std::string ch_id = tp.project.outline.chapters[0].id;
    agent::UpdateChapterScenesTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    json scenes = json::array();
    scenes.push_back(json{
        {"id", "sc-001"}, {"title", "开场"}, {"goal", "引入主角"},
        {"conflict", "内心挣扎"}, {"outcome", "做出决定"}
    });
    auto r = tool.execute(json{{"chapter_id", ch_id}, {"scenes", scenes}});
    CHECK(r.value("success", false) == true);
    CHECK(tp.project.outline.chapters[0].scenes[0].goal == "引入主角");
    PASS();
}

// A6: update_chapter_scenes 软校验——悬空 ID 仍写入（warn 不阻断），不返回 error。
// 验证语义：引用校验是"提示而非阻断"，与项目错误处理策略（单点失败友好降级）一致。
void test_update_chapter_scenes_dangling_softcheck() {
    TEST("update_chapter_scenes — 悬空引用软校验不阻断写入");
    TestProject tp;
    // 项目无任何角色/设定/剧情线，pov_character_id/participants 等必为悬空
    agent::CreateChapterTool create(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    create.execute(json{{"title", "悬空场景章"}});
    std::string ch_id = tp.project.outline.chapters[0].id;
    agent::UpdateChapterScenesTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));
    json scenes = json::array();
    scenes.push_back(json{
        {"id", "sc-001"}, {"title", "场景"},
        {"pov_character_id", "char-999"},          // 悬空角色
        {"location_id", "set-999"},                // 悬空设定
        {"participants", json::array({"char-888"})},
        {"plot_thread_ids", json::array({"pt-999"})}
    });
    auto r = tool.execute(json{{"chapter_id", ch_id}, {"scenes", scenes}});
    // 软校验：不阻断，仍写入成功
    CHECK(r.value("success", false) == true);
    CHECK(!r.contains("error"));
    // 悬空 ID 被原样保留（校验仅 warn，不清空）
    CHECK(tp.project.outline.chapters[0].scenes[0].pov_character_id == "char-999");
    CHECK(tp.project.outline.chapters[0].scenes[0].participants.size() == 1);
    PASS();
}

// =========================================================================
// D1+D2 回归测试（ID 生成统一 + 消除空 catch）
// =========================================================================

// D1 回归安全网：预置手动创建的非连续补零 ID + 非标准 ID，连续创建多章，
// 验证新建 ID 不与任何存量 ID 重复且全局唯一。
// 注意：此用例是回归安全网而非 fail-before-fix 证明——冲突回退分支属防御性
// 代码，正常编号扫描下不可达（存量补零 ID 必然被解析计入 max，候选
// pad(max+1) 不会与之重叠），无法构造让修复前失败的黑盒输入。
void should_avoid_id_collision_when_manual_id_overlaps_sequence() {
    TEST("create_chapter — 手动非连续 ID 不引发新建 ID 冲突");

    TestProject tp;  // 自带 ch-001

    // 手动预置非连续补零 ID（跳过 ch-002/ch-004）+ 一个非标准 ID
    for (const char* id : {"ch-003", "ch-005", "ch-005x"}) {
        Chapter ch;
        ch.id = id;
        ch.title = std::string("手动章 ") + id;
        ch.order = static_cast<int>(tp.project.outline.chapters.size()) + 1;
        tp.project.outline.chapters.push_back(ch);
    }

    agent::CreateChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    // 连续创建 3 章，应从 max(1,3,5)+1=6 开始递增
    for (int i = 0; i < 3; ++i) {
        auto r = tool.execute({{"title", "新章节 " + std::to_string(i)}});
        CHECK(r.value("success", false) == true);
    }

    // 新建 ID 遵循补零递增格式
    const auto& chs = tp.project.outline.chapters;
    CHECK(chs.size() == 7);
    CHECK(chs[4].id == "ch-006");
    CHECK(chs[5].id == "ch-007");
    CHECK(chs[6].id == "ch-008");

    // 全局唯一：任意两章 ID 不重复
    for (size_t i = 0; i < chs.size(); ++i)
        for (size_t j = i + 1; j < chs.size(); ++j)
            CHECK(chs[i].id != chs[j].id);

    PASS();
}

// D2 回归：非标准 ID（ch-abc）在编号统计中被跳过且不抛异常。
void should_log_and_skip_when_id_suffix_not_numeric() {
    TEST("create_chapter — 非标准 ID 尾号跳过统计且不抛异常");

    TestProject tp;  // 自带 ch-001

    Chapter odd;
    odd.id = "ch-abc";
    odd.title = "非标准 ID 章";
    odd.order = 2;
    tp.project.outline.chapters.push_back(odd);

    agent::CreateChapterTool tool(std::shared_ptr<Project>(&tp.project, [](Project*){}));

    // 不抛异常：异常已在 tryParseIdNumber 内部捕获并 debug 记录
    json r;
    try {
        r = tool.execute({{"title", "新章节"}});
    } catch (const std::exception& e) {
        FAIL(std::string("意外抛出异常: ") + e.what());
    }

    CHECK(r.value("success", false) == true);
    // ch-abc 不参与编号统计：max 仍为 1，新章为 ch-002
    CHECK(r["chapter"]["id"] == "ch-002");

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_chapter_tools ===\n\n";

    test_list_chapters();
    test_read_chapter();
    test_write_chapter();
    test_append_to_chapter();
    test_create_chapter();
    test_read_nonexistent();
    test_via_registry();
    test_delete_chapter();
    test_update_chapter_scenes();
    test_update_chapter_scenes_dangling_softcheck();
    should_avoid_id_collision_when_manual_id_overlaps_sequence();
    should_log_and_skip_when_id_suffix_not_numeric();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
