# Latest stutter follow-up — 2026-10-21

## User-session evidence

Preserved as `build/logs/play.before-present-followup-20261021.log`, with the
matching DXVK log in `build/logs/dxvk.before-present-followup-20261021.log`.

This session used the updated experimental executable (built 06:41), not the older
normal executable. All 4,620 startup objects loaded in 1.9 s, the driver-worker
heuristic settled after 6.1 s, and 13 compute preparations completed. There were
**zero logged compute-learning readbacks and zero FXC failures**.

Fifteen timing windows contained 40 intervals over 25 ms, 33 over 50 ms and 30 over
100 ms, including startup/transitions and diagnostic captures. Maximum interval:
1,316.69 ms. There were 26 slow Present calls, all foreground in this user log;
the largest was 802.2 ms. Final prepared-object statistics were 556 hits / 86 misses.
All 642 exact shader keys already existed in the 65,050-entry compilation inventory.

Every slow Present occurred within 12 log lines after a new shader-key message.
Timestamp evidence also associates the pauses with recently first-used keys. This
is an association, not proof that shader compilation caused every stall: native
pipeline specialization and first-use resources remain possible causes.

Two F4 frame dumps produced about 16,900 draw-log lines and GPU captures. A later
stats window reported 19.9 FPS / 63.8 ms renderer time around that work. These
captures introduce pauses but do not explain the earlier Present stalls.

## Delivered change

The launcher now builds a predictive profile for up to eight fighter modules loaded
in the latest game log, plus common fighter/stage effects. It scans their stored
programs at every byte alignment and prepares effects not yet observed as attacks.
This addresses a gap in an observed-only working set without loading all 65,050
GPU objects. This is prediction from recent play, not automatic detection of the
next selected fighters or stage.

For this user log: Ganondorf (`ganon`), Popo/Nana, Inkling, Piranha Plant (`packun`),
Peach and Wolf. Nana has no separate effect container; shared/Popo effects and real
observed keys remain covered. Eight existing containers produced 1,645 source blobs
and **3,668** static variants, all already cached and valid.

New paths:

- `dump/effect_profiles/recent-fighter-effects/`: bounded predictive effect profile.
- `dump/effect_profiles/startup-selected/shaders.txt`: observed history + predictions;
  this is now the normal experimental launcher's active manifest.
- `startup-observed/shaders.txt` remains observed-only. Unplayed predictions are not
  dated as played or persisted into this history; actual runtime use still marks them.

The first combined selection had 7,599 entries (4,829 observed). After diagnostic
coverage refresh, the final list is **7,668 entries**, with **4,904 observed**.
Every final selected entry passed D3DReflect and stage validation, and all keys
from the latest user session and subsequent validation are covered.

## DXVK descriptor experiment — not adopted

The retained `tools/effects-dxvk.conf` forces legacy descriptor bindings. An isolated
copy using Auto selected descriptor heaps on this driver. DXVK documents Auto as
selecting supported known-good descriptor models; it was tested without forcing an
unsupported feature or changing the display driver.

The first full preparation loaded 4,706 objects in 11.9 s and waited 170.8 s for
workers. A cached restart loaded 4,725 in 0.9 s and settled after 3.9 s, but still
logged 25 slow Presents / 30 intervals over 100 ms across 17 report windows. Most
of those stalls were background-window observations, while the user's run was
foreground, and the matchups/stages differed. No controlled improvement was
established, so the descriptor change was **not** installed in the play profile.

Evidence: `build/descriptors-auto-ready-validation-20261021/` and
`build/descriptors-auto-cached-validation-20261021/`. Shorter probe/aborted captures
were preparation diagnostics, not completed match tests.

Primary references:

- [DXVK 3.1.1 descriptor and pipeline options](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf)
- [DXVK graphics pipeline specialization/fast linking](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/dxvk/dxvk_graphics.cpp)

## Runtime and regression validation

`build/recent-effects-legacy-validation-20261021/game.log`: 120-second bounded run
with a copied save, retained legacy config, no full thread dumps, and one screenshot.
All 7,599 objects loaded in 2.3 s (private memory 737 -> 2,181 MB), workers settled
after 30.5 s, and all 13 compute preparations completed. Startup finished before play.
The first larger preparation therefore added roughly half a minute of driver work.

A battle capture at game time about 70 s shows Bayonetta versus Mario on Midgar,
with no obvious geometry corruption. These fighters differ from the predictive
profile, so this run does not establish its effectiveness on the user's affected
matchups. Final recorded object statistics: 430 hits / 56 misses, zero FXC failures.
First-use Present stalls remained, including background observations. The capture
window itself had an 888.35 ms interval and is not clean performance evidence.
All newly observed keys were retained in the final list.

**44 regression tests passed**, including bounded recent-fighter selection and
preservation of observed-only history and unplayed timestamps. The experimental
executable was rebuilt with optional diagnostic `SSBU_INPUT_RELATIVE=1`, making
script timing relative to first input poll; the diagnostic harness enables it so
long preparation does not invalidate menu scripts. Normal input behavior is unchanged.

The original save and installed AMD driver were not modified. All diagnostic game
instances stopped. Existing shader bytecode and caches were preserved.

## Remaining work

Stutters are not fully eliminated. Retest the same affected fighters and stage with
the updated launcher, avoiding F4/capture keys during timing collection. The new
prediction targets recent fighters; unseen stages, models, interfaces and driver
pipeline states can still require first-use work. A foreground, repeatable match
and GPU/driver trace are needed to attribute remaining Present stalls rather than
continuing to infer their cause from cache counts alone.
