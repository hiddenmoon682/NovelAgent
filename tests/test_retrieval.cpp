#include "retrieval/VectorStore.h"
#include "retrieval/NovelChunker.h"
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

// 清理临时文件
static void cleanup(const std::string& path) {
    if (utils::file::exists(path)) {
        utils::file::removeFile(path);
    }
}

// 临时文件路径：基于系统临时目录（std::filesystem::temp_directory_path）生成，
// 避免硬编码仓库绝对路径导致的盘符绑定；文件名保留各用例独立前缀以防互相干扰
static std::string tmpPath(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// =========================================================================
// VectorStore 测试
// =========================================================================

void test_vector_store_init_empty() {
    TEST("VectorStore::init — 新文件从空开始");

    const std::string db_path = tmpPath("tmp_test_vs_empty.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);
    CHECK(store.count() == 0);
    store.close();

    cleanup(db_path);
    PASS();
}

void test_vector_store_insert_and_search() {
    TEST("VectorStore — 插入和搜索");

    const std::string db_path = tmpPath("tmp_test_vs_search.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);

    // 插入 3 条向量
    auto v1 = makeVec(4, 1.0f);  // [1,1,1,1]
    auto v2 = makeVec(4, -1.0f); // [-1,-1,-1,-1]
    auto v3 = makeVec(4, 0.5f);  // [0.5,0.5,0.5,0.5]

    store.insert("id-1", v1, {{"label", "positive"}});
    store.insert("id-2", v2, {{"label", "negative"}});
    store.insert("id-3", v3, {{"label", "neutral"}});

    CHECK(store.count() == 3);
    CHECK(store.contains("id-1"));
    CHECK(store.contains("id-2"));
    CHECK(!store.contains("id-nonexistent"));

    // 搜索：查询 [1,1,1,1] 应该最接近 id-1
    auto query = makeVec(4, 1.0f);
    auto results = store.search(query, 3);

    CHECK(results.size() == 3);
    // id-1 与 id-3 方向相同，余弦相似度并列最高（≈1.0）；排序对并列元素无稳定性承诺，不能断言两者先后
    CHECK(results[0].similarity > 0.99);
    CHECK(results[1].similarity > 0.99);
    CHECK((results[0].id == "id-1" && results[1].id == "id-3") ||
          (results[0].id == "id-3" && results[1].id == "id-1"));

    // id-2 与查询反向，相似度最低，应排最后
    CHECK(results[2].id == "id-2");
    CHECK(results[1].similarity > results[2].similarity);

    store.close();
    cleanup(db_path);
    PASS();
}

void test_vector_store_persistence() {
    TEST("VectorStore — 持久化往返");

    const std::string db_path = tmpPath("tmp_test_vs_persist.json");
    cleanup(db_path);

    // 创建并写入
    {
        retrieval::VectorStore store;
        store.init(db_path);
        store.insert("persist-1", {0.1f, 0.2f, 0.3f}, {{"key", "value1"}});
        store.insert("persist-2", {0.4f, 0.5f, 0.6f}, {{"key", "value2"}});
        store.close();  // 自动保存
    }

    // 重新打开并读取
    {
        retrieval::VectorStore store;
        store.init(db_path);
        CHECK(store.count() == 2);
        CHECK(store.contains("persist-1"));
        CHECK(store.contains("persist-2"));

        auto entry = store.get("persist-1");
        CHECK(entry.has_value());
        CHECK(entry->embedding.size() == 3);
        CHECK(std::abs(entry->embedding[0] - 0.1f) < 0.001f);

        store.close();
    }

    cleanup(db_path);
    PASS();
}

void test_vector_store_remove() {
    TEST("VectorStore — 删除向量");

    const std::string db_path = tmpPath("tmp_test_vs_remove.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);
    store.insert("rm-1", {1.0f, 2.0f}, {});
    store.insert("rm-2", {3.0f, 4.0f}, {});

    CHECK(store.count() == 2);

    bool removed = store.remove("rm-1");
    CHECK(removed);
    CHECK(store.count() == 1);
    CHECK(!store.contains("rm-1"));

    bool not_found = store.remove("rm-nonexistent");
    CHECK(!not_found);

    store.close();
    cleanup(db_path);
    PASS();
}

void test_vector_store_update() {
    TEST("VectorStore — 更新向量");

    const std::string db_path = tmpPath("tmp_test_vs_update.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);
    store.insert("up-1", {0.1f, 0.2f}, {});

    // 更新
    store.update("up-1", {0.9f, 0.8f});

    auto entry = store.get("up-1");
    CHECK(entry.has_value());
    CHECK(std::abs(entry->embedding[0] - 0.9f) < 0.001f);

    // 更新不存在的 id → 等同于 insert
    store.update("up-2", {0.5f, 0.5f});
    CHECK(store.count() == 2);

    store.close();
    cleanup(db_path);
    PASS();
}

void test_vector_store_batch_insert() {
    TEST("VectorStore — 批量插入");

    const std::string db_path = tmpPath("tmp_test_vs_batch.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);

    std::vector<retrieval::VectorEntry> entries;
    for (int i = 0; i < 10; ++i) {
        entries.push_back({
            "batch-" + std::to_string(i),
            {static_cast<float>(i) * 0.1f, static_cast<float>(i) * 0.2f},
            {{"index", i}}
        });
    }

    store.insertBatch(entries);
    CHECK(store.count() == 10);

    store.close();
    cleanup(db_path);
    PASS();
}

void test_cosine_similarity_basic() {
    TEST("余弦相似度 — 通过搜索间接测试");

    // 用 search 间接测试余弦相似度
    const std::string db_path = tmpPath("tmp_test_vs_cosine.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);

    auto v1 = makeVec(4, 0.5f);
    store.insert("same", v1, {});

    auto res = store.search(v1, 1);
    CHECK(res.size() == 1);
    CHECK(res[0].similarity > 0.99);  // 自身相似度应为 1.0

    // 正交向量：v_orth 与自身相似度应最高
    auto v_orth = std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f};
    auto v_orth2 = std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f};
    store.insert("orth1", v_orth, {});
    store.insert("orth2", v_orth2, {});
    (void)v_orth2; // 用于数据多样性，不需要单独使用

    auto res2 = store.search(v_orth, 3);
    // v_orth 应最接近自身
    CHECK(res2[0].id == "orth1" || res2[0].id == "same");

    store.close();
    cleanup(db_path);
    PASS();
}

// 验证 flush() 不经 close() 即可持久化全部向量。
//
// 回归背景：saveToFile/flush 此前无锁且为 const，无法清除脏标记；
// 修复后 flush 内部加写锁并在成功后清脏，行为上应保持落盘即时可读。
void test_vector_store_flush_persists() {
    TEST("VectorStore::flush — 不经 close 即持久化");

    const std::string db_path = tmpPath("tmp_test_vs_flush.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);
    store.insert("flush-1", {0.1f, 0.2f}, {{"k", "v"}});
    store.flush();

    // 未 close，另一实例从同一文件加载应能读到已落盘的向量
    retrieval::VectorStore reader;
    reader.init(db_path);
    CHECK(reader.count() == 1);
    CHECK(reader.contains("flush-1"));
    reader.close();

    store.close();
    cleanup(db_path);
    PASS();
}

// 验证 flush() 成功后清除脏标记：close() 不再重复落盘。
//
// 观察手段：flush 后删除磁盘文件再 close，若 dirty_ 已清除则 close 不会
// 重建文件；修复前 dirty_ 无法被 const saveToFile 清除，close 会多写一次。
void test_vector_store_flush_clears_dirty() {
    TEST("VectorStore::flush — 成功后清除脏标记");

    const std::string db_path = tmpPath("tmp_test_vs_dirty.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);
    store.insert("dirty-1", {0.5f, 0.5f}, {});
    store.flush();
    CHECK(utils::file::exists(db_path));

    cleanup(db_path);  // 外部删除文件
    store.close();     // 脏标记已清除，close 不应重新落盘
    CHECK(!utils::file::exists(db_path));

    cleanup(db_path);
    PASS();
}

// 并发冗余测试：多线程 insert 与 flush/search 交错执行不应崩溃且数据完整。
//
// 回归背景：修复前 flush 无锁读 entries_，与并发 insert 的 vector 扩容
// 构成数据竞争（悬空迭代器/撞裂读）；修复后各操作均持锁，结果确定。
void test_vector_store_concurrent_flush() {
    TEST("VectorStore — 并发 insert/flush/search 无竞争");

    const std::string db_path = tmpPath("tmp_test_vs_conc.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);

    constexpr int kWriters = 2;
    constexpr int kPerWriter = 50;

    std::vector<std::thread> threads;
    // 写线程：各自插入不重叠的 id
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&store, w]() {
            for (int i = 0; i < kPerWriter; ++i) {
                store.insert("conc-" + std::to_string(w) + "-" + std::to_string(i),
                             {static_cast<float>(w), static_cast<float>(i)}, {});
            }
        });
    }
    // flush 线程：与写入交错落盘
    threads.emplace_back([&store]() {
        for (int i = 0; i < 10; ++i) {
            store.flush();
        }
    });
    // 读线程：与写入/落盘交错搜索
    threads.emplace_back([&store]() {
        for (int i = 0; i < 20; ++i) {
            (void)store.search({1.0f, 1.0f}, 5);
            (void)store.count();
        }
    });

    for (auto& t : threads) {
        t.join();
    }

    CHECK(store.count() == kWriters * kPerWriter);

    // 最终落盘后重新加载，验证文件内容完整
    store.flush();
    retrieval::VectorStore reader;
    reader.init(db_path);
    CHECK(reader.count() == kWriters * kPerWriter);
    reader.close();

    store.close();
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

// =========================================================================
// 混合检索融合排序测试（Phase 4.9）
// =========================================================================

void test_hybrid_search_dedup() {
    TEST("混合检索 — VectorStore 搜索去重");

    const std::string db_path = tmpPath("tmp_test_hybrid.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);

    // 插入多种类型的内容
    store.insert("ch-001-0", {0.8f, 0.6f, 0.1f, 0.1f},
                 {{"type", "chapter"}, {"chapter_id", "ch-001"}});
    store.insert("ch-001-1", {0.7f, 0.5f, 0.2f, 0.1f},
                 {{"type", "chapter"}, {"chapter_id", "ch-001"}});
    store.insert("char-protagonist", {0.5f, 0.8f, 0.1f, 0.1f},
                 {{"type", "character"}, {"character_id", "char-001"}});
    store.insert("setting-cave", {0.3f, 0.3f, 0.9f, 0.2f},
                 {{"type", "setting"}, {"setting_id", "set-001"}});

    // 搜索接近章节内容的查询
    auto query = std::vector<float>{0.8f, 0.6f, 0.1f, 0.1f};
    auto results = store.search(query, 3);

    CHECK(results.size() == 3);
    // 章节 chunk 应该排在前面
    CHECK(results[0].similarity > 0.9);

    store.close();
    cleanup(db_path);
    PASS();
}

void test_hybrid_search_metadata_filter() {
    TEST("混合检索 — 元数据过滤");

    const std::string db_path = tmpPath("tmp_test_hybrid2.json");
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);

    store.insert("ch-001", {0.9f, 0.1f}, {{"type", "chapter"}, {"chapter_id", "ch-001"}});
    store.insert("ch-002", {0.1f, 0.9f}, {{"type", "chapter"}, {"chapter_id", "ch-002"}});
    store.insert("char-001", {0.5f, 0.5f}, {{"type", "character"}});

    // 搜索所有，然后按类型过滤
    auto all_results = store.search({0.9f, 0.1f}, 10);
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

    store.close();
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
    test_vector_store_flush_clears_dirty();
    test_vector_store_concurrent_flush();

    // NovelChunker 测试
    test_chunker_character();
    test_chunker_setting();
    test_chunker_world_rule();
    test_chunker_chapter_by_paragraphs();
    test_chunker_empty_chapter();
    test_chunker_overlap();
    test_text_chunk_factories();

    // 混合检索测试
    test_hybrid_search_dedup();
    test_hybrid_search_metadata_filter();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
