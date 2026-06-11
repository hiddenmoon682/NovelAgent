@echo off
setlocal

set "PROJECT=%~1"
if "%PROJECT%"=="" set "PROJECT=.\my_novel"

:: Try MSYS2 bash first (handles UTF-8, DLLs, PATH correctly)
if exist "D:\SoftWare\msys2\usr\bin\bash.exe" (
    D:\SoftWare\msys2\usr\bin\bash.exe -lc "cd '%~dp0' && ./start.sh '%PROJECT%'"
    pause
    exit /b
)

:: Fallback: locate Node.js and run directly
set "NODE="
for %%p in (node.exe) do set "NODE=%%~$PATH:p"
if "%NODE%"=="" (
    if exist "D:\SoftWare\nodejs\node.exe" set "NODE=D:\SoftWare\nodejs\node.exe"
)
if "%NODE%"=="" (
    if exist "C:\Program Files\nodejs\node.exe" set "NODE=C:\Program Files\nodejs\node.exe"
)
if "%NODE%"=="" (
    echo Node.js not found. Please install from https://nodejs.org
    pause
    exit /b 1
)

:: Add MSYS2 DLL path for backend
if exist "D:\SoftWare\msys2\mingw64\bin" set "PATH=D:\SoftWare\msys2\mingw64\bin;%PATH%"

:: Check backend
if not exist "%~dp0build\novelagent.exe" (
    echo Backend not found: build\novelagent.exe
    echo Run: cmake --build build
    pause
    exit /b 1
)

:: Install deps if needed
if not exist "%~dp0tui\node_modules" (
    echo Installing TUI dependencies...
    cd /d "%~dp0tui"
    call npm install
    cd /d "%~dp0"
)

:: Launch
echo Starting NovelAgent...
echo Project: %PROJECT%
echo.

cd /d "%~dp0tui"
npx tsx src/main.tsx -p "%PROJECT%"

echo.
echo NovelAgent exited.
pause
