@echo off
setlocal
cd /d "%~dp0"
call tools\pick_version.bat
if errorlevel 1 goto failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Unlock-All-Fighters.ps1" -Tree "%SSBU_TREE%" %*
set "SSBU_UNLOCK_RESULT=%ERRORLEVEL%"
goto finish
:failed
set "SSBU_UNLOCK_RESULT=1"
:finish
echo.
if not defined SSBU_NO_PAUSE pause
exit /b %SSBU_UNLOCK_RESULT%
