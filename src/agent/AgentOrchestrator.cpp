/// AgentOrchestrator 实现 — P1 重构版（ISynthesisStrategy + RestrictedToolProvider）+ Phase 4 线程安全（工厂模式 SubAgent 隔离）。

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
    synthesis_ = std::make_unique<LlmSynthesis>(*client_, main_prompt_);
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
    if (!parallel_enabled_ || !getDetector().shouldParallelize(input)) {
        std::vector<llm::Message> msgs = { llm::Message::user(input) };
        auto resp = client_->chatNonStreaming(msgs, {}, main_prompt_);
        return resp.content;
    }

    spdlog::info("[Orchestrator] 并行编排: {}", input.substr(0, 60));

    auto tasks = decompose(input);
    if (tasks.empty()) {
        std::vector<llm::Message> msgs = { llm::Message::user(input) };
        return client_->chatNonStreaming(msgs, {}, main_prompt_).content;
    }

    spdlog::info("[Orchestrator] {} 个子任务", tasks.size());
    executeParallel(tasks);
    return synthesize(tasks);
}

std::vector<SubTask> AgentOrchestrator::decompose(const std::string& input)
{
    return getDecomposition().decompose(input, main_prompt_);
}

void AgentOrchestrator::executeParallel(std::vector<SubTask>& tasks)
{
    std::vector<std::future<SubAgentResult>> futures(tasks.size());
    std::vector<bool> consumed(tasks.size(), false);  // 追踪已被节流循环消费的 future
    int running = 0;

    for (size_t i = 0; i < tasks.size(); ++i) {
        // ── 节流控制：当并发数达到上限时，等待任一任务完成 ──
        while (running >= max_parallel_) {
            for (size_t j = 0; j < i; ++j) {
                if (!consumed[j] && futures[j].valid() &&
                    futures[j].wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
                    // 提前收集已完成任务的结果，避免最终循环重复 get()
                    auto result = futures[j].get();
                    consumed[j] = true;
                    tasks[j].status = result.timed_out ? "timed_out" :
                                      !result.error.empty() ? "failed" : "completed";
                    tasks[j].result = result.output;
                    tasks[j].error = result.error;
                    --running; break;
                }
            }
            // 避免忙等待——在轮询间隙让出 CPU
            if (running >= max_parallel_) {
                std::this_thread::yield();
            }
        }

        futures[i] = std::async(std::launch::async, [this, task = tasks[i]]() {
            SubAgentConfig config;
            config.task = task.description;
            config.system_prompt = task.system_prompt;
            config.allowed_tools = task.allowed_tools;
            config.timeout = std::chrono::seconds(120);
            config.max_tool_rounds = 3;

            // P0 改进：SubAgent 通过 RestrictedToolProvider 受限视图访问工具
            RestrictedToolProvider tools(registry_, task.allowed_tools);
            auto agent = agent_factory_(factory_, tools);
            return agent->execute(config);
        });
        ++running;
    }

    // ── 最终收集：跳过已被节流循环消费的 future ──
    for (size_t i = 0; i < futures.size(); ++i) {
        if (consumed[i]) {
            spdlog::info("[Orchestrator] {} {}: {} 字", tasks[i].id, tasks[i].status, tasks[i].result.size());
            continue;
        }
        auto result = futures[i].get();
        tasks[i].status = result.timed_out ? "timed_out" :
                          !result.error.empty() ? "failed" : "completed";
        tasks[i].result = result.output;
        tasks[i].error = result.error;
        spdlog::info("[Orchestrator] {} {}: {} 字", tasks[i].id, tasks[i].status, tasks[i].result.size());
    }
}

std::string AgentOrchestrator::synthesize(const std::vector<SubTask>& tasks)
{
    // P1 改进：委托 ISynthesisStrategy，可替换汇总方式
    return getSynthesis().synthesize(tasks, "");
}

} // namespace agent
