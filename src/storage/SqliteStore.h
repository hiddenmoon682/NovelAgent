#pragma once

// SqliteStore — SQLite 单库入口（产品代码内唯一的 SQLite 连接）。
//
// 线程模型：全库一把互斥锁。withLock/inTransaction 自行加锁，回调内
// **禁止**再调用本类任何加锁方法（包括二者本身，不可嵌套）。
// exec/getKV/setKV/ensureVectorTable/db 均为"锁内使用"的低层方法，
// 只能在 withLock/inTransaction 回调内调用（文档约束，编译期不强制）。
// open/close 是唯一可在锁外调用的方法（应用生命周期保证不与锁内回调并发）。
//
// 异常策略：inTransaction 回调抛异常 → 自动 ROLLBACK 并重抛；其他低层方法
// 不捕获 SQLiteCpp 异常（由调用方按语义处理）；open 的建表失败走损坏自愈
// （改名 .corrupt-<时间戳> → 重建空库），保证应用可用性。

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <memory>
#include <mutex>
#include <string>
#include <type_traits>

namespace storage {

class SqliteStore {
public:
    SqliteStore() = default;
    ~SqliteStore() { close(); }

    SqliteStore(const SqliteStore&) = delete;
    SqliteStore& operator=(const SqliteStore&) = delete;

    // 打开/创建 db_path；已打开则忽略。首次打开执行建表迁移与 PRAGMA。
    // 建表失败视为库损坏：改名 novel.db.corrupt-<时间戳> 后重建空库。
    void open(const std::string& db_path);
    // 关闭连接（WAL 由 SQLite 在最后连接关闭时自动 checkpoint）；未打开时 no-op。
    void close();
    bool isOpen() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }

    // 锁内执行读回调；回调仅可调用本类锁内方法。
    template <typename F>
    auto withLock(F&& f) -> decltype(f(*this)) {
        std::lock_guard<std::mutex> lock(mutex_);
        return f(*this);
    }

    // 锁内事务：回调正常返回 → COMMIT；抛异常 → ROLLBACK 并重抛。
    template <typename F>
    auto inTransaction(F&& f) -> decltype(f(*this)) {
        std::lock_guard<std::mutex> lock(mutex_);
        SQLite::Database& db = *db_;
        db.exec("BEGIN IMMEDIATE");
        try {
            if constexpr (std::is_void_v<decltype(f(*this))>) {
                f(*this);
                db.exec("COMMIT");
            } else {
                auto r = f(*this);
                db.exec("COMMIT");
                return r;
            }
        } catch (...) {
            db.exec("ROLLBACK");
            throw;
        }
    }

    // ── 锁内低层操作 ──

    SQLite::Database& db() { return *db_; }

    // 执行无参 SQL（建表/PRAGMA 等）。
    void exec(const std::string& sql);

    // kv_store 存取（模型指纹等）；key 不存在返回空串。
    std::string getKV(const std::string& key);
    void setKV(const std::string& key, const std::string& value);

    // 确保 vec_chunks 存在（维度 dimension）；维度与库中不同时 DROP 重建。
    // 注意：DROP 会清空全部向量，调用者须在指纹失配等语义下使用。
    void ensureVectorTable(int dimension);
    int vectorDimension() const { return vector_dimension_; }

private:
    // 建全部业务表与 PRAGMA（sessions/messages/message_history/index_sources/
    // index_chunks/kv_store；vec_chunks 由 ensureVectorTable 按维度创建）。
    void ensureSchema();
    // 损坏自愈：改名 + 重建空库；再次失败则保持未打开。
    void removeCorruptAndReopen();

    std::unique_ptr<SQLite::Database> db_;
    mutable std::mutex mutex_;
    std::string path_;
    int vector_dimension_ = 0;  // 内存缓存当前向量维度；打开时从库内推理
};

} // namespace storage