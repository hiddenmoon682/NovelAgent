#include "prompt/PromptContextBuilder.h"
#include "project/ProjectIO.h"
#include "utils/FileUtils.h"

#include <iostream>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        ++tests_run; \
        std::cout << "  TEST " << (name) << " ... "; \
    } while (0)

#define PASS() \
    do { \
        ++tests_passed; \
        std::cout << "PASSED\n"; \
    } while (0)

#define FAIL(msg) \
    do { \
        std::cout << "FAILED: " << (msg) << '\n'; \
        return; \
    } while (0)

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            FAIL(#cond); \
        } \
    } while (0)

const std::string kTestDir = "__test_prompt_context_tmp";

void cleanup() {
    if (utils::file::exists(kTestDir)) {
        utils::file::removeDir(kTestDir);
    }
}

Project buildSampleProject() {
    Project project;
    project.title = "Shadow of the Ancients";
    project.description = "A scholar uncovers a dangerous relic.";
    project.logline = "A scholar steals a relic that wants to be found.";
    project.theme = "Knowledge has a cost.";
    project.target_audience = "Adult fantasy readers";
    project.genre = {"fantasy", "mystery"};
    project.must_avoid_elements = {"cheap prophecy twist"};
    project.path = kTestDir;

    project.style.tone = "atmospheric";
    project.style.pov = "third_person_limited";
    project.style.tense = "past";
    project.style.dialogue_density = "dense";
    project.style.forbidden_phrases = {"suddenly"};

    project.outline.premise = "A buried relic drags a historian into a hidden war.";
    project.outline.story_structure = "three-act";

    Volume vol;
    vol.id = "vol-001";
    vol.title = "第一卷: 觉醒";
    vol.summary = "主角发现遗物，卷入隐藏战争。";
    vol.theme = "真相与代价";
    vol.goal = "建立世界观基础，引入核心冲突。";
    vol.focus_characters = {"elena"};
    vol.active_plot_threads = {"pt-main"};
    project.outline.volumes.push_back(vol);

    PlotThread plot;
    plot.id = "pt-main";
    plot.name = "Main Mystery";
    plot.description = "Discover what the relic wants.";
    plot.related_characters = {"elena", "marcus"};
    plot.related_settings = {"library"};
    project.outline.plot_threads.push_back(plot);

    Chapter chapter;
    chapter.id = "ch-001";
    chapter.title = "Discovery";
    chapter.order = 1;
    chapter.goal = "Secure the relic without exposing it.";
    chapter.conflict = "The archive starts collapsing.";
    chapter.hook = "Marcus knows more than he admits.";
    chapter.location_id = "library";
    chapter.focus_characters = {"elena"};
    chapter.focus_settings = {"library"};
    chapter.active_plot_threads = {"pt-main"};
    chapter.volume_id = "vol-001";
    chapter.file_path = "chapters/001-discovery.md";
    chapter.generation.exclude_fields = {"hook"};

    Scene scene;
    scene.id = "sc-001";
    scene.title = "Vault Opens";
    scene.summary = "Elena reaches the hidden chamber.";
    scene.goal = "Take the relic before the chamber seals.";
    scene.location_id = "library";
    scene.participants = {"elena", "marcus"};
    chapter.scenes.push_back(scene);

    project.outline.chapters.push_back(chapter);

    Character elena;
    elena.id = "elena";
    elena.name = "Elena";
    elena.goal = "Protect the relic.";
    elena.secret = "The relic reacted to her touch.";
    elena.speaking_style = "Precise and controlled.";
    elena.chapter_appearances = {"ch-001"};
    elena.generation.exclude_fields = {"secret"};

    CharacterDevelopment dev1;
    dev1.id = "dev-001";
    dev1.chapter_id = "ch-001";
    dev1.summary = "首次接触遗物，产生异常反应。";
    dev1.category = "ability";
    dev1.affected_fields = {"secret"};
    elena.development.push_back(dev1);

    CharacterDevelopment dev2;
    dev2.id = "dev-002";
    dev2.chapter_id = "ch-050"; // 未来章节，不应出现在 ch-001 的上下文中
    dev2.summary = "最终觉醒，掌控遗物之力。";
    dev2.category = "ability";
    elena.development.push_back(dev2);

    project.characters.push_back(elena);

    Character marcus;
    marcus.id = "marcus";
    marcus.name = "Marcus";
    marcus.goal = "Keep Elena alive long enough to reach the truth.";
    marcus.chapter_appearances = {"ch-001"};
    project.characters.push_back(marcus);

    Setting library;
    library.id = "library";
    library.name = "Thorne Library";
    library.category = "location";
    library.description = "An ancient library built above buried ruins.";
    library.related_rule_ids = {"wr-echo"};
    library.metadata["architecture"] = "gothic";
    project.settings.push_back(library);

    WorldRule rule;
    rule.id = "wr-echo";
    rule.name = "Relic Echo";
    rule.summary = "The relic amplifies obsession.";
    rule.related_settings = {"library"};
    project.world_rules.push_back(rule);

    return project;
}

void test_build_context_filters_fields() {
    TEST("build context filters fields");
    cleanup();
    ProjectIO::createProjectDir(kTestDir, "Prompt Context");

    Project project = buildSampleProject();
    ProjectIO::writeChapter(kTestDir, "chapters/001-discovery.md", "Existing draft text.");

    prompt::PromptContextOptions options;
    options.chapter_id = "ch-001";
    options.include_chapter_text = true;

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(context.has_value());
    CHECK(context->payload["volume"]["title"] == "第一卷: 觉醒");
    CHECK(context->payload["chapter"]["title"] == "Discovery");
    CHECK(!context->payload["chapter"].contains("hook"));
    CHECK(context->payload["characters"].size() == 2);
    CHECK(!context->payload["characters"][0].contains("secret"));
    // development 应只包含 ch-001 的记录，ch-050 的被过滤掉
    CHECK(context->payload["characters"][0]["development"].size() == 1);
    CHECK(context->payload["characters"][0]["development"][0]["summary"] == "首次接触遗物，产生异常反应。");
    CHECK(context->rendered_prompt.find("最终觉醒") == std::string::npos);
    CHECK(context->payload["settings"][0]["name"] == "Thorne Library");
    CHECK(context->payload["world_rules"][0]["name"] == "Relic Echo");
    CHECK(context->payload["chapter_text"] == "Existing draft text.");

    PASS();
}

void test_scene_targeting_and_render() {
    TEST("scene targeting and render");
    cleanup();
    ProjectIO::createProjectDir(kTestDir, "Prompt Context");

    Project project = buildSampleProject();

    prompt::PromptContextOptions options;
    options.chapter_id = "ch-001";
    options.scene_id = "sc-001";

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(context.has_value());
    CHECK(context->payload["scene"]["title"] == "Vault Opens");
    CHECK(context->rendered_prompt.find("## Volume") != std::string::npos);
    CHECK(context->rendered_prompt.find("第一卷: 觉醒") != std::string::npos);
    CHECK(context->rendered_prompt.find("发展记录") != std::string::npos);
    CHECK(context->rendered_prompt.find("首次接触遗物") != std::string::npos);
    CHECK(context->rendered_prompt.find("## Target Scene") != std::string::npos);
    CHECK(context->rendered_prompt.find("Vault Opens") != std::string::npos);
    CHECK(context->rendered_prompt.find("cheap prophecy twist") != std::string::npos);

    PASS();
}

void test_volume_id_mismatch_adds_note() {
    TEST("volume id mismatch adds note");

    Project project = buildSampleProject();
    // 修改 chapter 的 volume_id 指向不存在的卷
    project.outline.chapters[0].volume_id = "vol-404";

    prompt::PromptContextOptions options;
    options.chapter_id = "ch-001";

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(context.has_value());
    CHECK(!context->payload.contains("volume"));
    bool hasMismatchNote = false;
    for (const auto& note : context->notes) {
        if (note.find("vol-404") != std::string::npos) {
            hasMismatchNote = true;
        }
    }
    CHECK(hasMismatchNote);

    PASS();
}

void test_no_volume_id_produces_no_volume_section() {
    TEST("no volume_id produces no volume section");

    Project project = buildSampleProject();
    project.outline.chapters[0].volume_id = "";

    prompt::PromptContextOptions options;
    options.chapter_id = "ch-001";

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(context.has_value());
    CHECK(!context->payload.contains("volume"));
    CHECK(context->rendered_prompt.find("## Volume") == std::string::npos);

    PASS();
}

void test_development_excluded_by_generation_control() {
    TEST("development excluded by generation control");

    Project project = buildSampleProject();
    // 排除 development 字段
    project.characters[0].generation.exclude_fields = {"secret", "development"};

    prompt::PromptContextOptions options;
    options.chapter_id = "ch-001";

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(context.has_value());
    // development 不应出现在 payload 中
    CHECK(!context->payload["characters"][0].contains("development"));
    // development 不应出现在 rendered_prompt 中
    CHECK(context->rendered_prompt.find("发展记录") == std::string::npos);
    CHECK(context->rendered_prompt.find("首次接触遗物") == std::string::npos);

    PASS();
}

void test_development_order_zero_includes_all() {
    TEST("development order zero includes all");

    Project project = buildSampleProject();
    // chapter.order == 0 时不过滤，全部发展记录都应包含
    project.outline.chapters[0].order = 0;

    // 添加一个 chapter_id 不存在的记录，order==0 时全部包含
    CharacterDevelopment dev3;
    dev3.id = "dev-orphan";
    dev3.chapter_id = "ch-deleted";
    dev3.summary = "这条记录引用的章节不存在。";
    project.characters[0].development.push_back(dev3);

    prompt::PromptContextOptions options;
    options.chapter_id = "ch-001";

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(context.has_value());
    // order==0 时 filterByOrder=false，全部 3 条记录都应包含
    CHECK(context->payload["characters"][0]["development"].size() == 3);

    PASS();
}

// 验证正常 order 下 orphan 章节会产生告警
void test_development_orphan_chapter_warns() {
    TEST("development orphan chapter warns");

    Project project = buildSampleProject();
    // order>0，触发 orphan 检测
    project.outline.chapters[0].order = 2;
    CharacterDevelopment devOrphan;
    devOrphan.id = "dev-orphan";
    devOrphan.chapter_id = "ch-deleted";
    devOrphan.summary = "orphan record";
    project.characters[0].development.push_back(devOrphan);

    prompt::PromptContextOptions options;
    options.chapter_id = "ch-001";

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(context.has_value());
    bool hasOrphanNote = false;
    for (const auto& note : context->notes) {
        if (note.find("ch-deleted") != std::string::npos) {
            hasOrphanNote = true;
        }
    }
    CHECK(hasOrphanNote);

    PASS();
}

void test_missing_chapter_returns_nullopt() {
    TEST("missing chapter returns nullopt");

    Project project = buildSampleProject();
    prompt::PromptContextOptions options;
    options.chapter_id = "ch-404";

    auto context = prompt::PromptContextBuilder::buildForChapter(project, options);
    CHECK(!context.has_value());

    PASS();
}

int main() {
    std::cout << "=== test_prompt_context ===\n\n";

    test_build_context_filters_fields();
    test_scene_targeting_and_render();
    test_volume_id_mismatch_adds_note();
    test_no_volume_id_produces_no_volume_section();
    test_development_excluded_by_generation_control();
    test_development_order_zero_includes_all();
    test_development_orphan_chapter_warns();
    test_missing_chapter_returns_nullopt();

    cleanup();

    std::cout << "\nResult: " << tests_passed << '/' << tests_run << " passed\n";
    if (tests_passed == tests_run) {
        std::cout << "All tests passed!\n";
        return 0;
    }

    std::cout << (tests_run - tests_passed) << " tests failed\n";
    return 1;
}
