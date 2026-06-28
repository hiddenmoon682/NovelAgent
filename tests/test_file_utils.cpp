// FileUtils 单元测试。
// 重点验证 writeText 的原子写入语义（B1 修复）：
//   - 首次写入 + 读取往返内容一致
//   - 覆盖已有文件后内容正确更新（原子替换）
//   - 写入完成后不残留 .tmp 临时文件
//   - 父目录自动创建
//   - 写入内容完整无截断（含中文与换行）

#include "utils/FileUtils.h"

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

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

const std::string kTestDir = "__test_file_utils_tmp";

void cleanup() {
    if (utils::file::exists(kTestDir)) {
        utils::file::removeDir(kTestDir);
    }
}

// 列出 dir 下是否存在任何 .tmp 临时文件残留。
bool hasTmpResidue(const std::string& dir) {
    if (!fs::exists(dir)) return false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::string name = entry.path().filename().string();
        // 原子写临时文件名形如 <path>.tmp.<seq>
        if (name.find(".tmp.") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void test_writeReadRoundtrip() {
    TEST("writeText → readText 往返");
    cleanup();
    utils::file::createDirs(kTestDir);
    const std::string path = utils::file::joinPath(kTestDir, "a.txt");
    const std::string content = "第一行\n第二行 line two\n";
    utils::file::writeText(path, content);
    CHECK(utils::file::readText(path) == content);
    CHECK(!hasTmpResidue(kTestDir));
    PASS();
}

void test_overwriteAtomically() {
    TEST("writeText 覆盖已有文件（原子替换）");
    cleanup();
    utils::file::createDirs(kTestDir);
    const std::string path = utils::file::joinPath(kTestDir, "b.txt");
    utils::file::writeText(path, "旧内容 old");
    CHECK(utils::file::readText(path) == "旧内容 old");
    // 覆盖为更长的新内容
    utils::file::writeText(path, "新内容 new content，比旧的更长一些。");
    CHECK(utils::file::readText(path) == "新内容 new content，比旧的更长一些。");
    // 覆盖为更短的新内容（验证不是追加、不是残留旧字节）
    utils::file::writeText(path, "短");
    CHECK(utils::file::readText(path) == "短");
    CHECK(!hasTmpResidue(kTestDir));
    PASS();
}

void test_noTmpResidueAfterMultipleWrites() {
    TEST("多次连续写入不残留临时文件");
    cleanup();
    utils::file::createDirs(kTestDir);
    const std::string path = utils::file::joinPath(kTestDir, "c.txt");
    for (int i = 0; i < 50; ++i) {
        utils::file::writeText(path, std::to_string(i));
    }
    CHECK(utils::file::readText(path) == "49");
    CHECK(!hasTmpResidue(kTestDir));
    PASS();
}

void test_autoCreateParentDir() {
    TEST("writeText 自动创建多层父目录");
    cleanup();
    const std::string path = utils::file::joinPath(kTestDir, "sub/deep/dir/d.txt");
    utils::file::writeText(path, "嵌套");
    CHECK(utils::file::readText(path) == "嵌套");
    CHECK(!hasTmpResidue(utils::file::joinPath(kTestDir, "sub/deep/dir")));
    PASS();
}

void test_largeContentIntact() {
    TEST("大内容写入完整无截断");
    cleanup();
    utils::file::createDirs(kTestDir);
    const std::string path = utils::file::joinPath(kTestDir, "big.txt");
    // 构造约 200KB 的中文+英文混合内容，验证不被截断
    std::string chunk = "这是一段用于测试大文件写入完整性的内容。This is English. \n";
    std::string large;
    large.reserve(chunk.size() * 5000);
    for (int i = 0; i < 5000; ++i) large += chunk;
    utils::file::writeText(path, large);
    const std::string back = utils::file::readText(path);
    CHECK(back.size() == large.size());
    CHECK(back == large);
    CHECK(!hasTmpResidue(kTestDir));
    PASS();
}

void test_readMissingFileReturnsEmpty() {
    TEST("readText 读取不存在的文件返回空串");
    cleanup();
    CHECK(utils::file::readText(utils::file::joinPath(kTestDir, "nope.txt")).empty());
    PASS();
}

int main() {
    std::cout << "=== test_file_utils (原子写入) ===\n\n";

    test_writeReadRoundtrip();
    test_overwriteAtomically();
    test_noTmpResidueAfterMultipleWrites();
    test_autoCreateParentDir();
    test_largeContentIntact();
    test_readMissingFileReturnsEmpty();

    cleanup();

    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return tests_passed == tests_run ? 0 : 1;
}
