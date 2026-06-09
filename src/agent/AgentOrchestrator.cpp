#include "agent/AgentOrchestrator.h"
#include "agent/TemplateManager.h"
#include "agent/ToolRegistry.h"

#include <spdlog/spdlog.h>
#include <future>
#include <sstream>

namespace agent {

AgentOrchestrator::AgentOrchestrator(
    llm::ILLMClient& client, ToolRegistry& registry, std::string mainPrompt)
    : client_(client), registry_(registry), main_prompt_(std::move(mainPrompt))
{
}

std::string AgentOrchestrator::processMessage(const std::string& input)
{
    // 简单启发式：包含"检查所有"、"每个"等关键词 → 尝试并行
    bool should_parallel = parallel_enabled_ &&
        (input.find("所有") != std::string::npos ||
         input.find("检查") != std::string::npos ||
         input.find("分析") != std::string::npos);

    if (!should_parallel) {
        // 普通模式：直接让主 LLM 回答
        std::vector<llm::Message> msgs = { llm::Message::user(input) };
        auto resp = client_.chatNonStreaming(msgs, {}, main_prompt_);
        return resp.content;
    }

    spdlog::info("[Orchestrator] 尝试并行编排: {}", input.substr(0, 60));

    // 1. 分解任务
    auto tasks = decompose(input);
    if (tasks.empty()) {
        // 分解失败 → 回退到普通模式
        std::vector<llm::Message> msgs = { llm::Message::user(input) };
        auto resp = client_.chatNonStreaming(msgs, {}, main_prompt_);
        return resp.content;
    }

    spdlog::info("[Orchestrator] 分解为 {} 个子任务", tasks.size());

    // 2. 并行执行
    executeParallel(tasks);

    // 3. 汇总结果
    return synthesize(tasks);
}

std::vector<SubTask> AgentOrchestrator::decompose(const std::string& input)
{
    // 简化版：基于已有章节直接创建子任务
    // Phase 4 将改为让 LLM 分析输入并生成子任务 JSON
    // 当前版本：如果 TemplateManager 可用，使用模板驱动的分解

    if (template_mgr_) {
        // 尝试用模板匹配任务类型
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

    // 无模板时：创建简单的只读检查子任务
    std::vector<SubTask> tasks;
    SubTask st;
    st.id = "sub-1";
    st.description = input;
    st.system_prompt = main_prompt_;
    st.allowed_tools = {"read_chapter", "get_character", "get_outline", "get_setting"};
    st.status = "pending";
    tasks.push_back(std::move(st));

    return tasks;
}

void AgentOrchestrator::executeParallel(std::vector<SubTask>& tasks)
{
    // 使用索引追踪：futures 与 task 下标一一对应
    std::vector<std::future<SubAgentResult>> futures(tasks.size());
    int running = 0;  // 当前正在运行的子任务数

    for (size_t i = 0; i < tasks.size(); ++i) {
        // 节流：等待直到有空闲槽位
        while (running >= max_parallel_) {
            for (size_t j = 0; j < i; ++j) {
                if (futures[j].valid() &&
                    futures[j].wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
                    futures[j].get();
                    --running;
                    break;
                }
            }
        }

        futures[i] = std::async(std::launch::async, [this, task = tasks[i]]() {
            SubAgentConfig config;
            config.task = task.description;
            config.system_prompt = task.system_prompt;
            config.allowed_tools = task.allowed_tools;
            config.timeout = std::chrono::seconds(120);
            config.max_tool_rounds = 3;

            SubAgent agent(client_, registry_);
            return agent.execute(config);
        });
        ++running;
    }

    // 收集所有结果
    for (size_t i = 0; i < futures.size(); ++i) {
        auto result = futures[i].get();
        tasks[i].status = result.timed_out ? "timed_out" :
                          !result.error.empty() ? "failed" : "completed";
        tasks[i].result = result.output;
        tasks[i].error = result.error;
        spdlog::info("[Orchestrator] 子任务 {}: {} (输出{}字)",
                     tasks[i].id, tasks[i].status, tasks[i].result.size());
    }
}

std::string AgentOrchestrator::synthesize(const std::vector<SubTask>& tasks)
{
    // 汇总结果 → 发给主 LLM 做摘要
    std::ostringstream summary;
    summary << "子任务执行结果汇总:\n\n";
    for (const auto& t : tasks) {
        summary << "## " << t.id << " [" << t.status << "]\n";
        if (!t.error.empty()) {
            summary << "错误: " << t.error << "\n";
        }
        if (!t.result.empty()) {
            // 截断过长结果
            std::string r = t.result;
            if (r.size() > 800) r = r.substr(0, 800) + "...(已截断)";
            summary << r << "\n\n";
        }
    }

    summary << "请用中文汇总以上子任务的执行结果。";

    std::vector<llm::Message> msgs = { llm::Message::user(summary.str()) };
    auto resp = client_.chatNonStreaming(msgs, {}, main_prompt_);
    return resp.content;
}

} // namespace agent
