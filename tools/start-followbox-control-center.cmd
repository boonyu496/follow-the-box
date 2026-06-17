@echo off
setlocal EnableExtensions

set "TOOLS_DIR=%~dp0"
for %%I in ("%TOOLS_DIR%..") do set "REPO_ROOT=%%~fI"
set "LOG_DIR=%TOOLS_DIR%logs"

if not exist "%LOG_DIR%" (
  mkdir "%LOG_DIR%" >nul 2>&1
  if errorlevel 1 (
    echo [ERROR] Cannot create log directory: "%LOG_DIR%"
    exit /b 1
  )
)

for /f %%I in ('powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%I"
if not defined STAMP set "STAMP=unknown-time"
set "LOG_FILE=%LOG_DIR%\github-push-%STAMP%.log"

call :log INFO "FollowBox control center startup"
call :log INFO "Repo root: %REPO_ROOT%"
call :log INFO "GitHub push log: %LOG_FILE%"

call :run git --version || exit /b 1
call :run git -C "%REPO_ROOT%" rev-parse --is-inside-work-tree || exit /b 1

set "BRANCH="
for /f "delims=" %%B in ('git -C "%REPO_ROOT%" branch --show-current 2^>nul') do set "BRANCH=%%B"
if not defined BRANCH (
  call :log ERROR "Cannot push from a detached HEAD. Checkout a branch first."
  exit /b 1
)
call :log INFO "Current branch: %BRANCH%"

set "REMOTE_URL="
for /f "delims=" %%U in ('git -C "%REPO_ROOT%" remote get-url origin 2^>nul') do set "REMOTE_URL=%%U"
if not defined REMOTE_URL (
  call :log ERROR "Git remote 'origin' is not configured."
  exit /b 1
)
call :log INFO "Remote origin: %REMOTE_URL%"

call :run git -C "%REPO_ROOT%" status --short --branch || exit /b 1

set "DIRTY="
for /f "delims=" %%S in ('git -C "%REPO_ROOT%" status --porcelain 2^>nul') do set "DIRTY=1"
if defined DIRTY (
  call :log WARN "Working tree has uncommitted changes. GitHub can only receive committed history."
)

call :log INFO "Pushing committed HEAD to GitHub..."
call :run git -C "%REPO_ROOT%" push --set-upstream origin "%BRANCH%" || exit /b 1

set "LOCAL_HEAD="
for /f "delims=" %%H in ('git -C "%REPO_ROOT%" rev-parse HEAD 2^>nul') do set "LOCAL_HEAD=%%H"
if not defined LOCAL_HEAD (
  call :log ERROR "Cannot read local HEAD after push."
  exit /b 1
)

set "REMOTE_HEAD="
for /f "tokens=1" %%H in ('git -C "%REPO_ROOT%" ls-remote --heads origin "%BRANCH%" 2^>nul') do set "REMOTE_HEAD=%%H"
if not defined REMOTE_HEAD (
  call :log ERROR "Cannot verify remote branch origin/%BRANCH% after push."
  exit /b 1
)

if /I not "%LOCAL_HEAD%"=="%REMOTE_HEAD%" (
  call :log ERROR "Push verification failed. Local HEAD %LOCAL_HEAD% != remote HEAD %REMOTE_HEAD%"
  exit /b 1
)
call :log OK "Push verified. origin/%BRANCH% is %REMOTE_HEAD%"

if /I "%FOLLOWBOX_PUSH_ONLY%"=="1" (
  call :log INFO "FOLLOWBOX_PUSH_ONLY=1, skipping control center launch."
  exit /b 0
)

call :log INFO "Starting FollowBox control center..."
cd /d "%TOOLS_DIR%" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLS_DIR%followbox-control-center.ps1" %*
set "APP_EXIT=%ERRORLEVEL%"
call :log INFO "Control center exited with code %APP_EXIT%"
exit /b %APP_EXIT%

:run
set "TMP_LOG=%TEMP%\followbox-git-%RANDOM%-%RANDOM%.tmp"
set "LOG_LINE=[%DATE% %TIME%] [CMD] %*"
echo %LOG_LINE%
>> "%LOG_FILE%" echo %LOG_LINE%
%* > "%TMP_LOG%" 2>&1
set "RUN_EXIT=%ERRORLEVEL%"
if exist "%TMP_LOG%" (
  type "%TMP_LOG%"
  type "%TMP_LOG%" >> "%LOG_FILE%"
  del "%TMP_LOG%" >nul 2>&1
)
if not "%RUN_EXIT%"=="0" (
  call :log ERROR "Command failed with exit code %RUN_EXIT%"
  exit /b %RUN_EXIT%
)
exit /b 0

:log
set "LOG_LEVEL=%~1"
set "LOG_MESSAGE=%~2"
set "LOG_LINE=[%DATE% %TIME%] [%LOG_LEVEL%] %LOG_MESSAGE%"
echo %LOG_LINE%
>> "%LOG_FILE%" echo %LOG_LINE%
exit /b 0
