/// ISynthesisStrategy 实现。

#include "agent/ISynthesisStrategy.h"
#include "llm/ILLMClient.h"

#include <sstream>

namespace agent {

// ===========================================================================
// LlmSynthesis
// ===========================================================================

LlmSynthesis::LlmSynthesis(
    llm::ILLMClient& client, std::string main_prompt, int max_result_chars)
    : client_(client), main_prompt_(std::move(main_prompt))
    , max_result_chars_(max_result_chars)
{}

std::string LlmSynthesis::synthesize(
    const std::vector<SubTask>& tasks, const std::string& /*original_query*/)
{
    std::ostringstream summary;
    summary << "子任务执行结果汇总:\n\n";

    for (const auto& t : tasks) {
        summary << "## " << t.id << " [" << t.status << "]\n";
        if (!t.error.empty()) summary << "错误: " << t.error << "\n";
        if (!t.result.empty()) {
            std::string r = t.result;
            if (static_cast<int>(r.size()) > max_result_chars_)
                r = r.substr(0, max_result_chars_) + "...(已截断)";
            summary << r << "\n\n";
        }
    }

    summary << "请用中文汇总以上子任务的执行结果。";
    std::vector<llm::Message> msgs = { llm::Message::user(summary.str()) };
    auto resp = client_.chatNonStreaming(msgs, {}, main_prompt_);
    return resp.content;
}

// ===========================================================================
// ConcatSynthesis
// ===========================================================================

std::string ConcatSynthesis::synthesize(
    const std::vector<SubTask>& tasks, const std::string& /*original_query*/)
{
    std::ostringstream ss;
    for (const auto& t : tasks) {
        ss << "[" << t.id << ":" << t.status << "] ";
        if (!t.result.empty()) ss << t.result;
        if (!t.error.empty()) ss << " (错误: " << t.error << ")";
        ss << "\n";
    }
    return ss.str();
}

} // namespace agent
