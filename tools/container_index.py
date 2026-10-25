"""Which shaders each of the game's shader containers holds, for the renderer's container preparation.

The renderer prepares a container's shaders on background threads as soon as the game reads any
of that container from data.arc (a loading screen), and holds the loading screen until they are
ready, so the fight that follows meets no shader for the first time (runtime/gpu_container_prep.h).
For that it needs, for each container (.eff, .nushdb, .arc in data.arc, every region), where its
bytes lie in data.arc and the disk-cache files of the shaders found in it. This scans the
containers as the whole-game preparation does (every byte alignment), hashes each program the
way the renderer keys its cache (shaderaot --hashes, nothing translated), and keeps the names of
the whole-game list that belong to each container.

    python tools/container_index.py [--tree DIR] [--force]

Writes dump/effect_profiles/containers/index.bin (format below) and index.json (what it was made
from); does nothing when those still match the archive and the whole-game list.

index.bin, little-endian:
    "SCX1", u32 shader cache version, u32 containers, u32 names, u32 references
    names x 64 bytes: cache file basenames, NUL padded
    containers x {u64 data.arc offset, u64 stored size, u32 first reference, u32 references},
        sorted by offset
    references x u32: indices into the names
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import time

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))   # (the embeddable Python keeps a script's own folder off sys.path)
import shader_aot as aot

NAME_SIZE = 64
NAME = re.compile(r"v(\d+)_(vs|ps)_([0-9a-f]{16})_[0-9a-f]{8}_[0-9a-f]{16}\.dxbc")


def write_index(path, version, names, containers):
    """containers: [(offset, stored, [name index, ...])]"""
    refs = []
    table = []
    for offset, stored, members in sorted(containers):
        table.append((offset, stored, len(refs), len(members)))
        refs.extend(members)
    with open(path, "wb") as f:
        f.write(b"SCX1" + struct.pack("<4I", version, len(table), len(names), len(refs)))
        for name in names:
            raw = name.encode("ascii")
            if len(raw) >= NAME_SIZE:
                raise ValueError(f"cache name too long: {name}")
            f.write(raw.ljust(NAME_SIZE, b"\0"))
        for entry in table:
            f.write(struct.pack("<QQII", *entry))
        f.write(struct.pack(f"<{len(refs)}I", *refs))


def read_index(path):
    """(version, names, [(offset, stored, [names])]): the inverse of write_index (tests)."""
    data = Path(path).read_bytes()
    if data[:4] != b"SCX1":
        raise ValueError("not a container index")
    version, ncont, nnames, nrefs = struct.unpack_from("<4I", data, 4)
    at = 20
    names = [data[at + i * NAME_SIZE:at + (i + 1) * NAME_SIZE].rstrip(b"\0").decode("ascii") for i in range(nnames)]
    at += nnames * NAME_SIZE
    table = [struct.unpack_from("<QQII", data, at + i * 24) for i in range(ncont)]
    at += ncont * 24
    refs = struct.unpack_from(f"<{nrefs}I", data, at)
    return version, names, [(o, s, [names[r] for r in refs[first:first + n]]) for o, s, first, n in table]


def names_by_program(lines, version):
    """(stage, program hash) -> the cache names of that program in the list."""
    out = {}
    for name in lines:
        m = NAME.fullmatch(name)
        if m and int(m[1]) == version:
            out.setdefault((m[2], m[3]), []).append(name)
    return out


def source_fingerprint(tree, whole):
    digest = hashlib.sha256()
    for path in (tree / "dump/modules/romfs.cfg", whole):
        digest.update(path.read_bytes())
    return digest.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tree", type=Path, default=TOOLS.parent)
    ap.add_argument("--force", action="store_true", help="build it again even if it is up to date")
    args = ap.parse_args()
    tree = args.tree.resolve()
    whole = tree / "dump/effect_profiles/whole-game/shaders.txt"
    if not whole.is_file():
        raise SystemExit("No whole-game shader list yet: run Prepare Shaders.bat first.")
    out = tree / "dump/effect_profiles/containers"
    index, about = out / "index.bin", out / "index.json"
    version = aot.cache_version()
    fingerprint = source_fingerprint(tree, whole)
    if not args.force and index.is_file() and about.is_file():
        try:
            if json.loads(about.read_text(encoding="utf-8")).get("fingerprint") == fingerprint:
                print("Shader container index: up to date.")
                return 0
        except ValueError:
            pass
    start = time.monotonic()
    arc = aot.A.Arc(str(tree / "dump/modules/romfs.cfg"))
    entries = aot.containers(arc, all_regions=True)
    print(f"Shader container index: scanning {len(entries)} containers (every byte alignment)...", flush=True)
    found = []
    programs, unread = aot.find_programs(arc, entries=entries, all_alignments=True, byte_alignments=True,
                                         on_programs=lambda entry, pids: found.append((entry[1][0], entry[1][1], pids)))
    if unread:
        raise SystemExit(f"{unread} containers could not be read")
    exe = tree / "build/shaderaot.exe"
    aot.build_tool(str(exe))
    with tempfile.TemporaryDirectory(prefix="neosmash-index-") as tmp:
        pack, hashes = Path(tmp) / "programs.pack", Path(tmp) / "hashes.tsv"
        aot.write_pack(programs, pack)
        subprocess.run([str(exe), str(pack), tmp, "0", "1", "--hashes", str(hashes)], check=True)
        program_hash = {}
        for line in hashes.read_text(encoding="ascii").splitlines():
            record, stage, h = line.split("\t")
            program_hash[int(record)] = (stage, h)
    by_program = names_by_program(whole.read_text(encoding="ascii").split(), version)
    names, slot = [], {}
    containers = []
    for offset, stored, pids in found:
        members = set()
        for pid in pids:
            for name in by_program.get(program_hash.get(pid), ()):
                if name not in slot:
                    slot[name] = len(names)
                    names.append(name)
                members.add(slot[name])
        if members:
            containers.append((offset, stored, sorted(members)))
    out.mkdir(parents=True, exist_ok=True)
    temporary = index.with_suffix(".tmp")
    write_index(temporary, version, names, containers)
    temporary.replace(index)
    refs = sum(len(c[2]) for c in containers)
    about.write_text(json.dumps({"fingerprint": fingerprint, "shader_cache_version": version, "containers": len(containers),
                                 "containers_scanned": len(entries), "names": len(names), "references": refs,
                                 "seconds": round(time.monotonic() - start, 1)}, indent=2) + "\n", encoding="utf-8")
    print(f"Shader container index: {len(containers)} containers with shaders, {len(names)} shaders, {refs} references "
          f"in {time.monotonic() - start:.0f} s.\n{index}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
