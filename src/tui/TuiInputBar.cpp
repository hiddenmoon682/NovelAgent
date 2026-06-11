/// TuiInputBar 实现 — FTXUI Input 组件包装。

#include "tui/TuiInputBar.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

using namespace ftxui;

TuiInputBar::TuiInputBar() = default;

void TuiInputBar::onSubmit(SubmitCallback cb) {
    on_submit_ = std::move(cb);
}

void TuiInputBar::clear() {
    input_content_.clear();
}

void TuiInputBar::setPlaceholder(const std::string& text) {
    placeholder_ = text;
}

void TuiInputBar::addToHistory(const std::string& line) {
    if (line.empty()) return;
    if (!history_.empty() && history_.back() == line) return;
    history_.push_back(line);
    if (history_.size() > 100) history_.pop_front();
    history_index_ = -1;
}

Component TuiInputBar::render() {
    InputOption opt;
    opt.multiline = false;
    opt.on_enter = [this] {
        if (input_content_.empty()) return;
        addToHistory(input_content_);
        if (on_submit_) on_submit_(input_content_);
        input_content_.clear();
    };

    auto input = Input(&input_content_, placeholder_, opt);

    // 包装历史浏览：↑ 上一条  ↓ 下一条
    return CatchEvent(input, [this](Event event) {
        if (event == Event::ArrowUp) {
            if (!history_.empty() && history_index_ < (int)history_.size() - 1) {
                history_index_++;
                input_content_ = history_[history_.size() - 1 - history_index_];
            }
            return true;
        }
        if (event == Event::ArrowDown) {
            if (history_index_ > 0) {
                history_index_--;
                input_content_ = history_[history_.size() - 1 - history_index_];
            } else if (history_index_ == 0) {
                history_index_ = -1;
                input_content_.clear();
            }
            return true;
        }
        return false;
    });
}
