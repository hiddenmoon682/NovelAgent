// AppAssembly — NovelAgentApp::setupAgent 的分段装配实现（架构审查 O2 拆分）。
//
// 将原先平铺在 NovelAgentApp.cpp 中的装配代码按功能段拆为命名私有函数
//（不引入工厂抽象层）。各辅助函数均为原代码段的原样搬移（含注释），
// setupAgent 按原顺序依次调用，行为与拆分前逐行等价。
#include "NovelAgentApp.h"

#include "agent/tools/BuiltInTool.h"
#include "agent/index/ProjectIndexService.h"
#include "agent/skill/BuiltinSkills.h"
#include "utils/FileUtils.h"

// 装配入口：依序调用各分段辅助函数完成 Agent 全部初始化。
// 调用顺序即装配顺序，各段依赖关系：记忆/技能先就绪 → 工具注册 →
// prompt → 上下文/预算 → 持久化/向量库 → 摘要/索引。
void NovelAgentApp::setupAgent()
{
    setupLongTermMemoryAndSkills();
    registerBuiltInTools();
    setupSystemPrompt();
    setupContextAndTokenBudget();
    setupPersistenceAndVectorStore();
    setupSummarySinkAndIndexService();
}

// 初始化长期记忆日志与技能系统（装配第一段，须先于工具注册）。
// 步骤：绑定长期记忆到项目目录 → 安装内置技能与默认规则 → 登记项目级/全局搜索路径
// 并恢复禁用列表 → 扫描加载技能。
// 须最先执行的原因：save_memory/use_skill/save_skill 等工具以指针持有
// ltm_store_ 与 skill_registry_，若先注册工具后初始化，工具首次调用会拿到空状态。
void NovelAgentApp::setupLongTermMemoryAndSkills()
{
    // 长期记忆日志与技能发现均须先于工具注册完成
    //（save_memory 依赖已初始化的 store；use_skill/save_skill 持有 registry 指针）
    if (project_access_ && !project_access_->path().empty()) {
        ltm_store_.init(project_access_->path() + "/.novelagent/memories.json");

        // 内置技能安装到全局目录，再登记项目级 + 全局两个搜索路径
        const std::string global_skills = utils::file::configDir() + "/skills";
        // 配置内置技能与默认规则所在路径（缺失时写入，已存在则跳过）
        skill::installBuiltinSkills(global_skills);
        skill::installDefaultRules(utils::file::configDir());

        skill_registry_.addSearchPath(project_access_->path() + "/skills");  // 项目级优先
        skill_registry_.addSearchPath(global_skills);               // 全局兜底
        skill_registry_.setDisabledSkills(loadDisabledSkills());    // 恢复用户禁用列表
        skill_registry_.discoverAll();                              // 扫描并加载技能元数据
    }
}

// 将内置工具注册到工具注册表（仅项目有效时执行）。
// 依赖包以指针传入，工具借此访问项目/向量库/嵌入生成器/长期记忆/技能。
void NovelAgentApp::registerBuiltInTools()
{
    // 仅项目有效（有标题）时注册；依赖包以指针传入，工具借此访问向量库/记忆/技能。
    // 项目访问经 ProjectAccess 受控层（P2/P3），工具禁止直接碰 Project 裸字段。
    if (project_access_ && !project_access_->title().empty()) {
        agent::ToolDependencies deps{project_access_, &vector_store_, &embedding_gen_,
                                     &ltm_store_, &skill_registry_};
        agent::BuiltInTool::registerAllTo(registry_, deps);
    }
}

// 设置系统 prompt 并注册 prompt 重建 provider。
// 关键点：会话边界（新建/切换）时重建 prompt，使 save_skill 新增的技能目录
// 在下个会话对 LLM 可见；会话中途不重建，以保持 KV cache 稳定。
void NovelAgentApp::setupSystemPrompt()
{
    agent_.setSystemPrompt(buildSystemPrompt());
    // 会话边界（新建/切换）时重建 prompt：save_skill 新增的技能目录
    // 才能在下个会话对 LLM 可见（会话中途不重建，保 KV cache 稳定）
    agent_.setSystemPromptProvider([this] { return buildSystemPrompt(); });
}

// 注入上下文管理组件（Token 校准器）并设置 Token 预算。
// WHY：预算须先于持久化注入，启动恢复会话时的用量百分比需用真实模型上限计算。
void NovelAgentApp::setupContextAndTokenBudget()
{
    // 注入上下文管理组件
    agent_.setCalibrator(&calibrator_);

    // 注入真实模型上下文上限（先于持久化注入：启动恢复会话时的用量百分比需用真实上限计算）
    agent_.setModelLimit(client_.config().max_context_tokens);
}

// 启用会话持久化并初始化向量存储（Task 6 Step 1 提前接线：打开 SQLite 单库）。
// 仅库可用时启用持久化，避免空路径时写到盘符根目录 /.novelagent；
// 旧文件清理（removeLegacyStorageFiles）属 Task 6，此处不实现。
// P8 懒物化：启动仅注入持久化层，不物化任何会话（不建 runtime/client）；
// 历史会话由用户点开时经 materializeSession 恢复。
void NovelAgentApp::setupPersistenceAndVectorStore()
{
    if (project_access_ && !project_access_->path().empty()) {
        const std::string agent_dir = project_access_->path() + "/.novelagent";
        sqlite_store_.open(agent_dir + "/novel.db");
    }
    // 仅库可用时启用持久化（避免空路径时写到盘符根目录 /.novelagent；
    // sqlite_store_ 未 open 时持久化方法统一安全返回，不会崩溃）
    if (sqlite_store_.isOpen()) {
        agent_.setPersistence(&persistence_);
    }

    // 旧 JSON 向量库初始化保留不动（Task 6 随矢量库切换一并移除）
    if (project_access_ && !project_access_->path().empty()) {
        std::string vec_path = project_access_->path() + "/.novelagent/vectors.json";
        vector_store_.init(vec_path);
    }
}

// 装配摘要沉淀与索引服务（装配收尾段）。
// 步骤：① 注册摘要回调，会话压缩产生的摘要自动 append 到长期记忆日志；
// ② 创建项目索引服务，绑定项目、向量库、嵌入生成器与长期记忆存储。
void NovelAgentApp::setupSummarySinkAndIndexService()
{
    // 会话压缩摘要自动沉淀到长期记忆日志
    agent_.setSummarySink([this](const std::string& summary) {
        if (ltm_store_.initialized())
            ltm_store_.append(summary, "summary");
    });

    // 完整历史归档（D4）：SessionRuntime 直接持有 persistence 自调 appendHistory，
    // 无需此处注册 sink 回调（压缩时按会话自身 id 落盘 <id>.history）。

    index_service_ = std::make_unique<agent::ProjectIndexService>(
        project_access_, sqlite_store_, embedding_gen_, &ltm_store_);
}
