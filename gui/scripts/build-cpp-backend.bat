@echo off
REM 构建 C++ NovelAgent 后端
REM 从 gui/scripts/ 运行，切换到项目根目录执行 CMake

cd /d "%~dp0..\.."
echo [NovelAgent] 构建 C++ 后端...
cmake --build build --target novelagent --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [NovelAgent] C++ 后端构建失败！
    exit /b 1
)
echo [NovelAgent] C++ 后端构建完成
