// AgentOrchestrator 实现 — P1 重构版（ISynthesisStrategy + RestrictedToolProvider）+ Phase 4 线程安全（工厂模式 SubAgent 隔离）。

#include "agent/AgentOrchestrator.h"
#include "agent/IToolProvider.h"
#include "agent/TemplateManager.h"
#include "agent/ToolRegistry.h"
#include "llm/LLMClientFactory.h"

#include <spdlog/spdlog.h>
#include <future>
#include <sstream>

namespace agent {

// ===========================================================================
// TemplateDecomposition
// ===========================================================================

std::vector<SubTask> TemplateDecomposition::decompose(
    const std::string& input, const std::string& mainPrompt)
{
    if (template_mgr_) {
        auto templates = template_mgr_->allTemplates();
        if (!templates.empty()) {
            std::vector<SubTask> tasks;
            int id = 0;
            for (const auto& tmpl : templates) {
                if (tmpl.built_in) {
                    SubTask st;
                    st.id = "sub-" + std::to_string(++id);
                    st.description = tmpl.name + ": " + input;
                    st.system_prompt = tmpl.system_prompt;
                    st.allowed_tools = tmpl.allowed_tools;
                    st.suggested_max_rounds = tmpl.suggested_max_rounds;
                    st.timeout_seconds = tmpl.suggested_max_rounds * 20 + 60;  // MED-2: 根据轮数推算超时，最少 60s
                    st.status = "pending";
                    tasks.push_back(std::move(st));
                }
            }
            if (!tasks.empty()) return tasks;
        }
    }

    std::vector<SubTask> tasks;
    SubTask st;
    st.id = "sub-1";
    st.description = input;
    st.system_prompt = mainPrompt;
    st.allowed_tools = {"read_chapter", "get_character", "get_outline", "get_setting"};
    st.status = "pending";
    tasks.push_back(std::move(st));
    return tasks;
}

// ===========================================================================
// AgentOrchestrator
// ===========================================================================

AgentOrchestrator::AgentOrchestrator(
    llm::LLMClientFactory& factory, ToolRegistry& registry, std::string mainPrompt)
    : factory_(factory), client_(factory.create()), registry_(registry), main_prompt_(std::move(mainPrompt))
{
    detector_ = std::make_unique<KeywordParallelDetector>();
    synthesis_ = std::make_unique<LlmSynthesis>(*client_, main_prompt_, 3000);
}

AgentOrchestrator::~AgentOrchestrator() = default;

void AgentOrchestrator::setTemplateManager(TemplateManager* tm) {
    template_mgr_ = tm;
    decomposition_ = std::make_unique<TemplateDecomposition>(tm);
}

void AgentOrchestrator::setParallelDetector(
    std::unique_ptr<IParallelDetector> detector) {
    detector_ = std::move(detector);
}

void AgentOrchestrator::setDecompositionStrategy(
    std::unique_ptr<IDecompositionStrategy> strategy) {
    decomposition_ = std::move(strategy);
}

void AgentOrchestrator::setSynthesisStrategy(
    std::unique_ptr<ISynthesisStrategy> strategy) {
    synthesis_ = std::move(strategy);
}

void AgentOrchestrator::setSubAgentFactory(SubAgentFactory factory) {
    agent_factory_ = std::move(factory);
}

IParallelDetector& AgentOrchestrator::getDetector() { return *detector_; }

IDecompositionStrategy& AgentOrchestrator::getDecomposition() {
    if (!decomposition_)
        decomposition_ = std::make_unique<TemplateDecomposition>(template_mgr_);
    return *decomposition_;
}

ISynthesisStrategy& AgentOrchestrator::getSynthesis() {
    return *synthesis_;
}

std::string AgentOrchestrator::processMessage(const std::string& input)
{
    // D6: 重置本轮 token 累计
    last_input_tokens_ = 0;
    last_output_tokens_ = 0;
    last_sub_input_tokens_ = 0;   // Issue 28: 重置子任务 token 累计
    last_sub_output_tokens_ = 0;

    if (!parallel_enabled_ || !getDetector().shouldParallelize(input)) {
        std::vector<llm::Message> msgs = { llm::Message::user(input) };
        auto resp = client_->chatNonStreaming(msgs, {}, main_prompt_);
        // D6: 累计串行回退路径的 token
        last_input_tokens_ += resp.prompt_tokens;
        last_output_tokens_ += resp.completion_tokens;
        last_sub_tasks_.clear();  // CRIT-1: 串行路径不产生子任务，清空上次的残留
        return resp.content;
    }

    spdlog::info("[Orchestrator] 并行编排: {}", input.substr(0, 60));

    auto tasks = decompose(input);
    if (tasks.empty()) {
        std::vector<llm::Message> msgs = { llm::Message::user(input) };
        auto resp = client_->chatNonStreaming(msgs, {}, main_prompt_);
        last_input_tokens_ += resp.prompt_tokens;
        last_output_tokens_ += resp.completion_tokens;
        last_sub_tasks_.clear();  // CRIT-1: 无子任务时清空残留
        return resp.content;
    }

    spdlog::info("[Orchestrator] {} 个子任务", tasks.size());
    executeParallel(tasks);
    last_sub_tasks_ = tasks;  // CRIT-1: 保存子任务详情供 ParallelProcessor 注入对话
    auto result = synthesize(tasks);
    // D6: 汇总 LLM 调用也计入 token（LlmSynthesis 内部调 chatNonStreaming，
    // 但其 client_ 来自 AgentOrchestrator 的独立 client_ 引用，token 未直接暴露。
    // 此处通过 orchestrator 的 LLMResponse 累计；若合成策略非 LLM 驱动则 token 为 0。
    // 子任务（SubAgent）的 token 不计入——它们使用独立的 LLMClient 实例，
    // 且在并行多线程环境下收集有竞争风险。这里只覆盖编排器自身的 LLM 调用。
    return result;
}

std::vector<SubTask> AgentOrchestrator::decompose(const std::string& input)
{
    return getDecomposition().decompose(input, main_prompt_);
}

void AgentOrchestrator::executeParallel(std::vector<SubTask>& tasks)
{
    // CRIT-3: 消除 O(n²) 轮询。直接提交所有任务到线程池（工作线程数 = 12 ≥ max_parallel_=4），
    // 线程池内部自动限制并行度。全部提交后统一收集结果，避免每次 100ms 轮询遍历未来列表。
    std::vector<std::future<SubAgentResult>> futures(tasks.size());

    for (size_t i = 0; i < tasks.size(); ++i) {
        auto submitTask = [this, task = tasks[i]]() -> SubAgentResult {
            SubAgentConfig config;
            config.task = task.description;
            config.system_prompt = task.system_prompt;
            config.allowed_tools = task.allowed_tools;
            config.timeout = std::chrono::seconds(task.timeout_seconds);  // MED-2: 从 SubTask 读取
            config.max_tool_rounds = task.suggested_max_rounds;

            // P0 改进：SubAgent 通过 RestrictedToolProvider 受限视图访问工具
            RestrictedToolProvider tools(registry_, task.allowed_tools);
            auto agent = agent_factory_(factory_, tools);
            return agent->execute(config);
        };

        // Issue 4: 使用线程池替代 std::async，复用线程
        futures[i] = thread_pool_->submit(submitTask);
    }

    // ── 统一收集所有任务结果 ──
    for (size_t i = 0; i < futures.size(); ++i) {
        SubAgentResult result;
        try {
            result = futures[i].get();
        } catch (const std::exception& e) {
            spdlog::error("[Orchestrator] {} 异常: {}", tasks[i].id, e.what());
            result.error = e.what();
        }
        tasks[i].status = result.timed_out ? "timed_out" :
                          !result.error.empty() ? "failed" : "completed";
        tasks[i].result = result.output;
        tasks[i].error = result.error;
        // Issue 28: 收集子任务 token 统计
        last_sub_input_tokens_ += result.input_tokens;
        last_sub_output_tokens_ += result.output_tokens;
        spdlog::info("[Orchestrator] {} {}: {} 字", tasks[i].id, tasks[i].status, tasks[i].result.size());
        // A3: 日志子任务轨迹摘要（LLM 调用数/工具调用数/错误数）
        if (!result.trace_summary.empty() && result.trace_summary != "null") {
            spdlog::debug("[Orchestrator] {} 轨迹: {}", tasks[i].id, result.trace_summary);
        }
    }
}

std::string AgentOrchestrator::synthesize(const std::vector<SubTask>& tasks)
{
    // P1 改进：委托 ISynthesisStrategy，可替换汇总方式
    return getSynthesis().synthesize(tasks, "");
}

} // namespace agent
