# Handoff validation — 2026-10-21

**Subsequent coverage update:** [Shader audit](SHADER_COVERAGE_AUDIT.md) expanded the
whole-game inventory to 65,050 reflected entries and startup to 4,539 observed
entries. The focused suite now has 42 passing tests. The run measurements below
remain the evidence for the earlier renderer trial, not a new benchmark.

For the subsequent user session and current predictive startup selection, see
[Present stall follow-up](PRESENT_STALL_FOLLOWUP.md). The new focused suite has 44
passing tests. Descriptor-heap trials were not adopted.

## Delivered state

(Historical: the experimental executable was then `build/effects/ssbu.exe`; since the 2026-10-21 cleanup it is `build/ssbu.exe`.) Both effects
launchers use the observed startup working set. They default
`SSBU_COMPUTE_RESIDENT_EAGER=1` when unset; set it to `0` in the environment before
launch to compare with the former compute learning behavior. The ordinary native
executable is a separate, older build.

The final startup list has **4,495** validated cache entries, including **13 compute**
variants. It covers every exact key from the latest user log and the current trial.
The whole-game DXBC cache and compilation inventory remain intact. This does not
precompute unseen shader interfaces, compute sizes or all native driver pipelines.

## Latest user evidence

Preserved log: `build/logs/play.before-compute-residency-20261021.log`.
The user reported much better play with a few remaining stutters.

- 4,139/4,139 objects loaded before gameplay in 0.9 seconds.
- Final profile count: 669 prepared object hits and 246 misses; 915 unique keys.
- No reported FXC compilation failures.
- A synchronous compute-learning readback for `cs_86185ec386f9dcb8` took
  3,477.66 ms; later readbacks took 589.14 and 551.12 ms.
- Many foreground Present stalls also occurred. They are not fully diagnosed.

## Implemented fix and its boundaries

The experimental compute policy conservatively treats every storage binding of a
compute writer as potentially writable from its first dispatch. It uses existing
GPU residency, CPU write synchronization and explicit readback hooks, avoiding the
initial eight full-buffer readback learning samples. It does not infer that a buffer
is permanently read-only because a conditional store was absent in those samples.

If a writable binding falls back because residency/view creation fails, its result
is still read back. Previously that learned-path fallback could lose GPU writes.
The default normal-renderer policy remains unchanged; `SSBU_NO_RESIDENT` still forces
the diagnostic readback path. More buffers may remain resident, and broader resource
aliasing, CPU-consumer and effects correctness need coverage beyond this spot check.

## Runtime trial

`build/eager-compute-validation-20261021/game.log`: bounded 150-second run, copied
save, no periodic full thread dumps. The harness stopped only its own game process.

- 4,385/4,385 objects loaded in 1.6 s, private memory 734 -> 2,219 MB.
- Three distinct compute keys were exercised, including skinned fighter geometry.
- **Zero compute-learning readback calls logged**, zero resident-flush readbacks
  logged and zero FXC failures. API timings were enabled; this establishes absence
  of logged blocking readback calls in this trial, not zero possible GPU waits.
- 438 prepared object hits and 110 misses in the final recorded profile statistics.
  The additional observed keys were then added, producing the final 4,495-entry set.
- Captures at about 65/130 seconds show Bayonetta versus Lucas on Kongo Falls with
  no obvious geometry corruption. This is not complete graphics validation.
- Four consecutive report windows from 90.8 through 120.9 s had no interval over
  25 ms; maximums were 16.82–17.52 ms.
- Earlier windows still had Present stalls: the largest post-startup reported
  interval was 758.91 ms; startup/intro reached 1,300.01 ms. Screenshots themselves
  introduce readback overhead. The two captures are not clean timing samples.

The worst compute key from the user's session was not dispatched in this trial.
Different fighters/stages, cache history and durations prevent a controlled stutter
reduction percentage. The readback path is removed for resident bindings, but an
overall FPS/stutter improvement from the eager policy is **not yet established**.

## Source verification

**37 focused regression tests passed** across shader discovery/progress/native worker
partitioning, exact key sets, profile/manifest safety, startup selection/wait limits,
descriptor caches and composed-buffer snapshots. The new native compute-policy test
covers conservative masks, the 16-binding boundary, disabled mode, unconditional
learning readback and readback retained for a failed resident binding. The final
experimental executable was relinked after extracting those policy helpers.

The final 25-second copied-save startup smoke test
(`build/handoff-smoke-20261021/game.log`) loaded all **4,495/4,495** objects in
**0.8 s**, private memory 733 -> 2,039 MB. Driver workers settled after 3.9 s and
all **13** cached compute pipelines completed their zero-work GPU preparation in
0.02 s. This was a startup smoke test, not a further match benchmark. Both bounded
diagnostic processes have stopped.

All 21 local links in the rewritten/new entry-point documents resolved;
`git diff --check` passed. Current working-tree state is saved locally in
`build/HANDOFF_STATUS.txt`. The tracked patch is in `build/HANDOFF_TRACKED.patch`;
that patch excludes untracked source, so use the manifest and status list as well.

Earlier utility work passed seven save/unlock safety tests plus integration checks
on a copy of the actual save. Those were not rerun solely for this documentation
rewrite. Earlier D3DReflect validation covered the original 47,613-entry inventory.

## Handoff checklist and next work

- Read [PROJECT_CONTEXT.md](../PROJECT_CONTEXT.md), [MANIFEST.md](../MANIFEST.md) and
  the rebuilt launcher's settings before continuing.
- Keep tracked and untracked source changes. No reset, cleanup, commit, public
  upload, driver replacement or live save modification was requested or performed.
- Reproduce the user's affected match with the eager mode on/off, checking graphics,
  CPU/GPU coherency, memory use and timing. Explicitly exercise `cs_86185ec386f9dcb8`.
- Investigate foreground Present stalls separately; they persist even with no
  compute-learning readbacks. Capture driver/GPU evidence before claiming a cause.
- Preserve fresh user logs before another launch overwrites them. Continue expanding
  observed coverage without loading all speculative GPU objects during matches.

The source is ready for continuation. Stutters are **not fully resolved**, and Linux
has not been built or validated in this local work.
