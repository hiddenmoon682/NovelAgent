// Skill 模块单元测试 — SkillLoader 解析 / SkillRegistry 渐进式披露 /
// 启用禁用 / use_skill / save_skill 工具。
// 测试在临时目录中构造 SKILL.md 后走真实文件发现路径，黑盒验证行为。

#include "agent/skill/SkillRegistry.h"
#include "agent/tools/SkillTools.h"
#include "project/Models/Project.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while(0)

namespace fs = std::filesystem;
using json = nlohmann::json;

// 临时技能目录 fixture：析构时自动清理。
struct SkillDirFixture {
    fs::path root;

    SkillDirFixture() {
        root = fs::temp_directory_path() / "novelagent_skill_test";
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~SkillDirFixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    void addSkill(const std::string& name, const std::string& frontmatter,
                  const std::string& body) {
        fs::path dir = root / "skills" / name;
        fs::create_directories(dir);
        std::ofstream out(dir / "SKILL.md", std::ios::binary);
        out << "---\n" << frontmatter << "---\n\n" << body;
    }

    // 写入原始字节（测 BOM/畸形文件等不经标准模板的场景）
    void addRawSkill(const std::string& name, const std::string& raw) {
        fs::path dir = root / "skills" / name;
        fs::create_directories(dir);
        std::ofstream out(dir / "SKILL.md", std::ios::binary);
        out << raw;
    }

    std::string skillsDir() const { return (root / "skills").string(); }
};

// =========================================================================
// 测试 1: frontmatter 解析（元数据完整、正文不预读）
// =========================================================================

void test_frontmatter_parse() {
    TEST("frontmatter 解析 — 元数据字段与懒加载");
    SkillDirFixture fx;
    fx.addSkill("demo-skill",
                "name: demo-skill\n"
                "emoji: \"X\"\n"
                "description: 一个演示技能\n",
                "## 正文\n内容 A\n");

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    CHECK(reg.hasSkill("demo-skill"));
    auto skills = reg.listSkills();
    CHECK(skills.size() == 1);
    CHECK(skills[0].description == "一个演示技能");
    CHECK(skills[0].emoji == "X");
    CHECK(!skills[0].always);
    CHECK(skills[0].enabled);
    CHECK(!skills[0].content_loaded); // 发现阶段不读正文
    PASS();
}

// =========================================================================
// 测试 2: 渐进式披露 — always 全文常驻，非 always 仅进目录
// =========================================================================

void test_progressive_context() {
    TEST("渐进式披露 — 常驻全文 vs 按需目录");
    SkillDirFixture fx;
    fx.addSkill("resident-skill",
                "name: resident-skill\ndescription: 常驻技能\nalways: true\n",
                "RESIDENT_BODY_MARKER\n");
    fx.addSkill("lazy-skill",
                "name: lazy-skill\ndescription: 按需技能\n",
                "LAZY_BODY_MARKER\n");

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    std::string ctx = reg.getSkillContext();
    // 常驻技能全文出现
    CHECK(ctx.find("RESIDENT_BODY_MARKER") != std::string::npos);
    // 按需技能只出现在目录中，正文不出现
    CHECK(ctx.find("LAZY_BODY_MARKER") == std::string::npos);
    CHECK(ctx.find("<available_skills>") != std::string::npos);
    CHECK(ctx.find("lazy-skill: 按需技能") != std::string::npos);
    // 目录段包含 use_skill 指引
    CHECK(ctx.find("use_skill") != std::string::npos);
    PASS();
}

// =========================================================================
// 测试 3: 启用/禁用 — 禁用技能对 LLM 完全隐藏
// =========================================================================

void test_enable_disable() {
    TEST("启用禁用 — 禁用技能隐藏且 loadContent 拒绝");
    SkillDirFixture fx;
    fx.addSkill("skill-a", "name: skill-a\ndescription: 技能A\n", "BODY_A\n");
    fx.addSkill("skill-b", "name: skill-b\ndescription: 技能B\n", "BODY_B\n");

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    CHECK(reg.setEnabled("skill-a", false));
    CHECK(!reg.setEnabled("no-such-skill", false));

    std::string ctx = reg.getSkillContext();
    CHECK(ctx.find("skill-a") == std::string::npos);
    CHECK(ctx.find("skill-b") != std::string::npos);

    // loadContent 对禁用技能返回 nullopt
    CHECK(!reg.loadContent("skill-a").has_value());
    CHECK(reg.loadContent("skill-b").has_value());
    CHECK(reg.loadContent("skill-b")->find("BODY_B") != std::string::npos);

    // 禁用集合在重新发现后保持
    reg.discoverAll();
    auto skills = reg.listSkills();
    for (const auto& s : skills) {
        if (s.name == "skill-a") CHECK(!s.enabled);
        if (s.name == "skill-b") CHECK(s.enabled);
    }

    // 重新启用后恢复可见
    CHECK(reg.setEnabled("skill-a", true));
    CHECK(reg.loadContent("skill-a").has_value());
    PASS();
}

// =========================================================================
// 测试 4: use_skill 工具
// =========================================================================

void test_use_skill_tool() {
    TEST("use_skill 工具 — 加载全文/拒绝禁用与未知技能");
    SkillDirFixture fx;
    fx.addSkill("target", "name: target\ndescription: 目标技能\n", "TOOL_BODY\n");

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    agent::ToolDependencies deps;
    deps.skill_registry = &reg;
    agent::UseSkillTool tool(deps);

    auto r = tool.execute({{"name", "target"}});
    CHECK(!r.contains("error"));
    CHECK(r["content"].get<std::string>().find("TOOL_BODY") != std::string::npos);

    CHECK(tool.execute({{"name", "unknown"}}).contains("error"));

    reg.setEnabled("target", false);
    CHECK(tool.execute({{"name", "target"}}).contains("error"));
    PASS();
}

// =========================================================================
// 测试 5: save_skill 工具 — 落盘 + 立即注册 + 名称校验
// =========================================================================

void test_save_skill_tool() {
    TEST("save_skill 工具 — 落盘注册与非法名称拒绝");
    SkillDirFixture fx;

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    auto project = std::make_shared<Project>();
    project->title = "测试项目";
    project->path = fx.root.string();

    agent::ToolDependencies deps;
    deps.project = project;
    deps.skill_registry = &reg;
    agent::SaveSkillTool tool(deps);

    auto ok = tool.execute({{"name", "new-skill"},
                            {"description", "新建的技能"},
                            {"content", "## 使用场景\nSAVED_BODY\n"}});
    CHECK(ok.value("ok", false));
    CHECK(fs::exists(fx.root / "skills" / "new-skill" / "SKILL.md"));

    // 保存后立即出现在注册表与目录中
    CHECK(reg.hasSkill("new-skill"));
    CHECK(reg.loadContent("new-skill")->find("SAVED_BODY") != std::string::npos);
    CHECK(reg.getSkillContext().find("new-skill") != std::string::npos);

    // 非法名称拒绝（大写 / 路径穿越 / 连续连字符）
    json bad = {{"description", "d"}, {"content", "c"}};
    bad["name"] = "BadName";
    CHECK(tool.execute(bad).contains("error"));
    bad["name"] = "../evil";
    CHECK(tool.execute(bad).contains("error"));
    bad["name"] = "a--b";
    CHECK(tool.execute(bad).contains("error"));
    PASS();
}

// =========================================================================
// 测试 6: 解析健壮性 — frontmatter 空行与 UTF-8 BOM 不得丢技能
// =========================================================================

void test_parse_robustness() {
    TEST("解析健壮性 — frontmatter 空行 / UTF-8 BOM");
    SkillDirFixture fx;

    // frontmatter 含空行与纯空白行（曾触发 substr(npos) 抛异常后整个技能被丢弃）
    fx.addRawSkill("blank-lines",
                   "---\nname: blank-lines\n\n   \ndescription: 含空行\n---\n\nBODY\n");
    // 首行带 UTF-8 BOM（Windows 记事本保存常见，曾导致 frontmatter 整体丢失）
    fx.addRawSkill("bom-skill",
                   "\xEF\xBB\xBF---\nname: bom-skill\ndescription: BOM 技能\n---\n\nBOM_BODY\n");

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    CHECK(reg.hasSkill("blank-lines"));
    CHECK(reg.hasSkill("bom-skill"));
    auto skills = reg.listSkills();
    for (const auto& s : skills) {
        if (s.name == "blank-lines") CHECK(s.description == "含空行");
        if (s.name == "bom-skill") CHECK(s.description == "BOM 技能");
    }
    // BOM 文件的正文加载同样正常
    CHECK(reg.loadContent("bom-skill")->find("BOM_BODY") != std::string::npos);
    PASS();
}

// =========================================================================
// 测试 7: save_skill frontmatter 注入防护 — description 换行不得提权
// =========================================================================

void test_save_skill_injection() {
    TEST("save_skill — description 换行注入不得提权为常驻");
    SkillDirFixture fx;

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    auto project = std::make_shared<Project>();
    project->title = "测试项目";
    project->path = fx.root.string();

    agent::ToolDependencies deps;
    deps.project = project;
    deps.skill_registry = &reg;
    agent::SaveSkillTool tool(deps);

    // 恶意 description：换行后接 always: true 试图注入元数据字段
    auto r = tool.execute({{"name", "inject-test"},
                           {"description", "正常描述\nalways: true"},
                           {"content", "BODY\n"},
                           {"emoji", "\"\nbins:"}});
    CHECK(r.value("ok", false));

    auto skills = reg.listSkills();
    bool found = false;
    for (const auto& s : skills) {
        if (s.name != "inject-test") continue;
        found = true;
        CHECK(!s.always);                                    // 未被提权为常驻
        CHECK(s.required_bins.empty());                      // 未注入 bins
        CHECK(s.description.find('\n') == std::string::npos); // 单行化
    }
    CHECK(found);
    PASS();
}

// =========================================================================
// 测试 8: 正文读取失败 — loadContent 报错且恢复后可重试
// =========================================================================

void test_content_read_failure() {
    TEST("loadContent — SKILL.md 被删返回 nullopt，恢复后可重读");
    SkillDirFixture fx;
    fx.addSkill("volatile-skill", "name: volatile-skill\ndescription: 易失技能\n",
                "VOLATILE_BODY\n");

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    // 发现后删掉文件：不得把空内容当成功返回
    fs::path md = fx.root / "skills" / "volatile-skill" / "SKILL.md";
    fs::remove(md);
    CHECK(!reg.loadContent("volatile-skill").has_value());

    // 文件恢复后重试成功（失败不得被永久缓存）
    {
        std::ofstream out(md, std::ios::binary);
        out << "---\nname: volatile-skill\n---\n\nRESTORED_BODY\n";
    }
    auto content = reg.loadContent("volatile-skill");
    CHECK(content.has_value());
    CHECK(content->find("RESTORED_BODY") != std::string::npos);
    PASS();
}

// =========================================================================
// 测试 9: setEnabled 对不存在技能不污染禁用集合
// =========================================================================

void test_set_enabled_no_pollution() {
    TEST("setEnabled — 不存在的技能名不入 disabledSkills");
    SkillDirFixture fx;
    fx.addSkill("real-skill", "name: real-skill\ndescription: 真技能\n", "BODY\n");

    skill::SkillRegistry reg;
    reg.addSearchPath(fx.skillsDir());
    reg.discoverAll();

    CHECK(!reg.setEnabled("ghost-skill", false));
    CHECK(reg.disabledSkills().empty());  // 失败的 toggle 不留脏数据

    CHECK(reg.setEnabled("real-skill", false));
    auto disabled = reg.disabledSkills();
    CHECK(disabled.size() == 1 && disabled[0] == "real-skill");
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_skill_registry ===\n\n";
    test_frontmatter_parse();
    test_progressive_context();
    test_enable_disable();
    test_use_skill_tool();
    test_save_skill_tool();
    test_parse_robustness();
    test_save_skill_injection();
    test_content_read_failure();
    test_set_enabled_no_pollution();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
