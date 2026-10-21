# NeoSmash / WindowSmash handoff context

Updated 2026-10-21 (Australia/Brisbane). Read this before changing the renderer.

**Latest (2026-10-21, after 08:00): stutter root cause and fix.** See
[docs/NATIVE_DRIVER_CACHE.md](docs/NATIVE_DRIVER_CACHE.md). The remaining match freezes were
the AMD driver compiling each shader's Vulkan pipeline library at its first draw: 20-850 ms
cold, under 5 ms once in the driver's disk cache, which is keyed by the executable's path.
`ssbu.exe --native-warm <list> <cache dir>` (no game boot) and `tools/warm_native_cache.py`
warmed all 65,050 entries. Every play launcher (through `tools/play_setup.bat`) and
`Prepare Shaders.bat` runs it; after the first pass, only new entries are warmed. Bounded validation: 0 slow Presents (10-25 before); every gameplay window from
50 to 180 s peaked at 16.8-17.8 ms.
User session afterwards (`build/logs/play.after-native-warm-20261021.log`): "almost perfectly", 0 slow
Presents (previously 26). The only hitches left are at transitions: boot, 100 ms at match load, 66/83 ms entering the next screen.

Latest shader audit: [docs/SHADER_COVERAGE_AUDIT.md](docs/SHADER_COVERAGE_AUDIT.md).
The current published inventory is **65,050** reflected entries; observed-only history
contains **4,904**, and active startup selection contains **7,668** entries.
Whole-game discovery now uses every byte alignment (26,639 source
blobs, 64,940 static keys). An all-file 28.39 GiB archive audit found the older
alignment rules were missing variants; 26 invalid candidates were excluded. Historical
counts below describe the earlier optimization steps, not the current inventory.

Latest stutter follow-up: [docs/PRESENT_STALL_FOLLOWUP.md](docs/PRESENT_STALL_FOLLOWUP.md). The current launchers
also prepare bounded recent-fighter effects and use `startup-selected/shaders.txt`,
separate from observed-only history. The first selected set was 7,599 entries,
including 4,829 observed shaders. DXVK descriptor-heap Auto trials still stalled and
were not adopted; `tools/effects-dxvk.conf` retains legacy bindings. The executable
was rebuilt with an optional diagnostic `SSBU_INPUT_RELATIVE=1` (scripts start at
first input poll); normal gameplay input behavior is unchanged.

Current snapshot (counts change when more gameplay is observed):

| Item | Current value |
| --- | --- |
| Whole-game compilation inventory | 65,050: 18,519 vertex, 46,518 pixel, 13 compute |
| Observed-only history | 4,904 entries |
| Active startup selection | 7,668: 4,270 vertex, 3,385 pixel, 13 compute |
| Recent-fighter prediction | 3,668 effect variants from 1,645 source blobs / 8 containers |
| Executable | `build/ssbu.exe` with DXVK 3.1.1 beside it, the only build (cleanup 2026-10-21 replaced the old native one and `build/effects/`) |
| Regression validation | 53 tests passed (incl. `test_native_warm.py`); all 7,668 selected entries passed reflection |
| GPU driver cache | All 65,050 entries warmed for `build/ssbu.exe` (`dump/effect_profiles/native-warm/`) |

These are preparation/coverage results. No controlled FPS gain or complete stutter
elimination has been demonstrated.

## Objective and current state

Run the user's own Super Smash Bros. Ultimate dump on this Windows PC, with SDL3
controller support and fewer first-use effects freezes. The user reports much better
play, with some remaining stalls. Stutter elimination is **not** established.

Repository: `H:\Workspace\NeoSmash\WindowSmash`, upstream
`https://github.com/SNO2207/WindowSmash`. The checkout contains uncommitted source
changes and important untracked source files. No commit, push or PR was requested.
Do not reset or clean this working tree. See [MANIFEST.md](MANIFEST.md).

Current game: 13.0.3; main build ID
`730C2141EEBF9885E4857E7E50EAE441D46925DF`. Game inputs are supplied by the user;
this source project does not supply keys, firmware or game assets.

## Machine and constraints

- Intel i7-14700K, 32 GB RAM, AMD RX 7900 XTX 24 GB, 180 Hz display.
- Keep the current AMD driver. The user explicitly declined a driver replacement.
- Preserve shader caches and the original save. Runtime tests use `SSBU_DATA_ROOT`
  with a copied save and bounded process lifetime. Do not kill unrelated game windows.
- Original save: `save/0123456789abcdeffedcba9876543210/save_data/system_data.bin`.
- Controller support: SDL 3.4.18; a PS4 controller was detected in recent runs.
- No background thread dumps during timing runs: `--status` suspends game threads.
- Do not equate fewer buffer discards, fewer uploads or cache-file counts with FPS
  gains. Distinguish DXBC compilation, GPU object creation and native driver pipelines.
- Randomized fighters/stages and screenshot readbacks prevent controlled before/after
  performance percentages. Preserve the user's log before any launcher overwrites it.

## Launchers

`Play SSBU Offline.bat` is the offline launcher; `Play SSBU.bat` (online lobby) and
`Host`/`Join SSBU Netplay.bat` start the same `build/ssbu.exe`. All of them call
`tools/play_setup.bat`, which makes the recent-fighter/observed startup list, warms the
driver cache and sets the renderer environment (startup manifest, eager compute
residency, DXVK config `tools/effects-dxvk.conf`, DXVK logs in `build/`). The game log
is `build/play.log`, which the setup reads for the next start's prediction. Periodic
`--status` thread dumps were removed from the launchers. The former experimental,
whole-game and performance launchers were removed in the 2026-10-21 cleanup.

`Prepare Shaders.bat` builds the complete discoverable compilation list. It is not
necessary before every match. `Unlock All Fighters.bat` supports verified 11.0.0,
13.0.3 and 13.0.5 save layouts, rejects invalid saves/running games, and makes a backup.
Both utility launchers preserve error codes; `SSBU_NO_PAUSE=1` suppresses their pause.

## Retained renderer work

- Header validation rejects false shader candidates; shader-cache version remains 10.
- Shader discovery scans all 803 unique regional `.eff/.nushdb/.arc` containers at
  every byte alignment: 26,639 source programs yield 64,940 static keys. The exhaustive
  audit read 176,004 regional files / 28.39 GiB with zero unreadable files. It added
  17,383 newly compiled supported entries and retained older validated coverage,
  including 34 entries no longer enumerated at changed source boundaries. The 26
  unsupported/invalid candidates outside known containers remain quarantined.
- Removed the 16-interface-mask cap and lossy packed-key deduplication from the AOT
  compiler. Baseline comparison found zero omitted baseline keys and 80 regional additions.
- D3DReflect validated all 65,050 published entries and their stages. The earlier
  16-byte scan had 22,732 programs, 46,992 static keys and a 47,613-entry manifest.
- Per-draw parameter rings reduce discards; exact descriptor state caches and bounded
  composed-buffer snapshots avoid repeated work with conservative invalidation.
- DXVK config keeps graphics pipeline libraries in Auto mode and 16 compiler workers.
- GPU startup combines observed keys with bounded recent-fighter predictions. The former
  full-object load timed out at 180 s and continued during gameplay. Both preparation
  waits now honor `SSBU_EFFECT_WAIT_SECONDS`; object timeout stops startup. Progress
  includes memory use. `SSBU_PREWARM_ALL=1` explicitly restores the larger working set.
- Prepared objects actually used in gameplay now mark their cache files as played.
- Cached compute pipelines receive zero-work indirect dispatches and a completion
  query. Worker CPU quiescence is only a heuristic for graphics-driver readiness.

## Latest evidence and active work

Preserved latest user log: `build/logs/play.before-present-followup-20261021.log`;
matching driver log: `build/logs/dxvk.before-present-followup-20261021.log`.
Startup loaded 4,620 objects in 1.9 s, workers settled after 6.1 s, and all 13 compute
preparations passed. No compute-learning readbacks or FXC failures were logged.
All 642 exact shader keys were already in the full compilation inventory, but runtime
object coverage recorded 556 hits / 86 misses. Compilation coverage alone therefore
did not prevent first-use GPU/driver work.

Across 15 timing windows: 40 intervals exceeded 25 ms, 33 exceeded 50 ms, and 30
exceeded 100 ms; maximum 1,316.69 ms includes startup/transitions. There were 26 slow
foreground Present calls, up to 802.2 ms, each within 12 log lines of a new shader
key. This association does not prove shader compilation caused every stall. Two F4
dumps added about 16,900 draw-log lines and GPU readbacks; the later 19.9 FPS window
includes capture overhead, which cannot explain earlier Present stalls.

The delivered launcher builds predictions from up to eight recent distinct valid
fighter module names in the latest log. Current names are `ganon, popo, nana,
inkling, packun, peach, wolf` (Ganondorf, Ice Climbers, Inkling, Piranha Plant,
Peach, Wolf). Nana has no separate effect container. Shared effects are included;
the next matchup and stage are not automatically detected. Predictions merge only
into `startup-selected`; they do not pollute observed-only history or mark unused
cache files as played. Actual gameplay use still updates played timestamps.

Bounded validation: `build/recent-effects-legacy-validation-20261021/`, 120 seconds,
copied save. The initial 7,599 objects loaded in 2.3 s (private memory 737 -> 2,181 MB),
workers settled in 30.5 s, and 13 compute preparations passed. Final counters were
430 hits / 56 misses, no FXC failures. Stalls remained. The capture showed Bayonetta
versus Mario on Midgar, different from the predicted fighters; many stalls were in
the background, and one interval included a screenshot. This is not a controlled
comparison against the user's foreground session. Refreshing coverage afterward
produced the current 4,904 observed / 7,668 selected entries.

Isolated DXVK descriptor-heap Auto trials were rejected: the cached 180-second run
still recorded 25 slow Presents and 30 intervals over 100 ms. The first full trial
waited 170.8 s for workers; cached startup settled in 3.9 s. Different matchups and
background windows prevent a performance claim. Retain `tools/effects-dxvk.conf`:
16 compiler threads, graphics pipeline library Auto, descriptor heap False,
descriptor buffer False. The isolated Auto config is diagnostic evidence only.

Experimental launcher defaults (if unset): composed reuse and eager compute residency
enabled, compile device and D3D driver threads disabled, shader-key/API-time/frame
statistics logging enabled, watchdog disabled. No periodic full `--status` dumps.
Whole-game preparation waits up to 3,600 seconds; local preparation 600 seconds.
Object preparation uses the same limit and aborts startup on timeout. Driver-worker
quiet is a heuristic, not a native graphics-pipeline completion fence.

Detailed evidence and trial paths are in the follow-up report and manifest. All
diagnostic processes stopped; the original save, caches and display driver remain
preserved. Further work needs a repeatable foreground match without F4 captures,
then GPU/driver tracing to distinguish native pipeline work from resource waits.

## Historical compute-readback evidence

User log: `build/logs/play.before-compute-residency-20261021.log` (preserved from
the then experimental `play.log`). Startup completed 4,139 objects in 0.9 s; there were no
FXC failures. Final profile counts: 669 hits, 246 misses. Most frame percentiles were
17 ms, but one compute-learning readback blocked 3,477.66 ms and two later readbacks
for the same `cs_86185ec386f9dcb8` blocked 589.14 / 551.12 ms. Many foreground
Present stalls remain. A monitor-wait failure later switched pacing to the clock.

Refreshing observed coverage now includes all 915 exact keys from that user run;
startup grew to 4,385 entries, including newly observed compute variants. After the
current trial's additional observed keys, it was refreshed again; see validation
for the final count.

An opt-in conservative compute ownership trial (`SSBU_COMPUTE_RESIDENT_EAGER=1`)
uses the existing synchronized GPU-resident buffer path for every storage binding of
a compute writer from its first dispatch, instead of eight full-buffer readback
learning samples. It leaves the normal renderer default unchanged and honors
`SSBU_NO_RESIDENT`. If a writable binding cannot use residency, results still take
the readback fallback. This also fixes the existing learned-path fallback losing
writes when resident allocation/view creation fails. Validation and default launch
policy are recorded in `docs/HANDOFF_VALIDATION.md`. The experimental launcher now
defaults this flag to 1 if unset; setting it to 0 retains the prior eight-sample
behavior for comparison. This is an experimental default, not a full correctness
or performance certification.

## Rebuild and verify

Python: `.venv\Scripts\python.exe` (3.11). Clang-cl comes from Visual Studio 2022
Build Tools; `tools/cc.py` locates it. DXVK and SDL DLLs are local generated/dependency
artifacts; preserve them. Full build instructions: [docs/BUILDING.md](docs/BUILDING.md).

From the repository root in PowerShell, relink the executable:

```powershell
.\.venv\Scripts\python.exe -c 'import sys,pathlib;sys.path.insert(0,"tools");import recomp;dirs=[str(pathlib.Path("build")/n) for n in ("main","subsdk0","sdk")]+[str(p) for p in sorted(pathlib.Path("build/nro").iterdir()) if p.is_dir()];sys.exit(0 if recomp.link(dirs,"build/ssbu.exe") else 1)'
```

Focused tests: `test_shader*.py`, `test_effect_profile.py`, `test_startup_shaders.py`,
`test_gpu_state_cache.py`, `test_gpu_upload_snapshot.py`; unlock safety tests:
`test_unlock.py`. Run via `python -m unittest discover -s tests -p <pattern>` or a
combined unittest suite. Native tests require the Windows SDK/clang-cl. Also run
`git diff --check`. Do not rebuild game modules without a reason: relinking current
objects is sufficient for renderer changes.

The latest combined regression run passed 44 tests, including prediction bounds,
observed-history separation and preservation of unplayed timestamps.

`build/perf_run.py` provides bounded copied-save diagnostic runs with `--exe`,
`--seconds` and repeated `--env NAME=value`. It no longer adds full thread dumps.
It randomizes the observed matchup through menu inputs; it is not a benchmark.
It enables diagnostic `SSBU_INPUT_RELATIVE=1` so preparation time does not consume
the scripted menu schedule.

## Next investigation

1. Read `docs/PRESENT_STALL_FOLLOWUP.md`, `docs/HANDOFF_VALIDATION.md` and the newest
   logs before assuming which executable and preparation ran.
2. Retest the user's affected fighters/stage in the foreground, repeating the same
   match without F4 captures. Confirm prediction coverage for that matchup.
3. Separate slow Present/driver pipeline specialization from readback synchronization
   and first-use assets. CPU frame counters alone cannot identify all GPU stalls.
4. Keep the eight-sample baseline available when comparing compute residency; inspect
   geometry/effects and memory, not just wait counts. Broader graphics correctness
   and aliasing coverage remain important for the eager trial.
5. Do not adopt the rejected descriptor Auto profile without new evidence. Update
   this context, manifest and validation document after each meaningful result.

No new driver installation, cache deletion, live save rewrite, public upload or
handoff to another chat has been performed as part of this handoff preparation.
