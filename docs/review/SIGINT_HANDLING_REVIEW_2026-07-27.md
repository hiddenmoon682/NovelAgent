# SIGINT 信号处理方式审查

> 审查日期：2026-07-27
> 涉及文件：`src/Bootstrap.h`
> 审查触发：编码审查中讨论 `signal()` vs 更优方案

---

## 一、当前实现

```cpp
inline std::atomic<bool>* g_cancel_flag = nullptr;

// SIGINT（Ctrl+C）信号处理函数。
// 不直接调用 exit()，而是设置取消标志让 Agent 主循环检查后自行清理退出。
extern "C" inline void sigint_handler(int) {
    if (g_cancel_flag) g_cancel_flag->store(true);
}

// 在 run() 中注册：
g_cancel_flag = ctx.app->agent().cancelFlag();
signal(SIGINT, sigint_handler);
```

### 设计思路
- 信号处理函数只做 **原子 bool 写入**，不调用任何非信号安全函数
- Agent 主循环在每轮迭代中检查该标志，实现优雅退出
- 避免 `exit()`/`abort()` 强制终止导致的数据损坏

### 当前存在的问题

| 问题 | 说明 |
|------|------|
| `signal()` 行为不可靠 | 某些 POSIX 实现收到信号后会重置为 `SIG_DFL`，需要重新注册；Windows 上 `signal(SIGINT)` 行为与 POSIX 有差异 |
| `extern "C" inline` 隐患 | 函数标识符可能在不同翻译单元产生多份拷贝，取其地址作为信号处理函数的行为未明确定义 |
| 无信号屏蔽控制 | `signal()` 无法控制处理期间是否需要屏蔽其他信号 |
| Windows 专用 API 未用 | Windows 提供了 `SetConsoleCtrlHandler`，比 `signal(SIGINT)` 更可靠（可捕获 CTRL_BREAK_EVENT/CTRL_CLOSE_EVENT，支持多个 handler 链） |
| 无 `SA_RESTART` | POSIX `sigaction()` 的 `SA_RESTART` 让被信号中断的系统调用自动恢复，避免 `EINTR` 错误 |

---

## 二、备选方案对比

### 方案 A：`SetConsoleCtrlHandler`（Windows）+ `sigaction`（POSIX）

| 维度 | 说明 |
|------|------|
| 优点 | 平台原生最佳实践；`SetConsoleCtrlHandler` 支持 CTRL_BREAK/CLOSE 事件；`sigaction` 支持 `SA_RESTART` 和信号屏蔽字 |
| 缺点 | 需要平台条件编译；代码量略增 |
| 推荐度 | ⭐⭐⭐⭐⭐ |

### 方案 B：`std::signal` + `volatile std::sig_atomic_t`

| 维度 | 说明 |
|------|------|
| 优点 | 纯 C++ 标准，无需平台 API；`sig_atomic_t` 保证信号上下文的原子写入 |
| 缺点 | `std::sig_atomic_t` 限于整数类型，无法传递指针；`signal()` 的跨平台行为差异仍在 |
| 推荐度 | ⭐⭐⭐ |

### 方案 C：C++20 `std::stop_source`/`std::stop_token`

| 维度 | 说明 |
|------|------|
| 优点 | 最现代的并发取消模型；与 `std::jthread` 天然集成 |
| 缺点 | C++20 未标准化信号到 `stop_source` 的桥接，最终还是绕不开平台信号 API |
| 推荐度 | ⭐⭐（适合与线程池结合，不适合直接处理信号） |

### 方案 D：保持当前实现不变

| 维度 | 说明 |
|------|------|
| 优点 | 简洁、跨平台、已在 `signal()` 上层封装了原子标志 |
| 缺点 | `extern "C" inline` 的地址问题；缺少 `SA_RESTART` 等高级控制 |
| 推荐度 | ⭐⭐⭐（够用但非最佳） |

---

## 三、建议改进方向

1. **Windows 平台改用 `SetConsoleCtrlHandler`** 替换 `signal(SIGINT)`，并移除 `extern "C" inline` 改用 `static` 函数
2. **POSIX 平台改用 `sigaction`** 替换 `signal()`，设置 `SA_RESTART` 和空信号屏蔽字
3. **保留 `g_cancel_flag` 架构不变**——原子标志 + 主循环轮询的模式是正确的，不需要改为 `sig_atomic_t`
4. 将信号处理函数定义移入 `Bootstrap.cpp`（如果将来拆出 `.cpp`），或确保 `inline` 函数在头文件中仅定义一次

---

## 四、决定

> **待定** — 当前实现可正常工作，改进优先级较低。
> 建议在下次涉及 Windows 控制台行为修改时一并整改。
