#include "NovelAgentApp.h"

#include "agent/AgentSetup.h"
#include "agent/PromptComposer.h"
#include "agent/tools/SearchMemoryTools.h"
#include "cli/ConsoleOutput.h"
#include "cli/ReplHandler.h"
#include "cli/StreamDisplay.h"
#include "project/ProjectIO.h"
#include "retrieval/NovelChunker.h"

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
        "- 【主动获取上下文】使用 get_chapter_context() / get_relevant_characters() 等工具\n"
        "  按需获取本章相关的设定、角色和规则，不要在 system prompt 中等待被动注入\n"
        "- 【按需查询】不要一次性获取所有信息。先了解核心上下文，\n"
        "  写作中需要确认细节时再调用单个查询工具\n"
        "- 写完后确认内容已正确写入文件\n"
        "- 保持语言流畅、情节紧凑";
    agent_.setSystemPrompt(agent::PromptComposer::compose(pc));

    // 注入 Token 自校准器到 ContextManager（利用 API 返回的真实 token 做 EMA 修正）
    cm_.setCalibrator(&calibrator_);

    agent_.setContextManager(&cm_);
    agent_.setMaxContextTokens(client_.config().max_context_tokens);
    cm_.setModelContextLimit(client_.config().max_context_tokens);
    cm_.setProject(project_.get());

    // 初始化向量检索后端
    if (project_ && !project_->path.empty()) {
        std::string vec_path = project_->path + "/.novelagent/vectors.json";
        vector_store_.init(vec_path);
    }
    // 默认使用串行处理器（支持完整 ToolCallLoop + 工具集）。
    // Agent 构造函数已调用 useSerialProcessor()，无需再次设置。
    // 用户可通过 REPL 中 /parallel on 切换到并行编排模式。
}

void NovelAgentApp::runRepl(const std::string& welcomeMessage)
{
    ReplHandler repl(agent_, out_, project_);
    // Issue 6: 通过 IIndexService 接口注入，消除 ReplHandler→NovelAgentApp* 反向依赖
    repl.setIndexService(this);
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

// ============================================================================
// Issue 6: indexAll — IIndexService 实现
// 将 /index 命令的核心逻辑从 ReplHandler 移至 NovelAgentApp，
// ReplHandler 通过 IIndexService 接口调用，不再持有 NovelAgentApp* 反向指针。
// ============================================================================

agent::IndexResult NovelAgentApp::indexAll(std::function<void(const std::string&)> progress)
{
    agent::IndexResult result;

    if (!project_ || project_->path.empty()) {
        result.error = "未打开项目";
        return result;
    }

    auto report = [&](const std::string& msg) {
        if (progress) progress(msg);
    };

    report("正在为项目内容建立向量索引...");

    retrieval::NovelChunker chunker;
    std::vector<retrieval::TextChunk> all_chunks;

    // ① 章节正文切分
    for (const auto& ch : project_->outline.chapters) {
        if (ch.file_path.empty()) continue;
        std::string md = ProjectIO::readChapter(project_->path, ch.file_path);
        if (md.empty()) continue;
        auto chunks = chunker.chunkChapter(ch, md);
        for (auto& c : chunks) all_chunks.push_back(std::move(c));
        ++result.chapters;
    }
    report("  章节: " + std::to_string(result.chapters) + " 章 → "
         + std::to_string(all_chunks.size()) + " 个片段");

    // ② 角色嵌入
    for (const auto& c : project_->characters) {
        std::string text = retrieval::NovelChunker::chunkCharacter(c);
        if (text.empty()) continue;
        all_chunks.push_back(retrieval::TextChunk::characterChunk(c.id, text));
        ++result.characters;
    }
    report("  角色: " + std::to_string(result.characters) + " 个");

    // ③ 设定嵌入
    for (const auto& s : project_->settings) {
        std::string text = retrieval::NovelChunker::chunkSetting(s);
        if (text.empty()) continue;
        all_chunks.push_back(retrieval::TextChunk::settingChunk(s.id, text));
        ++result.settings;
    }
    report("  设定: " + std::to_string(result.settings) + " 个");

    // ④ 世界规则嵌入
    for (const auto& r : project_->world_rules) {
        std::string text = retrieval::NovelChunker::chunkWorldRule(r);
        if (text.empty()) continue;
        all_chunks.push_back(retrieval::TextChunk::worldRuleChunk(r.id, text));
        ++result.world_rules;
    }
    report("  世界规则: " + std::to_string(result.world_rules) + " 条");

    result.total_chunks = static_cast<int>(all_chunks.size());
    if (all_chunks.empty()) {
        result.error = "没有可索引的内容";
        return result;
    }

    // ⑤ 批量生成嵌入向量
    report("正在生成嵌入向量 (" + std::to_string(all_chunks.size()) + " 条)...");
    std::vector<std::string> texts;
    texts.reserve(all_chunks.size());
    for (const auto& c : all_chunks) texts.push_back(c.text);
    auto embeddings = embedding_gen_.generateEmbeddings(texts);

    if (embeddings.size() != all_chunks.size()) {
        result.error = "嵌入向量数量不匹配: " + std::to_string(embeddings.size())
                     + " vs " + std::to_string(all_chunks.size());
        return result;
    }

    // ⑥ 插入向量库 + 持久化
    for (size_t i = 0; i < all_chunks.size(); ++i) {
        vector_store_.insert(all_chunks[i].id, embeddings[i], all_chunks[i].metadata);
    }
    vector_store_.saveToFile();

    report("向量索引已建立: " + std::to_string(all_chunks.size()) + " 条 → "
         + project_->path + "/.novelagent/vectors.json");
    return result;
}
