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
    chapter.goal = "Secure the relic without exposing it.";
    chapter.conflict = "The archive starts collapsing.";
    chapter.hook = "Marcus knows more than he admits.";
    chapter.location_id = "library";
    chapter.focus_characters = {"elena"};
    chapter.focus_settings = {"library"};
    chapter.active_plot_threads = {"pt-main"};
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
    CHECK(context->payload["chapter"]["title"] == "Discovery");
    CHECK(!context->payload["chapter"].contains("hook"));
    CHECK(context->payload["characters"].size() == 2);
    CHECK(!context->payload["characters"][0].contains("secret"));
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
    CHECK(context->rendered_prompt.find("## Target Scene") != std::string::npos);
    CHECK(context->rendered_prompt.find("Vault Opens") != std::string::npos);
    CHECK(context->rendered_prompt.find("cheap prophecy twist") != std::string::npos);

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
