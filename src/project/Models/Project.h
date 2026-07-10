#pragma once

// Project — 小说项目顶层元数据。
//
// 是整个项目的"身份证"和"总控面板"，包含小说标题、作者、类型、
// 内容约束、进度追踪等全局信息。同时持有所有子对象（大纲、角色、
// 场景、世界规则、风格指南）的引用。
//
// 序列化说明：
// - 本结构体的字段序列化为 novel.json
// - 子对象（outline / characters / settings / world_rules / style）
//   分别存储在独立的 JSON 文件中，由 ProjectIO 管理
// - path 为运行期字段，不参与 to_json / from_json

#include "project/Models/ModelDetail.h"
#include "project/Models/Outline.h"
#include "project/Models/Character.h"
#include "project/Models/Setting.h"
#include "project/Models/WorldRule.h"
#include "project/Models/Style.h"

#include <map>
#include <string>
#include <vector>

struct Project {
    // ── 元数据 ──
    int format_version = 4;              //  项目格式版本号（用于向前兼容迁移）
    std::string title;                   //  小说标题
    std::string author;                  //  作者名
    std::string description;             //  项目描述/梗概
    std::string logline;                 //  一句话梗概（logline，用于 pitch 和 AI 快速理解）
    std::string theme;                   //  核心主题（如"牺牲与救赎"）
    std::string central_question;        //  中心问题（故事试图回答的核心问题）
    std::string target_audience;         //  目标读者群体
    std::vector<std::string> genre;      //  类型标签（如 ["奇幻", "冒险"]）
    std::vector<std::string> comps;      //  对标作品（comps，如 ["冰与火之歌", "时光之轮"]）

    // ── 内容约束 ──
    std::string content_rating;           //  内容分级（如 PG-13 / R）
    std::string world_rules_summary;      //  世界观规则简述（供 AI 快速了解世界设定）
    std::string ending_type;              //  结局类型（如 happy / bittersweet / open）
    std::vector<std::string> must_have_elements;     //  必备元素列表（AI 生成时必须包含）
    std::vector<std::string> must_avoid_elements;    //  禁忌元素列表（AI 生成时必须避免）
    std::vector<std::string> narrative_promises;     //  叙事承诺（对读者许下的"故事会包含 X"的承诺）

    // ── 进度 ──
    int target_word_count = 0;           //  目标总字数
    int current_word_count = 0;          //  当前已写字数
    std::string status = "planning";     //  项目状态：planning / writing / revising / done
    bool allow_auto_overwrite = false;   //  D1.2: 允许 Agent 自动覆写已有章节（false=覆写前需确认）

    // ── 时间戳 ──
    std::string created;                 //  创建时间（ISO 8601 字符串）
    std::string modified;                //  最后修改时间（ISO 8601 字符串）

    // ── 扩展与控制 ──
    std::map<std::string, nlohmann::json> metadata;  //  扩展元数据字典。存储 to_json/from_json 中的未知字段，
                                                     //  实现向前兼容：新增字段不会导致旧数据丢失

    // ── 运行期字段（不参与序列化）──
    std::string path;                    //  项目根目录的磁盘路径（运行期设置）

    // ── Issue 5: 增量保存脏标记 ──
    // 脏数据位图 — 标记哪些子实体自上次 save 后发生了变化。
    // save() 仅写入置位的文件，避免每次修改都触发全量 6 文件写入。
    enum DirtyBit : uint32_t {
        DIRTY_NOVEL       = 1 << 0,  //  novel.json（标题/状态/元数据）
        DIRTY_OUTLINE     = 1 << 1,  //  outline.json（大纲/章节/卷/剧情线）
        DIRTY_CHARACTERS  = 1 << 2,  //  characters.json
        DIRTY_SETTINGS    = 1 << 3,  //  settings.json
        DIRTY_WORLD_RULES = 1 << 4,  //  world_rules.json
        DIRTY_STYLE       = 1 << 5,  //  style.json
        DIRTY_ALL         = 0x3F,    //  全部文件（向后兼容：首次保存/手动保存）
    };
    // 标记指定子实体为脏（下次 save 时写入对应文件）。
    // 接受 uint32_t 以支持位运算 OR 组合（如 DIRTY_OUTLINE | DIRTY_CHARACTERS）。
    void markDirty(uint32_t bit) { dirty_flags |= bit; }
    // 清除所有脏标记（save 成功后调用）。
    void markClean() { dirty_flags = 0; }
    // 检查指定位是否为脏。
    bool isDirty(DirtyBit bit) const { return (dirty_flags & static_cast<uint32_t>(bit)) != 0; }

    uint32_t dirty_flags = DIRTY_ALL;  //  内部：请使用 markDirty/markClean/isDirty 方法操作

    // ── 子对象（分别独立 JSON 文件存储）──
    Outline outline;                     //  全局大纲（story beats / arcs 等）
    std::vector<Character> characters;   //  角色列表
    std::vector<Setting> settings;       //  场景/地点列表
    std::vector<WorldRule> world_rules;  //  世界观规则列表
    Style style;                         //  风格指南（文风、语调、句式偏好）
};

inline void to_json(nlohmann::json& j, const Project& p) {
    j = nlohmann::json{
        {"format_version", p.format_version}, {"title", p.title},
        {"author", p.author}, {"description", p.description},
        {"logline", p.logline}, {"theme", p.theme},
        {"central_question", p.central_question},
        {"target_audience", p.target_audience},
        {"genre", p.genre}, {"comps", p.comps},
        {"content_rating", p.content_rating},
        {"must_have_elements", p.must_have_elements},
        {"must_avoid_elements", p.must_avoid_elements},
        {"narrative_promises", p.narrative_promises},
        {"world_rules_summary", p.world_rules_summary},
        {"ending_type", p.ending_type},
        {"target_word_count", p.target_word_count},
        {"current_word_count", p.current_word_count},
        {"status", p.status}, {"allow_auto_overwrite", p.allow_auto_overwrite},
        {"created", p.created}, {"modified", p.modified},
        {"metadata", p.metadata}
    };
}

inline void from_json(const nlohmann::json& j, Project& p) {
    using namespace project::model_detail;
    static const std::set<std::string> kKnownKeys = {
        "format_version", "title", "author", "description", "logline",
        "theme", "central_question", "target_audience", "genre", "comps",
        "content_rating", "must_have_elements", "must_avoid_elements",
        "narrative_promises", "world_rules_summary", "ending_type",
        "target_word_count", "current_word_count", "status", "allow_auto_overwrite",
        "created", "modified", "metadata"
    };
    p.format_version = utils::json::getOrDefault(j, "format_version", 1);
    p.title = utils::json::getOrDefault(j, "title", std::string{});
    p.author = utils::json::getOrDefault(j, "author", std::string{});
    p.description = utils::json::getOrDefault(j, "description", std::string{});
    p.logline = utils::json::getOrDefault(j, "logline", std::string{});
    p.theme = utils::json::getOrDefault(j, "theme", std::string{});
    p.central_question = utils::json::getOrDefault(j, "central_question", std::string{});
    p.target_audience = utils::json::getOrDefault(j, "target_audience", std::string{});
    p.genre = utils::json::getOrDefault(j, "genre", std::vector<std::string>{});
    p.comps = utils::json::getOrDefault(j, "comps", std::vector<std::string>{});
    p.content_rating = utils::json::getOrDefault(j, "content_rating", std::string{});
    p.must_have_elements = utils::json::getOrDefault(j, "must_have_elements", std::vector<std::string>{});
    p.must_avoid_elements = utils::json::getOrDefault(j, "must_avoid_elements", std::vector<std::string>{});
    p.narrative_promises = utils::json::getOrDefault(j, "narrative_promises", std::vector<std::string>{});
    p.world_rules_summary = utils::json::getOrDefault(j, "world_rules_summary", std::string{});
    p.ending_type = utils::json::getOrDefault(j, "ending_type", std::string{});
    p.target_word_count = utils::json::getOrDefault(j, "target_word_count", 0);
    p.current_word_count = utils::json::getOrDefault(j, "current_word_count", 0);
    p.status = utils::json::getOrDefault(j, "status", std::string{"planning"});
    p.allow_auto_overwrite = utils::json::getOrDefault(j, "allow_auto_overwrite", false);
    p.created = utils::json::getOrDefault(j, "created", std::string{});
    p.modified = utils::json::getOrDefault(j, "modified", std::string{});
    p.metadata = getMetadataWithUnknownKeys(j, kKnownKeys);
}
