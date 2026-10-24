"""Two games on this PC play a Local Wireless match against each other through a lobby on loopback.

Alpha creates a room, Bravo finds it in the Rooms List and asks to join, Alpha accepts, and both
then pick fighters, and Alpha (the room's host) starts the battle with L+R. A lobby
(tools/ssbu_relay.py) runs here on 127.0.0.1 unless --lobby names one. Each game gets a copy of
save/ and filters packets as a router would (SSBU_NET_TEST_NAT=1), so the direct connection is
punched for real. No router, firewall or save change. Windows-native counterpart of
tools/netlobby.sh, which covers menus only.

    python tools/netmatch.py <out prefix> [--seconds 300] [--lobby host:port] [--prepared] [--steam-test]

--steam-test plays through the Steam bridge (tools/steam_bridge.c, build/steam/) in its UDP test
mode instead of a lobby: Alpha hosts a private room behind one bridge, Bravo joins through another.

Logs: <out>-lobby.log, <out>-alpha.log, <out>-bravo.log; frames in <out>-alpha/ and <out>-bravo/.
Prints each check and PASS or FAIL.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
# Title screen, then the main menu's cursor to Smash from wherever it starts (the save remembers
# it), then right into the side menu, which opens on Local Wireless from there (13.0.3). The side
# menu wraps around, so counting from another tile is not reliable.
NAV = ("9:A,13:UP,13.5:UP,14:LEFT,14.5:LEFT,15:RIGHT,15.5:RIGHT,16:RIGHT,16.5:RIGHT,17:RIGHT,"
       "18.6:A,21.5:PLUS")
JOIN_OFFSET = 40   # seconds Bravo starts after Alpha: it asks to join at about Alpha's 71 s


def presses(start, end, host, every=9.0, starts=6):
    """Through the room's dialogs and fighter select into the battle: each cycle pushes the stick
    up (the cursor starts on the player's own panel, below the fighters), picks with A, and in
    the first few cycles also presses + (done picking). Everyone then waits in a practice area
    until the room's host holds L+R, which starts the battle."""
    t, out, cycle = start, [], 0
    while t + 7 <= end:
        out += [f"{t:g}:SUP/0.3", f"{t + 1:g}:A"] + ([] if host else [f"{t + 5:g}:A"])
        if cycle < starts:
            out.append(f"{t + 3:g}:PLUS")
        if host:
            out += [f"{t + 6:g}:L/1", f"{t + 6:g}:R/1"]
        t += every
        cycle += 1
    return out   # (the game takes 128 scripted presses at most)


def game_env(who, relay, out, seconds, prepared):
    env = dict(os.environ)
    data = ROOT / f"build/testdata-{who.lower()}"
    env.update({
        "SSBU_NET_RELAY": relay, "SSBU_NET_NAME": who, "SSBU_NET_LOG": "1", "SSBU_NET_TEST_NAT": "1",
        "SSBU_DATA_ROOT": str(data), "SSBU_INPUT_RELATIVE": "1", "SSBU_LOG_STATS": "1", "SSBU_FRAME_TIMES": "1",
        "SSBU_CAPTURE": f"{out}-{who.lower()}", "SSBU_CAPTURE_EVERY": "3", "SSBU_CAPTURE_MAX": str(seconds // 3 + 1),
        "SSBU_SHADER_CACHE": str(ROOT / "dump/shadercache"), "SSBU_WATCHDOG": "0",
        "DXVK_CONFIG_FILE": str(ROOT / "tools/effects-dxvk.conf"), "DXVK_LOG_PATH": str(Path(out).parent),
    })
    if not prepared:   # (no start-up shader preparation: menu timing stays as scripted)
        env["SSBU_SHADER_PREWARM"] = "0"
    else:   # the launchers' startup preparation (tools/play_setup.bat)
        env.update({"SSBU_EFFECT_MANIFEST": str(ROOT / "dump/effect_profiles/startup-selected/shaders.txt"),
                    "SSBU_EFFECT_WAIT_SECONDS": "600", "SSBU_COMPOSED_REUSE": "1", "SSBU_COMPUTE_RESIDENT_EAGER": "1",
                    "SSBU_COMPILE_DEVICE": "0", "SSBU_D3D_DRIVER_THREADS": "0"})
    for name in ("SSBU_NET_HOST", "SSBU_NET_ROOM", "SSBU_NET_DIRECT", "SSBU_NET_BIND"):
        env.pop(name, None)
    return env


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="output prefix, e.g. build/net/match")
    ap.add_argument("--seconds", type=int, default=300, help="Alpha's run time (Bravo's is 40 s less)")
    ap.add_argument("--lobby", help="host:port of a lobby to use instead of one here")
    ap.add_argument("--port", type=int, default=39511)
    ap.add_argument("--prepared", action="store_true", help="with the launchers' shader preparation")
    ap.add_argument("--exe", default=str(ROOT / "build/ssbu.exe"))
    ap.add_argument("--steam-test", action="store_true", help="through the Steam bridge's UDP test mode")
    ap.add_argument("--env", action="append", default=[], metavar="NAME=VALUE", help="more environment for both games")
    args = ap.parse_args()
    out = str(Path(args.out).resolve())
    args.exe = str(Path(args.exe).resolve())
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    for who in ("alpha", "bravo"):
        shutil.rmtree(f"{out}-{who}", ignore_errors=True)
        Path(f"{out}-{who}").mkdir()
        data = ROOT / f"build/testdata-{who}"
        shutil.rmtree(data, ignore_errors=True)
        data.mkdir(parents=True)
        if (ROOT / "save").is_dir():
            shutil.copytree(ROOT / "save", data / "save")

    lobby = None
    relay = args.lobby
    if not relay and not args.steam_test:
        relay = f"127.0.0.1:{args.port}"
        lobby = subprocess.Popen([sys.executable, "-I", str(ROOT / "tools/ssbu_relay.py"), "--bind", "127.0.0.1",
                                  "--port", str(args.port), "--verbose", "--stats", "10"],
                                 stdout=open(f"{out}-lobby.log", "w"), stderr=subprocess.STDOUT)
        time.sleep(1)

    try:
        return run(args, out, relay, lobby)
    finally:
        if lobby:
            lobby.terminate()
            lobby.wait()


def run(args, out, relay, lobby):
    end = args.seconds - 10
    alpha_input = ",".join([NAV, "25:A,28.5:PLUS,36:A", "85:A", *presses(90, end, host=True)])
    bravo_input = ",".join([NAV, "30:DOWN,31:A", *presses(55, end - JOIN_OFFSET, host=False)])
    games = []
    bridge = str(ROOT / "build/steam/neosmash_steam.exe")
    build = next((line.split()[1] for line in (ROOT / "version.txt").read_text().splitlines() if line.startswith("main ")), "")
    game = [args.exe, str(ROOT / "dump/modules")]
    commands = {"Alpha": [bridge, "host", "--udp-test", f"{args.port + 1}:0", "--port", str(args.port), "--build", build, "--", *game],
                "Bravo": [bridge, "join", "--udp-test", f"{args.port + 2}:{args.port + 1}", "--port", str(args.port + 3),
                          "--build", build, "--", *game]} if args.steam_test else {"Alpha": game, "Bravo": game}
    for who, script, seconds, delay in (("Alpha", alpha_input, args.seconds, 0),
                                        ("Bravo", bravo_input, args.seconds - JOIN_OFFSET, JOIN_OFFSET)):
        time.sleep(delay)
        env = game_env(who, relay, out, seconds, args.prepared)
        env["SSBU_INPUT"] = script
        env.update(dict(e.split("=", 1) for e in args.env))
        if args.steam_test:   # (the bridge sets the game's network settings)
            for name in ("SSBU_NET_RELAY", "SSBU_NET_TEST_NAT"):
                env.pop(name, None)
        log = open(f"{out}-{who.lower()}.log", "w", encoding="utf-8", errors="replace")
        games.append((who, subprocess.Popen(commands[who], cwd=ROOT, env=env,
                                            stdout=log, stderr=subprocess.STDOUT), time.monotonic() + seconds))
        print(f"{who} started", flush=True)
    for who, proc, until in games:
        try:
            proc.wait(timeout=max(1, until - time.monotonic()))
        except subprocess.TimeoutExpired:   # (the whole tree: a bridge's game too)
            subprocess.run(["taskkill", "/T", "/F", "/PID", str(proc.pid)], capture_output=True)
            proc.wait()
    if lobby:   # (its log is complete before the checks)
        lobby.terminate()
        lobby.wait()

    A = Path(f"{out}-alpha.log").read_text(encoding="utf-8", errors="replace")
    B = Path(f"{out}-bravo.log").read_text(encoding="utf-8", errors="replace")
    ok = True

    def check(what, good, detail=""):
        nonlocal ok
        print(f"  {'ok' if good else 'MISSING'}: {what}{(' - ' + detail) if detail else ''}")
        ok = ok and good

    def after(text, marker):
        i = text.find(marker)
        return text[i:] if i >= 0 else ""

    if not args.steam_test:
        check("Alpha joined the lobby", "joined the lobby" in A)
    check("Alpha created a room", "network created by" in A)
    check("Bravo joined Alpha's room", "network: joined" in B)
    check("Alpha took Bravo in", "joined as node" in A)
    if not args.steam_test:
        check("they connected directly", "Bravo: direct, via public" in A and "Alpha: direct, via public" in B)
    # A game loads a fighter's code when its own player picks it, and the other players' fighters
    # when the battle starts: a fighter loaded in both games after they met means a battle.
    fa = set(re.findall(r"loaded lua2cpp_(\w+)", after(A, "joined as node"))) - {"common", "item"}
    fb = set(re.findall(r"loaded lua2cpp_(\w+)", after(B, "network: joined"))) - {"common", "item"}
    check("a battle started in both games", bool(fa & fb), f"Alpha loaded {sorted(fa)}, Bravo {sorted(fb)}")
    check("Bravo stayed in the room", "ldn: state 5 -> " not in B)
    bridged = re.findall(r"steam: joined; (\d+) sent, (\d+) received", B)
    if args.steam_test:
        check("Bravo's bridge carried the game's traffic", bool(bridged) and min(map(int, bridged[-1])) > 1000,
              f"last: {bridged[-1][0]} sent, {bridged[-1][1]} received" if bridged else "no bridge statistics")
    peers = [] if args.steam_test else re.findall(r"peer Bravo: (\w+)[^;]*; in (\d+) direct \+ (\d+) relayed, out (\d+) direct \+ (\d+) relayed, (\d+) dropped", A)
    if args.steam_test:
        pass
    elif peers:
        kind, ind, inr, outd, outr, dropped = peers[-1]
        check("game traffic kept flowing", int(ind) + int(inr) > 1000,
              f"last: {kind}, in {ind} direct + {inr} relayed, out {outd} + {outr}, {dropped} dropped")
    else:
        check("game traffic kept flowing", False, "no peer statistics")
    for who, text in (("Alpha", A), ("Bravo", B)):
        windows = re.findall(r"\[frames\] at ([\d.]+) s: .*max ([\d.]+) ms; >25/50/100 ms (\d+)/(\d+)/(\d+)", after(text, "loaded lua2cpp_") or text)
        slow = sum(int(w[4]) for w in windows)
        print(f"  {who}: {len(windows)} frame windows after fighters loaded, {slow} intervals over 100 ms, "
              f"slow Presents {text.count('slow Present')}")
    for line in (A + B).splitlines():
        if "exception" in line or "stopped during" in line:
            print("  ", line[:160])
    shutil.rmtree(ROOT / "build/testdata-alpha", ignore_errors=True)
    shutil.rmtree(ROOT / "build/testdata-bravo", ignore_errors=True)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
