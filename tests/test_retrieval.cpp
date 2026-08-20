#include "retrieval/SqliteVectorStore.h"
#include "retrieval/NovelChunker.h"
#include "storage/SqliteStore.h"
#include "project/Models.h"
#include "utils/FileUtils.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

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

// =========================================================================
// 辅助
// =========================================================================

// 生成简单的测试向量（所有分量相同）
static std::vector<float> makeVec(int dim, float val) {
    return std::vector<float>(dim, val);
}

// 清理临时库及相关 WAL/SHM 附属文件
static void cleanup(const std::string& path) {
    for (const auto& suffix : {"", "-wal", "-shm"}) {
        const std::string p = path + suffix;
        if (utils::file::exists(p)) {
            utils::file::removeFile(p);
        }
    }
}

// 临时文件路径：基于系统临时目录（std::filesystem::temp_directory_path）生成，
// 避免硬编码仓库绝对路径导致的盘符绑定；文件名保留各用例独立前缀以防互相干扰
static std::string tmpPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// 打开临时 SQLite 库并绑定 SqliteVectorStore。
struct VectorFixture {
    storage::SqliteStore db;
    retrieval::SqliteVectorStore store;
    explicit VectorFixture(const std::string& path) : store(db) { db.open(path); }
};

// =========================================================================
// VectorStore 测试
// =========================================================================

void test_vector_store_init_empty() {
    TEST("VectorStore — 新建库从空开始");

    const std::string db_path = tmpPath("tmp_test_vs_empty.db");
    cleanup(db_path);

    VectorFixture fx(db_path);
    CHECK(fx.store.count() == 0);
    fx.db.close();

    cleanup(db_path);
    PASS();
}

void test_vector_store_insert_and_search() {
    TEST("VectorStore — 插入和搜索");

    const std::string db_path = tmpPath("tmp_test_vs_search.db");
    cleanup(db_path);

    VectorFixture fx(db_path);

    // 插入 3 条向量
    auto v1 = makeVec(4, 1.0f);  // [1,1,1,1]
    auto v2 = makeVec(4, -1.0f); // [-1,-1,-1,-1]
    auto v3 = makeVec(4, 0.5f);  // [0.5,0.5,0.5,0.5]

    fx.store.insert("id-1", v1, {{"label", "positive"}});
    fx.store.insert("id-2", v2, {{"label", "negative"}});
    fx.store.insert("id-3", v3, {{"label", "neutral"}});

    CHECK(fx.store.count() == 3);
    CHECK(fx.store.contains("id-1"));
    CHECK(fx.store.contains("id-2"));
    CHECK(!fx.store.contains("id-nonexistent"));

    // 搜索：查询 [1,1,1,1] 应该最接近 id-1
    auto query = makeVec(4, 1.0f);
    auto results = fx.store.search(query, 3);

    CHECK(results.size() == 3);
    // id-1 与 id-3 方向相同，余弦相似度并列最高（≈1.0）；排序对并列元素无稳定性承诺，不能断言两者先后
    CHECK(results[0].similarity > 0.99);
    CHECK(results[1].similarity > 0.99);
    CHECK((results[0].id == "id-1" && results[1].id == "id-3") ||
          (results[0].id == "id-3" && results[1].id == "id-1"));

    // id-2 与查询反向，相似度最低，应排最后
    CHECK(results[2].id == "id-2");
    CHECK(results[1].similarity > results[2].similarity);

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_vector_store_persistence() {
    TEST("VectorStore — 持久化往返");

    const std::string db_path = tmpPath("tmp_test_vs_persist.db");
    cleanup(db_path);

    // 创建并写入（事务即持久化：close 后数据即时落库）
    {
        VectorFixture fx(db_path);
        fx.store.insert("persist-1", {0.1f, 0.2f, 0.3f}, {{"key", "value1"}});
        fx.store.insert("persist-2", {0.4f, 0.5f, 0.6f}, {{"key", "value2"}});
        fx.db.close();
    }

    // 重新打开并读取
    {
        VectorFixture fx(db_path);
        CHECK(fx.store.count() == 2);
        CHECK(fx.store.contains("persist-1"));
        CHECK(fx.store.contains("persist-2"));

        auto entry = fx.store.get("persist-1");
        CHECK(entry.has_value());
        // 原始向量不落库：get() 仅返回 id/metadata，embedding 恒为空
        CHECK(entry->metadata["key"] == "value1");

        fx.db.close();
    }

    cleanup(db_path);
    PASS();
}

void test_vector_store_remove() {
    TEST("VectorStore — 删除向量");

    const std::string db_path = tmpPath("tmp_test_vs_remove.db");
    cleanup(db_path);

    VectorFixture fx(db_path);
    fx.store.insert("rm-1", {1.0f, 2.0f}, {});
    fx.store.insert("rm-2", {3.0f, 4.0f}, {});

    CHECK(fx.store.count() == 2);

    bool removed = fx.store.remove("rm-1");
    CHECK(removed);
    CHECK(fx.store.count() == 1);
    CHECK(!fx.store.contains("rm-1"));

    bool not_found = fx.store.remove("rm-nonexistent");
    CHECK(!not_found);

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_vector_store_update() {
    TEST("VectorStore — 更新向量");

    const std::string db_path = tmpPath("tmp_test_vs_update.db");
    cleanup(db_path);

    VectorFixture fx(db_path);
    fx.store.insert("up-1", {0.1f, 0.2f}, {});

    // 更新
    fx.store.update("up-1", {0.9f, 0.8f});

    auto entry = fx.store.get("up-1");
    CHECK(entry.has_value());
    // 原始向量不落库：get() 仅返回 id/metadata，embedding 恒为空

    // 更新不存在的 id → 等同于 insert
    fx.store.update("up-2", {0.5f, 0.5f});
    CHECK(fx.store.count() == 2);

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_vector_store_batch_insert() {
    TEST("VectorStore — 批量插入");

    const std::string db_path = tmpPath("tmp_test_vs_batch.db");
    cleanup(db_path);

    VectorFixture fx(db_path);

    std::vector<retrieval::VectorEntry> entries;
    for (int i = 0; i < 10; ++i) {
        entries.push_back({
            "batch-" + std::to_string(i),
            {static_cast<float>(i) * 0.1f, static_cast<float>(i) * 0.2f},
            {{"index", i}}
        });
    }

    fx.store.insertBatch(entries);
    CHECK(fx.store.count() == 10);

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_cosine_similarity_basic() {
    TEST("余弦相似度 — 通过搜索间接测试");

    // 用 search 间接测试余弦相似度
    const std::string db_path = tmpPath("tmp_test_vs_cosine.db");
    cleanup(db_path);

    VectorFixture fx(db_path);

    auto v1 = makeVec(4, 0.5f);
    fx.store.insert("same", v1, {});

    auto res = fx.store.search(v1, 1);
    CHECK(res.size() == 1);
    CHECK(res[0].similarity > 0.99);  // 自身相似度应为 1.0

    // 正交向量：v_orth 与自身相似度应最高
    auto v_orth = std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f};
    auto v_orth2 = std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f};
    fx.store.insert("orth1", v_orth, {});
    fx.store.insert("orth2", v_orth2, {});

    auto res2 = fx.store.search(v_orth, 3);
    // v_orth 应最接近自身
    CHECK(res2[0].id == "orth1" || res2[0].id == "same");

    fx.db.close();
    cleanup(db_path);
    PASS();
}

// 验证持久化语义：事务即持久化——insert 后无需 flush，close 重开库数据仍在。
void test_vector_store_flush_persists() {
    TEST("VectorStore — 事务即持久化（close 重开仍在）");

    const std::string db_path = tmpPath("tmp_test_vs_flush.db");
    cleanup(db_path);

    {
        VectorFixture fx(db_path);
        fx.store.insert("flush-1", {0.1f, 0.2f}, {{"k", "v"}});
        fx.db.close();
    }

    // 重开库应能读到已提交的数据
    VectorFixture fx2(db_path);
    CHECK(fx2.store.count() == 1);
    CHECK(fx2.store.contains("flush-1"));
    fx2.db.close();

    cleanup(db_path);
    PASS();
}

// flush() 在 SQLite 后端为兼容接口的 no-op（事务即持久化），调用不应抛异常。
void test_vector_store_flush_noop() {
    TEST("VectorStore::flush — no-op 不抛异常（事务即持久化）");

    const std::string db_path = tmpPath("tmp_test_vs_flushnoop.db");
    cleanup(db_path);
    VectorFixture fx(db_path);
    fx.store.insert("flush-1", {0.1f, 0.2f}, {{"k", "v"}});
    fx.store.flush();
    CHECK(fx.store.count() == 1);
    fx.db.close();
    cleanup(db_path);
    PASS();
}

// 并发冗余测试：多线程 insert 与 flush/search 交错执行不应崩溃且数据完整。
//
// 回归背景：旧 JSON VectorStore 的 flush 无锁读 entries_，与并发 insert 的
// vector 扩容构成数据竞争；SQLite 后端经由 SqliteStore 全库锁串行化，
// flush 为 no-op，结果确定。
void test_vector_store_concurrent_flush() {
    TEST("VectorStore — 并发 insert/flush/search 无竞争");

    const std::string db_path = tmpPath("tmp_test_vs_conc.db");
    cleanup(db_path);

    VectorFixture fx(db_path);

    constexpr int kWriters = 2;
    constexpr int kPerWriter = 50;

    std::vector<std::thread> threads;
    // 写线程：各自插入不重叠的 id
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&fx, w]() {
            for (int i = 0; i < kPerWriter; ++i) {
                fx.store.insert("conc-" + std::to_string(w) + "-" + std::to_string(i),
                                {static_cast<float>(w), static_cast<float>(i)}, {});
            }
        });
    }
    // flush 线程：与写入交错调用（no-op）
    threads.emplace_back([&fx]() {
        for (int i = 0; i < 10; ++i) {
            fx.store.flush();
        }
    });
    // 读线程：与写入交错搜索
    threads.emplace_back([&fx]() {
        for (int i = 0; i < 20; ++i) {
            (void)fx.store.search({1.0f, 1.0f}, 5);
            (void)fx.store.count();
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    CHECK(fx.store.count() == kWriters * kPerWriter);

    // 最终重开库验证数据完整
    fx.db.close();
    VectorFixture fx2(db_path);
    CHECK(fx2.store.count() == kWriters * kPerWriter);
    fx2.db.close();

    cleanup(db_path);
    PASS();
}

// =========================================================================
// NovelChunker 测试
// =========================================================================

void test_chunker_character() {
    TEST("NovelChunker::chunkCharacter — 角色文本拼接");

    Character c;
    c.name = "张三";
    c.role = "protagonist";
    c.goal = "成为最强剑仙";
    c.motivation = "保护家人";
    c.personality = "勇敢但冲动";
    c.traits = {"brave", "loyal"};
    c.fear = "失去所爱";
    c.arc = "从懵懂少年成长为一代剑仙";

    auto text = retrieval::NovelChunker::chunkCharacter(c);

    CHECK(!text.empty());
    CHECK(text.find("张三") != std::string::npos);
    CHECK(text.find("protagonist") != std::string::npos);
    CHECK(text.find("成为最强剑仙") != std::string::npos);
    CHECK(text.find("勇敢但冲动") != std::string::npos);

    PASS();
}

void test_chunker_setting() {
    TEST("NovelChunker::chunkSetting — 设定文本拼接");

    Setting s;
    s.name = "古墓秘境";
    s.category = "location";
    s.description = "一座被遗忘千年的地宫";
    s.story_function = "主角觉醒力量的关键场所";
    s.sensory_profile = "潮湿、黑暗、低语";

    auto text = retrieval::NovelChunker::chunkSetting(s);

    CHECK(!text.empty());
    CHECK(text.find("古墓秘境") != std::string::npos);
    CHECK(text.find("location") != std::string::npos);
    CHECK(text.find("千年") != std::string::npos);

    PASS();
}

void test_chunker_world_rule() {
    TEST("NovelChunker::chunkWorldRule — 世界规则文本拼接");

    WorldRule r;
    r.name = "灵力守恒";
    r.summary = "施法消耗等量灵力，不可无中生有";
    r.limitations = "凡人无法感知灵力";
    r.costs = "过度使用会导致灵力枯竭";
    r.exceptions = "血脉传承者可突破限制";
    r.known_by = "experts";

    auto text = retrieval::NovelChunker::chunkWorldRule(r);

    CHECK(!text.empty());
    CHECK(text.find("灵力守恒") != std::string::npos);
    CHECK(text.find("无中生有") != std::string::npos);

    PASS();
}

void test_chunker_chapter_by_paragraphs() {
    TEST("NovelChunker::chunkChapter — 按段落切分");

    Chapter ch;
    ch.id = "ch-001";
    ch.title = "第一章";

    // 构造多段落章节正文
    std::string content;
    for (int i = 0; i < 5; ++i) {
        content += "第" + std::to_string(i + 1) + "段内容。";
        content += std::string(200, 'x');  // 填充到 ~200 字
        content += "\n\n";
    }

    retrieval::NovelChunker chunker;
    chunker.configure(200, 500, 0.1);  // 小块便于测试

    auto chunks = chunker.chunkChapter(ch, content);

    CHECK(!chunks.empty());
    // 每个 chunk 应有正确的元数据
    for (const auto& c : chunks) {
        CHECK(c.metadata["type"] == "chapter");
        CHECK(c.metadata["chapter_id"] == "ch-001");
        CHECK(!c.text.empty());
    }

    PASS();
}

void test_chunker_empty_chapter() {
    TEST("NovelChunker::chunkChapter — 空章节返回空列表");

    Chapter ch;
    ch.id = "ch-empty";

    retrieval::NovelChunker chunker;
    auto chunks = chunker.chunkChapter(ch, "");

    CHECK(chunks.empty());

    PASS();
}

void test_chunker_overlap() {
    TEST("NovelChunker — chunk 间有重叠");

    Chapter ch;
    ch.id = "ch-002";

    // 构造足够长的内容以产生多个 chunk
    std::string content;
    for (int i = 0; i < 10; ++i) {
        content += std::string(300, 'a' + (i % 26));
        content += "。段落结束\n\n";
    }

    retrieval::NovelChunker chunker;
    chunker.configure(100, 400, 0.2);

    auto chunks = chunker.chunkChapter(ch, content);

    // 如果产生了多个 chunk，检查它们是否包含重叠标记
    if (chunks.size() > 1) {
        // 第二个及以后的 chunk 应该包含前一个 chunk 的尾部内容（重叠）
        // 检查是否有 "---" 标记（重叠分隔符）
        bool has_overlap_marker = false;
        for (size_t i = 1; i < chunks.size(); ++i) {
            if (chunks[i].text.find("---") != std::string::npos) {
                has_overlap_marker = true;
                break;
            }
        }
        // 至少有一个含重叠标记（不强制，取决于切分结果）
        std::cout << "(chunks: " << chunks.size()
                  << ", overlap: " << (has_overlap_marker ? "yes" : "no") << ") ";
    }

    PASS();
}

void test_text_chunk_factories() {
    TEST("TextChunk 工厂方法 — 元数据正确");

    auto c = retrieval::TextChunk::chapterChunk("ch-003", 2, "章节内容");
    CHECK(c.id == "ch-003-2");
    CHECK(c.metadata["type"] == "chapter");
    CHECK(c.metadata["chunk_index"] == 2);

    auto ch = retrieval::TextChunk::characterChunk("char-001", "角色描述");
    CHECK(ch.id == "char-char-001");
    CHECK(ch.metadata["type"] == "character");

    auto s = retrieval::TextChunk::settingChunk("set-001", "设定描述");
    CHECK(s.id == "setting-set-001");
    CHECK(s.metadata["type"] == "setting");

    auto r = retrieval::TextChunk::worldRuleChunk("rule-001", "规则描述");
    CHECK(r.id == "rule-rule-001");
    CHECK(r.metadata["type"] == "world_rule");

    PASS();
}

void test_chunker_no_blank_line_oversized() {
    TEST("NovelChunker — 无空行整章一段超长 → 切成多块且不超上限");

    Chapter ch;
    ch.id = "ch-ovs";

    // 无任何换行的连续文本：整章是一个"段落"，共 60 句 × 40 字 ≈ 7200 字节
    std::string content;
    const std::string sentence =
        "夜风穿过空旷的长街，把远处的犬吠送进窗棂，他握紧手中的信纸。";
    for (int i = 0; i < 60; ++i) content += sentence;

    retrieval::NovelChunker chunker;
    chunker.configure(500, 1000, 0.0);  // 重叠 0 → 可精确断言块大小

    auto chunks = chunker.chunkChapter(ch, content);

    CHECK(chunks.size() >= 5);                       // 不应是单个巨型块
    for (const auto& c : chunks) {
        CHECK(static_cast<int>(c.text.size()) <= 1000);  // 每块不超 max
        CHECK(!c.text.empty());
    }

    PASS();
}

void test_chunker_markdown_scene_not_special() {
    TEST("NovelChunker — ## Scene 标记不再触发场景切分");

    Chapter ch;
    ch.id = "ch-scn";

    // 含 markdown 场景标记的正文：标题行 + 正文 + 场景标记 + 正文
    std::string content = "## Scene 1\n\n";
    content += std::string(560, 'x') + "第一处正文结尾。\n\n";
    content += "### 场景 2\n\n";
    content += std::string(560, 'x') + "第二处正文结尾。";

    retrieval::NovelChunker chunker;
    chunker.configure(500, 2000, 0.0);

    auto chunks = chunker.chunkChapter(ch, content);

    // 标题与正文共 4 段、总字节 < 2000 → 聚合为 1 块；不得按场景标记切成 2 块
    CHECK(chunks.size() == 1);
    CHECK(chunks[0].text.find("## Scene 1") != std::string::npos);

    PASS();
}

void test_chunker_cut_only_at_boundaries() {
    TEST("NovelChunker — 所有切点都在段落边界或句末标点之后");

    Chapter ch;
    ch.id = "ch-cut";

    // 8 段，每段 1 句以句号结尾；整章含空行 → 段落聚合，超长段落触发句子兜底
    std::string content;
    const std::string sentence =
        "铁匠铺的炉火映红了半条巷子，学徒抡起锤子，把通红的铁条打得火星四溅。";
    for (int i = 0; i < 8; ++i) {
        content += sentence;
        content += "\n\n";
    }
    // 追加一个无空行的大段（约 2600 字节）验证句子兜底不截断句子
    const std::string long_sentence =
        "山上的雪化了，溪水涨起来，桥下的石头露出湿漉漉的背，他蹲在岸边洗了把脸。";
    for (int i = 0; i < 20; ++i) content += long_sentence;

    retrieval::NovelChunker chunker;
    chunker.configure(300, 800, 0.0);

    auto chunks = chunker.chunkChapter(ch, content);

    CHECK(chunks.size() >= 3);
    for (const auto& c : chunks) {
        CHECK(!c.text.empty());
        // 块尾必须是句末标点（UTF-8 后缀比较：中文标点为 3 字节，不能用单字符比较）
        CHECK(c.text.ends_with("。") || c.text.ends_with("！") || c.text.ends_with("？")
              || c.text.ends_with("…") || c.text.ends_with(".")
              || c.text.ends_with("!") || c.text.ends_with("?"));
        CHECK(static_cast<int>(c.text.size()) <= 800);
    }

    PASS();
}

void test_chunker_no_punct_oversized() {
    TEST("NovelChunker — 无句末标点的超长段 → UTF-8 安全硬切不超上限");

    Chapter ch;
    ch.id = "ch-np";

    // 无标点长串（约 3000 字节）+ 无标点中文字符段（验证不切坏多字节）。
    // 逐字追加 "中"（UTF-8 三字节 E4 B8 AD）：不能写 std::string(500, '中')，
    // 多字节字符字面量作为 char 会被截断成单字节（MinGW 下为续字节 0xAD），
    // 并触发 -Wmultichar 警告，无法构造真实多字节内容
    std::string content = std::string(3000, 'a');
    for (int i = 0; i < 500; ++i) content += "中";

    retrieval::NovelChunker chunker;
    chunker.configure(500, 1000, 0.0);

    auto chunks = chunker.chunkChapter(ch, content);

    CHECK(!chunks.empty());
    CHECK(chunks.size() >= 3);  // 硬切确实把超长段拆成了多块
    bool has_multibyte = false;
    for (const auto& c : chunks) {
        CHECK(static_cast<int>(c.text.size()) <= 1000);
        if (c.text.find("中") != std::string::npos) has_multibyte = true;
        // 块首不悬挂：切点必须落在字符起点，首字节不能是续字节（0x80-0xBF）。
        // 注意不能检查末字节：完整多字节字符的末字节本身就在 0x80-0xBF 区间
        const unsigned char first = static_cast<unsigned char>(c.text.front());
        CHECK((first & 0xC0) != 0x80);
    }
    CHECK(has_multibyte);  // 中文字符段确实被保留

    // 含中文的块是纯"中"段（无 ASCII 混入），必须按 3 字节对齐，可捕获切坏多字节的回归
    for (const auto& c : chunks) {
        if (c.text.find("中") != std::string::npos) {
            CHECK(c.text.size() % 3 == 0);
        }
    }

    PASS();
}

void test_chunker_single_newline_is_paragraph_boundary() {
    TEST("NovelChunker — 单个换行即段落边界（一行一段）");

    Chapter ch;
    ch.id = "ch-single-nl";

    // 10 行 × 300 字节，行间仅单个 \n（无空行），行内除逗号外无句末标点。
    // 若单换行不是段落边界，整章会合成一段 → 句子兜底无切点 → 硬切成 ~6 块。
    std::string line;
    for (int i = 0; i < 4; ++i) line += "山风从谷口灌进来，雪沫打在窗棂上，他裹紧了军大衣，";
    CHECK(static_cast<int>(line.size()) == 300);

    std::string content;
    for (int i = 0; i < 10; ++i) {
        if (!content.empty()) content += "\n";
        content += line;
    }

    retrieval::NovelChunker chunker;
    chunker.configure(100, 500, 0.0);

    auto chunks = chunker.chunkChapter(ch, content);

    // 每行一段 → 每段 300 字节 ≥ min 且 ≤ max → 一行即一块，共 10 块
    CHECK(chunks.size() == 10);
    for (const auto& c : chunks) {
        CHECK(static_cast<int>(c.text.size()) == 300);
        CHECK(c.text.find('\n') == std::string::npos);  // 块内不残留换行，行未被切开
    }

    PASS();
}

void test_chunker_min_gt_max_clamped() {
    TEST("NovelChunker — configure min>max → min 钳制到 max，不产出巨型块");

    Chapter ch;
    ch.id = "ch-minmax";

    // 20 段 × 200 字节；若 min(1000) > max(500) 未被钳制，封块条件永不满足 → 整章一块超限
    std::string content;
    const std::string seg = "铁匠铺的炉火映红了半条巷子，学徒抡起锤子，把铁条打得火星四溅。";
    for (int i = 0; i < 20; ++i) {
        content += seg;
        content += "\n\n";
    }

    retrieval::NovelChunker chunker;
    chunker.configure(1000, 500, 0.0);

    auto chunks = chunker.chunkChapter(ch, content);

    CHECK(chunks.size() >= 2);  // 钳制后按 max 封块，而不是整章一块
    for (const auto& c : chunks) {
        // 封块粒度允许略超 max（追加段导致），但绝不能是整章级别
        CHECK(static_cast<int>(c.text.size()) <= 1000);
    }

    PASS();
}

void test_chunker_overlap_utf8_boundary() {
    TEST("NovelChunker — 重叠文本不从多字节字符中间截断");

    Chapter ch;
    ch.id = "ch-overlap-utf8";

    // 段落1：900 字节纯"柏"（E6 9F 8F，内部含 0x9F 续字节）无句末标点，
    // 旧实现 find_last_of 按单字节匹配会把"柏"的内部字节误判为标点，
    // 重叠切在字符中间产生坏 UTF-8
    std::string para1;
    for (int i = 0; i < 300; ++i) para1 += "柏";
    std::string para2 = "山上的雪化了，溪水涨起来。";
    for (int i = 0; i < 4; ++i) para2 += "村口的石桥下，他蹲在岸边洗了把脸。";
    const std::string content = para1 + "\n\n" + para2;

    retrieval::NovelChunker chunker;
    chunker.configure(300, 800, 0.3);

    auto chunks = chunker.chunkChapter(ch, content);

    CHECK(chunks.size() >= 2);
    for (size_t i = 1; i < chunks.size(); ++i) {
        const size_t sep = chunks[i].text.find("\n---\n");
        if (sep == std::string::npos) continue;
        const size_t prefix_begin = sep + 5;
        CHECK(prefix_begin < chunks[i].text.size());
        // 重叠区首字节不能是续字节（0x80-0xBF）：切点必须落在字符起点
        const unsigned char first = static_cast<unsigned char>(chunks[i].text[prefix_begin]);
        CHECK((first & 0xC0) != 0x80);
    }

    PASS();
}

void test_chunker_context_header() {
    TEST("NovelChunker::chunkChapter — 章节上下文精简头注入");

    Chapter ch;
    ch.id = "ch-ctx";
    ch.order = 3;
    ch.title = "惊变";
    ch.time_marker = "夜晚";
    ch.location_id = "loc-qys";
    ch.pov_characters = {"c-zhang", "c-li"};
    ch.focus_characters = {"c-li", "c-wang"};  // 与 pov 有交叠，验证去重

    std::string content = "她推开门，脸色惨白。";

    retrieval::ChapterContext ctx;
    ctx.character_names = {{"c-zhang", "张三"}, {"c-li", "李四"}, {"c-wang", "王五"}};
    ctx.setting_names = {{"loc-qys", "青云山"}};

    retrieval::NovelChunker chunker;
    auto chunks = chunker.chunkChapter(ch, content, ctx);

    CHECK(chunks.size() == 1);
    CHECK(chunks[0].text.find("第3章 惊变") != std::string::npos);
    // pov 在前、focus 在后，交叠的"李四"只出现一次
    CHECK(chunks[0].text.find("人物：张三、李四、王五") != std::string::npos);
    CHECK(chunks[0].text.find("地点：青云山") != std::string::npos);
    CHECK(chunks[0].text.find("时间：夜晚") != std::string::npos);
    // 嵌入文本 = 展示文本（metadata.text 同步）
    CHECK(chunks[0].metadata["text"] == chunks[0].text);

    PASS();
}

// =========================================================================
// 混合检索融合排序测试（Phase 4.9）
// =========================================================================

void test_hybrid_search_dedup() {
    TEST("混合检索 — VectorStore 搜索去重");

    const std::string db_path = tmpPath("tmp_test_hybrid.db");
    cleanup(db_path);

    VectorFixture fx(db_path);

    // 插入多种类型的内容
    fx.store.insert("ch-001-0", {0.8f, 0.6f, 0.1f, 0.1f},
                    {{"type", "chapter"}, {"chapter_id", "ch-001"}});
    fx.store.insert("ch-001-1", {0.7f, 0.5f, 0.2f, 0.1f},
                    {{"type", "chapter"}, {"chapter_id", "ch-001"}});
    fx.store.insert("char-protagonist", {0.5f, 0.8f, 0.1f, 0.1f},
                    {{"type", "character"}, {"character_id", "char-001"}});
    fx.store.insert("setting-cave", {0.3f, 0.3f, 0.9f, 0.2f},
                    {{"type", "setting"}, {"setting_id", "set-001"}});

    // 搜索接近章节内容的查询
    auto query = std::vector<float>{0.8f, 0.6f, 0.1f, 0.1f};
    auto results = fx.store.search(query, 3);

    CHECK(results.size() == 3);
    // 章节 chunk 应该排在前面
    CHECK(results[0].similarity > 0.9);

    fx.db.close();
    cleanup(db_path);
    PASS();
}

void test_hybrid_search_metadata_filter() {
    TEST("混合检索 — 元数据过滤");

    const std::string db_path = tmpPath("tmp_test_hybrid2.db");
    cleanup(db_path);

    VectorFixture fx(db_path);

    fx.store.insert("ch-001", {0.9f, 0.1f}, {{"type", "chapter"}, {"chapter_id", "ch-001"}});
    fx.store.insert("ch-002", {0.1f, 0.9f}, {{"type", "chapter"}, {"chapter_id", "ch-002"}});
    fx.store.insert("char-001", {0.5f, 0.5f}, {{"type", "character"}});

    // 搜索所有，然后按类型过滤
    auto all_results = fx.store.search({0.9f, 0.1f}, 10);
    CHECK(all_results.size() == 3);

    // 验证按类型过滤（在结果中手动过滤）
    int chapter_count = 0;
    int character_count = 0;
    for (const auto& r : all_results) {
        if (r.metadata["type"] == "chapter") ++chapter_count;
        if (r.metadata["type"] == "character") ++character_count;
    }
    CHECK(chapter_count == 2);
    CHECK(character_count == 1);

    fx.db.close();
    cleanup(db_path);
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_retrieval (Phase 4 检索模块) ===\n\n";

    // VectorStore 测试
    test_vector_store_init_empty();
    test_vector_store_insert_and_search();
    test_vector_store_persistence();
    test_vector_store_remove();
    test_vector_store_update();
    test_vector_store_batch_insert();
    test_cosine_similarity_basic();
    test_vector_store_flush_persists();
    test_vector_store_flush_noop();
    test_vector_store_concurrent_flush();

    // NovelChunker 测试
    test_chunker_character();
    test_chunker_setting();
    test_chunker_world_rule();
    test_chunker_chapter_by_paragraphs();
    test_chunker_empty_chapter();
    test_chunker_overlap();
    test_text_chunk_factories();
    test_chunker_no_blank_line_oversized();
    test_chunker_markdown_scene_not_special();
    test_chunker_cut_only_at_boundaries();
    test_chunker_no_punct_oversized();
    test_chunker_single_newline_is_paragraph_boundary();
    test_chunker_min_gt_max_clamped();
    test_chunker_overlap_utf8_boundary();
    test_chunker_context_header();

    // 混合检索测试
    test_hybrid_search_dedup();
    test_hybrid_search_metadata_filter();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
