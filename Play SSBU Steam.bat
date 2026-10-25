@echo off
setlocal
set "SSBU_DATA_ROOT="
set "SSBU_INPUT="
set "SSBU_NET_HOST="
set "SSBU_NET_RELAY="
set "SSBU_NET_BIND="
set "SSBU_NET_ROOM="
rem Plays online through Steam in one shared lobby: everyone who starts this with the same game
rem version is in "NeoSmash Australia" together. In the game, Local Wireless lists every room the
rem players in it create; create one, or pick one. Nobody hosts anything by hand: whoever is in
rem the lobby longest runs it from their PC (another takes over when they leave, and rooms in
rem progress then end). Steam must be running and signed in; you show as playing Spacewar.
rem The game's log and the bridge's messages go to <version folder>\build\play.log.
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
rem (the lobby server, should this PC be the one to run it: tools\ssbu_relay.py)
set "SSBU_STEAM_RELAY_CMD=%PY% -I "%~dp0tools\ssbu_relay.py""
"%~dp0build\steam\neosmash_steam.exe" lobby --region au --build %SSBU_BUILD% -- "%SSBU_TREE%\build\ssbu.exe" "%SSBU_TREE%\dump\modules" 2> "%SSBU_TREE%\build\play.log"
rem (a crash ends with a negative code, which "if errorlevel 1" does not see)
set "SSBU_RESULT=%ERRORLEVEL%"
if not "%SSBU_RESULT%"=="0" (
    echo The game or the Steam bridge reported an error ^(exit code %SSBU_RESULT%^). The end of %SSBU_TREE%\build\play.log:
    powershell -NoProfile -Command "Get-Content -LiteralPath '%SSBU_TREE%\build\play.log' -Tail 15"
    pause
)
