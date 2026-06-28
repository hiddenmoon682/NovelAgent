#include "project/Models.h"

#include <cassert>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

void test_chapter_roundtrip() {
    TEST("Chapter roundtrip");

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "Opening";
    ch.order = 1;
    ch.synopsis = "The protagonist finds an impossible artifact.";
    ch.goal = "Secure the artifact before anyone else sees it.";
    ch.conflict = "The vault begins to collapse.";
    ch.hook = "Someone already knows the relic is missing.";
    ch.location_id = "thorne-library";
    ch.time_marker = "Night One";
    Scene s1;
    s1.id = "sc-001";
    s1.summary = "Elena studies the sealed archive.";
    s1.goal = "Find the hidden chamber.";
    s1.location_id = "library";
    Scene s2;
    s2.id = "sc-002";
    s2.summary = "The basement shakes as the vault opens.";
    s2.conflict = "The old mechanism becomes unstable.";
    ch.scenes = {s1, s2};
    ch.pov_characters = {"elena"};
    ch.key_events = {"artifact_discovered"};
    ch.themes = {"discovery", "curiosity"};
    ch.active_plot_threads = {"pt-main"};
    ch.focus_characters = {"elena"};
    ch.focus_settings = {"thorne-library"};
    ch.status = "drafted";
    ch.word_count = 4500;
    ch.file_path = "chapters/001-intro.md";
    ch.metadata["goal"] = "introduce_artifact";
    ch.metadata["intensity"] = 0.7;

    json j = ch;
    Chapter ch2 = j.get<Chapter>();

    CHECK(ch2.id == "ch-001");
    CHECK(ch2.title == "Opening");
    CHECK(ch2.order == 1);
    CHECK(ch2.synopsis == "The protagonist finds an impossible artifact.");
    CHECK(ch2.goal == "Secure the artifact before anyone else sees it.");
    CHECK(ch2.scenes.size() == 2);
    CHECK(ch2.scenes[0].summary == "Elena studies the sealed archive.");
    CHECK(ch2.scenes[1].conflict == "The old mechanism becomes unstable.");
    CHECK(ch2.pov_characters[0] == "elena");
    CHECK(ch2.key_events[0] == "artifact_discovered");
    CHECK(ch2.themes[1] == "curiosity");
    CHECK(ch2.active_plot_threads[0] == "pt-main");
    CHECK(ch2.focus_settings[0] == "thorne-library");
    CHECK(ch2.status == "drafted");
    CHECK(ch2.word_count == 4500);
    CHECK(ch2.file_path == "chapters/001-intro.md");
    CHECK(ch2.metadata.at("goal") == "introduce_artifact");
    CHECK(ch2.metadata.at("intensity") == 0.7);

    PASS();
}

void test_chapter_defaults() {
    TEST("Chapter defaults");

    Chapter ch;
    ch.id = "ch-min";
    ch.title = "Minimal";

    json j = ch;
    Chapter ch2 = j.get<Chapter>();

    CHECK(ch2.id == "ch-min");
    CHECK(ch2.order == 0);
    CHECK(ch2.status == "outlined");
    CHECK(ch2.scenes.empty());
    CHECK(ch2.pov_characters.empty());
    CHECK(ch2.key_events.empty());
    CHECK(ch2.themes.empty());
    CHECK(ch2.word_count == 0);
    CHECK(ch2.metadata.empty());

    PASS();
}

void test_character_roundtrip() {
    TEST("Character roundtrip");

    Character c;
    c.id = "elena";
    c.name = "Elena Vasquez";
    c.role = "protagonist";
    c.age = "28";
    c.appearance = "Tall, dark-haired, sharp-eyed.";
    c.personality = "Curious, decisive, occasionally reckless.";
    c.background = "A historian teaching at Thorne University.";
    c.goal = "Understand the relic before the cult finds it.";
    c.motivation = "Her father vanished pursuing the same mystery.";
    c.secret = "She has already touched the relic in private.";
    c.speaking_style = "Measured, precise, but sharp under pressure.";
    c.traits = {"intelligent", "brave", "impulsive"};
    c.core_values = {"truth", "loyalty"};
    c.taboos = {"abandoning a teammate"};
    Relationship mentor;
    mentor.target_character_id = "marcus";
    mentor.type = "mentor";
    mentor.private_feeling = "She wants his approval.";
    Relationship rival;
    rival.target_character_id = "lyra";
    rival.type = "rival";
    rival.tension = 7;
    c.relationships = {mentor, rival};
    c.chapter_appearances = {"ch-001", "ch-002"};
    c.arc = "Moves from observer to world-changing actor.";
    c.notes = "Strengthen the emotional motive.";
    c.metadata["secret"] = "family-archive";

    json j = c;
    Character c2 = j.get<Character>();

    CHECK(c2.id == "elena");
    CHECK(c2.name == "Elena Vasquez");
    CHECK(c2.role == "protagonist");
    CHECK(c2.age == "28");
    CHECK(c2.goal == "Understand the relic before the cult finds it.");
    CHECK(c2.traits.size() == 3);
    CHECK(c2.relationships.size() == 2);
    CHECK(c2.relationships[0].target_character_id == "marcus");
    CHECK(c2.relationships[0].type == "mentor");
    CHECK(c2.relationships[1].target_character_id == "lyra");
    CHECK(c2.relationships[1].tension == 7);
    CHECK(c2.chapter_appearances[1] == "ch-002");
    CHECK(c2.arc == "Moves from observer to world-changing actor.");
    CHECK(c2.notes == "Strengthen the emotional motive.");
    CHECK(c2.metadata.at("secret") == "family-archive");

    PASS();
}

void test_setting_roundtrip() {
    TEST("Setting roundtrip");

    Setting s;
    s.id = "thorne-university";
    s.name = "Thorne University";
    s.category = "location";
    s.description = "An ancient university built over older ruins.";
    s.story_function = "mystery-gateway";
    s.sensory_profile = "dust, stone, whispers, cold iron";
    s.related_characters = {"elena"};
    s.related_rule_ids = {"rule-relic"};
    s.notes = "The library has a sealed restricted wing.";
    s.metadata["architecture"] = "gothic revival";
    s.metadata["location"] = "Kingsport";
    s.metadata["founded"] = "1642";
    s.metadata["hazards"] = json::array({"sealed-wing", "artifact-vault"});

    json j = s;
    Setting s2 = j.get<Setting>();

    CHECK(s2.id == "thorne-university");
    CHECK(s2.name == "Thorne University");
    CHECK(s2.category == "location");
    CHECK(s2.story_function == "mystery-gateway");
    CHECK(s2.notes == "The library has a sealed restricted wing.");
    CHECK(s2.metadata.at("founded") == "1642");
    CHECK(s2.metadata.at("location") == "Kingsport");
    CHECK(s2.metadata.at("hazards").is_array());

    PASS();
}

void test_outline_roundtrip() {
    TEST("Outline roundtrip");

    Outline outline;
    outline.premise = "A young scholar discovers an impossible relic.";
    outline.story_structure = "three-act";
    outline.act_summaries = {"discovery", "descent", "confrontation"};
    PlotThread mainThread;
    mainThread.id = "pt1";
    mainThread.name = "Main";
    mainThread.description = "Discover what the relic is";
    PlotThread bondThread;
    bondThread.id = "pt2";
    bondThread.name = "Bond";
    bondThread.description = "Learn to trust her allies";
    outline.plot_threads = {mainThread, bondThread};

    Chapter ch1;
    ch1.id = "ch-001";
    ch1.title = "Discovery";
    ch1.order = 1;
    ch1.metadata["emotion_curve"] = json::array({"calm", "alarm"});

    Chapter ch2;
    ch2.id = "ch-002";
    ch2.title = "Departure";
    ch2.order = 2;

    outline.chapters = {ch1, ch2};

    json j = outline;
    Outline o2 = j.get<Outline>();

    CHECK(o2.premise == outline.premise);
    CHECK(o2.story_structure == "three-act");
    CHECK(o2.plot_threads.size() == 2);
    CHECK(o2.plot_threads[0].name == "Main");
    CHECK(o2.chapters.size() == 2);
    CHECK(o2.chapters[0].metadata.at("emotion_curve").is_array());

    PASS();
}

void test_style_roundtrip() {
    TEST("Style roundtrip");

    Style s;
    s.tone = "atmospheric";
    s.pacing = "moderate";
    s.prose_style = "literary";
    s.dialogue_style = "naturalistic";
    s.narrative_distance = "close";
    s.chapter_length_target = 4000;
    s.sentence_length = "varied";
    s.vocabulary = "rich";
    s.voice_reference = "Elegant but tense.";
    s.dialogue_density = "dense";
    s.forbidden_phrases = {"suddenly", "all of a sudden"};
    s.notes = "Avoid modern slang.";
    s.metadata["forbidden_topics"] = json::array({"internet slang"});

    json j = s;
    Style s2 = j.get<Style>();

    CHECK(s2.tone == "atmospheric");
    CHECK(s2.chapter_length_target == 4000);
    CHECK(s2.dialogue_density == "dense");
    CHECK(s2.forbidden_phrases[0] == "suddenly");
    CHECK(s2.notes == "Avoid modern slang.");
    CHECK(s2.metadata.at("forbidden_topics").is_array());

    PASS();
}

void test_project_serialization() {
    TEST("Project serialization");

    Project p;
    p.format_version = 3;
    p.title = "Shadow of the Ancients";
    p.author = "Test Author";
    p.description = "A fantasy adventure.";
    p.logline = "A scholar steals a relic that wants to be found.";
    p.theme = "Knowledge always has a cost.";
    p.target_audience = "Adult fantasy readers";
    p.genre = {"fantasy", "epic"};
    p.target_word_count = 100000;
    p.current_word_count = 12500;
    p.status = "in_progress";
    p.created = "2026-05-17T10:00:00Z";
    p.modified = "2026-05-17T14:30:00Z";
    p.metadata["workflow"] = "drafting";
    p.path = "D:/novels/my-novel";

    json j = p;

    CHECK(!j.contains("path"));
    CHECK(j["format_version"] == 3);
    CHECK(j["metadata"]["workflow"] == "drafting");

    Project p2 = j.get<Project>();
    CHECK(p2.title == "Shadow of the Ancients");
    CHECK(p2.logline == "A scholar steals a relic that wants to be found.");
    CHECK(p2.genre[1] == "epic");
    CHECK(p2.metadata.at("workflow") == "drafting");

    PASS();
}

void test_project_subobjects() {
    TEST("Project subobjects");

    Project p;
    p.title = "Test Novel";

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "First";
    p.outline.chapters.push_back(ch);

    Character c;
    c.id = "hero";
    c.name = "Hero";
    p.characters.push_back(c);

    Setting s;
    s.id = "castle";
    s.name = "Castle";
    p.settings.push_back(s);

    p.style.tone = "dark";

    CHECK(p.outline.chapters.size() == 1);
    CHECK(p.characters[0].name == "Hero");
    CHECK(p.settings[0].id == "castle");
    CHECK(p.style.tone == "dark");

    PASS();
}

void test_legacy_metadata_capture() {
    TEST("Legacy metadata capture");

    json chapterJson = {
        {"id", "ch-legacy"},
        {"title", "Legacy"},
        {"emotion_curve", json::array({"calm", "panic"})}
    };
    Chapter ch = chapterJson.get<Chapter>();
    CHECK(ch.metadata.at("emotion_curve").is_array());
    CHECK(ch.scenes.empty());

    json settingJson = {
        {"id", "tower"},
        {"name", "Tower"},
        {"height", "high"}
    };
    Setting s = settingJson.get<Setting>();
    CHECK(s.metadata.at("height") == "high");

    json projectJson = {
        {"format_version", 1},
        {"title", "legacy-project"},
        {"custom_flag", true}
    };
    Project p = projectJson.get<Project>();
    CHECK(p.format_version == 1);
    CHECK(p.metadata.at("custom_flag") == true);

    PASS();
}

void test_volume_roundtrip() {
    TEST("Volume roundtrip");

    Volume vol;
    vol.id = "vol-001";
    vol.title = "第一卷: 学院篇";
    vol.order = 1;
    vol.summary = "主角进入学院，结识盟友，发现隐藏在学院地下的秘密。";
    vol.theme = "成长与觉醒";
    vol.goal = "建立主角的初始人际关系网，铺设主线伏笔。";
    vol.start_chapter_id = "ch-001";
    vol.end_chapter_id = "ch-030";
    vol.key_events = {"入学考试", "图书馆发现", "密室初探"};
    vol.focus_characters = {"elena", "marcus"};
    vol.active_plot_threads = {"pt-main", "pt-academy"};
    vol.metadata["target_word_count"] = 120000;

    json j = vol;
    Volume v2 = j.get<Volume>();

    CHECK(v2.id == "vol-001");
    CHECK(v2.title == "第一卷: 学院篇");
    CHECK(v2.order == 1);
    CHECK(v2.summary == "主角进入学院，结识盟友，发现隐藏在学院地下的秘密。");
    CHECK(v2.theme == "成长与觉醒");
    CHECK(v2.goal == "建立主角的初始人际关系网，铺设主线伏笔。");
    CHECK(v2.start_chapter_id == "ch-001");
    CHECK(v2.end_chapter_id == "ch-030");
    CHECK(v2.key_events.size() == 3);
    CHECK(v2.key_events[1] == "图书馆发现");
    CHECK(v2.focus_characters.size() == 2);
    CHECK(v2.active_plot_threads[0] == "pt-main");
    CHECK(v2.metadata.at("target_word_count") == 120000);

    PASS();
}

void test_volume_defaults() {
    TEST("Volume defaults");

    Volume vol;
    vol.id = "vol-min";
    vol.title = "Minimal Volume";

    json j = vol;
    Volume v2 = j.get<Volume>();

    CHECK(v2.id == "vol-min");
    CHECK(v2.order == 0);
    CHECK(v2.summary.empty());
    CHECK(v2.theme.empty());
    CHECK(v2.key_events.empty());
    CHECK(v2.focus_characters.empty());
    CHECK(v2.active_plot_threads.empty());
    CHECK(v2.metadata.empty());

    PASS();
}

void test_outline_with_volumes() {
    TEST("Outline with volumes");

    Outline outline;
    outline.premise = "A young scholar discovers an impossible relic.";

    Volume vol;
    vol.id = "vol-001";
    vol.title = "第一卷";
    vol.order = 1;
    outline.volumes.push_back(vol);

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "Opening";
    ch.volume_id = "vol-001";
    outline.chapters.push_back(ch);

    json j = outline;
    Outline o2 = j.get<Outline>();

    CHECK(o2.volumes.size() == 1);
    CHECK(o2.volumes[0].title == "第一卷");
    CHECK(o2.chapters.size() == 1);
    CHECK(o2.chapters[0].volume_id == "vol-001");

    PASS();
}

void test_chapter_volume_id_serialization() {
    TEST("Chapter volume_id serialization");

    Chapter ch;
    ch.id = "ch-005";
    ch.title = "The Revelation";
    ch.volume_id = "vol-002";

    json j = ch;
    Chapter ch2 = j.get<Chapter>();

    CHECK(ch2.volume_id == "vol-002");

    // volume_id 缺失时应为空字符串
    json minimalChapter = {{"id", "ch-min"}, {"title", "Minimal"}};
    Chapter ch3 = minimalChapter.get<Chapter>();
    CHECK(ch3.volume_id.empty());

    PASS();
}

void test_character_development_roundtrip() {
    TEST("CharacterDevelopment roundtrip");

    CharacterDevelopment dev;
    dev.id = "dev-001";
    dev.chapter_id = "ch-005";
    dev.summary = "目睹导师背叛，性格从天真转向谨慎多疑。";
    dev.category = "personality";
    dev.affected_fields = {"personality", "goal"};
    dev.metadata["trigger"] = "mentor_betrayal";

    json j = dev;
    CharacterDevelopment d2 = j.get<CharacterDevelopment>();

    CHECK(d2.id == "dev-001");
    CHECK(d2.chapter_id == "ch-005");
    CHECK(d2.summary == "目睹导师背叛，性格从天真转向谨慎多疑。");
    CHECK(d2.category == "personality");
    CHECK(d2.affected_fields.size() == 2);
    CHECK(d2.affected_fields[1] == "goal");
    CHECK(d2.metadata.at("trigger") == "mentor_betrayal");

    PASS();
}

void test_character_development_defaults() {
    TEST("CharacterDevelopment defaults");

    CharacterDevelopment dev;
    dev.id = "dev-min";
    dev.chapter_id = "ch-001";
    dev.summary = "剪短了长发。";

    json j = dev;
    CharacterDevelopment d2 = j.get<CharacterDevelopment>();

    CHECK(d2.id == "dev-min");
    CHECK(d2.category == "other");
    CHECK(d2.affected_fields.empty());
    CHECK(d2.metadata.empty());

    PASS();
}

void test_character_with_development() {
    TEST("Character with development");

    Character c;
    c.id = "elena";
    c.name = "Elena";
    c.personality = "cynical, guarded";
    c.goal = "revenge";

    CharacterDevelopment dev1;
    dev1.id = "dev-001";
    dev1.chapter_id = "ch-003";
    dev1.summary = "剪短了长发，象征与过去决裂。";
    dev1.category = "appearance";
    dev1.affected_fields = {"appearance"};

    CharacterDevelopment dev2;
    dev2.id = "dev-002";
    dev2.chapter_id = "ch-005";
    dev2.summary = "目睹导师背叛，性格从天真转向谨慎。";
    dev2.category = "personality";
    dev2.affected_fields = {"personality", "goal"};

    c.development = {dev1, dev2};

    json j = c;
    Character c2 = j.get<Character>();

    CHECK(c2.personality == "cynical, guarded");
    CHECK(c2.development.size() == 2);
    CHECK(c2.development[0].category == "appearance");
    CHECK(c2.development[1].affected_fields[0] == "personality");

    // development 缺失时应为空
    json minimalChar = {{"id", "min"}, {"name", "Min"}};
    Character c3 = minimalChar.get<Character>();
    CHECK(c3.development.empty());

    PASS();
}

int main() {
    std::cout << "=== test_models ===\n\n";

    test_chapter_roundtrip();
    test_chapter_defaults();
    test_character_roundtrip();
    test_setting_roundtrip();
    test_outline_roundtrip();
    test_style_roundtrip();
    test_project_serialization();
    test_project_subobjects();
    test_legacy_metadata_capture();
    test_volume_roundtrip();
    test_volume_defaults();
    test_outline_with_volumes();
    test_chapter_volume_id_serialization();
    test_character_development_roundtrip();
    test_character_development_defaults();
    test_character_with_development();

    std::cout << "\nResult: " << tests_passed << '/' << tests_run << " passed\n";
    if (tests_passed == tests_run) {
        std::cout << "All tests passed!\n";
        return 0;
    }

    std::cout << (tests_run - tests_passed) << " tests failed\n";
    return 1;
}
