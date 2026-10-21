# NeoSmash / WindowSmash — local Windows build

This checkout builds the user's own Super Smash Bros. Ultimate dump as a Windows
program. The current installation is **13.0.3**. It renders through DXVK, with the GPU
driver's shader cache warmed ahead of time; this removed the mid-match first-use freezes
(see [the root cause](docs/NATIVE_DRIVER_CACHE.md)).

Start with [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) for the handoff, then
[MANIFEST.md](MANIFEST.md) for source files and local artifacts.

## Play this installation

Double-click **Play SSBU Offline.bat**. Before the game starts, it:
- prepares the shaders of recently played fighters;
- warms any shader the GPU driver has not compiled yet. That takes about 30–40 minutes
  the first time, or after a display-driver or DXVK update, and nothing afterwards.

The game's log is `build/play.log`.

| Task | Entry point |
| --- | --- |
| Play offline | `Play SSBU Offline.bat` |
| Play online (lobby in `online_server.txt`) | `Play SSBU.bat` |
| Netplay with friends | `Host SSBU Netplay.bat`, `Join SSBU Netplay.bat` ([NETPLAY.md](NETPLAY.md)) |
| Compile all game shaders (once per game version) | `Prepare Shaders.bat` |
| Unlock base fighters, with a backup | `Unlock All Fighters.bat`, while the game is closed |
| Set up, build, bundle | `Install WindowSmash.bat`, `Build WindowSmash.bat`, `Configure Game Data.bat`, `Bundle Latest SSBU.bat` |
| Controls, display settings, save locations | [Playing guide](docs/PLAYING.md) |
| Fresh source build | [Build guide](docs/BUILDING.md) |

All play launchers share `tools\play_setup.bat`. `SSBU_NATIVE_WARM=0` skips the
driver warm-up.

## Current local work

- Exact shader-key deduplication and uncapped pixel interface discovery; strict
  program-header validation removes false compilation candidates.
- Whole-game discovery: 803 unique regional containers, 26,639 source blobs and
  64,940 static keys. The validated inventory now has 65,050 entries; see the
  [coverage audit](docs/SHADER_COVERAGE_AUDIT.md). Startup combines observed shaders
  with a bounded effect profile for recently played fighters; see the
  [latest stutter follow-up](docs/PRESENT_STALL_FOLLOWUP.md).
- Observed startup preparation replaces the former large speculative GPU-object
  load that timed out and continued during matches.
- Parameter upload rings, descriptor caches and exact composed-buffer reuse reduce
  repeated renderer work. Their work-count reductions are not FPS improvement claims.
- Optional controller support through SDL3, and fixed utility launcher error handling.
- Current compute readback/residency validation is documented in the handoff report;
  consult it before treating an experiment as a proven fix.

## Develop and continue

Use the local `.venv\Scripts\python.exe` and Visual Studio clang-cl. The repository
contains generated game objects, local dependencies and uncommitted/untracked source
changes; **do not reset, clean or delete the cache** to start a handoff. Build and test
commands, machine constraints and next investigations are in the project context.

Preserve original saves. Use a copied save and a bounded run for renderer tests.
Keep the installed AMD driver: the user declined a driver replacement. Preserve
`build/play.log` before launching another run, which overwrites it.

## Scope and notices

This source does not supply game assets, keys or firmware. Game-derived builds,
extracted data, caches and saves stay local and are excluded from source handoff.
See [third-party notices](NOTICE.md); no new project-wide license is implied.

The upstream repository is [SNO2207/WindowSmash](https://github.com/SNO2207/WindowSmash).
The previous Windows and Linux READMEs are preserved as
[README-UPSTREAM.md](README-UPSTREAM.md) and
[README-LINUX-UPSTREAM.md](README-LINUX-UPSTREAM.md). Those contain broader upstream
features and instructions, not claims that they were validated in this local effort.
