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
set "LOG_FILE=%LOG_DIR%\control-center-start-%STAMP%.log"
set "PYTHON_CMD="
set "LAUNCH_ARGS="

call :log INFO "FollowBox control center startup"
call :log INFO "Repo root: %REPO_ROOT%"
call :log INFO "Startup log: %LOG_FILE%"
call :log INFO "The launcher no longer auto-pushes Git on startup. Use the control center UI for push/pull/deploy actions."

call :run git --version || exit /b 1
call :run git -C "%REPO_ROOT%" rev-parse --is-inside-work-tree || exit /b 1
call :run git -C "%REPO_ROOT%" status --short --branch || exit /b 1

if defined FOLLOWBOX_DEV_CONSOLE_PORT (
  set "LAUNCH_ARGS=--port %FOLLOWBOX_DEV_CONSOLE_PORT%"
)
if /I "%FOLLOWBOX_NO_BROWSER%"=="1" (
  if defined LAUNCH_ARGS (
    set "LAUNCH_ARGS=%LAUNCH_ARGS% --no-browser"
  ) else (
    set "LAUNCH_ARGS=--no-browser"
  )
)

call :launch_python
if "%ERRORLEVEL%"=="0" exit /b 0

call :launch_exe
if "%ERRORLEVEL%"=="0" exit /b 0

call :launch_powershell
exit /b %ERRORLEVEL%

:launch_python
if not exist "%TOOLS_DIR%dev-console.py" (
  call :log INFO "Python backend not found: %TOOLS_DIR%dev-console.py"
  exit /b 1
)

where python >nul 2>&1
if not errorlevel 1 set "PYTHON_CMD=python"
if not defined PYTHON_CMD (
  where py >nul 2>&1
  if not errorlevel 1 set "PYTHON_CMD=py -3"
)
if not defined PYTHON_CMD (
  call :log INFO "Python launcher not found in PATH."
  exit /b 1
)

call :log INFO "Starting Python control center backend..."
call :log INFO "Command: %PYTHON_CMD% -u ""%TOOLS_DIR%dev-console.py"" %LAUNCH_ARGS%"
cd /d "%REPO_ROOT%" || exit /b 1
%PYTHON_CMD% -u "%TOOLS_DIR%dev-console.py" %LAUNCH_ARGS%
set "APP_EXIT=%ERRORLEVEL%"
call :log INFO "Python control center exited with code %APP_EXIT%"
exit /b %APP_EXIT%

:launch_exe
if not exist "%REPO_ROOT%\FollowBox-DevConsole.exe" (
  call :log INFO "Bundled dev console not found: %REPO_ROOT%\FollowBox-DevConsole.exe"
  exit /b 1
)

call :log INFO "Starting bundled FollowBox Dev Console..."
cd /d "%REPO_ROOT%" || exit /b 1
"%REPO_ROOT%\FollowBox-DevConsole.exe"
set "APP_EXIT=%ERRORLEVEL%"
call :log INFO "Bundled dev console exited with code %APP_EXIT%"
exit /b %APP_EXIT%

:launch_powershell
if not exist "%TOOLS_DIR%followbox-control-center.ps1" (
  call :log ERROR "No available control center backend was found."
  exit /b 1
)

call :log WARN "Falling back to legacy PowerShell control center backend."
cd /d "%TOOLS_DIR%" || exit /b 1
powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLS_DIR%followbox-control-center.ps1" %*
set "APP_EXIT=%ERRORLEVEL%"
call :log INFO "Legacy PowerShell control center exited with code %APP_EXIT%"
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
