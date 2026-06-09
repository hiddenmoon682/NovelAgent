/// StreamDisplay 实现 — Phase 5 增强版。

#include "cli/StreamDisplay.h"

llm::StreamCallbacks StreamDisplay::create(IOutputChannel& out,
                                            bool show_token_stats) {
    llm::StreamCallbacks cb;

    cb.on_content = [&out](const std::string& delta) {
        out.write(Ansi::assistant() + delta + Ansi::reset());
    };

    cb.on_reasoning = [&out](const std::string& delta) {
        out.write(Ansi::thinking() + delta + Ansi::reset());
    };

    cb.on_tool_call_start = [&out]() {
        out.write("\n  " + Ansi::toolCall() + "[工具调用] " + Ansi::reset());
    };

    cb.on_complete = [&out, show_token_stats](const llm::LLMResponse& resp) {
        if (show_token_stats && resp.total_tokens > 0) {
            out.write("\n  " + Ansi::dim() + Ansi::fgBrightBlack()
                       + "(" + std::to_string(resp.total_tokens) + " tokens)"
                       + Ansi::reset());
        }
    };

    cb.on_error = [&out](const std::string& err) {
        out.write("\n  " + Ansi::error() + "错误: " + err + Ansi::reset() + "\n");
    };

    return cb;
}

llm::StreamCallbacks StreamDisplay::createWithState(
    IOutputChannel& out,
    std::function<void()> on_content_start,
    std::function<void()> on_tool_start,
    std::function<void()> on_complete)
{
    auto cb = create(out, true);

    // 包装原有回调，注入状态信号
    auto orig_content = cb.on_content;
    cb.on_content = [orig_content, on_content_start](const std::string& delta) {
        on_content_start();
        if (orig_content) orig_content(delta);
    };

    auto orig_tool = cb.on_tool_call_start;
    cb.on_tool_call_start = [orig_tool, on_tool_start]() {
        on_tool_start();
        if (orig_tool) orig_tool();
    };

    auto orig_complete = cb.on_complete;
    cb.on_complete = [orig_complete, on_complete](const llm::LLMResponse& resp) {
        on_complete();
        if (orig_complete) orig_complete(resp);
    };

    return cb;
}
