# NovelAgent 存储层迁移设计：SQLite 单库（会话 + 向量 + 索引清单）

- 日期：2026-08-17
- 状态：已评审（用户批准方案 A）
- 范围：Phase 1（核心库）；Phase 2（FTS5 全文检索）另立计划，不在本文档实施范围内

## 1. 背景与动机

当前全部数据以 JSON/Markdown 文件持久化：

| 数据 | 位置 | 现状问题 |
|---|---|---|
| 向量索引 | `.novelagent/vectors.json` | 全量加载到内存 + 每轮 RAG 全量暴力扫描；全量覆写落盘 |
| 索引清单 | `.novelagent/index_manifest.json` | 与向量库分属两文件、无事务；索引中途崩溃产生不一致，靠下次索引重建兜底 |
| 会话 | `sessions/index.json`、`sessions/<id>.json`、`sessions/<id>.history`、`archive/` | 快照层全量覆写、无事务保证；索引读-改-写需自行加锁串行化 |

核心问题：**无事务边界**。向量+清单+会话的多次文件写入无法原子提交，崩溃即脏状态。

## 2. 分层原则（已确认）

> 文件存"事实源"，SQLite 存"高频写入的结构化数据"与"派生索引"。

**进 SQLite**：会话/消息（sessions、messages）、向量索引（sqlite-vec）、索引清单（index_sources/index_chunks/kv_store）、（可选 Phase 2）全文索引（FTS5）。

**留文件**：章节 `.md`、novel.json/characters.json/outline.json/settings.json/style.json、长期记忆 `memories.json`（低频、体小、人工可读）、技能/规则文件。

判断标准（一票）：写入频率高？数据量大？人工不需要直接读写？→ 三个都是 → 进 SQLite。

记忆特例：记忆**本体**（memories.json）不进库；记忆**向量索引**随索引层进库，靠 `metadata.type == "memory"` 区分来源。

## 3. 兼容性决策（用户已授权）

程序尚未正式发布，**不做任何向后兼容迁移**：

- 首次建库时检测到旧布局文件（`vectors.json`、`index_manifest.json`、`sessions/`、`archive/`）**直接删除**。
- 不提供 JSON → SQLite 的导入/迁移逻辑；旧内容弃用即删。

## 4. 依赖引入（全部 vendor，固定版本）

| 组件 | 内容 | 说明 |
|---|---|---|
| `third_party/sqlite/` | sqlite3 amalgamation（`sqlite3.c/.h`、`sqlite3ext.h`） | 编译时定义 `SQLITE_ENABLE_FTS5`（FTS5 源码随 amalgamation 自带，Phase 2 直接可用） |
| `third_party/sqlite-vec/` | sqlite-vec amalgamation（`sqlite-vec.c/.h`，MIT） | 静态链接：`SQLITE_CORE` + 启动时 `sqlite3_vec_init()` 自动注册（`sqlite3_auto_extension`） |
| `third_party/sqlitecpp/` | SQLiteCpp（header-only，RAII 封装） | 薄封装、异常安全，贴合项目现代 C++ 风格 |

vendor 而非 pacman 包的理由：离线可构建、版本可控、FTS5 开关可控（MSYS2 包是否开启 FTS5 不可控）。

## 5. 新增组件：`src/storage/`

### SqliteStore

单一数据库连接管理，产品代码内唯一的 SQLite 入口。

- 职责：打开 `<project>/.novelagent/novel.db`、建表迁移（`CREATE TABLE IF NOT EXISTS` + kv_store 记录 schema 版本）、WAL 模式、外键开启、注册 sqlite-vec 扩展。
- 线程模型：**全库一把互斥锁串行化**（对应现状 VectorStore 的 shared_mutex + SessionPersistence 的 index_mutex 模式）；注释标注未来可优化为 thread_local 连接池（WAL 下读者不阻塞写者）。
- 错误处理：库打开失败/损坏 → spdlog 记录 → 重命名 `novel.db.corrupt-<时间戳>` → 重建空库，保证应用可用（无发布阶段可承受丢索引/会话）。
- 事务：所有多表写操作提供 `BEGIN IMMEDIATE ... COMMIT` 事务包装；`SqliteStore::transaction(callback)` 便捷方法。

## 6. 数据 Schema（`novel.db`）

```sql
PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;

-- 会话（archived=1 为已删除会话的归档态，对应原 archive/ 目录语义：数据保留、列表不可见）
CREATE TABLE IF NOT EXISTS sessions (
  id         TEXT PRIMARY KEY,
  title      TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,          -- ISO 8601 UTC
  updated_at TEXT NOT NULL,          -- ISO 8601 UTC
  archived   INTEGER NOT NULL DEFAULT 0
);

-- 快照层（对应原 <id>.json）：save() 事务内 DELETE + 重插
CREATE TABLE IF NOT EXISTS messages (
  session_id       TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  seq              INTEGER NOT NULL,       -- 会话内顺序号，恢复顺序
  role             TEXT NOT NULL,
  content          TEXT NOT NULL DEFAULT '',
  tool_calls       TEXT,                   -- JSON 数组字符串
  tool_call_id     TEXT,
  reasoning_content TEXT,
  preserved        INTEGER NOT NULL DEFAULT 0,  -- pin 标记（原 preserved 字段）
  is_control       INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (session_id, seq)
);

-- 完整历史层（对应原 <id>.history）：append-only
CREATE TABLE IF NOT EXISTS message_history (
  session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
  seq        INTEGER NOT NULL,
  role       TEXT NOT NULL,
  content    TEXT NOT NULL DEFAULT '',
  tool_calls TEXT,
  tool_call_id TEXT,
  reasoning_content TEXT,
  preserved  INTEGER NOT NULL DEFAULT 0,
  is_control INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (session_id, seq)
);

-- 索引清单（对应原 index_manifest.json）
CREATE TABLE IF NOT EXISTS index_sources (
  source_key   TEXT PRIMARY KEY,     -- "chapter:ch-001" / "memory:mem-..."
  content_hash TEXT NOT NULL,        -- FNV-1a 十六进制
  updated_at   INTEGER NOT NULL      -- epoch 秒
);
CREATE TABLE IF NOT EXISTS index_chunks (
  source_key TEXT NOT NULL REFERENCES index_sources(source_key) ON DELETE CASCADE,
  chunk_id   TEXT NOT NULL,          -- "ch-001-seg-0"
  PRIMARY KEY (source_key, chunk_id)
);

-- 单行 KV（模型指纹等）
CREATE TABLE IF NOT EXISTS kv_store (
  key   TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
-- 约定 key: embedding_model / embedding_dimension

-- 向量表（sqlite-vec vec0 虚拟表；维度由模型指纹决定，失配时 DROP 重建）
CREATE VIRTUAL TABLE IF NOT EXISTS vec_chunks USING vec0(
  chunk_id  TEXT,
  metadata  TEXT,                    -- JSON 字符串（type/chapter_id/memory_id/kind/...）
  embedding float[<dimension>],
  distance_metric='cosine'
);
```

## 7. 模块改造映射

| 现状 | 改造 |
|---|---|
| `retrieval/VectorStore.{h,cpp}`（JSON 暴力扫） | **删除**；新增 `retrieval/SqliteVectorStore.{h,cpp}` 实现 `IVectorStore`；`flush()` 为 no-op（事务即持久化） |
| `retrieval/IVectorStore.h` | 接口不变；注释更新（JsonVectorStore → SqliteVectorStore） |
| `agent/index/IndexManifest.{h,cpp}` | **删除类**；`ProjectIndexService` 直接经 SqliteStore 读写表 |
| `agent/index/ProjectIndexService.cpp` | 哈希比较/孤儿清理/指纹失效逻辑保留；`indexAll` 全程**单事务**：向量删旧+插新+清单更新原子提交（消除现有"先落盘向量后写清单"的两段式） |
| `agent/session/SessionPersistence.{h,cpp}` | **公开接口不变**（save/load/appendHistory/loadHistory/listSessions/createSession/deleteSession）；内部改为 SQL 实现。`deleteSession` 置 `sessions.archived=1`（原归档可恢复语义），`listSessions` 过滤 archived=1；system prompt 不落盘、标题自动提取、preserved 标记等语义全部保留 |
| 组装处（`AppAssembly` / `NovelAgentApp`） | 创建 SqliteStore + SqliteVectorStore 注入；SessionPersistence 构造改收 SqliteStore 引用 |
| `.novelagent/` 布局 | 保留 `novel.db` + `memories.json` + 技能/规则；旧文件首次建库时删除（见 §3） |

### 关键语义映射

- **相似度**：sqlite-vec 返回 cosine distance（范围 [0,2]）；现有契约 similarity = (cos+1)/2 ∈ [0,1] 降序 → `similarity = 1 - distance/2`，`SqliteVectorStore::search` 内部换算，测试锁定。
- **模型/维度失配**：`DROP TABLE vec_chunks` + 清空 index_sources/index_chunks → 下次 indexAll 重建（等价现有"整库失效"）。
- **消息序列化**：`Message` 的 tool_calls / reasoning_content / preserved / is_control 落独立 TEXT/INTEGER 列，load 时还原为 `llm::Message`（沿用 `SessionPersistence.cpp` 现有 serialize 逻辑改造）。

## 8. 构建与测试

### 构建

- 新 CMake target `novelagent_sqlite`：sqlite3.c + sqlite-vec.c 编译为 C 静态库（`SQLITE_ENABLE_FTS5`、`SQLITE_CORE`），**不进 PCH**；`novelagent_core` 链接之。
- `cmake/Sources.cmake`：新增 `NOVELAGENT_STORAGE`（SqliteStore）；`NOVELAGENT_RETRIEVAL` 移除 `VectorStore.cpp` 换 `SqliteVectorStore`；`NOVELAGENT_AGENT` 移除 `IndexManifest`（若并入）。

### 测试

- 新 `tests/test_sqlite_store.cpp`：建表迁移、WAL/外键、事务回滚、损坏自愈（.corrupt 重命名重建）。
- 新 `tests/test_sqlite_vector_store.cpp`：IVectorStore 全语义对齐（insert 覆盖/remove/update/search 的 similarity 映射 [0,1] 降序/count/contains/insertBatch/并发读）。
- 新 `tests/test_session_sqlite.cpp`：save/load 往返、appendHistory/loadHistory、listSessions 排序、deleteSession 归档、标题提取、preserved 传递。
- 改造既有：`test_retrieval`（改用 SqliteVectorStore + 临时库文件）、`test_index_manifest`（改 SQL 版或并入索引服务流程）、`test_agent`/`test_search_memory_tools` 等依赖 IVectorStore 的照常运行。
- 验证门：`./scripts/verify.sh test_sqlite_store test_sqlite_vector_store test_session_sqlite test_retrieval` 聚焦，交付前全量。

## 9. 执行阶段

- **Phase 1（本文档范围）**：vendor 依赖 + SqliteStore + SqliteVectorStore + IndexManifest 表化 + SessionPersistence SQL 化 + 旧文件清理 + 全量测试打通。
- **Phase 2（另立计划，可选）**：FTS5 contentless 全文索引（正文不复制，按 rowid 回查文件），支持"哪个章节出现过这句话"。

## 10. 非目标（YAGNI）

- 不做旧数据迁移/导入（用户明确：未见正式发布，旧内容直接删除）。
- 不引入 ORM（消息为可变 JSON 结构，SQLiteCpp + 明文 SQL 足够）。
- 不做嵌入式缓存表、不做向量量化。
- 不做连接池（单机、规模小；注释标注未来优化点即可）。
- 长期记忆 `memories.json` 保持文件事实源，本计划不迁移。