# 引入 yaml-cpp 替换手写技能 frontmatter 解析器

> 日期：2026-07-30
> 状态：方案已确认，待实施

## Context（背景）

NovelAgent 的技能元数据解析器 `SkillLoader::parseFrontmatter`（`src/agent/skill/SkillLoader.cpp:141`）是一个手写的 YAML 子集解析器，仅识别逐行 `key: value` 与块状数组。面对行业标准 SKILL.md 写法时存在缺陷：

- 多行块标量 `description: |` 会被解析成字面量 `|`，后续缩进内容丢失；
- 流式数组 `bins: [git, python]` 不支持（值被丢弃）；
- 引号剥离逻辑粗糙，可能误伤合法值。

引入成熟的 yaml-cpp 库可获得完整 YAML 兼容性与健壮性，使 NovelAgent 真正对齐 OpenCode 等业界 SKILL.md 约定。

**范围（已确认）**：
- 仅替换解析器，保持现有字段集（`name`/`description`/`emoji`/`always`/`required_bins`+`bins`/`required_envs`+`envs`/`os`/`commands`）与全部现有行为；
- 不扩展 `SkillMetadata` 结构，不新增 `allowed_tools`/`version` 等字段；
- 完全移除手写解析逻辑，不保留双轨回退。

## 构建系统现状（关键事实）

- 工具链：MSYS2 MinGW-w64 GCC 15.2 + Ninja（`CMakePresets.json`），C++20，CMake 3.24。
- 依赖管理统一在 `cmake/FetchDependencies.cmake`，模式为 `find_package(X QUIET)` + `FetchContent` 回退（nlohmann_json、spdlog 均如此）。
- 共享第三方库通过根 `CMakeLists.txt` 的 `COMMON_LIBS`（约 59-62 行）以 `PUBLIC` 链接到 `novelagent_core`，下游 target（tools/app/qt/gui/tests）全部传递继承 —— 这是 yaml-cpp 的接入点。
- WIN32 有 POST_BUILD DLL 拷贝步骤，硬编码 `NEEDED_DLLS` 列表（约 138-154 行）；若 yaml-cpp 为共享库需追加其 DLL。
- 用户以 MSYS2 管理包（MSYS2 提供 `mingw-w64-x86_64-yaml-cpp`，`find_package` 可命中）。

## 实施步骤

### 1. 添加 yaml-cpp 依赖 — `cmake/FetchDependencies.cmake`
镜像 spdlog/nlohmann_json 的现有模式：

```cmake
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_Declare(yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
        GIT_SHALLOW TRUE)
    set(YAML_CPP_BUILD_TESTS OFF CACHE INTERNAL "")
    set(YAML_CPP_BUILD_TOOLS OFF CACHE INTERNAL "")
    set(YAML_CPP_BUILD_CONTRIB OFF CACHE INTERNAL "")
    FetchContent_MakeAvailable(yaml-cpp)
endif()
```

- 固定 `0.8.0`（导出目标名为 `yaml-cpp::yaml-cpp`；旧版目标名为 `yaml-cpp`，需注意）。
- 实施时先用 `pacman -Ss yaml-cpp` 确认 MSYS2 包名/版本；已安装则 `find_package` 命中，否则走 FetchContent（默认静态）。

### 2. 链接到核心 — `CMakeLists.txt`
将 `yaml-cpp::yaml-cpp` 追加进 `COMMON_LIBS`，使 `novelagent_core` 及所有下游 target 继承。

### 3. DLL 部署 — `CMakeLists.txt`
首次构建后确认 yaml-cpp 是否解析为共享库；若是，将其 DLL（如 `libyaml-cpp.dll`，以实际名为准）追加到 `NEEDED_DLLS`。静态链接则无需改动。

### 4. 重写 parseFrontmatter — `src/agent/skill/SkillLoader.cpp`
- 在 `.cpp` 顶部 `#include <yaml-cpp/yaml.h>`。
- 新逻辑：读取文件 → 复用 `---` 扫描提取 frontmatter 文本块（首个 `---` 到次个 `---` 之间，首行经 `stripBom` 处理）→ `YAML::Load` 得到 `YAML::Node` → 按字段契约读取：
  - `name`：`as<std::string>("")`，空则回退目录名（保留现有兜底）；
  - `description` / `emoji`：`as<std::string>("")`；
  - `always`：`as<bool>(false)`；
  - `required_bins`：取 `required_bins` 或别名 `bins`，遍历 sequence → `vector<string>`；
  - `required_envs`：取 `required_envs` 或别名 `envs`；
  - `os_restrict`：取 `os`；
  - `commands`：sequence of map，读取 `name`/`description`，丢弃无名命令。
- 保留：`root_dir = parent_path`；文件不可打开时 `throw`（由 `discover` 捕获 → 跳过 + 告警）。
- CRLF、空行、引号、多行块标量、流式数组均由 yaml-cpp 原生处理。
- `ensureLoaded`（正文提取，非 YAML）**保持不变**。
- 删除原手写解析中不再需要的逐行状态机代码。

### 5. 头文件 — `src/agent/skill/SkillLoader.h`
`parseFrontmatter` 签名不变；`isBinaryAvailable`/`isEnvAvailable`/`currentOS`（供 `checkGating` 使用）保留。一般无需改动头文件。

### 6. 测试 — `tests/test_skill_registry.cpp`
现有用例必须全部保持绿色（不改断言）：
- `test_frontmatter_parse`（引号 emoji `"X"` → `X`）
- `test_progressive_context`（`always: true`）
- `test_parse_robustness`（空行 / 首行 BOM）
- `test_save_skill_injection`（消毒后的单行 description 不被拆分）
- `test_content_read_failure`（仅 name 的 frontmatter）
- `test_save_skill_tool`（写后重发现往返）

可选新增（低风险，展示新能力）：多行 `description: |` 与流式数组 `bins: [git, python]` 的解析用例。

## 受影响文件

| 文件 | 改动 |
|------|------|
| `cmake/FetchDependencies.cmake` | 新增 yaml-cpp find_package + FetchContent 块 |
| `CMakeLists.txt` | `COMMON_LIBS` 追加 `yaml-cpp::yaml-cpp`；视情况追加 `NEEDED_DLLS` |
| `src/agent/skill/SkillLoader.cpp` | 用 yaml-cpp 重写 `parseFrontmatter`，移除手写状态机 |
| `tests/test_skill_registry.cpp` | 可选新增多行/流式数组用例（现有不改） |

## 验证

1. 配置与构建：`cmake --preset default && cmake --build build`。
2. 运行技能测试：`ctest --test-dir build -R skill`（或直接跑 `test_skill_registry`），确认全部绿色。
3. 手工验证新能力：创建一个含 `description: |` 多行描述与 `bins: [git]` 流式数组的技能，确认解析正确（旧解析器会丢信息）。
4. 启动应用，确认内置 `create-skill` 技能正常加载并出现在 system prompt。
5. 若为共享库，确认 yaml-cpp DLL 已拷贝到 `novelagent_gui` 旁且应用可启动。

## 风险与备注

- **MSYS2 包名/版本**：实施时以 `pacman -Ss yaml-cpp` 实测为准；未安装则 FetchContent 回退（默认静态）。
- **目标名版本差异**：固定 0.8.0 以保证 `yaml-cpp::yaml-cpp` 目标名。
- **BOM**：验证 yaml-cpp 对 BOM 的容忍度；保留对提取出的 frontmatter 首行 `stripBom` 以稳妥。
- **`always` 语义轻微放宽**：旧解析器要求值严格等于 `"true"`；yaml-cpp `as<bool>` 还接受 `True`/`yes`。这是更正确的行为，现有测试仅用 `always: true`，不受影响。
