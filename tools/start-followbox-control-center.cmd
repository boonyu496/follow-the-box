@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "REPO_ROOT=%~dp0.."
for %%I in ("%REPO_ROOT%") do set "REPO_ROOT=%%~fI"
set "LOCAL_TOOLS_DIR=%REPO_ROOT%\tools_local"
set "CONTROL_PS1=%LOCAL_TOOLS_DIR%\followbox-control-center.ps1"
set "CONTROL_HTML=%LOCAL_TOOLS_DIR%\followbox-control-center.html"
set "CONTROL_CONFIG=%LOCAL_TOOLS_DIR%\followbox-control-center.config.json"
set "LOG_DIR=%LOCAL_TOOLS_DIR%\logs"
set "PORT=%FOLLOWBOX_CONTROL_PORT%"
if not defined PORT set "PORT=8787"

if /I "%~1"=="help" goto :help
if /I "%~1"=="--help" goto :help
if /I "%~1"=="/?" goto :help
if /I "%~1"=="status" goto :status

echo [INFO] FollowBox control center launcher
echo [INFO] Repo root: "%REPO_ROOT%"
echo [INFO] Local tools: "%LOCAL_TOOLS_DIR%"
echo [INFO] Port: %PORT%

if not exist "%LOCAL_TOOLS_DIR%\" (
  echo [INFO] Creating local-only tools directory: "%LOCAL_TOOLS_DIR%"
  mkdir "%LOCAL_TOOLS_DIR%" >nul 2>&1
  if errorlevel 1 (
    echo [ERROR] Cannot create local-only tools directory.
    exit /b 1
  )
)

if not exist "%CONTROL_PS1%" (
  echo [ERROR] Missing control center backend: "%CONTROL_PS1%"
  echo [ERROR] Cloud deploy, cloud OTA publish, repo upload, and repo pull all live in this local backend.
  echo [HINT] Restore tools_local/followbox-control-center.ps1 on this machine, then start this file again.
  exit /b 1
)

if not exist "%CONTROL_HTML%" (
  echo [ERROR] Missing control center UI: "%CONTROL_HTML%"
  echo [HINT] Restore tools_local/followbox-control-center.html on this machine, then start this file again.
  exit /b 1
)

if not exist "%CONTROL_CONFIG%" (
  echo [WARN] Missing local config: "%CONTROL_CONFIG%"
  echo [WARN] The backend will create defaults, but cloud SSH, OTA, and Git settings may need review in the UI.
)

if not exist "%LOG_DIR%\" (
  mkdir "%LOG_DIR%" >nul 2>&1
  if errorlevel 1 (
    echo [ERROR] Cannot create log directory: "%LOG_DIR%"
    exit /b 1
  )
)

call :require powershell.exe || exit /b 1
call :require git.exe || exit /b 1

git -C "%REPO_ROOT%" rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Repo root is not a Git worktree: "%REPO_ROOT%"
  exit /b 1
)

if /I not "%FOLLOWBOX_KEEP_EXISTING_CONTROL_CENTER%"=="1" (
  call :stop_existing
) else (
  echo [INFO] FOLLOWBOX_KEEP_EXISTING_CONTROL_CENTER=1, keeping any running control center process.
)

echo [INFO] Starting backend directly from tools_local/followbox-control-center.ps1
echo [INFO] Browser/UI: http://127.0.0.1:%PORT%/
echo [INFO] Use this entry point for cloud deploy, cloud OTA publish, repo upload, and repo pull.
powershell -NoProfile -ExecutionPolicy Bypass -File "%CONTROL_PS1%" -Port %PORT% %*
set "APP_EXIT=%ERRORLEVEL%"
echo [INFO] Control center exited with code %APP_EXIT%
exit /b %APP_EXIT%

:help
echo FollowBox control center launcher
echo.
echo Usage:
echo   tools\start-followbox-control-center.cmd
echo   tools\start-followbox-control-center.cmd status
echo.
echo Environment:
echo   FOLLOWBOX_CONTROL_PORT=8787
echo   FOLLOWBOX_KEEP_EXISTING_CONTROL_CENTER=1
echo.
echo Notes:
echo   This file is the stable repo entry point.
echo   The cloud deploy, cloud OTA publish, repo upload, and repo pull APIs are served by tools_local\followbox-control-center.ps1.
exit /b 0

:status
powershell -NoProfile -ExecutionPolicy Bypass -Command "try { $r = Invoke-RestMethod -Uri 'http://127.0.0.1:%PORT%/api/state' -TimeoutSec 10; $r | ConvertTo-Json -Depth 8 } catch { Write-Output ('[ERROR] Control center is not responding on port %PORT%: ' + $_.Exception.Message); exit 1 }"
exit /b %ERRORLEVEL%

:require
where %~1 >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Required command not found in PATH: %~1
  exit /b 1
)
exit /b 0

:stop_existing
set "FOLLOWBOX_CONTROL_SCRIPT=%CONTROL_PS1%"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$target = $env:FOLLOWBOX_CONTROL_SCRIPT; $relative1 = 'tools_local\followbox-control-center.ps1'; $relative2 = 'tools_local/followbox-control-center.ps1'; Get-CimInstance Win32_Process | Where-Object { $_.ProcessId -ne $PID -and $_.CommandLine -and (($_.CommandLine.IndexOf($target, [StringComparison]::OrdinalIgnoreCase) -ge 0) -or ($_.CommandLine.IndexOf($relative1, [StringComparison]::OrdinalIgnoreCase) -ge 0) -or ($_.CommandLine.IndexOf($relative2, [StringComparison]::OrdinalIgnoreCase) -ge 0)) } | ForEach-Object { Write-Output ('[INFO] Stopping old control center process PID ' + $_.ProcessId); Stop-Process -Id $_.ProcessId -Force }"
if errorlevel 1 (
  echo [WARN] Could not scan/stop old control center processes. Continuing startup.
)
exit /b 0
