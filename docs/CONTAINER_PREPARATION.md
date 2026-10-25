# Container preparation and the loading barrier — 2026-10-25

Shaders are now prepared from the game's own shader containers by background threads, and a
loading screen waits until what it loaded is ready. The startup preparation is on by default
inside the game. The design follows the one described by
[OpenSmash-Ultimate/ssbu-recomp](https://github.com/OpenSmash-Ultimate/ssbu-recomp)
(an index of the game's containers, announcing a container when the game shows a sign of it, a
loading barrier). That project renders NVN through Vulkan directly and is GPL-3.0; nothing of its
code is used here, only the approach, adapted to this D3D11/DXVK renderer.

## How it works

1. **Index** (`tools/container_index.py`, about 40 s; run by `Prepare Shaders.bat` and checked at
   each launch by `tools/play_setup.bat`). It scans all 803 shader containers in `data.arc` (every
   region, every byte alignment, as the whole-game preparation does). It hashes each program as the
   renderer keys its cache (`shaderaot --hashes`), and writes `dump/effect_profiles/containers/index.bin`:
   each container's byte range in `data.arc` and the cache files of its shaders. 640 containers hold
   shaders: 65,011 of the 65,054 inventory entries, 156,403 references.
2. **Announce** (`runtime/gpu_container_prep.h`, fed by `runtime/hle_fs.c`). Every read the game
   makes of `data.arc` is looked up in the index. The first read of a container queues all its
   shaders to the prewarm threads (queue 2, below the game's own requests). Containers read before
   the renderer exists, during boot, are queued as soon as it does.
3. **Barrier.** At Present, a frame with no compute dispatches (not a fight: fights skin their
   fighters with compute every frame) waits while announced shaders are still being made, then
   for DXVK's shader workers to go quiet. The hold is at most 20 s in all, and at most 5 s of it
   waiting for DXVK. A fight is never held. Netplay (`SSBU_NET_HOST` or `SSBU_NET_RELAY` set)
   turns the barrier off, so one player's loading screen can't hold up the others; preparation
   still runs.
4. **Default on.** Under DXVK the game itself now runs the startup preparation. With no
   `SSBU_EFFECT_MANIFEST` it uses `dump/effect_profiles/startup-selected/shaders.txt` (played plus
   recent-fighter shaders); with no list at all, the shaders the game has played. Before this, the
   preparation ran only when a launcher set the variable.

### Large containers are left to the play history

Ten global shader libraries that the game reads at boot hold 3,000–4,000 variants each (each pixel
program times every interface the file's vertex programs could give it); 27 containers hold more
than 1,000 variants. The game uses about 10% of them: 2,088 of 49,495 pixel variants and 3,399 of
9,666 vertex variants across those 27. Preparing them whole held the first boot for 93 s and grew
the process by 5 GB. Their used variants do not cluster on a few interface masks (each common mask
has about 4,600 variants, about 10% used), so no cheap filter exists. Containers over
`SSBU_CONTAINER_PREP_MAX` (default 1,000) variants are therefore not queued whole. Their used
variants come from the startup list, which `play_setup.bat` refreshes from the play history at
each launch. On a first run they load at their first draw.

## Measured (bounded copied-save runs, one scripted fight each)

| Run | Startup list | First-use shaders in the fight: prepared / not | Fight windows | Fight frames over 50 ms | Slow Presents |
| --- | --- | --- | --- | --- | --- |
| `build/prep-20261025/history3/` (300 s, watchdog on) | default | 711 / **5** | 23 (about 230 s of fighting) | **1** (66.6 ms) | **0** |
| `build/prep-20261025/history2/` (watchdog on) | default (8,125, found by the game itself) | 647 / **0** | 17 | 4: 797 ms barrier hold at the fight-to-results transition, 116 ms at fight start | **0** |
| `build/prep-20261025/firstrun/` | compute only (no play history) | 382 / 328 | 15 | 18 (340–850 ms) | 17, all with the window in the **background** |

- With the history, the barrier held 5 loading frames, 16.4 s in all (longest 5.1 s), and queued
  3,312 container shaders.
- In the first-run case, 195 of the 328 misses live only in large containers and 21 in no
  container. The rest were in small containers that weren't ready by first use. All 17 slow
  Presents came with the window not in the foreground; earlier investigations found background
  Presents stalling independently of shaders. That run is not a clean first-run measurement.
- Private memory reached about 7.7 GB in the history run (11,437 prepared objects plus the game's
  own). `SSBU_CONTAINER_PREP_MB` (default 8192) stops queuing above that.

## Settings

| Variable | Default | Meaning |
| --- | --- | --- |
| `SSBU_SHADER_PREWARM=0` | on | No startup preparation and no container preparation |
| `SSBU_EFFECT_MANIFEST` | `startup-selected` | Another startup list |
| `SSBU_CONTAINER_PREP=0` | on | No container preparation (and no barrier) |
| `SSBU_LOAD_BARRIER=0` / `=1` | on offline, off in netplay | The barrier off, or forced on in netplay |
| `SSBU_CONTAINER_PREP_MAX` | 1000 | Largest container queued whole |
| `SSBU_CONTAINER_PREP_MB` | 8192 | Private memory above which no more containers are queued |
| `SSBU_CONTAINER_INDEX` | `dump/effect_profiles/containers/index.bin` | Another index |

With `SSBU_INPUT_RELATIVE=1` (test scripts), the time the barrier holds frames doesn't count on
the script's clock, so menu inputs land on the screens they were written for.

## Open

- A clean first-run test with the window in the foreground.
- One bounded run (`build/prep-20261025/history/`, watchdog off) stopped producing frames at about
  90 s and was killed at its time limit. Two later runs with the watchdog on (220 s and 300 s) did
  not reproduce it, and no freeze report was written; its cause is **not established**. Another run
  was closed at 68 s by a window close request of unknown origin (no crash).
