// Memory 类单元测试 — 覆盖 IMemory 接口所有方法的正常路径和边界情况。

#include "agent/context/Memory.h"
#include "agent/context/IMemory.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() \
    do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) \
    do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) \
    do { if (!(cond)) { FAIL(#cond); } } while(0)

// =========================================================================
// inject() — 核心注入口
// =========================================================================

void test_inject_system_role() {
    TEST("inject — System 角色存入 system_prompt_");
    llm::Memory mem;
    mem.inject(llm::Message::system("你是助手"));
    CHECK(mem.systemPrompt() == "你是助手");
    CHECK(mem.messages().empty());
    CHECK(mem.size() == 1);
    PASS();
}

void test_inject_non_system_roles() {
    TEST("inject — User/Assistant/Tool 追加到 messages_");
    llm::Memory mem;
    mem.inject(llm::Message::user("你好"));
    mem.inject(llm::Message::assistant("你好！"));
    mem.inject(llm::Message::toolResult("id1", "结果"));
    CHECK(mem.messages().size() == 3);
    CHECK(mem.messages()[0].role == llm::MessageRole::User);
    CHECK(mem.messages()[1].role == llm::MessageRole::Assistant);
    CHECK(mem.messages()[2].role == llm::MessageRole::Tool);
    CHECK(mem.systemPrompt().empty());
    PASS();
}

void test_inject_order() {
    TEST("inject — 多次注入保持顺序");
    llm::Memory mem;
    mem.inject(llm::Message::user("A"));
    mem.inject(llm::Message::user("B"));
    mem.inject(llm::Message::user("C"));
    CHECK(mem.messages()[0].content == "A");
    CHECK(mem.messages()[1].content == "B");
    CHECK(mem.messages()[2].content == "C");
    PASS();
}

// =========================================================================
// injectSystemPrompt()
// =========================================================================

void test_inject_system_prompt() {
    TEST("injectSystemPrompt — 设置和替换");
    llm::Memory mem;
    mem.injectSystemPrompt("v1");
    CHECK(mem.systemPrompt() == "v1");
    mem.injectSystemPrompt("v2");
    CHECK(mem.systemPrompt() == "v2");
    CHECK(mem.messages().empty());
    PASS();
}

void test_inject_system_prompt_empty() {
    TEST("injectSystemPrompt — 空字符串清除");
    llm::Memory mem;
    mem.injectSystemPrompt("有内容");
    mem.injectSystemPrompt("");
    CHECK(mem.systemPrompt().empty());
    CHECK(mem.empty());
    PASS();
}

// =========================================================================
// apply(MemoryDiff) — 批量原子修改
// =========================================================================

void test_apply_adds_messages() {
    TEST("apply — 追加多条消息");
    llm::Memory mem;
    mem.addUser("已有");
    llm::MemoryDiff diff;
    diff.added.push_back(llm::Message::assistant("回复1"));
    diff.added.push_back(llm::Message::assistant("回复2"));
    mem.apply(std::move(diff));
    CHECK(mem.messages().size() == 3);
    CHECK(mem.messages()[1].content == "回复1");
    CHECK(mem.messages()[2].content == "回复2");
    PASS();
}

void test_apply_pins_indices() {
    TEST("apply — pinned_indices 自动 pin");
    llm::Memory mem;
    llm::MemoryDiff diff;
    diff.added.push_back(llm::Message::user("msg0"));
    diff.added.push_back(llm::Message::user("msg1"));
    diff.added.push_back(llm::Message::user("msg2"));
    diff.pinned_indices = {0, 2};  // pin diff 中的第 0 和第 2 条
    mem.apply(std::move(diff));
    auto pinned = mem.pinnedIndices();
    CHECK(pinned.size() == 2);
    CHECK(pinned[0] == 0);
    CHECK(pinned[1] == 2);
    PASS();
}

void test_apply_empty_diff() {
    TEST("apply — 空 diff 无操作");
    llm::Memory mem;
    mem.addUser("保持");
    llm::MemoryDiff diff;
    mem.apply(std::move(diff));
    CHECK(mem.messages().size() == 1);
    PASS();
}

void test_apply_retryable_flag() {
    TEST("apply — retryable 标志不影响消息");
    llm::Memory mem;
    llm::MemoryDiff diff;
    diff.added.push_back(llm::Message::toolResult("id", "错误"));
    diff.retryable = true;
    mem.apply(std::move(diff));
    CHECK(mem.messages().size() == 1);
    CHECK(mem.messages()[0].content == "错误");
    PASS();
}

// =========================================================================
// prepend()
// =========================================================================

void test_prepend_message() {
    TEST("prepend — 头部插入消息");
    llm::Memory mem;
    mem.addUser("第二条");
    mem.prepend(llm::Message::user("第一条"));
    CHECK(mem.messages().size() == 2);
    CHECK(mem.messages()[0].content == "第一条");
    CHECK(mem.messages()[1].content == "第二条");
    PASS();
}

void test_prepend_system() {
    TEST("prepend — System 角色覆盖 system_prompt_");
    llm::Memory mem;
    mem.injectSystemPrompt("旧");
    mem.prepend(llm::Message::system("新"));
    CHECK(mem.systemPrompt() == "新");
    PASS();
}

// =========================================================================
// checkpoint() / restore()
// =========================================================================

void test_checkpoint_restore_roundtrip() {
    TEST("checkpoint/restore — 快照回滚往返");
    llm::Memory mem;
    mem.injectSystemPrompt("系统");
    mem.addUser("消息1");
    mem.addAssistant("回复1");

    auto snap = mem.checkpoint();

    mem.addUser("消息2");
    mem.injectSystemPrompt("被修改");
    CHECK(mem.size() == 4);

    mem.restore(snap);
    CHECK(mem.size() == 3);
    CHECK(mem.systemPrompt() == "系统");
    CHECK(mem.messages().size() == 2);
    CHECK(mem.messages()[0].content == "消息1");
    PASS();
}

void test_restore_then_continue() {
    TEST("restore — 恢复后继续 inject 正常");
    llm::Memory mem;
    mem.addUser("A");
    auto snap = mem.checkpoint();
    mem.addUser("B");
    mem.restore(snap);
    mem.addUser("C");
    CHECK(mem.messages().size() == 2);
    CHECK(mem.messages()[0].content == "A");
    CHECK(mem.messages()[1].content == "C");
    PASS();
}

// =========================================================================
// truncateTo()
// =========================================================================

void test_truncate_basic() {
    TEST("truncateTo — 保留前 N 条");
    llm::Memory mem;
    mem.addUser("A");
    mem.addUser("B");
    mem.addUser("C");
    mem.truncateTo(2);
    CHECK(mem.messages().size() == 2);
    CHECK(mem.messages()[0].content == "A");
    CHECK(mem.messages()[1].content == "B");
    PASS();
}

void test_truncate_zero() {
    TEST("truncateTo(0) — 清空全部");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    mem.truncateTo(0);
    CHECK(mem.empty());
    PASS();
}

void test_truncate_with_system_prompt() {
    TEST("truncateTo — 含 system prompt 计数");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    mem.addUser("B");
    // size = 3 (system + 2 messages), truncateTo(2) 保留 system + A
    mem.truncateTo(2);
    CHECK(mem.systemPrompt() == "sys");
    CHECK(mem.messages().size() == 1);
    CHECK(mem.messages()[0].content == "A");
    PASS();
}

void test_truncate_noop() {
    TEST("truncateTo — 大于 size 无操作");
    llm::Memory mem;
    mem.addUser("A");
    mem.truncateTo(100);
    CHECK(mem.messages().size() == 1);
    PASS();
}

// =========================================================================
// removeOldest()
// =========================================================================

void test_remove_oldest_basic() {
    TEST("removeOldest — 删除头部 N 条");
    llm::Memory mem;
    mem.addUser("A");
    mem.addUser("B");
    mem.addUser("C");
    mem.removeOldest(2);
    CHECK(mem.messages().size() == 1);
    CHECK(mem.messages()[0].content == "C");
    PASS();
}

void test_remove_oldest_all() {
    TEST("removeOldest — count >= size 清空（system 保留）");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    mem.addUser("B");
    mem.removeOldest(10);
    CHECK(mem.messages().empty());
    CHECK(mem.systemPrompt() == "sys");
    PASS();
}

void test_remove_oldest_zero() {
    TEST("removeOldest(0) — 无操作");
    llm::Memory mem;
    mem.addUser("A");
    mem.removeOldest(0);
    CHECK(mem.messages().size() == 1);
    PASS();
}

// =========================================================================
// edit()
// =========================================================================

void test_edit_user_message() {
    TEST("edit — 编辑 User 消息");
    llm::Memory mem;
    mem.addUser("旧内容");
    CHECK(mem.edit(0, "新内容"));
    CHECK(mem.messages()[0].content == "新内容");
    PASS();
}

void test_edit_assistant_message() {
    TEST("edit — 编辑 Assistant 消息");
    llm::Memory mem;
    mem.addAssistant("旧");
    CHECK(mem.edit(0, "新"));
    CHECK(mem.messages()[0].content == "新");
    PASS();
}

void test_edit_tool_message_fails() {
    TEST("edit — Tool 消息不允许编辑");
    llm::Memory mem;
    mem.addToolResult("id", "结果");
    CHECK(!mem.edit(0, "修改"));
    CHECK(mem.messages()[0].content == "结果");
    PASS();
}

void test_edit_system_index_fails() {
    TEST("edit — System 索引不允许编辑");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    CHECK(!mem.edit(0, "修改"));
    CHECK(mem.systemPrompt() == "sys");
    PASS();
}

void test_edit_out_of_range() {
    TEST("edit — 越界返回 false");
    llm::Memory mem;
    mem.addUser("A");
    CHECK(!mem.edit(99, "X"));
    PASS();
}

void test_edit_resets_preserved() {
    TEST("edit — 编辑后重置 preserved 标记");
    llm::Memory mem;
    mem.addUser("内容");
    mem.pin(0);
    CHECK(mem.pinnedIndices().size() == 1);
    mem.edit(0, "新内容");
    CHECK(mem.pinnedIndices().empty());
    PASS();
}

// =========================================================================
// pin() / unpin()
// =========================================================================

void test_pin_valid() {
    TEST("pin — 有效索引返回 true");
    llm::Memory mem;
    mem.addUser("A");
    mem.addUser("B");
    CHECK(mem.pin(0));
    CHECK(mem.pin(1));
    CHECK(mem.pinnedIndices().size() == 2);
    PASS();
}

void test_pin_out_of_range() {
    TEST("pin — 越界返回 false");
    llm::Memory mem;
    mem.addUser("A");
    CHECK(!mem.pin(5));
    PASS();
}

void test_pin_system_fails() {
    TEST("pin — System 索引返回 false");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    CHECK(!mem.pin(0));  // index 0 = system
    CHECK(mem.pin(1));   // index 1 = 第一条消息
    PASS();
}

void test_unpin_roundtrip() {
    TEST("unpin — pin 后 unpin 往返");
    llm::Memory mem;
    mem.addUser("A");
    mem.pin(0);
    CHECK(mem.pinnedIndices().size() == 1);
    mem.unpin(0);
    CHECK(mem.pinnedIndices().empty());
    PASS();
}

// =========================================================================
// at() / operator[]
// =========================================================================

void test_at_with_system() {
    TEST("at — 有 system prompt 时偏移");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    auto msg0 = mem.at(0);
    CHECK(msg0.role == llm::MessageRole::System);
    CHECK(msg0.content == "sys");
    auto msg1 = mem.at(1);
    CHECK(msg1.role == llm::MessageRole::User);
    CHECK(msg1.content == "A");
    PASS();
}

void test_at_without_system() {
    TEST("at — 无 system prompt 时直接索引");
    llm::Memory mem;
    mem.addUser("A");
    mem.addUser("B");
    CHECK(mem.at(0).content == "A");
    CHECK(mem.at(1).content == "B");
    PASS();
}

void test_operator_bracket() {
    TEST("operator[] — 委托到 at()");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    CHECK(mem[0].role == llm::MessageRole::System);
    CHECK(mem[1].content == "A");
    PASS();
}

// =========================================================================
// all()
// =========================================================================

void test_all_with_system() {
    TEST("all — 含 system prompt");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    auto all = mem.all();
    CHECK(all.size() == 2);
    CHECK(all[0].role == llm::MessageRole::System);
    CHECK(all[1].role == llm::MessageRole::User);
    PASS();
}

void test_all_without_system() {
    TEST("all — 无 system prompt");
    llm::Memory mem;
    mem.addUser("A");
    auto all = mem.all();
    CHECK(all.size() == 1);
    CHECK(all[0].role == llm::MessageRole::User);
    PASS();
}

void test_all_empty() {
    TEST("all — 空 Memory 返回空 vector");
    llm::Memory mem;
    CHECK(mem.all().empty());
    PASS();
}

// =========================================================================
// size() / empty()
// =========================================================================

void test_size_with_system() {
    TEST("size — 含 system prompt 时 +1");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    CHECK(mem.size() == 1);
    mem.addUser("A");
    CHECK(mem.size() == 2);
    PASS();
}

void test_size_without_system() {
    TEST("size — 无 system prompt 仅计 messages");
    llm::Memory mem;
    CHECK(mem.size() == 0);
    mem.addUser("A");
    CHECK(mem.size() == 1);
    PASS();
}

void test_empty() {
    TEST("empty — 两者都空才 true");
    llm::Memory mem;
    CHECK(mem.empty());
    mem.injectSystemPrompt("sys");
    CHECK(!mem.empty());
    mem.clear();
    CHECK(mem.empty());
    mem.addUser("A");
    CHECK(!mem.empty());
    PASS();
}

// =========================================================================
// 便捷方法（通过 IMemory 接口）
// =========================================================================

void test_convenience_add_methods() {
    TEST("便捷方法 — addUser/addAssistant/addToolResult");
    llm::Memory mem;
    llm::IMemory& iface = mem;
    iface.addUser("用户");
    iface.addAssistant("助手");
    iface.addToolResult("id", "工具");
    CHECK(mem.messages().size() == 3);
    CHECK(mem.messages()[0].content == "用户");
    CHECK(mem.messages()[1].content == "助手");
    CHECK(mem.messages()[2].content == "工具");
    CHECK(mem.messages()[2].tool_call_id == "id");
    PASS();
}

void test_convenience_system_methods() {
    TEST("便捷方法 — setSystemPrompt/addSystem");
    llm::Memory mem;
    llm::IMemory& iface = mem;
    iface.setSystemPrompt("v1");
    CHECK(mem.systemPrompt() == "v1");
    iface.addSystem("v2");
    CHECK(mem.systemPrompt() == "v2");
    PASS();
}

void test_convenience_add_delegates() {
    TEST("便捷方法 — add() 委托到 inject()");
    llm::Memory mem;
    llm::IMemory& iface = mem;
    iface.add(llm::Message::user("通过add"));
    CHECK(mem.messages().size() == 1);
    CHECK(mem.messages()[0].content == "通过add");
    PASS();
}

void test_convenience_pin_delegates() {
    TEST("便捷方法 — pinMessage/unpinMessage/editMessage 委托");
    llm::Memory mem;
    llm::IMemory& iface = mem;
    iface.addUser("内容");
    CHECK(iface.pinMessage(0));
    CHECK(mem.pinnedIndices().size() == 1);
    CHECK(iface.unpinMessage(0));
    CHECK(mem.pinnedIndices().empty());
    CHECK(iface.editMessage(0, "新"));
    CHECK(mem.messages()[0].content == "新");
    PASS();
}

// =========================================================================
// popBack()
// =========================================================================

void test_pop_back() {
    TEST("popBack — 删除最后一条非 system 消息");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    mem.addUser("B");
    mem.popBack();
    CHECK(mem.messages().size() == 1);
    CHECK(mem.messages()[0].content == "A");
    CHECK(mem.systemPrompt() == "sys");
    PASS();
}

// =========================================================================
// 迭代器
// =========================================================================

void test_iterators() {
    TEST("迭代器 — begin/end 遍历 messages_（不含 system）");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    mem.addUser("B");
    int count = 0;
    std::string combined;
    for (const auto& msg : mem) {
        count++;
        combined += msg.content;
    }
    CHECK(count == 2);
    CHECK(combined == "AB");
    PASS();
}

// =========================================================================
// clear()
// =========================================================================

void test_clear() {
    TEST("clear — 清空全部状态");
    llm::Memory mem;
    mem.injectSystemPrompt("sys");
    mem.addUser("A");
    mem.pin(1);
    mem.clear();
    CHECK(mem.empty());
    CHECK(mem.size() == 0);
    CHECK(mem.systemPrompt().empty());
    CHECK(mem.pinnedIndices().empty());
    PASS();
}

// =========================================================================
// reserve()
// =========================================================================

void test_reserve() {
    TEST("reserve — 预分配不影响逻辑");
    llm::Memory mem;
    mem.reserve(100);
    CHECK(mem.empty());
    mem.addUser("A");
    CHECK(mem.size() == 1);
    PASS();
}

// =========================================================================
// main
// =========================================================================

int main() {
    std::cout << "=== test_memory ===\n";

    // inject
    test_inject_system_role();
    test_inject_non_system_roles();
    test_inject_order();

    // injectSystemPrompt
    test_inject_system_prompt();
    test_inject_system_prompt_empty();

    // apply
    test_apply_adds_messages();
    test_apply_pins_indices();
    test_apply_empty_diff();
    test_apply_retryable_flag();

    // prepend
    test_prepend_message();
    test_prepend_system();

    // checkpoint/restore
    test_checkpoint_restore_roundtrip();
    test_restore_then_continue();

    // truncateTo
    test_truncate_basic();
    test_truncate_zero();
    test_truncate_with_system_prompt();
    test_truncate_noop();

    // removeOldest
    test_remove_oldest_basic();
    test_remove_oldest_all();
    test_remove_oldest_zero();

    // edit
    test_edit_user_message();
    test_edit_assistant_message();
    test_edit_tool_message_fails();
    test_edit_system_index_fails();
    test_edit_out_of_range();
    test_edit_resets_preserved();

    // pin/unpin
    test_pin_valid();
    test_pin_out_of_range();
    test_pin_system_fails();
    test_unpin_roundtrip();

    // at/operator[]
    test_at_with_system();
    test_at_without_system();
    test_operator_bracket();

    // all
    test_all_with_system();
    test_all_without_system();
    test_all_empty();

    // size/empty
    test_size_with_system();
    test_size_without_system();
    test_empty();

    // 便捷方法
    test_convenience_add_methods();
    test_convenience_system_methods();
    test_convenience_add_delegates();
    test_convenience_pin_delegates();

    // popBack
    test_pop_back();

    // 迭代器
    test_iterators();

    // clear
    test_clear();

    // reserve
    test_reserve();

    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
