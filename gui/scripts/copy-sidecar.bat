@echo off
REM 复制 C++ 后端及所有依赖 DLL 到 Tauri 目录
REM DLL 需同时放在 src-tauri\ 和 src-tauri\binaries\（dev + production 路径不同）

cd /d "%~dp0..\.."

set "SRC=build\novelagent.exe"
set "DST_EXE=gui\src-tauri\binaries\novelagent-x86_64-pc-windows-msvc.exe"

if not exist "%SRC%" (
    echo [NovelAgent] 错误：找不到 %SRC%，请先构建 C++ 后端
    exit /b 1
)

echo [NovelAgent] 复制 sidecar: %SRC% -^> %DST_EXE%
copy /Y "%SRC%" "%DST_EXE%"

REM 复制 DLL 到两个位置
echo [NovelAgent] 复制依赖 DLL...
for %%f in (
    libssl-3-x64.dll
    libcrypto-3-x64.dll
    libspdlog-1.17.dll
    libwinpthread-1.dll
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libftxui-screen.dll
    libftxui-dom.dll
    libftxui-component.dll
) do (
    if exist "build\%%f" (
        copy /Y "build\%%f" "gui\src-tauri\%%f" >nul
        copy /Y "build\%%f" "gui\src-tauri\binaries\%%f" >nul
        echo   %%f
    ) else (
        echo   [警告] 找不到 build\%%f
    )
)

echo [NovelAgent] Sidecar + DLL 复制完成
