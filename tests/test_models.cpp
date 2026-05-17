// 测试 src/project/Models.h 中所有数据结构的 JSON 序列化往返

#include "project/Models.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <cassert>
#include <string>

using json = nlohmann::json;

static int tests_run = 0;
static int tests_passed = 0;

// 简单的测试宏，比 assert 更友好
#define TEST(name) \
    do { \
        tests_run++; \
        std::cout << "  TEST " << (name) << " ... "; \
    } while(0)

#define PASS() \
    do { \
        tests_passed++; \
        std::cout << "PASSED\n"; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAILED: " << (msg) << '\n'; \
    } while(0)

#define CHECK(cond) \
    do { \
        if (!(cond)) { FAIL(#cond); return; } \
    } while(0)

// ──────────────────────────────────────────────
// Chapter 序列化往返
// ──────────────────────────────────────────────

void test_chapter_roundtrip() {
    TEST("Chapter 序列化往返");

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "开端";
    ch.order = 1;
    ch.synopsis = "主角发现了一个古老的秘密";
    ch.scenes = {"图书馆", "地下密室", "发现水晶"};
    ch.pov_characters = {"elena"};
    ch.key_events = {"artifact_discovered"};
    ch.themes = {"discovery", "curiosity"};
    ch.status = "drafted";
    ch.word_count = 4500;
    ch.file_path = "chapters/001-intro.md";

    // 序列化 → 反序列化
    json j = ch;
    Chapter ch2 = j.get<Chapter>();

    CHECK(ch2.id == "ch-001");
    CHECK(ch2.title == "开端");
    CHECK(ch2.order == 1);
    CHECK(ch2.synopsis == "主角发现了一个古老的秘密");
    CHECK(ch2.scenes.size() == 3);
    CHECK(ch2.scenes[2] == "发现水晶");
    CHECK(ch2.pov_characters.size() == 1);
    CHECK(ch2.key_events[0] == "artifact_discovered");
    CHECK(ch2.themes[1] == "curiosity");
    CHECK(ch2.status == "drafted");
    CHECK(ch2.word_count == 4500);
    CHECK(ch2.file_path == "chapters/001-intro.md");

    PASS();
}

// ──────────────────────────────────────────────
// Chapter 默认值 — 空字段
// ──────────────────────────────────────────────

void test_chapter_defaults() {
    TEST("Chapter 默认值");

    Chapter ch;
    ch.id = "ch-min";
    ch.title = "最小章节";

    json j = ch;
    Chapter ch2 = j.get<Chapter>();

    CHECK(ch2.id == "ch-min");
    CHECK(ch2.order == 0);              // 默认值
    CHECK(ch2.status == "outlined");    // 默认值
    CHECK(ch2.scenes.empty());
    CHECK(ch2.pov_characters.empty());
    CHECK(ch2.key_events.empty());
    CHECK(ch2.themes.empty());
    CHECK(ch2.word_count == 0);

    PASS();
}

// ──────────────────────────────────────────────
// Character 序列化往返（含 relationships map）
// ──────────────────────────────────────────────

void test_character_roundtrip() {
    TEST("Character 序列化往返（含 relationships）");

    Character c;
    c.id = "elena";
    c.name = "Elena Vasquez";
    c.role = "protagonist";
    c.age = "28";
    c.appearance = "高挑身材，深色头发，学者气质";
    c.personality = "好奇、果断、偶尔鲁莽";
    c.background = "Thorne 大学的古代历史初级教授";
    c.traits = {"intelligent", "brave", "impulsive"};
    c.relationships = {
        {"marcus", "mentor"},
        {"lyra", "rival"}
    };
    c.chapter_appearances = {"ch-001", "ch-002"};
    c.arc = "从书斋学者到改变世界的发现者";
    c.notes = "需要加强动机描写";

    json j = c;
    Character c2 = j.get<Character>();

    CHECK(c2.id == "elena");
    CHECK(c2.name == "Elena Vasquez");
    CHECK(c2.role == "protagonist");
    CHECK(c2.age == "28");
    CHECK(c2.appearance == "高挑身材，深色头发，学者气质");
    CHECK(c2.personality == "好奇、果断、偶尔鲁莽");
    CHECK(c2.background == "Thorne 大学的古代历史初级教授");
    CHECK(c2.traits.size() == 3);
    CHECK(c2.traits[1] == "brave");
    CHECK(c2.relationships.size() == 2);
    CHECK(c2.relationships["marcus"] == "mentor");
    CHECK(c2.relationships["lyra"] == "rival");
    CHECK(c2.chapter_appearances.size() == 2);
    CHECK(c2.chapter_appearances[1] == "ch-002");
    CHECK(c2.arc == "从书斋学者到改变世界的发现者");
    CHECK(c2.notes == "需要加强动机描写");

    PASS();
}

// ──────────────────────────────────────────────
// Character 空 relationships
// ──────────────────────────────────────────────

void test_character_empty_maps() {
    TEST("Character 空 relationships/chapter_appearances");

    Character c;
    c.id = "ghost";
    c.name = "无名";
    // 不设置 relationships 和 chapter_appearances

    json j = c;
    Character c2 = j.get<Character>();

    CHECK(c2.id == "ghost");
    CHECK(c2.relationships.empty());
    CHECK(c2.chapter_appearances.empty());

    PASS();
}

// ──────────────────────────────────────────────
// Setting 序列化往返（含 attributes map）
// ──────────────────────────────────────────────

void test_setting_roundtrip() {
    TEST("Setting 序列化往返（含 attributes）");

    Setting s;
    s.id = "thorne-university";
    s.name = "Thorne 大学";
    s.category = "location";
    s.description = "一座古老的大学，建在更古老的废墟之上";
    s.attributes = {
        {"architecture", "哥特复兴式"},
        {"location", "沿海城市 Kingsport"},
        {"founded", "1642"}
    };
    s.notes = "图书馆包含一个密封区域";

    json j = s;
    Setting s2 = j.get<Setting>();

    CHECK(s2.id == "thorne-university");
    CHECK(s2.name == "Thorne 大学");
    CHECK(s2.category == "location");
    CHECK(s2.description == "一座古老的大学，建在更古老的废墟之上");
    CHECK(s2.attributes.size() == 3);
    CHECK(s2.attributes["architecture"] == "哥特复兴式");
    CHECK(s2.attributes["founded"] == "1642");
    CHECK(s2.notes == "图书馆包含一个密封区域");

    PASS();
}

// ──────────────────────────────────────────────
// Outline 序列化往返（含嵌套 Chapter 和 PlotThread）
// ──────────────────────────────────────────────

void test_outline_roundtrip() {
    TEST("Outline 序列化往返（含嵌套结构）");

    Outline outline;
    outline.premise = "一位年轻学者发现了一件上古神器，就此踏上改变世界的旅程";

    PlotThread pt1{"pt1", "主线", "追寻神器真相"};
    PlotThread pt2{"pt2", "爱情线", "与同伴的感情发展"};
    outline.plot_threads = {pt1, pt2};

    Chapter ch1;
    ch1.id = "ch-001";
    ch1.title = "发现";
    ch1.order = 1;
    ch1.synopsis = "Elena 在图书馆废墟中发现神器";

    Chapter ch2;
    ch2.id = "ch-002";
    ch2.title = "启程";
    ch2.order = 2;
    ch2.synopsis = "Elena 出发寻找神器起源";

    outline.chapters = {ch1, ch2};

    json j = outline;
    Outline o2 = j.get<Outline>();

    CHECK(o2.premise == outline.premise);
    CHECK(o2.plot_threads.size() == 2);
    CHECK(o2.plot_threads[0].id == "pt1");
    CHECK(o2.plot_threads[0].name == "主线");
    CHECK(o2.plot_threads[1].id == "pt2");
    CHECK(o2.plot_threads[1].name == "爱情线");
    CHECK(o2.chapters.size() == 2);
    CHECK(o2.chapters[0].id == "ch-001");
    CHECK(o2.chapters[0].title == "发现");
    CHECK(o2.chapters[1].id == "ch-002");
    CHECK(o2.chapters[1].synopsis == "Elena 出发寻找神器起源");

    PASS();
}

// ──────────────────────────────────────────────
// Style 序列化往返
// ──────────────────────────────────────────────

void test_style_roundtrip() {
    TEST("Style 序列化往返");

    Style s;
    s.tone = "atmospheric";
    s.pacing = "moderate";
    s.pov = "third_person_limited";
    s.tense = "past";
    s.prose_style = "literary";
    s.dialogue_style = "naturalistic";
    s.narrative_distance = "close";
    s.chapter_length_target = 4000;
    s.sentence_length = "varied";
    s.vocabulary = "rich";
    s.notes = "避免现代习语，多用感官细节";

    json j = s;
    Style s2 = j.get<Style>();

    CHECK(s2.tone == "atmospheric");
    CHECK(s2.pacing == "moderate");
    CHECK(s2.pov == "third_person_limited");
    CHECK(s2.tense == "past");
    CHECK(s2.prose_style == "literary");
    CHECK(s2.dialogue_style == "naturalistic");
    CHECK(s2.narrative_distance == "close");
    CHECK(s2.chapter_length_target == 4000);
    CHECK(s2.sentence_length == "varied");
    CHECK(s2.vocabulary == "rich");
    CHECK(s2.notes == "避免现代习语，多用感官细节");

    PASS();
}

// ──────────────────────────────────────────────
// Project 手动序列化 — path 不应出现在 JSON 中
// ──────────────────────────────────────────────

void test_project_serialization() {
    TEST("Project to_json/from_json（path 不序列化）");

    Project p;
    p.format_version = 1;
    p.title = "上古之影";
    p.author = "测试作者";
    p.description = "一个关于发现与冒险的奇幻故事";
    p.genre = {"fantasy", "epic"};
    p.target_word_count = 100000;
    p.current_word_count = 12500;
    p.status = "in_progress";
    p.pov = "third_person_limited";
    p.tense = "past";
    p.created = "2026-05-17T10:00:00Z";
    p.modified = "2026-05-17T14:30:00Z";
    p.path = "D:/novels/my-novel";  // 运行时字段，不应被序列化

    // 序列化
    json j = p;

    // path 不应该出现在 JSON 中
    CHECK(!j.contains("path"));

    // 所有其他字段应正确序列化
    CHECK(j["format_version"] == 1);
    CHECK(j["title"] == "上古之影");
    CHECK(j["author"] == "测试作者");
    CHECK(j["genre"].size() == 2);
    CHECK(j["genre"][0] == "fantasy");
    CHECK(j["genre"][1] == "epic");
    CHECK(j["target_word_count"] == 100000);
    CHECK(j["current_word_count"] == 12500);
    CHECK(j["status"] == "in_progress");
    CHECK(j["pov"] == "third_person_limited");
    CHECK(j["tense"] == "past");
    CHECK(j["created"] == "2026-05-17T10:00:00Z");
    CHECK(j["modified"] == "2026-05-17T14:30:00Z");

    // 反序列化
    Project p2 = j.get<Project>();
    CHECK(p2.title == "上古之影");
    CHECK(p2.author == "测试作者");
    CHECK(p2.genre[1] == "epic");
    CHECK(p2.target_word_count == 100000);

    PASS();
}

// ──────────────────────────────────────────────
// Project 子对象赋值后再检查
// ──────────────────────────────────────────────

void test_project_subobjects() {
    TEST("Project 子对象赋值");

    Project p;
    p.title = "测试小说";

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";
    p.outline.chapters.push_back(ch);

    Character c;
    c.id = "hero";
    c.name = "英雄";
    p.characters.push_back(c);

    Setting s;
    s.id = "castle";
    s.name = "城堡";
    p.settings.push_back(s);

    p.style.tone = "dark";

    CHECK(p.outline.chapters.size() == 1);
    CHECK(p.outline.chapters[0].id == "ch-001");
    CHECK(p.characters.size() == 1);
    CHECK(p.characters[0].name == "英雄");
    CHECK(p.settings.size() == 1);
    CHECK(p.settings[0].id == "castle");
    CHECK(p.style.tone == "dark");

    PASS();
}

// ──────────────────────────────────────────────
// 入口
// ──────────────────────────────────────────────

int main() {
    std::cout << "=== test_models ===\n\n";

    test_chapter_roundtrip();
    test_chapter_defaults();
    test_character_roundtrip();
    test_character_empty_maps();
    test_setting_roundtrip();
    test_outline_roundtrip();
    test_style_roundtrip();
    test_project_serialization();
    test_project_subobjects();

    std::cout << '\n';
    std::cout << "结果: " << tests_passed << '/' << tests_run << " 通过\n";

    if (tests_passed == tests_run) {
        std::cout << "All tests passed!\n";
        return 0;
    } else {
        std::cout << (tests_run - tests_passed) << " 个测试失败\n";
        return 1;
    }
}
