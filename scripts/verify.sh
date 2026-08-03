#!/usr/bin/env bash
#
# 机械验证门：构建 + 聚焦测试 + 失败诊断，一条命令完成改动验证。
#
# 用法：
#   ./scripts/verify.sh                 # 全量构建 + 全量测试（临近交付/推送前）
#   ./scripts/verify.sh <模块名>         # 全量构建 + 仅跑覆盖该模块的测试（默认做法）
#   ./scripts/verify.sh --build         # 仅构建，不跑测试
#   ./scripts/verify.sh --list          # 列出全部测试名
#
# 说明：
#   - 聚焦验证通过 `ctest -R <正则>` 按测试名过滤，只跑与本次改动相关的用例。
#   - 失败时保留测试输出（--output-on-failure），供诊断与修复后复验。
#   - 退出码：0=全部通过；1=构建/测试失败（供 CI 或回归门直接消费）。
set -euo pipefail

# 项目根目录（脚本所在目录的上一级）
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# 聚焦验证：按模块名匹配测试（test_<module>.cpp 对应的 ctest 名）。
# 传入的模块名会作为正则片段，因此可以传 test_models、models、context 等。
run_focused_tests() {
    local pattern="$1"
    echo "==> 聚焦验证：ctest -R \"${pattern}\""
    (cd "$BUILD_DIR" && ctest -R "$pattern" --output-on-failure)
}

run_full_tests() {
    echo "==> 全量回归：ctest"
    (cd "$BUILD_DIR" && ctest --output-on-failure)
}

build() {
    echo "==> 构建：cmake --build --preset default"
    (cd "$PROJECT_ROOT" && cmake --build --preset default)
}

list_tests() {
    (cd "$BUILD_DIR" && ctest -N)
}

# 无参数 / 无匹配参数关键字 → 全量构建 + 全量测试
case "${1:-}" in
    --build)
        build
        ;;
    --list)
        list_tests
        ;;
    --help|-h)
        sed -n '2,16p' "$0"
        ;;
    "")
        build
        run_full_tests
        ;;
    *)
        build
        run_focused_tests "$1"
        ;;
esac