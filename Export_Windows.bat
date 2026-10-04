@echo off
REM Export_Windows.bat — Build a self-contained Claudette distribution for Windows
REM
REM Same pattern as Export_Linux.sh. Pulls from the live project tree,
REM packs assets, sanitizes identity, generates the installer.
REM
REM Usage: Export_Windows.bat

setlocal enabledelayedexpansion

set "PROJECT_ROOT=%~dp0"
REM Remove trailing backslash
if "%PROJECT_ROOT:~-1%"=="\" set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

set "EXPORT_DIR=%PROJECT_ROOT%\Export_Windows"
set "BIN_SRC=%PROJECT_ROOT%\bin"
set "BUILD_BIN=%PROJECT_ROOT%\build\bin"
set "HOOKS_SRC=%PROJECT_ROOT%\.claude\hooks"
set "PACKAGE_TOOL=%BUILD_BIN%\tool\PackageTool.exe"

set "DEV_USER=%USERNAME%"
set "DEV_HOME=%USERPROFILE%"
set "DEV_HOST=%COMPUTERNAME%"

echo === Claudette Export (Windows) ===
echo.

REM --- Verify prerequisites ---
set FAIL=0
if exist "%PACKAGE_TOOL%" (echo [OK] PackageTool) else (echo [FAIL] PackageTool not found & set FAIL=1)
if exist "%BUILD_BIN%\Claudette.exe" (echo [OK] Claudette) else (echo [FAIL] Claudette not found & set FAIL=1)
if exist "%BUILD_BIN%\WorkboardManager.exe" (echo [OK] WorkboardManager) else (echo [FAIL] WorkboardManager not found & set FAIL=1)
if %FAIL% GTR 0 exit /b 1

REM --- Create output structure ---
echo.
if not exist "%EXPORT_DIR%\bin" mkdir "%EXPORT_DIR%\bin"
if not exist "%EXPORT_DIR%\.claude\hooks" mkdir "%EXPORT_DIR%\.claude\hooks"
if not exist "%EXPORT_DIR%\Claude" mkdir "%EXPORT_DIR%\Claude"

REM --- Pack CoreData.pak ---
echo.
echo Packing CoreData.pak...
set "STAGING=%TEMP%\claudette_staging_%RANDOM%"
mkdir "%STAGING%\CoreData\Textures"
mkdir "%STAGING%\CoreData\Techniques"
mkdir "%STAGING%\CoreData\RenderPaths"
mkdir "%STAGING%\CoreData\Shaders\GLSL"

for %%f in (Textures\Ramp.png Textures\Spot.png Techniques\NoTexture.xml RenderPaths\Forward.xml Shaders\GLSL\Basic.glsl) do (
    if not exist "%BIN_SRC%\CoreData\%%f" (echo [FAIL] Missing CoreData\%%f & exit /b 1)
    copy /y "%BIN_SRC%\CoreData\%%f" "%STAGING%\CoreData\%%f" >nul
)

"%PACKAGE_TOOL%" -pqc "%STAGING%\CoreData" "%EXPORT_DIR%\bin\CoreData.pak"
echo [OK] CoreData.pak

REM --- Pack Data.pak ---
echo Packing Data.pak...
mkdir "%STAGING%\Data\Textures"
mkdir "%STAGING%\Data\UI"
mkdir "%STAGING%\Data\Fonts"

copy /y "%BIN_SRC%\Data\Textures\UI.png" "%STAGING%\Data\Textures\UI.png" >nul
copy /y "%BIN_SRC%\Data\UI\DefaultStyle.xml" "%STAGING%\Data\UI\DefaultStyle.xml" >nul
copy /y "%BIN_SRC%\Data\Fonts\UbuntuMono-R.ttf" "%STAGING%\Data\Fonts\UbuntuMono-R.ttf" >nul
copy /y "%BIN_SRC%\Data\Fonts\Anonymous Pro.ttf" "%STAGING%\Data\Fonts\Anonymous Pro.ttf" >nul

"%PACKAGE_TOOL%" -pqc "%STAGING%\Data" "%EXPORT_DIR%\bin\Data.pak"
echo [OK] Data.pak

REM --- Cleanup staging ---
rmdir /s /q "%STAGING%" 2>nul

REM --- Binaries ---
copy /y "%BUILD_BIN%\Claudette.exe" "%EXPORT_DIR%\bin\Claudette.exe" >nul
echo [OK] bin\Claudette.exe

copy /y "%BUILD_BIN%\WorkboardManager.exe" "%EXPORT_DIR%\bin\WorkboardManager.exe" >nul
echo [OK] bin\WorkboardManager.exe

REM --- Hook scripts (bat wrappers for Windows) ---
echo.

REM claude_ipc — Windows needs a .bat wrapper around the bash script (runs via Git Bash or WSL)
(
echo @echo off
echo REM claude_ipc.bat — wrapper for claude_ipc.sh via Git Bash or WSL
echo setlocal
echo set "SCRIPT_DIR=%%~dp0"
echo if exist "%%PROGRAMFILES%%\Git\bin\bash.exe" ^(
echo     "%%PROGRAMFILES%%\Git\bin\bash.exe" "%%SCRIPT_DIR%%claude_ipc.sh" %%*
echo ^) else if exist "%%LOCALAPPDATA%%\Programs\Git\bin\bash.exe" ^(
echo     "%%LOCALAPPDATA%%\Programs\Git\bin\bash.exe" "%%SCRIPT_DIR%%claude_ipc.sh" %%*
echo ^) else ^(
echo     wsl bash "%%SCRIPT_DIR%%claude_ipc.sh" %%*
echo ^)
) > "%EXPORT_DIR%\.claude\hooks\claude_ipc.bat"

REM safe_build — same pattern
(
echo @echo off
echo REM safe_build.bat — wrapper for safe_build.sh via Git Bash or WSL
echo setlocal
echo set "SCRIPT_DIR=%%~dp0"
echo if exist "%%PROGRAMFILES%%\Git\bin\bash.exe" ^(
echo     "%%PROGRAMFILES%%\Git\bin\bash.exe" "%%SCRIPT_DIR%%safe_build.sh" %%*
echo ^) else if exist "%%LOCALAPPDATA%%\Programs\Git\bin\bash.exe" ^(
echo     "%%LOCALAPPDATA%%\Programs\Git\bin\bash.exe" "%%SCRIPT_DIR%%safe_build.sh" %%*
echo ^) else ^(
echo     wsl bash "%%SCRIPT_DIR%%safe_build.sh" %%*
echo ^)
) > "%EXPORT_DIR%\.claude\hooks\safe_build.bat"

REM Copy the actual sh scripts too (for Git Bash / WSL)
for %%s in (claude_ipc.sh safe_build.sh file_lock.sh tty-inject.sh) do (
    if exist "%HOOKS_SRC%\%%s" (
        copy /y "%HOOKS_SRC%\%%s" "%EXPORT_DIR%\.claude\hooks\%%s" >nul
        echo [OK] .claude\hooks\%%s
    )
)
echo [OK] .claude\hooks\claude_ipc.bat
echo [OK] .claude\hooks\safe_build.bat

REM --- C sources (Windows variant) ---
if exist "%HOOKS_SRC%\tty-listen-win.c" (
    copy /y "%HOOKS_SRC%\tty-listen-win.c" "%EXPORT_DIR%\tty-listen-win.c" >nul
    echo [OK] tty-listen-win.c
)
if exist "%HOOKS_SRC%\pty-proxy.c" (
    copy /y "%HOOKS_SRC%\pty-proxy.c" "%EXPORT_DIR%\pty-proxy.c" >nul
    echo [OK] pty-proxy.c
)

REM --- settings.json ---
if exist "%PROJECT_ROOT%\.claude\settings.template.json" (
    copy /y "%PROJECT_ROOT%\.claude\settings.template.json" "%EXPORT_DIR%\.claude\settings.json" >nul
    echo [OK] .claude\settings.json
)

REM --- settings.local.json ---
(
echo {
echo   "permissions": {
echo     "allow": [
echo       "Bash($CLAUDE_PROJECT_DIR/.claude/hooks/claude_ipc.sh:*)",
echo       "Bash($CLAUDE_PROJECT_DIR/.claude/hooks/safe_build.sh:*)"
echo     ],
echo     "deny": [],
echo     "ask": []
echo   }
echo }
) > "%EXPORT_DIR%\.claude\settings.local.json"
echo [OK] .claude\settings.local.json

REM --- CLAUDE.md ---
if exist "%PROJECT_ROOT%\Claude\CLAUDETTE_RULES.md" (
    copy /y "%PROJECT_ROOT%\Claude\CLAUDETTE_RULES.md" "%EXPORT_DIR%\CLAUDE.md" >nul
    echo [OK] CLAUDE.md
)

REM --- Workboard template ---
(
echo # Workboard
echo ## Team
echo ^| Name ^| Role ^| Focus ^|
echo ^|------^|------^|-------^|
echo ^| **User** ^| Boss ^| Final authority ^|
echo ^| **Planner** ^| Architecture ^| Plans, docs, research ^|
echo ^| **Coder** ^| Implementation ^| Code, shaders, builds ^|
echo ## Planned
echo ^| Pri ^| Plan ^| File ^| Review ^| Summary ^|
echo ^|-----^|------^|------^|--------^|---------^|
echo ## Ready
echo ^| Pri ^| Plan ^| File ^| Summary ^|
echo ^|-----^|------^|------^|---------^|
echo ## In Progress
echo ^| Task ^| Owner ^| Started ^| Review ^| Notes ^|
echo ^|------^|-------^|---------^|--------^|-------^|
echo ## Done
echo ^| Task ^| Owner ^| Completed ^| Review ^| Notes ^|
echo ^|------^|-------^|-----------^|--------^|-------^|
echo ## Archive
echo ^| Task ^| Owner ^| Completed ^| Notes ^|
echo ^|------^|-------^|-----------^|-------^|
) > "%EXPORT_DIR%\Claude\WORKBOARD.md"
echo [OK] Claude\WORKBOARD.md

REM --- Reference docs ---
for %%d in (CLAUDETTE_RULES.md IPC_PROTOCOL.md) do (
    if exist "%PROJECT_ROOT%\Claude\%%d" (
        copy /y "%PROJECT_ROOT%\Claude\%%d" "%EXPORT_DIR%\Claude\%%d" >nul
        echo [OK] Claude\%%d
    )
)

REM --- Sanitize ---
echo.
echo Sanitizing...
REM Use PowerShell for text replacement (no sed on Windows)
powershell -NoProfile -Command ^
    "$root='%PROJECT_ROOT:\=\\%'; $home='%DEV_HOME:\=\\%'; $host='%DEV_HOST%'; $user='%DEV_USER%';" ^
    "Get-ChildItem -Path '%EXPORT_DIR%' -Recurse -Include *.sh,*.md,*.json,*.c,*.bat | ForEach-Object {" ^
    "  $c = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue;" ^
    "  if ($c) {" ^
    "    $c = $c -replace [regex]::Escape($root + '\\build\\bin'), '.\\bin';" ^
    "    $c = $c -replace [regex]::Escape($root + '\\build'), '.\\build';" ^
    "    $c = $c -replace [regex]::Escape($root), '.';" ^
    "    $c = $c -replace [regex]::Escape($root -replace '\\\\','/'), '.';" ^
    "    $c = $c -replace [regex]::Escape($home), '$HOME';" ^
    "    $c = $c -replace [regex]::Escape($host), 'localhost';" ^
    "    $c = $c -replace ('(?<=[\s,;:!?\"''(])' + [regex]::Escape($user) + '(?=[\s,;:!?\"'').$])'), 'User';" ^
    "    Set-Content $_.FullName $c -NoNewline;" ^
    "  }" ^
    "}"
echo [OK] Clean

REM --- Generate Install_Windows.bat ---
echo.
echo Generating installer...

(
echo @echo off
echo REM Install_Windows.bat — Install Claudette ^(Claudette^) on Windows
echo setlocal enabledelayedexpansion
echo.
echo set "SCRIPT_DIR=%%~dp0"
echo if "%%SCRIPT_DIR:~-1%%"=="\" set "SCRIPT_DIR=%%SCRIPT_DIR:~0,-1%%"
echo set "BIN_DIR=%%SCRIPT_DIR%%\bin"
echo.
echo echo === Claudette Installer ^(Windows^) ===
echo echo.
echo.
echo REM --- Verify distribution ---
echo if not exist "%%BIN_DIR%%\Claudette.exe" ^(echo [FAIL] bin\Claudette.exe missing ^& exit /b 1^)
echo if not exist "%%BIN_DIR%%\CoreData.pak" ^(echo [FAIL] bin\CoreData.pak missing ^& exit /b 1^)
echo if not exist "%%BIN_DIR%%\Data.pak" ^(echo [FAIL] bin\Data.pak missing ^& exit /b 1^)
echo echo [OK] Distribution intact
echo.
echo REM --- Check for Claude Code CLI ---
echo where claude ^>nul 2^>^&1
echo if errorlevel 1 ^(
echo     echo [MISS] claude — checking for npm...
echo     where npm ^>nul 2^>^&1
echo     if errorlevel 1 ^(
echo         echo.
echo         echo [FAIL] Claude Code CLI required.
echo         echo   1. Install Node.js: https://nodejs.org/
echo         echo   2. Run: npm install -g @anthropic-ai/claude-code
echo         echo   3. Re-run this script.
echo         exit /b 1
echo     ^)
echo     echo [MISS] claude — installing via npm...
echo     npm install -g @anthropic-ai/claude-code
echo     where claude ^>nul 2^>^&1
echo     if errorlevel 1 ^(
echo         echo [FAIL] Installation failed. Try manually: npm install -g @anthropic-ai/claude-code
echo         exit /b 1
echo     ^)
echo     echo [OK] claude installed
echo ^) else ^(
echo     echo [OK] claude
echo ^)
echo.
echo REM --- Check for Git Bash or WSL ^(needed for hook scripts^) ---
echo set "HAS_BASH=0"
echo if exist "%%PROGRAMFILES%%\Git\bin\bash.exe" set "HAS_BASH=1"
echo if exist "%%LOCALAPPDATA%%\Programs\Git\bin\bash.exe" set "HAS_BASH=1"
echo where wsl ^>nul 2^>^&1 ^&^& set "HAS_BASH=1"
echo if "%%HAS_BASH%%"=="0" ^(
echo     echo [WARN] No Git Bash or WSL found — hook scripts will not work
echo     echo        Install Git for Windows: https://git-scm.com/download/win
echo ^) else ^(
echo     echo [OK] Bash available ^(Git Bash or WSL^)
echo ^)
echo.
echo REM --- Create IPC directories ---
echo if not exist "%%TEMP%%\urho_claude\instances" mkdir "%%TEMP%%\urho_claude\instances"
echo if not exist "%%TEMP%%\urho_claude\locks" mkdir "%%TEMP%%\urho_claude\locks"
echo echo [OK] IPC directories
echo.
echo echo.
echo echo === Installation Complete ===
echo echo Launch Claudette: %%BIN_DIR%%\Claudette.exe
echo echo Or add %%BIN_DIR%% to your PATH and run: Claudette --planner
) > "%EXPORT_DIR%\Install_Windows.bat"
echo [OK] Install_Windows.bat

REM --- Summary ---
echo.
echo === Export Complete ===
echo.
dir /s /b "%EXPORT_DIR%"
echo.
