"""Select the observed shader working set for GPU object preparation.

The whole-game DXBC compilation inventory remains separate: eagerly creating all
of its speculative interface variants can overwhelm startup and continue in play.
"""
import argparse
from pathlib import Path
import re
import sys

# (the embeddable Python keeps a script's own folder off sys.path)
sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_effects as effects
import shader_aot as aot


def select_names(cache, version, logs=(), previous=()):
    selected = effects.played_names(cache, version)
    candidates = set(previous)
    for log in logs:
        candidates.update(re.findall(r"\[shader-key\] (\S+)", log))
    pattern = re.compile(rf"v{version}_(vs|ps|cs)_[0-9a-f]{{16}}_[0-9a-f]{{8}}_[0-9a-f]{{16}}\.dxbc")
    # Do not read all speculative bytecode just to select a small working set.
    for name in candidates - selected:
        if not pattern.fullmatch(name):
            continue
        file = cache / name
        if not file.is_file():
            continue
        with file.open("rb") as stream:
            if file.stat().st_size <= 32 or stream.read(4) != b"DXBC":
                raise ValueError(f"invalid observed shader cache file: {file}")
        selected.add(name)
    return sorted(selected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=Path(aot.ROOT))
    parser.add_argument("--scan-history", action="store_true", help="also recover observed keys from saved diagnostic logs under build/")
    parser.add_argument("--effects-profile", type=Path, help="merge a bounded predictive effect manifest into a separate startup-selected list")
    args = parser.parse_args()
    tree = args.tree.resolve()
    out = tree / "dump/effect_profiles/startup-observed/shaders.txt"
    log_paths = tree.joinpath("build").rglob("*.log") if args.scan_history else (tree / "build/play.log",)
    logs = [path.read_text(encoding="utf-8", errors="replace") for path in log_paths if path.is_file()]
    previous = out.read_text(encoding="ascii").splitlines() if out.is_file() else ()
    names = select_names(tree / "dump/shadercache", aot.cache_version(), logs, previous)
    if not names:
        raise SystemExit("No observed shader cache yet. Play once with the normal launcher first.")
    out.parent.mkdir(parents=True, exist_ok=True)
    temporary = out.with_suffix(".tmp")
    temporary.write_text("\n".join(names) + "\n", encoding="ascii")
    temporary.replace(out)
    print(f"Startup preparation: {len(names)} observed shaders; whole-game compilation cache retained.\n{out}")
    if args.effects_profile:
        effect_names = set(args.effects_profile.read_text(encoding="ascii").splitlines())
        # Validate the requested cache files without marking speculative effects
        # as played or persisting them into the observed-only history.
        selected = select_names(tree / "dump/shadercache", aot.cache_version(), previous=effect_names | set(names))
        if not effect_names <= set(selected):
            raise SystemExit("predictive effect profile has missing or invalid cache entries")
        selected_out = tree / "dump/effect_profiles/startup-selected/shaders.txt"
        selected_out.parent.mkdir(parents=True, exist_ok=True)
        temporary = selected_out.with_suffix(".tmp")
        temporary.write_text("\n".join(selected) + "\n", encoding="ascii")
        temporary.replace(selected_out)
        print(f"Startup selection: {len(selected)} observed/predictive shaders.\n{selected_out}")


if __name__ == "__main__":
    main()
