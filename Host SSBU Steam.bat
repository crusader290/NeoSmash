@echo off
setlocal
set "SSBU_DATA_ROOT="
set "SSBU_INPUT="
set "SSBU_NET_HOST="
set "SSBU_NET_RELAY="
set "SSBU_NET_BIND="
set "SSBU_NET_ROOM="
rem Hosts a netplay room through Steam: no port forwarding, Steam finds the way between the PCs
rem (directly, or through its relay servers). Steam must be running and signed in; you show as
rem playing Spacewar, Valve's test app. Friends start Join SSBU Steam.bat and type the room code
rem shown below (or pick you from their friends list). See NETPLAY.md. The game's log and the
rem bridge's messages go to <version folder>\build\play.log.
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
set /p "SSBU_STEAM_CODE=Room code to use [Enter: a new random one]: "
set "SSBU_STEAM_ARGS="
if defined SSBU_STEAM_CODE set "SSBU_STEAM_ARGS=--code %SSBU_STEAM_CODE%"
echo Starting the game as the room's host. In the game: Local Wireless ^> Create a Room.
"%~dp0build\steam\neosmash_steam.exe" host --build %SSBU_BUILD% %SSBU_STEAM_ARGS% -- "%SSBU_TREE%\build\ssbu.exe" "%SSBU_TREE%\dump\modules" 2> "%SSBU_TREE%\build\play.log"
if errorlevel 1 (
    echo The game or the Steam bridge reported an error. See %SSBU_TREE%\build\play.log
    pause
)
