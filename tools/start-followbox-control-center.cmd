@echo off
setlocal EnableExtensions

set "REPO_ROOT=%~dp0.."
for %%I in ("%REPO_ROOT%") do set "REPO_ROOT=%%~fI"
set "LOCAL_TOOLS_DIR=%REPO_ROOT%\tools_local"
set "LOCAL_START=%LOCAL_TOOLS_DIR%\start-followbox-control-center.cmd"

if exist "%LOCAL_START%" (
  call "%LOCAL_START%" %*
  exit /b %ERRORLEVEL%
)

echo [ERROR] Local control center was moved to "%LOCAL_TOOLS_DIR%".
echo [ERROR] Missing file: "%LOCAL_START%"
echo [HINT] Keep machine-specific launchers and configs under "tools_local/" so Git will not upload or pull them.
exit /b 1
