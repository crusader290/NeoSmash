@echo off
rem Shared by the play launchers (Play SSBU, Play SSBU Offline, Host/Join SSBU Netplay): readies
rem the shaders of this folder's build\ssbu.exe and sets how it renders. Run from the port's
rem folder with SSBU_TREE set (tools\pick_version.bat); exits with 1, after saying why, on failure.
rem
rem Under DXVK (d3d11.dll beside build\ssbu.exe) the GPU driver compiles each shader once more
rem at first use: 20-850 ms freezes mid-match unless it is in the driver's cache already
rem (docs\NATIVE_DRIVER_CACHE.md). So, before starting:
rem   - the shaders of recently played fighters and those the game has used go in the start-up list
rem     (made before play: dump\effect_profiles\startup-selected);
rem   - every shader of the whole-game list is warmed into the driver's cache, once (minutes the
rem     first time, nothing afterwards). SSBU_NATIVE_WARM=0 skips it.
if not exist "%SSBU_TREE%\build\d3d11.dll" exit /b 0
call tools\find_python.bat || exit /b 1
%PY% -u tools\prepare_effects.py --include-played --extra-fighter mario --extra-fighter ness || exit /b 1
%PY% -u tools\prepare_effects.py --recent-fighters || exit /b 1
%PY% -u tools\prepare_startup_shaders.py --effects-profile "%~dp0..\dump\effect_profiles\recent-fighter-effects\shaders.txt" || exit /b 1
if not "%SSBU_NATIVE_WARM%"=="0" (
    %PY% -u tools\warm_native_cache.py || exit /b 1
)
rem Which shaders each of the game's containers holds: the game then prepares a container's shaders
rem in the background as it loads it, and holds the loading screen until they are ready (instant
rem when up to date; without a whole-game list the game just goes without it).
%PY% -u tools\container_index.py
set "SSBU_EFFECT_MANIFEST=%~dp0..\dump\effect_profiles\startup-selected\shaders.txt"
set "SSBU_EFFECT_WAIT_SECONDS=600"
set "SSBU_SHADER_CACHE=%~dp0..\dump\shadercache"
set "SSBU_SHADER_KEYS=1"
set "SSBU_COMPOSED_REUSE=1"
rem Conservative first-dispatch residency avoids eight blocking learning readbacks.
if not defined SSBU_COMPUTE_RESIDENT_EAGER set "SSBU_COMPUTE_RESIDENT_EAGER=1"
set "SSBU_API_TIMES=1"
set "SSBU_COMPILE_DEVICE=0"
set "SSBU_D3D_DRIVER_THREADS=0"
set "SSBU_WATCHDOG=0"
set "SSBU_FRAME_TIMES=1"
set "SSBU_LOG_STATS=1"
set "DXVK_CONFIG_FILE=%~dp0effects-dxvk.conf"
set "DXVK_LOG_PATH=%~dp0..\build"
exit /b 0
