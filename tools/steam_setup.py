"""Set up the Steam bridge (tools/steam_bridge.c) in build/steam/: build it, and put beside it
steam_appid.txt (Spacewar, 480) and a steam_api64.dll.

Steam's API library is Valve's redistributable; it ships with many Steam games. This copies the
newest one found in the installed Steam libraries that Valve signed (Authenticode) and that has
every function the bridge calls; --dll names one instead. It stays local (build/ is ignored).

    python tools/steam_setup.py [--dll PATH] [--force]
"""
import argparse
import glob
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
OUT = ROOT / "build/steam"
NEEDED = ["SteamAPI_InitFlat", "SteamAPI_SteamMatchmaking_v009", "SteamAPI_SteamNetworkingMessages_SteamAPI_v002",
          "SteamAPI_SteamFriends_v018", "SteamAPI_SteamUser_v023", "SteamAPI_SteamNetworkingUtils_SteamAPI_v004",
          "SteamAPI_ManualDispatch_Init", "SteamAPI_SteamNetworkingMessage_t_Release"]


def exports(path):
    """The names a PE DLL exports."""
    data = Path(path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    export_rva = struct.unpack_from("<I", data, pe + 24 + 112)[0]   # PE32+ data directory 0
    table = []
    for i in range(sections):
        s = pe + 24 + optional + 40 * i
        size, va, raw_size, raw = struct.unpack_from("<IIII", data, s + 8)
        table.append((va, max(size, raw_size), raw))

    def off(rva):
        for va, size, raw in table:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError("bad RVA")
    if not export_rva:
        return set()
    e = off(export_rva)
    count, names = struct.unpack_from("<I", data, e + 24)[0], off(struct.unpack_from("<I", data, e + 32)[0])
    out = set()
    for i in range(count):
        n = off(struct.unpack_from("<I", data, names + 4 * i)[0])
        out.add(data[n:data.index(b"\0", n)].decode("ascii", "replace"))
    return out


def valve_signed(path):
    ps = ("$s = Get-AuthenticodeSignature -LiteralPath $env:SB_DLL; "
          "if ($s.Status -eq 'Valid' -and $s.SignerCertificate.Subject -match 'O=Valve') { 'yes' } else { 'no' }")
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True,
                       env={**os.environ, "SB_DLL": str(path)}, timeout=60)
    return r.stdout.strip() == "yes"


def steam_libraries():
    roots = [Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Steam"]
    vdf = roots[0] / "steamapps/libraryfolders.vdf"
    if vdf.is_file():
        roots += [Path(p.replace("\\\\", "\\")) for p in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(errors="replace"))]
    seen, out = set(), []
    for r in roots:
        key = str(r).lower()
        if key not in seen and (r / "steamapps/common").is_dir():
            seen.add(key)
            out.append(r / "steamapps/common")
    return out


def find_dll():
    candidates = []
    for common in steam_libraries():
        for depth in ("*", "*/*", "*/*/*", "*/*/*/*"):
            candidates += glob.glob(str(common / depth / "steam_api64.dll"))
    good = []
    for c in candidates:
        try:
            if all(n in exports(c) for n in NEEDED):
                good.append(Path(c))
        except (OSError, ValueError, struct.error):
            pass
    good.sort(key=lambda p: (p.stat().st_mtime, p.stat().st_size), reverse=True)
    for c in good[:8]:
        if valve_signed(c):
            return c
    return None


def build_bridge():
    vcvars = glob.glob(r"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Auxiliary\Build\vcvars64.bat")
    if not vcvars:
        raise SystemExit("Visual Studio's build tools were not found (docs/BUILDING.md).")
    OUT.mkdir(parents=True, exist_ok=True)
    cmd = (f'"{vcvars[0]}" >nul && clang-cl /nologo /O2 /W3 "{TOOLS / "steam_bridge.c"}" '
           f'/Fe:"{OUT / "neosmash_steam.exe"}" /Fo:"{OUT}\\\\" ws2_32.lib winmm.lib')
    r = subprocess.run(f'cmd /d /c "{cmd}"', capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-4000:], r.stderr[-2000:])
        raise SystemExit("building the Steam bridge failed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dll", type=Path, help="a steam_api64.dll to use")
    ap.add_argument("--force", action="store_true", help="rebuild and copy again")
    args = ap.parse_args()
    exe = OUT / "neosmash_steam.exe"
    if args.force or not exe.is_file() or exe.stat().st_mtime < (TOOLS / "steam_bridge.c").stat().st_mtime:
        build_bridge()
        print(f"Built {exe}")
    (OUT / "steam_appid.txt").write_text("480\n", encoding="ascii")
    dll = OUT / "steam_api64.dll"
    if args.dll or args.force or not dll.is_file():
        source = args.dll or find_dll()
        if not source:
            raise SystemExit("No suitable steam_api64.dll found in your Steam libraries. Pass one with --dll "
                             "(Valve-signed, from a recent Steam game or the Steamworks SDK).")
        if not all(n in exports(source) for n in NEEDED):
            raise SystemExit(f"{source} is too old for the bridge.")
        if not valve_signed(source):
            raise SystemExit(f"{source} is not signed by Valve; not used.")
        shutil.copy2(source, dll)
        print(f"Steam API: {source}")
    print(f"Steam bridge ready in {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
