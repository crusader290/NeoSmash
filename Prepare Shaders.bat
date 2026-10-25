@echo off
rem Builds the whole-game compilation list and compiles discoverable shader variants.
rem GPU pipeline compilation and unseen runtime variants may still cause first-use stalls.
rem It runs once per game version, from minutes to about two hours, at low priority (the PC stays
rem usable). It can be stopped (close this window) and started again: what is done stays done.
rem Needs Python and the build tools, as for building (docs\BUILDING.md).
setlocal
cd /d "%~dp0"
call tools\find_python.bat
if errorlevel 1 goto failed
call tools\pick_version.bat
if errorlevel 1 goto failed
echo Preparing whole-game shaders. Archive scanning can take several minutes.
%PY% -u tools\prepare_effects.py --tree "%SSBU_TREE%" --whole-game %*
set "SSBU_PREPARE_RESULT=%ERRORLEVEL%"
if not "%SSBU_PREPARE_RESULT%"=="0" goto finish
rem The index of which shaders each container holds (the game prepares a container as it loads it).
%PY% -u tools\container_index.py
set "SSBU_PREPARE_RESULT=%ERRORLEVEL%"
if not "%SSBU_PREPARE_RESULT%"=="0" goto finish
rem Then the GPU driver's own cache for build\ssbu.exe under DXVK (first-use freezes).
if not exist "build\ssbu.exe" goto warmed
if not exist "build\d3d11.dll" goto warmed
%PY% -u tools\warm_native_cache.py
set "SSBU_PREPARE_RESULT=%ERRORLEVEL%"
if not "%SSBU_PREPARE_RESULT%"=="0" goto finish
:warmed
echo Shader preparation completed successfully.
goto finish
:failed
set "SSBU_PREPARE_RESULT=1"
:finish
if not "%SSBU_PREPARE_RESULT%"=="0" echo ERROR: Shader preparation failed. Read the message above before retrying.
echo.
if not defined SSBU_NO_PAUSE pause
exit /b %SSBU_PREPARE_RESULT%
