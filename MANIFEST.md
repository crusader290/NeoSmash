# Local project manifest

Updated 2026-10-21. This is a map of the source changes and local artifacts, not a
redistributable game package or a cryptographic integrity manifest.

## Current handoff snapshot

| Item | State on 2026-10-21 |
| --- | --- |
| Game | 13.0.3; build ID `730C2141EEBF9885E4857E7E50EAE441D46925DF` |
| Compilation inventory | 65,050 reflected entries: 18,519 VS / 46,518 PS / 13 CS; cache version 10 |
| Discovery | 26,639 source blobs / 64,940 static keys / 803 known containers, every byte alignment |
| Observed-only history | 4,904 entries; predictions never persisted as played history |
| Active startup manifest | 7,668 entries: 4,270 VS / 3,385 PS / 13 CS |
| Predictive effects | 3,668 variants / 1,645 blobs / 8 containers, up to eight recent fighter names |
| Validation | 53 regression tests passed; all selected entries reflected successfully |
| Runtime outcome | Root cause: cold driver library compiles. After warming all 65,050, a bounded run and the user's session both had 0 slow Presents (was 10-25 / 26); transition hitches of at most 100 ms remain |
| Driver cache | 65,050 warmed for `build/ssbu.exe`; keep `%LOCALAPPDATA%\AMD\VkCache` |
| Constraints | Keep installed driver, original save and caches; source is committed to crusader290/NeoSmash `main` |

Latest user session: 4,620 prepared objects in 1.9 s, 6.1 s worker wait, all 13
compute preparations passed; 556 object hits / 86 misses, no FXC failures or logged
compute-learning readbacks. All 642 exact keys were already compiled. There were
26 slow foreground Presents (maximum 802.2 ms) and 30 intervals above 100 ms across
15 reporting windows. New-key proximity is correlation, not a proven cause. F4
captures added overhead in later windows but do not explain the earlier stalls.

Delivered prediction covers recent fighter modules plus shared effects, not the
next selected matchup/stage. Current fighters: Ganondorf, Ice Climbers, Inkling,
Piranha Plant, Peach and Wolf. The copied-save 120-second legacy-profile validation
loaded its initial 7,599 objects in 2.3 s and waited 30.5 s for workers; it still
stalled (430 hits / 56 misses, no FXC failures). Its Bayonetta/Mario matchup on
Midgar differed from the prediction and included background/capture observations.
Coverage was then refreshed to the counts above. No clean before/after comparison
or complete fix is claimed. All diagnostic games stopped.

Rejected experiment: DXVK descriptor heap/buffer Auto. Cached validation still had
25 slow Presents / 30 intervals above 100 ms. The installed play config retains
legacy bindings, 16 compiler workers and graphics pipeline library Auto. The normal
executable remains older than the experimental build. See
[the detailed follow-up](docs/PRESENT_STALL_FOLLOWUP.md) and
[continuation context](PROJECT_CONTEXT.md).

## Entry points and documentation

| Path | Purpose |
| --- | --- |
| `README.md` | Current local Windows entry point and status |
| `PROJECT_CONTEXT.md` | Decisions, constraints, build recipe and continuation steps |
| `docs/HANDOFF_VALIDATION.md` | Latest test evidence and unresolved limitations |
| `docs/SHADER_COVERAGE_AUDIT.md` | All-file audit, expanded inventory and current shader counts |
| `docs/PRESENT_STALL_FOLLOWUP.md` | Latest user log, descriptor trial and bounded predictive preparation |
| `docs/CONTAINER_PREPARATION.md` | Container preparation, loading barrier, default-on startup preparation |
| `docs/NATIVE_DRIVER_CACHE.md` | First-use stall root cause (driver library compile) and the native warm-up |
| `README-UPSTREAM.md` | Preserved upstream Windows README, before local rewrite |
| `README-LINUX.md` | Linux scope and links; not validated by this Windows effort |
| `README-LINUX-UPSTREAM.md` | Preserved original Linux instructions |
| `docs/PLAYING.md` | Controls, utilities, shader preparation and file locations |
| `docs/BUILDING.md`, `docs/TECHNICAL.md`, `NOTICE.md` | Build/runtime background and notices |

## Local source and launchers to preserve

Some are untracked. Include them when transferring source work; do not rely on
`git diff` alone. `git status --short` is authoritative for the checkout.

| Path | Responsibility |
| --- | --- |
| `runtime/gpu_d3d11.c` | Prewarm objects, uploads, state caching, compute residency and timing |
| `runtime/gpu_display.c` | Display/clock pacing, including 180 Hz divisor |
| `runtime/hle_hid.c` | Optional first-input-poll-relative clock for diagnostic scripts |
| `runtime/gpu_shader_cache.h` | Strict candidate validation; cache version 10 |
| `runtime/gpu_prewarm_manifest.h` | Canonical safe shader basenames |
| `runtime/gpu_prewarm_driver.h` | Shared preparation limits and driver-worker heuristic |
| `runtime/gpu_prewarm_compute.h` | Zero-work indirect compute preparation |
| `runtime/gpu_container_prep.h` | Container preparation (data.arc reads -> background shader jobs) and the loading barrier |
| `runtime/gpu_native_warm.h` | `ssbu.exe --native-warm`: windowless driver-cache warming, no game boot |
| `runtime/gpu_state_cache.h` | Exact descriptor caches |
| `runtime/gpu_upload_snapshot.h` | Bounded exact-byte composed-upload snapshots |
| `runtime/gpu_compute_policy.h` | Conservative compute ownership and safe readback fallback policy |
| `tools/shader_aot.py`, `tools/shaderaot.c` | Archive inventory and exact-key compilation |
| `tools/prepare_effects.py` | Full/local/recent-fighter profiles; every-byte discovery, bounded recent-module selection and compilation diagnostics |
| `tools/prepare_startup_shaders.py` | Publishes observed-only history, validates predictions separately, then merges active startup selection without promoting unused timestamps |
| `tools/audit_shader_archive.py`, `tools/check_shader_manifest.py` | Exhaustive archive audit and GPU-free bytecode reflection |
| `tools/effects-dxvk.conf` | Local DXVK profile |
| `tools/container_index.py` | Index of which shaders each data.arc container holds (`dump/effect_profiles/containers/`) |
| `tools/warm_native_cache.py` | Batched, resumable GPU driver cache warming for `build/ssbu.exe` |
| `tools/play_setup.bat` | Shared pre-launch step of every play launcher |
| `tools/find_python.bat` | Prefer installed local virtual environment |
| `tools/Unlock-All-Fighters.ps1`, `tools/unlock_all.py` | Validated version/save layouts |
| `Prepare Shaders.bat`, `Unlock All Fighters.bat` | Fixed utility dispatch and exit status |
| `Play SSBU Offline.bat`, `Play SSBU.bat`, `Host`/`Join SSBU Netplay.bat` | Play launchers; call `tools/play_setup.bat`, no `--status` dumps |
| `tests/test_shader*.py`, `tests/test_shader_aot_sets.c` | Discovery, exact sets, worker partitioning/progress |
| `tests/test_effect_profile.py`, `tests/test_startup_shaders.py` | Bounded recent-fighter selection, manifest/wait policy, observed-history and unplayed-timestamp preservation |
| `tests/test_gpu_state_cache.*`, `tests/test_gpu_upload_snapshot.py` | Cache/snapshot invariants |
| `tests/test_gpu_prewarm_compute.c`, `tests/test_unlock.py` | Compute preparation and save safety |
| `tests/test_container_index.py` | Container index format and program-to-name mapping |
| `tests/test_native_warm.py` | Warm list order, resume, failure skip, DXVK-change restart |

## Local generated/runtime files

These are ignored by Git and are needed for this installed game, not source-only
handoff. Do not distribute game-derived binaries/data, original saves or personal
configuration as part of a source handoff.

| Path | Purpose / handling |
| --- | --- |
| `build/ssbu.exe` | The game, relinked 2026-10-21 17:53 with `--native-warm`; DXVK `d3d11.dll`/`dxgi.dll` and SDL3 beside it |
| `dump/modules/`, `dump/exefs/`, game archive referenced by `romfs.cfg` | User's extracted game inputs |
| `dump/shadercache/` | Preserve compiled DXBC and played timestamps; do not clear casually |
| `dump/effect_profiles/whole-game/` | 64,940 discovered static keys; 65,050-entry validated inventory, pack and logs |
| `dump/effect_profiles/startup-observed/shaders.txt` | 4,904 observed entries; refreshes each experimental launch |
| `dump/effect_profiles/startup-selected/shaders.txt` | 7,668 observed-plus-predicted entries; active launcher manifest |
| `dump/effect_profiles/native-warm/` | `done.txt`/`fingerprint.txt` of the driver-cache warming, last batch log |
| `%LOCALAPPDATA%\AMD\VkCache`, `%LOCALAPPDATA%\dxvk` | Driver and DXVK caches keyed to `build/ssbu.exe`; preserve |
| `dump/effect_profiles/recent-fighter-effects/` | 3,668 variants for up to eight recently loaded fighter modules plus shared effects |
| `dump/effect_profiles/local-match-preparation/` | Four-fighter/shared effects compilation profile |
| `save/`, `save_backups/` | Original player save and unlock backups; preserve privately |
| `controllers.ini`, `display.ini`, `version.txt` | Machine settings and installed game version |
| `build/perf_run.py` | Local copied-save bounded diagnostic harness; input-relative scripts, no periodic thread dumps |
| `build/validate_launchers.py` | Copied-save launcher checks and D3DReflect inventory audit |
| `build/HANDOFF_STATUS.txt`, `HANDOFF_TRACKED.patch` | Local working-tree status and tracked diff snapshot (both under `build/`) |

## Evidence worth retaining privately

| Path | Evidence |
| --- | --- |
| `build/logs/` | Preserved play logs (before/after the fix, the native build's last log) and driver-cache warm logs |
| `build/logs/native-warm-first-run.log`, `build/native-warm-validation-20261021/` | First full driver-cache warm; 180-second post-warm validation run (0 slow Presents) |
| `build/logs/play.before-present-followup-20261021.log` | Latest preserved experimental user session with remaining foreground Present stalls |
| `build/logs/dxvk.before-present-followup-20261021.log` | Matching driver log for that user session |
| `build/latest-present-followup.json` | Latest session timing/coverage analysis |
| `build/latest-first-use-missing.keys` | 92 timestamp-inferred keys; distinct metric from the 86 recorded object misses |
| `build/recent-effects-legacy-validation-20261021/` | Copied-save 120-second predictive preparation validation; game/driver logs and capture |
| `build/dxvk-descriptors-auto.conf` | Isolated experimental config; rejected, not installed in play profile |
| `build/descriptors-auto-probe-20261021/`, `build/descriptors-auto-match-20261021/` | Short preparation probe and deliberately aborted trial; not completed matches |
| `build/descriptors-auto-ready-validation-20261021/` | 240-second wall trial; 170.8-second worker wait, limited gameplay window |
| `build/descriptors-auto-cached-validation-20261021/` | 180-second cached trial; persistent stalls, no controlled benefit established |
| `screenshots/frame_20261021_071633/screen.bmp` | User F4 capture: Peach/Wolf result screen; capture timing is intrusive |
| `build/logs/play.before-compute-residency-20261021.log` | Historical user session exposing the 3.48-second compute-learning readback |
| `build/eager-compute-validation-20261021/` | Earlier compute residency diagnostic and captures |
| `dump/effect_profiles/archive-audit/`, `build/shader-coverage-before-20261021/` | Coverage audit, rejected candidates and pre-expansion backup |
| `build/observed-startup-validation/` | Fixed startup gate; 4,131 objects in 1.2 s |
| `build/OBSERVED_STARTUP_FIX.md`, `build/WHOLE_GAME_SHADERS.md` | Prior findings and audit details |
| `build/REMAINING_STUTTERS.md`, `MATCH_STUTTER_FOLLOWUP.md`, `EFFECT_PREPARATION.md` | Earlier investigation reports (all in `build/`) |
| `build/composed-auto-validation/`, `build/composed-reuse-validation/` | Earlier upload reuse trials |

For an actual source transfer, review both tracked and untracked files, include the
source/test/documentation paths above, and retain third-party notices. A clean clone
will require the user's own game extraction and generated artifacts to run.
