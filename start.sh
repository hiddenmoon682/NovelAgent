#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════
# NovelAgent 一键启动脚本 (MSYS2 / Git Bash / WSL)
# ./start.sh [项目路径]
# ═══════════════════════════════════════════════════════════

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_PATH="${1:-./my_novel}"

# ── 设置 PATH ──
if [ -d "/d/SoftWare/msys2/mingw64/bin" ]; then
    export PATH="/d/SoftWare/msys2/mingw64/bin:$PATH"
fi

# ── 查找 Node.js（不依赖 PATH，主动搜索） ──
find_node() {
    # 1. PATH 中已有
    if command -v node &>/dev/null; then
        command -v node
        return 0
    fi
    # 2. 搜索常见安装位置
    local dirs=(
        "/d/SoftWare/NodeJs"
        "/d/SoftWare/nodejs"
        "/c/Program Files/nodejs"
        "/c/Program Files (x86)/nodejs"
        "/d/SoftWare/msys2/mingw64/bin"
        "/d/SoftWare/msys2/usr/bin"
    )
    for dir in "${dirs[@]}"; do
        # 递归查找 node.exe（最多2层）
        local found
        found=$(find "$dir" -maxdepth 3 -name "node.exe" -type f 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            echo "$found"
            return 0
        fi
    done
    return 1
}

NODE=$(find_node)
if [ -z "$NODE" ]; then
    echo "[ERROR] Node.js not found."
    echo "Please install Node.js from https://nodejs.org"
    echo ""
    read -p "Press Enter to exit..."
    exit 1
fi

NODE_DIR="$(dirname "$NODE")"
export PATH="$NODE_DIR:$PATH"

# 确保 npm 可用（可能与 node 在同一目录）
if ! command -v npm &>/dev/null; then
    if [ -f "$NODE_DIR/npm" ]; then
        # Unix-style npm
        alias npm="$NODE_DIR/npm"
    elif [ -f "$NODE_DIR/npm.cmd" ]; then
        # Windows npm — 通过 node 调用 npm-cli
        NPM_CLI="$NODE_DIR/node_modules/npm/bin/npm-cli.js"
        if [ -f "$NPM_CLI" ]; then
            npm() { "$NODE" "$NPM_CLI" "$@"; }
        fi
    fi
fi

echo "[INFO] Node.js: $NODE"

# ── 检查后端 ──
if [ ! -f "$SCRIPT_DIR/build/novelagent.exe" ]; then
    echo "[ERROR] Backend not found: build/novelagent.exe"
    echo "Please build first: cd $SCRIPT_DIR && cmake --build build"
    read -p "Press Enter to exit..."
    exit 1
fi

# ── 安装 TUI 依赖 ──
if [ ! -d "$SCRIPT_DIR/tui/node_modules" ]; then
    echo "[INFO] Installing TUI dependencies..."
    (cd "$SCRIPT_DIR/tui" && npm install) || {
        echo "[ERROR] npm install failed."
        read -p "Press Enter to exit..."
        exit 1
    }
fi

# ── 启动 ──
echo "============================================================"
echo "  NovelAgent - AI Novel Writing Assistant"
echo "============================================================"
echo "  Project : $PROJECT_PATH"
echo "  Backend : build/novelagent.exe"
echo "  Frontend: tui (Node.js + Ink)"
echo "  Config  : ~/.novelagent/config.json"
echo "============================================================"
echo ""
echo "Make sure API Key is set in config.json before writing!"
echo "Press Ctrl+C to exit."
echo ""

cd "$SCRIPT_DIR/tui"
npx tsx src/main.tsx -p "$PROJECT_PATH"

echo ""
echo "NovelAgent exited."
