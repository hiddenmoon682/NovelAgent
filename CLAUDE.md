# NovelAgent — C++20 CLI 写小说 Agent

## 项目规则

### 注释语言
- **所有注释、docstring、文档说明必须使用中文。**
- 包括：文件头注释、函数说明、行内注释、TODO/FIXME/HACK 标记、README 中的说明文字。
- 代码标识符（变量名、函数名、类型名）仍使用英文。
- Git 提交信息使用中文。

### 代码风格
- C++20, CMake 构建, `nlohmann/json` + `spdlog` + `CLI11`。
- 头文件使用 `#pragma once`。
- 命名空间：`utils::file`, `utils::string` 等。

### 工作流
- 每 Phase 结束后：更新 CHANGELOG → git commit → git push。
