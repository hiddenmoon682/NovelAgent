// ShellTools 白名单拦截测试（C1/C2）。
// 通过 RunPowerShellTool::execute() 的黑盒返回值 {blocked: true} 验证拦截行为，
// 不依赖 isAllowedCommand 的内部可见性（它在匿名命名空间内）。
// 重点验证 A18 审查发现的绕过向量：foreach-object 脚本块、{} / ; / & 注入字符。

#include "agent/tools/ShellTools.h"

#include <iostream>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; std::cout << "  TEST " << name << " ... "; } while(0)
#define PASS() do { tests_passed++; std::cout << "PASSED\n"; } while(0)
#define FAIL(msg) do { std::cout << "FAILED: " << msg << "\n"; return; } while(0)
#define CHECK(cond) do { if (!(cond)) { FAIL(#cond); } } while(0)

using json = nlohmann::json;

// 执行命令并返回是否被白名单拦截（blocked 字段）。
static bool isBlocked(const std::string& cmd) {
    agent::RunPowerShellTool tool;
    auto r = tool.execute({{"command", cmd}});
    return r.value("blocked", false);
}

// =========================================================================
// 测试 1: 只读查询命令放行（不被拦截）
// =========================================================================

void test_safe_readonly_allowed() {
    TEST("白名单放行 — 只读查询命令不拦截");
    // 注意：这里只验证"未被白名单拦截"，不验证执行结果（执行依赖 PowerShell 环境）
    CHECK(!isBlocked("Get-ChildItem"));
    CHECK(!isBlocked("Get-Content config.json"));
    CHECK(!isBlocked("Select-String -Pattern foo file.txt"));
    CHECK(!isBlocked("Get-Location"));
    CHECK(!isBlocked("Test-Path chapters/ch-001.md"));
    PASS();
}

// =========================================================================
// 测试 2: 写/执行类 cmdlet 拦截
// =========================================================================

void test_write_cmdlets_blocked() {
    TEST("白名单拦截 — 写/执行类 cmdlet");
    CHECK(isBlocked("Remove-Item chapters/ch-001.md"));
    CHECK(isBlocked("Set-Content file.txt 'x'"));
    CHECK(isBlocked("New-Item -ItemType File x.txt"));
    CHECK(isBlocked("Copy-Item a.txt b.txt"));
    CHECK(isBlocked("Move-Item a.txt b.txt"));
    CHECK(isBlocked("Invoke-Expression 'rm -rf'"));
    CHECK(isBlocked("Start-Process notepad"));
    PASS();
}

// =========================================================================
// 测试 3: 脚本注入字符拦截（C2 收紧重点：{}/`/;/&/$(
// =========================================================================

void test_injection_chars_blocked() {
    TEST("白名单拦截 — 脚本注入字符 { } ; & $( `");
    // 脚本块——foreach-object 已移出白名单，且 {} 本身被拦截
    CHECK(isBlocked("Get-ChildItem | ForEach-Object { Remove-Item $_ }"));
    // 即使 cmdlet 在白名单，{} 脚本块也必须拦截
    CHECK(isBlocked("Get-Content { malicious }"));
    // 命令分隔符 ; —— 逃逸逐段校验的向量
    CHECK(isBlocked("Get-ChildItem; Remove-Item x"));
    // 后台执行 &
    CHECK(isBlocked("Get-ChildItem & del x"));
    // 子命令替换 $(
    CHECK(isBlocked("Get-Content $(Remove-Item x)"));
    // 反引号
    CHECK(isBlocked("Get-Content `malicious"));
    PASS();
}

// =========================================================================
// 测试 4: 管道组合——每段都必须在白名单
// =========================================================================

void test_pipeline_all_segments_checked() {
    TEST("管道组合 — 每段都必须在白名单");
    // 两段都只读 → 放行
    CHECK(!isBlocked("Get-ChildItem | Select-String foo"));
    CHECK(!isBlocked("Get-Content x | Sort-Object"));
    // 首段合法、次段非法 → 拦截
    CHECK(isBlocked("Get-ChildItem | Remove-Item"));
    // 首段非法 → 拦截
    CHECK(isBlocked("Remove-Item | Get-ChildItem"));
    PASS();
}

// =========================================================================
// 测试 5: 空命令拦截
// =========================================================================

void test_empty_command_blocked() {
    TEST("空命令拦截");
    agent::RunPowerShellTool tool;
    auto r = tool.execute({{"command", ""}});
    CHECK(r.contains("error"));
    PASS();
}

// =========================================================================
// 测试 6: 扩展白名单新增的 cmdlet 放行
// =========================================================================

void test_expanded_whitelist() {
    TEST("扩展白名单 — 新增只读 cmdlet 放行");
    // Get-Process / Get-Service / Get-ItemProperty / Get-Acl / Get-Member
    CHECK(!isBlocked("Get-Process"));
    CHECK(!isBlocked("Get-Service"));
    CHECK(!isBlocked("Get-ItemProperty -Path .\\config.json"));
    CHECK(!isBlocked("Get-Acl .\\file.txt"));
    CHECK(!isBlocked("Get-Member -InputObject x"));
    // Write-Output / Write-Host
    CHECK(!isBlocked("Write-Output 'hello'"));
    CHECK(!isBlocked("Write-Host 'done'"));
    // 别名
    CHECK(!isBlocked("ps"));        // Get-Process
    CHECK(!isBlocked("echo hello"));
    CHECK(!isBlocked("gp .\\path")); // Get-ItemProperty
    PASS();
}

// =========================================================================

int main() {
    std::cout << "=== test_shell_tools ===\n\n";
    test_safe_readonly_allowed();
    test_write_cmdlets_blocked();
    test_injection_chars_blocked();
    test_pipeline_all_segments_checked();
    test_empty_command_blocked();
    test_expanded_whitelist();
    std::cout << "\n" << tests_passed << "/" << tests_run << " 测试通过\n";
    return (tests_passed == tests_run) ? 0 : 1;
}
