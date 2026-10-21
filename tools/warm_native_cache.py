"""Warm the GPU driver's own shader cache for build/ssbu.exe under DXVK.

Compiling DXBC ahead of time is not enough: under DXVK the driver still compiles each
shader's Vulkan pipeline library the first time the game creates it, and when that is
in the middle of a match the next Present waits 20-850 ms for it (measured on an RX 7900
XTX: under 5 ms once the driver has the shader in its disk cache). The driver keys that
cache by the program's path, so the game's own executable does the warming:
`ssbu.exe --native-warm <list> <cache dir>` creates the listed shaders on a device with no
window or game, waits for DXVK's workers and exits (runtime/gpu_native_warm.h).

This runs it in batches (each process keeps every library it made) over the whole-game
inventory plus the startup lists, and records what is done, so it resumes after an
interruption and later runs only warm new entries. A different DXVK, DXVK configuration
or display driver starts again from nothing.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
# (the embeddable Python keeps a script's own folder off sys.path)
sys.path.insert(0, str(ROOT / "tools"))
NAME = re.compile(r"v(\d+)_(vs|ps|cs)_[0-9a-f]{16}_[0-9a-f]{8}_[0-9a-f]{16}\.dxbc")
LISTS = ("startup-selected", "startup-observed", "whole-game")


def wanted_names(tree, version):
    """Every listed shader, the startup lists first (the most likely to be played)."""
    names, seen = [], set()
    for profile in LISTS:
        path = tree / "dump/effect_profiles" / profile / "shaders.txt"
        if not path.is_file():
            continue
        for name in path.read_text(encoding="ascii").split():
            match = NAME.fullmatch(name)
            if match and int(match.group(1)) == version and name not in seen:
                seen.add(name)
                names.append(name)
    return names


def driver_version():
    """The display driver's version: a new driver has an empty cache of its own."""
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-Command",
                              "(Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name + ' ' + $_.DriverVersion }) -join ';'"],
                             capture_output=True, text=True, timeout=60).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        out = ""
    return out or "unknown"


def fingerprint(exe, config):
    digest = hashlib.sha256()
    digest.update(str(exe.resolve()).lower().encode())
    for path in (exe.parent / "d3d11.dll", exe.parent / "dxgi.dll", config):
        digest.update(path.read_bytes() if path.is_file() else b"-")
    digest.update(driver_version().encode())
    return digest.hexdigest()


def game_running(exe):
    """Whether this very executable is running (the driver's cache is shared with it)."""
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-Command",
                              "Get-Process ssbu -ErrorAction SilentlyContinue | ForEach-Object { $_.Path }"],
                             capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return False
    target = str(exe.resolve()).lower()
    return any(line.strip().lower() == target for line in out.splitlines())


def run_batch(exe, batch_file, cache, config, log_dir, log_path, wait_seconds):
    env = dict(os.environ)
    env.update({
        "DXVK_CONFIG_FILE": str(config),
        "DXVK_LOG_PATH": str(log_dir),
        "SSBU_SHADER_CACHE": str(cache),
        "SSBU_EFFECT_WAIT_SECONDS": str(wait_seconds),
        "SSBU_D3D_DRIVER_THREADS": "0",
    })
    flags = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)   # (the PC stays usable)
    failed = []
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        process = subprocess.Popen([str(exe), "--native-warm", str(batch_file), str(cache)], cwd=exe.parent, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", creationflags=flags)
        for line in process.stdout:
            log.write(line)
            if line.startswith("[warm] cannot create "):
                failed.append(line.split()[-1])
            elif line.startswith(("[warm]", "[gfx] effect preparation: still")):
                print("   ", line.rstrip(), flush=True)
        code = process.wait()
    return code, failed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tree", type=Path, default=ROOT)
    parser.add_argument("--exe", type=Path, help="default: build/ssbu.exe")
    parser.add_argument("--config", type=Path, help="default: tools/effects-dxvk.conf (the play launchers' DXVK settings)")
    parser.add_argument("--batch", type=int, default=8000, help="shaders per process (bounds memory: about 2-3 GB)")
    parser.add_argument("--limit", type=int, default=0, help="warm at most this many (testing)")
    parser.add_argument("--force", action="store_true", help="forget what is recorded as done")
    args = parser.parse_args()
    tree = args.tree.resolve()
    exe = (args.exe or tree / "build/ssbu.exe").resolve()
    config = (args.config or tree / "tools/effects-dxvk.conf").resolve()
    cache = tree / "dump/shadercache"
    state = tree / "dump/effect_profiles/native-warm"
    if not exe.is_file() or not (exe.parent / "d3d11.dll").is_file():
        raise SystemExit(f"{exe} with DXVK beside it is needed; build the game first (Build NeoSmash.bat) and put DXVK beside it.")
    import shader_aot as aot
    names = wanted_names(tree, aot.cache_version())
    if not names:
        raise SystemExit("No shader lists yet: run Prepare Shaders.bat (whole-game list) first.")

    state.mkdir(parents=True, exist_ok=True)
    done_path, print_path = state / "done.txt", state / "fingerprint.txt"
    print_now = fingerprint(exe, config)
    done = set()
    if not args.force and print_path.is_file() and print_path.read_text(encoding="ascii").strip() == print_now and done_path.is_file():
        done = set(done_path.read_text(encoding="ascii").split())
    elif done_path.is_file():
        print("GPU driver cache: the executable path, DXVK, its settings or the display driver changed (or --force); warming everything again.")
        done_path.unlink()
    print_path.write_text(print_now + "\n", encoding="ascii")
    todo = [name for name in names if name not in done and (cache / name).is_file()]
    if args.limit:
        todo = todo[:args.limit]
    if not todo:
        print(f"GPU driver cache: all {len(names)} shaders already warm.")
        return 0
    if game_running(exe):
        raise SystemExit("The game is running; close it first (it shares the driver's cache).")

    batches = [todo[i:i + args.batch] for i in range(0, len(todo), args.batch)]
    print(f"GPU driver cache: warming {len(todo)} of {len(names)} shaders in {len(batches)} batches "
          f"(once per DXVK/driver version; it can be stopped and resumed).", flush=True)
    start = time.time()
    for index, batch in enumerate(batches, 1):
        batch_file = state / "batch.txt"
        batch_file.write_text("\n".join(batch) + "\n", encoding="ascii")
        print(f"  batch {index}/{len(batches)}: {len(batch)} shaders", flush=True)
        code, failed = run_batch(exe, batch_file, cache, config, state, state / "warm.log", 3600)
        if code not in (0, 1):
            raise SystemExit(f"GPU driver cache warming stopped (exit code {code}); see {state / 'warm.log'}. Run again to resume.")
        good = [name for name in batch if name not in set(failed)]
        with done_path.open("a", encoding="ascii") as out:
            out.write("".join(name + "\n" for name in good))
        if failed:
            print(f"  {len(failed)} shaders could not be created (see warm.log); they are skipped.")
        elapsed = time.time() - start
        left = elapsed / index * (len(batches) - index)
        print(f"  {index}/{len(batches)} batches in {elapsed / 60:.1f} min; about {left / 60:.0f} min left", flush=True)
    print(f"GPU driver cache: {len(todo)} shaders warmed in {(time.time() - start) / 60:.1f} min.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
