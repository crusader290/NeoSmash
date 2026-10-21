"""Audit every distinct regional archive file for additional stored VS/PS candidates.

Candidates remain separate from the published compilation/preparation manifests.
Use the native compiler's --source-report diagnostics, successful compilation and
check_shader_manifest.py before promoting candidates. A successful header match
or --dry key alone is not validation.
"""
import argparse
import json
from pathlib import Path
import time

import shader_aot as aot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, default=Path(aot.ROOT))
    args = parser.parse_args()
    tree = args.tree.resolve()
    out = tree / "dump/effect_profiles/archive-audit"
    out.mkdir(parents=True, exist_ok=True)
    archive = aot.A.Arc(str(tree / "dump/modules/romfs.cfg"))
    entries = aot.containers(archive, all_regions=True, all_files=True)
    total_bytes = sum(where[2] for _, where in entries)
    print(f"Auditing {len(entries):,} distinct regional files, {total_bytes / 2**30:.2f} GiB unpacked, every byte alignment.", flush=True)
    started = last = time.monotonic()
    done = unpacked = 0

    def progress(_ext, size):
        nonlocal done, unpacked, last
        done += 1
        unpacked += size
        now = time.monotonic()
        if now - last >= 10:
            print(f"Archive audit: {done:,}/{len(entries):,} files, {unpacked / 2**30:.2f}/{total_bytes / 2**30:.2f} GiB, {now-started:.0f} s", flush=True)
            last = now

    stats = {}
    programs, unread = aot.find_programs(archive, stats, entries=entries,
                                       byte_alignments=True, on_container=progress)
    aot.write_pack(programs, out / "programs.pack")
    report = dict(files=len(entries), unpacked_bytes=total_bytes, programs=len(programs),
                  unread=unread, scan=stats, seconds=round(time.monotonic()-started, 2),
                  scope="All file extensions, all regional variants, all byte alignments; stored VS/PS candidates only")
    (out / "scan.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report), flush=True)
    if unread:
        raise SystemExit("Some archive files could not be decoded; see scan.json")


if __name__ == "__main__":
    main()
