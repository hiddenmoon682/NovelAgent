#include "NovelAgentApp.h"

#include "agent/AgentSetup.h"
#include "agent/PromptComposer.h"
#include "agent/tools/SearchMemoryTools.h"
#include "cli/ConsoleOutput.h"
#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "project/ProjectIO.h"

#include <iostream>

NovelAgentApp::NovelAgentApp(const ProviderConfig& provider,
                               std::shared_ptr<Project> project,
                               IOutputChannel* out,
                               std::vector<std::string> disabledTools)
    : ownedOutput_(out ? nullptr : std::make_unique<ConsoleOutput>())
    , out_(out ? *out : *ownedOutput_)
    , client_(provider)
    , agent_(client_, registry_)
    , project_(project ? std::move(project) : std::make_shared<Project>())
    , storage_(project_ ? project_->path : "")
    , cm_(storage_)
    , embedding_gen_(provider)
{
    setupAgent(std::move(disabledTools));
}

void NovelAgentApp::setupAgent(const std::vector<std::string>& disabledTools)
{
    // 初始化语义搜索工具的后端指针（必须在 registerAllTools 之前）
    // vector_store_ / embedding_gen_ 是值成员，在构造函数初始化列表中已完成构造。
    if (project_ && !project_->title.empty()) {
        agent::initSearchMemoryBackend(&vector_store_, &embedding_gen_);
    }

    // 工具自注册（仅在打开项目时注册）
    if (project_ && !project_->title.empty()) {
        agent::registerAllTools(registry_, project_, disabledTools);
    }

    agent::PromptComponents pc;
    pc.personality =
        "你是一个专业的网络小说写作助手 NovelAgent。\n\n"
        "你的能力：\n"
        "- 使用工具读写章节、管理角色和设定\n"
        "- 根据大纲和现有内容创作连贯的章节\n"
        "- 维护角色一致性、剧情连贯性和世界观设定\n\n"
        "工作原则：\n"
        "- 写作前先读取相关章节和设定\n"
        "- 写完后确认内容已正确写入文件\n"
        "- 保持语言流畅、情节紧凑";
    agent_.setSystemPrompt(agent::PromptComposer::compose(pc));

    agent_.setContextManager(&cm_);
    agent_.setMaxContextTokens(client_.config().max_context_tokens);
    cm_.setModelContextLimit(client_.config().max_context_tokens);
    cm_.setProject(project_.get());

    // 初始化向量检索后端
    if (project_ && !project_->path.empty()) {
        std::string vec_path = project_->path + "/.novelagent/vectors.json";
        vector_store_.init(vec_path);
        cm_.setRetrievalBackend(&vector_store_, &embedding_gen_);
    }
    // 默认使用串行处理器（支持完整 ToolCallLoop + 工具集）。
    // Agent 构造函数已调用 useSerialProcessor()，无需再次设置。
    // 用户可通过 REPL 中 /parallel on 切换到并行编排模式。
}

void NovelAgentApp::saveConversationIfNeeded(const llm::LLMResponse& /*response*/)
{
    // 基础版：每次对话轮次后持久化到 .novelagent/conversation.json
    // Phase 4 将添加增量保存和压缩
    if (project_->path.empty()) return;
    try {
        // 使用 Agent 的 conversation 状态
        // TODO: Phase 4 添加 loadConversation 恢复支持
    } catch (...) {
        // 持久化失败不阻塞主流程
    }
}

void NovelAgentApp::runRepl(const std::string& welcomeMessage)
{
    ReplHandler repl(agent_, out_, project_);
    if (!welcomeMessage.empty()) {
        repl.setWelcomeMessage(welcomeMessage);
    } else {
        repl.setWelcomeMessage(
            "欢迎使用 NovelAgent！\n"
            "你可以让我帮你写章节、创建角色、管理设定等。"
        );
    }
    repl.run();
}

void NovelAgentApp::runExec(const std::string& command)
{
    out_.write("执行: " + command + "\n\n");
    try {
        auto callbacks = StreamDisplay::create(out_);
        agent_.execute(command, callbacks);
        out_.write("\n");
    } catch (const std::exception& e) {
        std::string err = e.what();
        // 友好错误提示
        if (err.find("401") != std::string::npos || err.find("API Key") != std::string::npos)
            out_.writeError("错误: API Key 无效，请检查 config.json 中的密钥配置。\n");
        else if (err.find("Connection") != std::string::npos || err.find("连接") != std::string::npos)
            out_.writeError("错误: 网络连接失败，请检查网络后重试。\n");
        else if (err.find("json.exception") != std::string::npos)
            out_.writeError("错误: API 响应解析失败，请检查 API 密钥和网络连接。\n");
        else
            out_.writeError("错误: " + err + "\n");
    }
}
