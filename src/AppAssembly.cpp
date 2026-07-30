// AppAssembly — NovelAgentApp::setupAgent 的分段装配实现（架构审查 O2 拆分）。
//
// 将原先平铺在 NovelAgentApp.cpp 中的装配代码按功能段拆为命名私有函数
//（不引入工厂抽象层）。各辅助函数均为原代码段的原样搬移（含注释），
// setupAgent 按原顺序依次调用，行为与拆分前逐行等价。
#include "NovelAgentApp.h"

#include "agent/tools/BuiltInTool.h"
#include "agent/index/ProjectIndexService.h"
#include "agent/skill/BuiltinSkills.h"
#include "project/Models/Project.h"
#include "utils/FileUtils.h"

void NovelAgentApp::setupAgent(const std::vector<std::string>& disabledTools)
{
    setupLongTermMemoryAndSkills();
    registerBuiltInTools(disabledTools);
    setupSystemPrompt();
    setupContextAndTokenBudget();
    setupPersistenceAndVectorStore();
    setupSummarySinkAndIndexService();
}

void NovelAgentApp::setupLongTermMemoryAndSkills()
{
    // 长期记忆日志与技能发现均须先于工具注册完成
    //（save_memory 依赖已初始化的 store；use_skill/save_skill 持有 registry 指针）
    if (project_ && !project_->path.empty()) {
        ltm_store_.init(project_->path + "/.novelagent/memories.json");

        // 内置技能安装到全局目录，再登记项目级 + 全局两个搜索路径
        const std::string global_skills = utils::file::configDir() + "/skills";
        skill::installBuiltinSkills(global_skills);
        skill::installDefaultRules(utils::file::configDir());  // 默认全局规则（rules.md）同步落盘

        skill_registry_.addSearchPath(project_->path + "/skills");  // 项目级优先
        skill_registry_.addSearchPath(global_skills);               // 全局兜底
        skill_registry_.setDisabledSkills(loadDisabledSkills());    // 恢复用户禁用列表
        skill_registry_.discoverAll();                              // 扫描并加载技能元数据
    }
}

void NovelAgentApp::registerBuiltInTools(const std::vector<std::string>& disabledTools)
{
    // 仅项目有效（有标题）时注册；依赖包以指针传入，工具借此访问向量库/记忆/技能
    if (project_ && !project_->title.empty()) {
        agent::ToolDependencies deps{project_, &vector_store_, &embedding_gen_,
                                     &ltm_store_, &skill_registry_};
        agent::BuiltInTool::registerAllTo(registry_, deps, disabledTools);
    }
}

void NovelAgentApp::setupSystemPrompt()
{
    agent_.setSystemPrompt(buildSystemPrompt());
    // 会话边界（新建/切换）时重建 prompt：save_skill 新增的技能目录
    // 才能在下个会话对 LLM 可见（会话中途不重建，保 KV cache 稳定）
    agent_.setSystemPromptProvider([this] { return buildSystemPrompt(); });
}

void NovelAgentApp::setupContextAndTokenBudget()
{
    // 注入上下文管理组件
    agent_.setProject(project_.get());
    agent_.setCalibrator(&calibrator_);

    // Token 预算先于持久化注入：启动恢复时的用量百分比需用真实模型上限计算
    agent::TokenBudget budget;
    budget.model_limit = client_.config().max_context_tokens;
    agent_.setTokenBudget(budget);
}

void NovelAgentApp::setupPersistenceAndVectorStore()
{
    // 仅项目已打开时启用持久化（避免空路径时写到盘符根目录 /.novelagent）
    if (project_ && !project_->path.empty()) {
        agent_.setPersistence(&persistence_);
        agent_.loadSessionState();  // 启动时恢复 active 会话（system prompt 以本次装配为准）
    }

    if (project_ && !project_->path.empty()) {
        std::string vec_path = project_->path + "/.novelagent/vectors.json";
        vector_store_.init(vec_path);
    }
}

void NovelAgentApp::setupSummarySinkAndIndexService()
{
    // 会话压缩摘要自动沉淀到长期记忆日志
    agent_.setSummarySink([this](const std::string& summary) {
        if (ltm_store_.initialized())
            ltm_store_.append(summary, "summary");
    });

    index_service_ = std::make_unique<agent::ProjectIndexService>(
        project_, vector_store_, embedding_gen_, &ltm_store_);
}
