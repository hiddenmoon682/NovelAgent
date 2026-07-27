// test_index_manifest — 索引清单 / 长期记忆日志 / 增量索引服务测试。
//
// 覆盖 RAG 时效性三重保证：
//   1. 增量索引（内容哈希未变跳过重嵌入）
//   2. 孤儿清理（源删除后遗留向量随下次索引移除）
//   3. 模型指纹（嵌入模型变化时整库失效重建）

#include "agent/index/IndexManifest.h"
#include "agent/index/ProjectIndexService.h"
#include "agent/memory/LongTermMemoryStore.h"
#include "project/Models.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "retrieval/VectorStore.h"
#include "utils/FileUtils.h"

#include <iostream>
#include <string>
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

// 每次调用生成确定性向量的假嵌入生成器（可切换模型名以测指纹失效）。
class FakeEmbeddingGen : public retrieval::IEmbeddingGenerator {
public:
    std::string model = "fake-embed-v1";
    int calls = 0;   // 累计嵌入的文本条数（衡量是否真的跳过重嵌入）

    retrieval::EmbeddingVector generateEmbedding(const std::string& text) override {
        ++calls;
        return makeVec(text);
    }
    std::vector<retrieval::EmbeddingVector> generateEmbeddings(
        const std::vector<std::string>& texts) override {
        std::vector<retrieval::EmbeddingVector> out;
        out.reserve(texts.size());
        for (const auto& t : texts) { ++calls; out.push_back(makeVec(t)); }
        return out;
    }
    int dimension() const override { return 4; }
    std::string modelName() const override { return model; }

private:
    static retrieval::EmbeddingVector makeVec(const std::string& t) {
        float a = static_cast<float>(t.size() % 17) / 17.0f;
        return {a, 1.0f - a, 0.5f, 0.25f};
    }
};

// 建立临时项目目录（含 .novelagent 子目录），返回项目路径。
static std::string makeTempProjectDir(const std::string& name) {
    std::string dir = "D:/C++Code/C++NovelAgent/build/tmp_idx_" + name;
    utils::file::removeDir(dir);
    utils::file::createDirs(dir + "/.novelagent");
    return dir;
}

static Character makeCharacter(const std::string& id, const std::string& goal) {
    Character c;
    c.id = id;
    c.name = "角色" + id;
    c.role = "protagonist";
    c.goal = goal;
    c.personality = "坚毅";
    return c;
}

// =========================================================================
// IndexManifest 测试
// =========================================================================

void test_hash_content() {
    TEST("IndexManifest::hashContent — 确定性与区分度");

    auto h1 = agent::IndexManifest::hashContent("你好，世界");
    auto h2 = agent::IndexManifest::hashContent("你好，世界");
    auto h3 = agent::IndexManifest::hashContent("你好，世界!");

    CHECK(!h1.empty());
    CHECK(h1 == h2);
    CHECK(h1 != h3);
    CHECK(agent::IndexManifest::hashContent("") != h1);

    PASS();
}

void test_manifest_roundtrip() {
    TEST("IndexManifest — save/load 往返");

    const std::string dir = makeTempProjectDir("manifest_rt");
    const std::string path = dir + "/.novelagent/index_manifest.json";

    {
        agent::IndexManifest m;
        m.setModelFingerprint("model-x", 1024);
        agent::ManifestEntry e;
        e.content_hash = "abc123";
        e.chunk_ids = {"ch-1-0", "ch-1-1"};
        e.updated_at = 1721980800;
        m.upsert("chapter:ch-1", std::move(e));
        m.save(path);
    }

    {
        agent::IndexManifest m;
        m.load(path);
        CHECK(m.embeddingModel() == "model-x");
        CHECK(m.dimension() == 1024);
        CHECK(m.contains("chapter:ch-1"));
        const auto* e = m.find("chapter:ch-1");
        CHECK(e != nullptr);
        CHECK(e->content_hash == "abc123");
        CHECK(e->chunk_ids.size() == 2);
        CHECK(e->updated_at == 1721980800);
        CHECK(m.allChunkIds().size() == 2);
    }

    utils::file::removeDir(dir);
    PASS();
}

void test_manifest_load_missing_file() {
    TEST("IndexManifest::load — 文件不存在从空开始");

    agent::IndexManifest m;
    m.load("D:/C++Code/C++NovelAgent/build/nonexistent_manifest_xyz.json");
    CHECK(m.sources().empty());
    CHECK(m.embeddingModel().empty());

    PASS();
}

void test_manifest_fingerprint() {
    TEST("IndexManifest::fingerprintMatches — 指纹语义");

    agent::IndexManifest m;
    // 空清单（未设置指纹）视为兼容
    CHECK(m.fingerprintMatches("any-model", 4));

    m.setModelFingerprint("model-a", 1024);
    CHECK(m.fingerprintMatches("model-a", 1024));
    CHECK(!m.fingerprintMatches("model-b", 1024));   // 模型名变化
    CHECK(!m.fingerprintMatches("model-a", 512));    // 维度变化
    CHECK(m.fingerprintMatches("model-a", 0));       // 维度 0 = 未知，不比较

    PASS();
}

void test_manifest_erase_clear() {
    TEST("IndexManifest — erase/clear");

    agent::IndexManifest m;
    agent::ManifestEntry e;
    e.chunk_ids = {"a", "b"};
    m.upsert("k1", e);
    m.upsert("k2", e);

    CHECK(m.erase("k1"));
    CHECK(!m.erase("k1"));      // 重复删除
    CHECK(!m.contains("k1"));
    CHECK(m.contains("k2"));

    m.clear();
    CHECK(m.sources().empty());

    PASS();
}

// =========================================================================
// LongTermMemoryStore 测试
// =========================================================================

void test_ltm_append_and_roundtrip() {
    TEST("LongTermMemoryStore — append 持久化往返");

    const std::string dir = makeTempProjectDir("ltm_rt");
    const std::string path = dir + "/.novelagent/memories.json";

    std::string id1, id2;
    {
        agent::LongTermMemoryStore store;
        CHECK(!store.initialized());
        CHECK(store.append("未初始化写入应失败", "fact").empty());

        store.init(path);
        CHECK(store.initialized());
        id1 = store.append("主角的剑名为青霜", "fact");
        id2 = store.append("用户偏好短句", "preference");
        CHECK(!id1.empty());
        CHECK(!id2.empty());
        CHECK(id1 != id2);          // 同秒追加也不能撞 id
        CHECK(store.count() == 2);
    }

    {
        agent::LongTermMemoryStore store;
        store.init(path);
        CHECK(store.count() == 2);
        auto entries = store.entries();
        CHECK(entries[0].id == id1);
        CHECK(entries[0].text == "主角的剑名为青霜");
        CHECK(entries[0].kind == "fact");
        CHECK(entries[0].created_at > 0);
        CHECK(entries[1].kind == "preference");

        // 重启后继续追加，id 仍不冲突
        std::string id3 = store.append("第三条", "event");
        CHECK(id3 != id1 && id3 != id2);
    }

    utils::file::removeDir(dir);
    PASS();
}

void test_ltm_remove() {
    TEST("LongTermMemoryStore — remove 持久化");

    const std::string dir = makeTempProjectDir("ltm_rm");
    const std::string path = dir + "/.novelagent/memories.json";

    agent::LongTermMemoryStore store;
    store.init(path);
    std::string id = store.append("待删除", "fact");
    CHECK(store.remove(id));
    CHECK(!store.remove(id));       // 重复删除
    CHECK(store.count() == 0);

    agent::LongTermMemoryStore reopened;
    reopened.init(path);
    CHECK(reopened.count() == 0);   // 删除已落盘

    utils::file::removeDir(dir);
    PASS();
}

// =========================================================================
// ProjectIndexService 增量索引测试
// =========================================================================

void test_index_incremental_skip() {
    TEST("ProjectIndexService — 首次索引 + 未变更跳过");

    const std::string dir = makeTempProjectDir("inc_skip");
    auto project = std::make_shared<Project>();
    project->path = dir;
    project->title = "测试项目";
    project->characters.push_back(makeCharacter("c1", "成为剑仙"));
    project->characters.push_back(makeCharacter("c2", "守护家园"));

    retrieval::VectorStore vs;
    vs.init(dir + "/.novelagent/vectors.json");
    FakeEmbeddingGen eg;

    agent::ProjectIndexService svc(project, vs, eg);

    // 首次：两个源全部嵌入
    auto r1 = svc.indexAll();
    CHECK(r1.ok());
    CHECK(r1.updated_sources == 2);
    CHECK(r1.skipped_sources == 0);
    CHECK(r1.total_chunks == 2);
    CHECK(vs.contains("char-c1"));
    CHECK(vs.contains("char-c2"));
    const int calls_after_first = eg.calls;
    CHECK(calls_after_first == 2);

    // 再跑：内容未变，零嵌入调用
    auto r2 = svc.indexAll();
    CHECK(r2.ok());
    CHECK(r2.updated_sources == 0);
    CHECK(r2.skipped_sources == 2);
    CHECK(eg.calls == calls_after_first);

    // 修改一个角色：只重嵌入该源
    project->characters[0].goal = "转修丹道";
    auto r3 = svc.indexAll();
    CHECK(r3.ok());
    CHECK(r3.updated_sources == 1);
    CHECK(r3.skipped_sources == 1);
    CHECK(eg.calls == calls_after_first + 1);

    vs.close();
    utils::file::removeDir(dir);
    PASS();
}

void test_index_orphan_cleanup() {
    TEST("ProjectIndexService — 源删除后孤儿向量清理");

    const std::string dir = makeTempProjectDir("orphan");
    auto project = std::make_shared<Project>();
    project->path = dir;
    project->title = "测试项目";
    project->characters.push_back(makeCharacter("c1", "目标一"));
    project->characters.push_back(makeCharacter("c2", "目标二"));

    retrieval::VectorStore vs;
    vs.init(dir + "/.novelagent/vectors.json");
    FakeEmbeddingGen eg;
    agent::ProjectIndexService svc(project, vs, eg);

    CHECK(svc.indexAll().ok());
    CHECK(vs.count() == 2);

    // 删除 c2 后再索引：其向量应被清理
    project->characters.pop_back();
    auto r = svc.indexAll();
    CHECK(r.ok());
    CHECK(r.removed_sources == 1);
    CHECK(!vs.contains("char-c2"));
    CHECK(vs.contains("char-c1"));
    CHECK(vs.count() == 1);

    vs.close();
    utils::file::removeDir(dir);
    PASS();
}

void test_index_model_fingerprint_rebuild() {
    TEST("ProjectIndexService — 换嵌入模型触发整库重建");

    const std::string dir = makeTempProjectDir("fingerprint");
    auto project = std::make_shared<Project>();
    project->path = dir;
    project->title = "测试项目";
    project->characters.push_back(makeCharacter("c1", "目标一"));

    retrieval::VectorStore vs;
    vs.init(dir + "/.novelagent/vectors.json");
    FakeEmbeddingGen eg;
    agent::ProjectIndexService svc(project, vs, eg);

    CHECK(svc.indexAll().ok());
    const int calls_v1 = eg.calls;

    // 换模型：即使内容未变也必须整库重嵌入
    eg.model = "fake-embed-v2";
    auto r = svc.indexAll();
    CHECK(r.ok());
    CHECK(r.updated_sources == 1);
    CHECK(r.skipped_sources == 0);
    CHECK(eg.calls == calls_v1 + 1);
    CHECK(vs.contains("char-c1"));

    // 换回后再跑一次应重建（指纹已是 v2），随后稳定跳过
    auto r2 = svc.indexAll();
    CHECK(r2.skipped_sources == 1);

    vs.close();
    utils::file::removeDir(dir);
    PASS();
}

void test_index_force_rebuild() {
    TEST("ProjectIndexService — force 强制全量重建");

    const std::string dir = makeTempProjectDir("force");
    auto project = std::make_shared<Project>();
    project->path = dir;
    project->title = "测试项目";
    project->characters.push_back(makeCharacter("c1", "目标一"));

    retrieval::VectorStore vs;
    vs.init(dir + "/.novelagent/vectors.json");
    FakeEmbeddingGen eg;
    agent::ProjectIndexService svc(project, vs, eg);

    CHECK(svc.indexAll().ok());
    const int calls_first = eg.calls;

    auto r = svc.indexAll(nullptr, /*force=*/true);
    CHECK(r.ok());
    CHECK(r.updated_sources == 1);
    CHECK(eg.calls == calls_first + 1);

    vs.close();
    utils::file::removeDir(dir);
    PASS();
}

void test_index_memory_entries() {
    TEST("ProjectIndexService — 长期记忆条目纳入索引");

    const std::string dir = makeTempProjectDir("memidx");
    auto project = std::make_shared<Project>();
    project->path = dir;
    project->title = "测试项目";

    agent::LongTermMemoryStore ltm;
    ltm.init(dir + "/.novelagent/memories.json");
    std::string m1 = ltm.append("主角的剑名为青霜", "fact");
    std::string m2 = ltm.append("用户偏好短句", "preference");

    retrieval::VectorStore vs;
    vs.init(dir + "/.novelagent/vectors.json");
    FakeEmbeddingGen eg;
    agent::ProjectIndexService svc(project, vs, eg, &ltm);

    auto r = svc.indexAll();
    CHECK(r.ok());
    CHECK(r.memories == 2);
    CHECK(vs.contains("memory-" + m1));
    CHECK(vs.contains("memory-" + m2));

    // 元数据带 type=memory，检索侧可识别
    auto entry = vs.get("memory-" + m1);
    CHECK(entry.has_value());
    CHECK(entry->metadata["type"] == "memory");
    CHECK(entry->metadata["kind"] == "fact");

    // 删除一条记忆 → 下次索引清理其向量
    CHECK(ltm.remove(m2));
    auto r2 = svc.indexAll();
    CHECK(r2.ok());
    CHECK(r2.removed_sources == 1);
    CHECK(!vs.contains("memory-" + m2));
    CHECK(vs.contains("memory-" + m1));

    vs.close();
    utils::file::removeDir(dir);
    PASS();
}

void test_index_no_project() {
    TEST("ProjectIndexService — 未打开项目返回错误");

    retrieval::VectorStore vs;
    FakeEmbeddingGen eg;
    auto project = std::make_shared<Project>();   // path 为空
    agent::ProjectIndexService svc(project, vs, eg);

    auto r = svc.indexAll();
    CHECK(!r.ok());
    CHECK(!r.error.empty());

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_index_manifest (RAG 时效性 / 长期记忆) ===\n\n";

    // IndexManifest
    test_hash_content();
    test_manifest_roundtrip();
    test_manifest_load_missing_file();
    test_manifest_fingerprint();
    test_manifest_erase_clear();

    // LongTermMemoryStore
    test_ltm_append_and_roundtrip();
    test_ltm_remove();

    // ProjectIndexService 增量索引
    test_index_incremental_skip();
    test_index_orphan_cleanup();
    test_index_model_fingerprint_rebuild();
    test_index_force_rebuild();
    test_index_memory_entries();
    test_index_no_project();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
