@echo off
setlocal
set "SSBU_DATA_ROOT="
set "SSBU_INPUT="
set "SSBU_NET_HOST="
set "SSBU_NET_RELAY="
set "SSBU_NET_BIND="
set "SSBU_NET_ROOM="
rem Joins a friend's netplay room through Steam (see Host SSBU Steam.bat and NETPLAY.md). Type
rem the host's room code, or press Enter to pick among friends who are hosting. Steam must be
rem running and signed in. The game's log and the bridge's messages go to
rem <version folder>\build\play.log.
cd /d "%~dp0"
call tools\pick_version.bat || (pause & exit /b 1)
call tools\find_python.bat || (pause & exit /b 1)
%PY% -u tools\steam_setup.py || (pause & exit /b 1)
call tools\play_setup.bat || (pause & exit /b 1)
set "SSBU_BUILD="
for /f "tokens=1,2" %%a in ('findstr /b /c:"main " "%SSBU_TREE%\version.txt"') do set "SSBU_BUILD=%%b"
if not defined SSBU_BUILD (
    echo Cannot read the game's build from %SSBU_TREE%\version.txt.
    pause
    exit /b 1
)
set "SSBU_STEAM_CODE="
set /p "SSBU_STEAM_CODE=Host's room code [Enter: pick a friend who is hosting]: "
set "SSBU_STEAM_ARGS="
if defined SSBU_STEAM_CODE set "SSBU_STEAM_ARGS=--code %SSBU_STEAM_CODE%"
echo In the game: Local Wireless ^> Rooms List, then the host's room.
"%~dp0build\steam\neosmash_steam.exe" join --build %SSBU_BUILD% %SSBU_STEAM_ARGS% -- "%SSBU_TREE%\build\ssbu.exe" "%SSBU_TREE%\dump\modules" 2> "%SSBU_TREE%\build\play.log"
rem (a crash ends with a negative code, which "if errorlevel 1" does not see)
set "SSBU_RESULT=%ERRORLEVEL%"
if not "%SSBU_RESULT%"=="0" (
    echo No room was found, or the game reported an error ^(exit code %SSBU_RESULT%^). The end of %SSBU_TREE%\build\play.log:
    powershell -NoProfile -Command "Get-Content -LiteralPath '%SSBU_TREE%\build\play.log' -Tail 15"
    pause
)
