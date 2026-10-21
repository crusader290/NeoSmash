# First-use stalls: the GPU driver's own shader cache — 2026-10-21

## Finding

The remaining match freezes are the AMD driver compiling a shader's Vulkan pipeline
library the first time the game creates it. DXBC coverage was already complete
(65,050 entries), but DXVK still has to hand each shader to the driver, and the
driver's compile happens on DXVK's command thread at the first draw, so the next
Present waits for it.

Measured with an offline probe (`ssbu`'s own DXVK 3.1.1 DLLs and `tools/effects-dxvk.conf`,
real VS/PS pairs from the latest user log, one draw each timed to GPU completion):

| Case | First draw |
| --- | --- |
| Pair created, DXVK workers settled (a prepared startup object) | ≤ 7 ms (756 draws, 4 blend states) |
| Never-compiled pixel shader, drawn right after creation (a mid-match miss) | 20–850 ms; 175 of 300 over 20 ms |
| The same pairs again, driver cache warm | ≤ 4.8 ms (150 of 150) |

The 618 ms stall in the user's first match window came with 15 misses: about 40 ms
each, in line with the cold measurements. Shader-pair linkage, render-target formats
(including B5G6R5/B5G5R5A1 and 4 MRT), depth-only draws, points/lines and depth bias
all fast-link (≤ 1 ms). Two states defeat DXVK's fast link and compile a full pipeline
synchronously: dual-source blending (up to 828 ms) and wireframe (up to 62 ms). The
logged game frames use neither.

The driver keys its cache by the program's path: the probe renamed `ssbu.exe` in
another folder got a new, empty cache file under `%LOCALAPPDATA%\AMD\VkCache`. The
game's executable must do the warming itself. Relinking the executable
keeps its cache (2,000 played shaders settled in 4.2 s right after a relink).

## Change

- `ssbu.exe --native-warm <list> <cache dir>` (`runtime/gpu_native_warm.h`, dispatched
  first thing in `host.c`'s `main`): creates the listed DXBC files on a windowless
  device with the game's flags, waits for DXVK's workers (the existing quiescence
  heuristic), exits. No game code, save or window is touched.
- `tools/warm_native_cache.py` runs it in 8,000-shader processes (about 2–3 GB each)
  over `startup-selected`, `startup-observed` and the whole-game list. It records
  finished names in `dump/effect_profiles/native-warm/done.txt` and resumes after an
  interruption. A different DXVK DLL, DXVK config, exe path or display driver
  version starts again. It refuses while this executable is running and runs at
  below-normal priority.
- The experimental launcher runs it before each start; with nothing new it returns
  at once. `SSBU_NATIVE_WARM=0` skips it. `Prepare Shaders.bat` runs it after
  building the whole-game list.

## Result

First full warm (`build/logs/native-warm-first-run.log`): 65,050/65,050 shaders, 0 failures,
16.3 min, nine processes of at most 5.1 GB private memory. The game's driver cache grew
from 1.54 to 2.89 GB (DXVK's own cache from 552 to about 700 MB); many entries were
already there from earlier full-Vulkan experiments. Re-running the last batch took
4.4 s instead of 78.7 s. 2,000 entries of the largest cold batch settled in 4.7 s with
the cache file unchanged, so there was no eviction.

Bounded copied-save match, 180 s, launcher settings, startup-selected manifest
(`build/native-warm-validation-20261021/game.log`). No capture was written, so the
matchup is unconfirmed; the last fighter modules loaded were Mario and Bayonetta, which
an earlier validation run had also played. 21 shaders were still first-use misses,
which is the case that froze before:

| | Before (three comparable bounded runs) | After |
| --- | --- | --- |
| Slow Presents (>100 ms) | 10 / 21 / 25 | **0** |
| Gameplay windows 50–180 s, max interval | up to 953 ms | 16.8–17.8 ms in every window |
| Prepared misses | 56 / 104 / 110 | 21, all served from the warm driver cache |

What remains: the boot/intro window (one 1,316 ms interval at 0–10 s, present in every
earlier run), and one 100 ms interval at 30–40 s, while the game loaded the
`lua2cpp_bayonetta` module at match start. This is one bounded run, not a controlled
benchmark; the user's own foreground session is still to be confirmed.

## User session after the warm-up

Preserved as `build/logs/play.after-native-warm-20261021.log` (150 s, foreground,
launcher reported everything warm). The user reported it ran "almost perfectly".

- **0 slow Presents**; the previous user session had 26 (up to 802 ms).
- 51 first-use misses, 586 prepared hits, and no stall attributable to them.
- Every window from 40 to 130 s peaked at 16.8–17.7 ms.
- Remaining hitches, all at transitions: boot (1,332 ms, before play); 100 ms at
  30–40 s while fighter modules loaded for the match; 66 ms and 83 ms at 130–150 s,
  when 86 new shader keys appeared (the end of the match into the next screen) and a
  few draws took about 10 ms to prepare on the CPU.

The 51 new keys join the observed startup set at the next launch, so the same screens
will not miss again.

## Limits

Not covered: full pipelines for states that cannot fast-link (dual-source blending,
wireframe), new resources/textures, and shaders outside the 65,050-entry inventory
(the runtime still compiles those with FXC and skips their draws meanwhile). A new
display driver or DXVK version empties the driver's cache; the script detects that
and warms everything again.

## Single executable (cleanup, 2026-10-21)

`build/effects/` and the experimental, whole-game and performance launchers were removed.
`build/ssbu.exe`, the build `Build WindowSmash.bat` produces, now has DXVK beside it, and
all play launchers run `tools/play_setup.bat` before starting it. The driver keys its cache
by path, so the cache was warmed again for `build/ssbu.exe`
(`build/logs/native-warm-build-ssbu.log`).
Re-warm: 65,050 shaders in 30.6 min, 0 failures. The driver dropped the old path's cache
itself. Bounded copied-save check of `build/ssbu.exe` afterwards
(`build/single-exe-validation-20261021/`): one slow Present, at the first frame of boot;
gameplay windows from 50 to 150 s peaked at 17–28 ms; 117 ms at 30–40 s while fighter
modules loaded for the match.
