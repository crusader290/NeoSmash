"""Build versioned shader compilation manifests from the user's game archive.

This prepares DXBC; the optional Vulkan launcher also waits for driver workers.
Neither step guarantees that every render-state combination is compiled.
"""
import argparse
import datetime
import json
from pathlib import Path
import re
import subprocess
import sys
import time

import shader_aot as aot

PROFILE = "link-sephiroth-lylat"
PATHS = (
    "effect/fighter/link/ef_link.eff",
    "effect/fighter/edge/ef_edge.eff",
    "effect/system/common/ef_common.eff",
    "effect/stage/stgcommon/ef_stgcommon.eff",
    "effect/stage/fox_lylatcruise/ef_fox_lylatcruise.eff",
)


def recent_fighters(log, limit=8):
    """Bound the predictive working set to recent game-loaded fighter modules."""
    valid = set(aot.A.NRO_NAMES) - {"common", "item", "element"}
    names = []
    for name in re.findall(r"\[ro\] loaded lua2cpp_([a-z0-9]+) at", log):
        if name not in valid:
            continue
        if name in names:
            names.remove(name)
        names.append(name)
    return names[-limit:]


def cache_names(lines, version):
    names = set()
    for line in lines:
        match = re.fullmatch(r"(vs|ps) ([0-9a-f]{16})(?: ([0-9a-f]{8}))?", line)
        if not match:
            raise ValueError(f"invalid shader key: {line!r}")
        stage, digest, mask = match.groups()
        if (stage == "ps") != (mask is not None):
            raise ValueError(f"invalid shader interface: {line!r}")
        names.add(f"v{version}_{stage}_{digest}_{mask or '00000000'}_0000000000000000.dxbc")
    return sorted(names)


def cached_names(cache, version, *, played_only=False):
    """Validated current-version DXBC, including observed compute/dynamic variants."""
    cutoff = datetime.datetime(2002, 1, 1, tzinfo=datetime.timezone.utc).timestamp()
    pattern = re.compile(rf"v{version}_(vs|ps|cs)_[0-9a-f]{{16}}_[0-9a-f]{{8}}_[0-9a-f]{{16}}\.dxbc")
    result = set()
    for file in cache.glob(f"v{version}_*.dxbc"):
        if not pattern.fullmatch(file.name) or (played_only and file.stat().st_mtime < cutoff):
            continue
        with file.open("rb") as stream:
            if file.stat().st_size <= 32 or stream.read(4) != b"DXBC":
                raise ValueError(f"invalid shader cache file: {file}")
        result.add(file.name)
    return result


def played_names(cache, version):
    return cached_names(cache, version, played_only=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=Path(aot.ROOT))
    parser.add_argument("--include-played", action="store_true", help="also prepare previously used compute, model and other shaders")
    parser.add_argument("--extra-fighter", action="append", default=[], choices=("mario", "ness"))
    parser.add_argument("--whole-game", action="store_true", help="scan every shader container at every byte alignment and include known observed variants")
    parser.add_argument("--recent-fighters", action="store_true", help="prepare common effects and up to eight fighters loaded in the last play log")
    args = parser.parse_args()
    tree = args.tree.resolve()
    profile = "local-match-preparation" if args.include_played or args.extra_fighter else PROFILE
    if args.whole_game:
        profile = "whole-game"
    elif args.recent_fighters:
        profile = "recent-fighter-effects"
    paths = list(PATHS) + [f"effect/fighter/{fighter}/ef_{fighter}.eff" for fighter in sorted(set(args.extra_fighter))]
    out = tree / "dump/effect_profiles" / profile
    out.mkdir(parents=True, exist_ok=True)
    # Publish only after success. A failed expansion must not remove the last
    # validated working inventory used by the launchers.
    manifest = out / "shaders.txt"
    previous = set(manifest.read_text(encoding="ascii").splitlines()) if args.whole_game and manifest.is_file() else set()
    stats = {}
    pack = out / "programs.pack"
    archive = aot.A.Arc(str(tree / "dump/modules/romfs.cfg"))
    fighters = []
    if args.recent_fighters and not args.whole_game:
        paths = list(PATHS[2:4])
        log = tree / "build/play.log"
        fighters = recent_fighters(log.read_text(encoding="utf-8", errors="replace")) if log.is_file() else []
        for fighter in fighters:
            path = f"effect/fighter/{fighter}/ef_{fighter}.eff"
            if archive.locate(path):
                paths.append(path)
            else:
                print(f"No separate effect container for {fighter}; observed shaders remain covered.", flush=True)
        print("Recent fighter effects: " + (", ".join(fighters) or "shared effects only"), flush=True)
    entries = aot.containers(archive, all_regions=True) if args.whole_game else []
    for path in ([] if args.whole_game else paths):
        region = archive.locate(path)
        if not region:
            raise SystemExit(f"effect container missing: {path}")
        entries.append(("eff", region))
    print(f"Scanning {len(entries)} shader containers for {profile}...", flush=True)
    programs, unread = aot.find_programs(archive, stats, entries=entries,
                                       all_alignments=args.whole_game, byte_alignments=True)
    if unread or not programs:
        raise SystemExit(f"cannot prepare effects: {unread} unread containers, {len(programs)} programs")
    program_count, container_count = len(programs), len(entries)
    aot.write_pack(programs, pack)
    exe = Path(aot.ROOT) / "build/shaderaot.exe"
    aot.build_tool(str(exe))
    cache = tree / "dump/shadercache"
    cache.mkdir(exist_ok=True)
    command = [str(exe), str(pack), str(cache), "0", "1"]
    print(f"Preparing {program_count} programs for {profile}", flush=True)
    start = time.monotonic()
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    with (out / "compile.log").open("w", encoding="utf-8") as diagnostics:
        parts = aot.follow([process], aot.Progress(sys.stdout), diagnostics=diagnostics)
        diagnostics.write("\n".join(parts) + "\n")
    if process.returncode:
        raise SystemExit(f"shader compiler exited with {process.returncode}; see {out / 'compile.log'}")
    compiled_summary = "\n".join(parts)
    print(aot.summary(parts) or compiled_summary, flush=True)
    summary = re.search(r"(\d+) fxc failures, (\d+) not translated", compiled_summary)
    if not summary or any(int(n) for n in summary.groups()):
        raise SystemExit(f"effect compilation incomplete; see {out / 'compile.log'}")
    keys = subprocess.run(command + ["--dry"], capture_output=True, text=True, check=True)
    names = cache_names(keys.stdout.splitlines(), aot.cache_version())
    present, omitted = [], []
    for name in names:
        file = cache / name
        # Dynamic jump-table shaders deliberately have no static AOT entry.
        valid = False
        if file.is_file() and file.stat().st_size > 32:
            with file.open("rb") as stream:
                valid = stream.read(4) == b"DXBC"
        (present if valid else omitted).append(name)
    jump_tables = re.search(r"(\d+) with jump tables", compiled_summary)
    if omitted and (not jump_tables or len(omitted) > int(jump_tables[1])):
        raise SystemExit("prepared cache files are missing or corrupt; manifest not published")
    if not present:
        raise SystemExit("no valid cached effect shaders were produced")
    # Only promote extra cache keys actually observed in gameplay. An old cache
    # may contain speculative AOT variants that no validated source produces.
    played = played_names(cache, aot.cache_version()) if args.whole_game or args.include_played else set()
    history = tree / "dump/effect_profiles/startup-observed/shaders.txt"
    retained = set()
    if args.whole_game:
        valid_cache = cached_names(cache, aot.cache_version())
        retained = previous & valid_cache
        # Historical prewarm hits before timestamp tracking was fixed are still
        # genuine observed keys. Validate the cache before adding their names.
        if history.is_file():
            played |= set(history.read_text(encoding="ascii").splitlines()) & valid_cache
    additional = played - set(present)
    retained_only = retained - set(present) - played
    present = sorted(set(present) | played | retained)
    (out / "compilation-list.txt").write_text("\n".join(sorted(set(names) | played | retained)) + "\n", encoding="ascii")
    metadata = dict(profile=profile, shader_cache_version=aot.cache_version(),
                    containers="all .eff/.nushdb/.arc" if args.whole_game else paths,
                    container_count=container_count, all_alignments=args.whole_game,
                    byte_alignments=True, recent_fighters=fighters,
                    programs=program_count, shaders=len(present), discovered_static_keys=len(names),
                    shaders_by_stage={stage: sum(f"_{stage}_" in name for name in present) for stage in ("vs", "ps", "cs")},
                    added_played_shaders=len(additional), compute_shaders=sum("_cs_" in name for name in present),
                    retained_previous_shaders=len(retained_only),
                    omitted_dynamic_keys=omitted, scan=stats,
                    preparation_seconds=round(time.monotonic() - start, 2))
    if args.whole_game:
        metadata["coverage_limits"] = [
            "Archive scanning identifies stored vertex/pixel candidates; it does not prove every candidate is used.",
            "Compute programs require dispatch dimensions and are included when already observed in the runtime cache.",
            "Unseen jump-table values, shader interfaces and pipeline states can create additional runtime variants.",
            "Compiled DXBC and a complete manifest do not imply all GPU driver pipelines are compiled.",
        ]
    version_file = tree / "version.txt"
    if version_file.is_file():
        metadata["game_version"] = version_file.read_text(encoding="utf-8").splitlines()[0]
    (out / "profile.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    temporary = manifest.with_suffix(".tmp")
    temporary.write_text("\n".join(present) + "\n", encoding="ascii")
    temporary.replace(manifest)
    print(f"Prepared {len(present)} cached shaders; {len(omitted)} dynamic keys omitted.\n{manifest}", flush=True)


if __name__ == "__main__":
    main()
