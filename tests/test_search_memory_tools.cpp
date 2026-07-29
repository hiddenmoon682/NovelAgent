// test_search_memory_tools — 验证 SearchMemoryTool 通过 ToolDependencies 注入依赖。
//
// 此前工具依赖全局静态指针（服务定位器反模式），无法在测试中注入 mock；
// 迁移为构造注入后，以下路径均可直接测试：
//   1. 依赖未就绪时的容错（返回 error 而非崩溃）
//   2. 空查询校验
//   3. 注入 fake 嵌入生成器 + 真实 VectorStore 的端到端执行
//   4. 嵌入生成异常时的错误包装

#include "agent/tools/SearchMemoryTools.h"
#include "retrieval/VectorStore.h"
#include "retrieval/IEmbeddingGenerator.h"
#include "utils/FileUtils.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
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

using json = nlohmann::json;

// =========================================================================
// 辅助：fake 嵌入生成器（固定向量，无网络依赖）
// =========================================================================

class FakeEmbeddingGenerator : public retrieval::IEmbeddingGenerator {
public:
    retrieval::EmbeddingVector generateEmbedding(const std::string& text) override {
        // 简单确定性映射：以文本首字符区分方向，保证相似度可预测
        float val = (!text.empty() && text[0] == 'n') ? -1.0f : 1.0f;
        return retrieval::EmbeddingVector(4, val);
    }
    std::vector<retrieval::EmbeddingVector> generateEmbeddings(
        const std::vector<std::string>& texts) override {
        std::vector<retrieval::EmbeddingVector> out;
        for (const auto& t : texts) out.push_back(generateEmbedding(t));
        return out;
    }
    int dimension() const override { return 4; }
    std::string modelName() const override { return "fake-embedding"; }
};

// 总是抛异常的嵌入生成器，用于验证错误包装路径
class ThrowingEmbeddingGenerator : public retrieval::IEmbeddingGenerator {
public:
    retrieval::EmbeddingVector generateEmbedding(const std::string&) override {
        throw std::runtime_error("embedding API 不可用");
    }
    std::vector<retrieval::EmbeddingVector> generateEmbeddings(
        const std::vector<std::string>&) override {
        throw std::runtime_error("embedding API 不可用");
    }
    int dimension() const override { return 4; }
    std::string modelName() const override { return "throwing-embedding"; }
};

// 清理临时文件
static void cleanup(const std::string& path) {
    if (utils::file::exists(path)) {
        utils::file::removeFile(path);
    }
}

// =========================================================================
// 测试 1: 依赖未就绪 — 容错返回 error
// =========================================================================

void test_backend_not_ready() {
    TEST("search_memory — 依赖未注入时返回 error 而非崩溃");

    agent::ToolDependencies deps;  // vector_store / embedding_gen 均为 nullptr
    agent::SearchMemoryTool tool(deps);

    auto r = tool.execute({{"query", "任意查询"}});
    CHECK(r.contains("error"));
    CHECK(r["error"].get<std::string>().find("未初始化") != std::string::npos);

    PASS();
}

void test_backend_partial_ready() {
    TEST("search_memory — 仅注入 vector_store 仍视为未就绪");

    retrieval::VectorStore store;
    agent::ToolDependencies deps;
    deps.vector_store = &store;   // embedding_gen 缺失
    agent::SearchMemoryTool tool(deps);

    auto r = tool.execute({{"query", "任意查询"}});
    CHECK(r.contains("error"));

    PASS();
}

// =========================================================================
// 测试 2: 空查询校验
// =========================================================================

void test_empty_query() {
    TEST("search_memory — 空查询返回 error");

    retrieval::VectorStore store;
    FakeEmbeddingGenerator gen;
    agent::ToolDependencies deps;
    deps.vector_store = &store;
    deps.embedding_gen = &gen;
    agent::SearchMemoryTool tool(deps);

    auto r = tool.execute({{"query", ""}});
    CHECK(r.contains("error"));
    CHECK(r["error"].get<std::string>().find("不能为空") != std::string::npos);

    PASS();
}

// =========================================================================
// 测试 3: 注入 fake 生成器 + 真实 VectorStore 的端到端执行
// =========================================================================

void test_execute_with_injected_backend() {
    TEST("search_memory — 注入依赖后端到端检索");

    const std::string db_path =
        "D:/C++Code/C++NovelAgent/build/tmp_test_search_memory.json";
    cleanup(db_path);

    retrieval::VectorStore store;
    store.init(db_path);
    // 与 FakeEmbeddingGenerator 的方向约定一致：正向量匹配、负向量不匹配
    store.insert("vec-pos", retrieval::EmbeddingVector(4, 1.0f),
                 {{"text", "主角在雨夜遇见神秘老人"}, {"type", "chapter"},
                  {"chapter_id", "ch-001"}});
    store.insert("vec-neg", retrieval::EmbeddingVector(4, -1.0f),
                 {{"text", "无关内容"}, {"type", "chapter"}});

    FakeEmbeddingGenerator gen;
    agent::ToolDependencies deps;
    deps.vector_store = &store;
    deps.embedding_gen = &gen;
    agent::SearchMemoryTool tool(deps);

    auto r = tool.execute({{"query", "雨夜的神秘老人"}, {"top_k", 1}});
    CHECK(!r.contains("error"));
    CHECK(r.contains("results"));
    CHECK(r["results"].size() == 1);
    CHECK(r["results"][0]["id"] == "vec-pos");
    CHECK(r["results"][0]["text"] == "主角在雨夜遇见神秘老人");
    CHECK(r["results"][0]["chapter_id"] == "ch-001");
    CHECK(r["query"] == "雨夜的神秘老人");

    store.close();
    cleanup(db_path);
    PASS();
}

void test_top_k_clamped() {
    TEST("search_memory — top_k 越界时被钳制到 [1,20]");

    retrieval::VectorStore store;
    FakeEmbeddingGenerator gen;
    agent::ToolDependencies deps;
    deps.vector_store = &store;
    deps.embedding_gen = &gen;
    agent::SearchMemoryTool tool(deps);

    // 空存储 + 非法 top_k：不应报错，返回空结果集
    auto r1 = tool.execute({{"query", "q"}, {"top_k", 0}});
    CHECK(!r1.contains("error"));
    CHECK(r1["results"].empty());

    auto r2 = tool.execute({{"query", "q"}, {"top_k", 999}});
    CHECK(!r2.contains("error"));
    CHECK(r2["results"].empty());

    PASS();
}

// =========================================================================
// 测试 4: 嵌入生成异常 — 包装为 error 返回
// =========================================================================

void test_embedding_exception_wrapped() {
    TEST("search_memory — 嵌入生成异常被包装为 error");

    retrieval::VectorStore store;
    ThrowingEmbeddingGenerator gen;
    agent::ToolDependencies deps;
    deps.vector_store = &store;
    deps.embedding_gen = &gen;
    agent::SearchMemoryTool tool(deps);

    auto r = tool.execute({{"query", "任意查询"}});
    CHECK(r.contains("error"));
    CHECK(r["error"].get<std::string>().find("检索失败") != std::string::npos);

    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_search_memory_tools ===\n\n";

    test_backend_not_ready();
    test_backend_partial_ready();
    test_empty_query();
    test_execute_with_injected_backend();
    test_top_k_clamped();
    test_embedding_exception_wrapped();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
