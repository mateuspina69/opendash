#!/usr/bin/env python3
"""A room in the windowed game (hidden) and in a world process, with the same fighting pad: their world digests compared
tick by tick (app/world_process, game/world_digest). It prints the first tick that differs and whether anything
was really fought. Copies the game to run_dir and runs it from there, never from the build folder.

  tools/world_compare.py <build dir> <run dir> <save> ST:SUB [ST:SUB ...]   (--sweep: every room, world process only)
  tools/world_compare.py <build dir> <run dir> <save> --record <file>        every room's digests, as a baseline
  tools/world_compare.py <build dir> <run dir> <save> --check <file>         every room against that baseline
"""
import json
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

BITS = {"up": 0x10, "right": 0x20, "down": 0x40, "left": 0x80, "cross": 0x4000, "square": 0x8000}
ROUND = [(45, "up"), (20, "square"), (40, "up+square"), (12, "cross"), (30, "left+square"), (25, "square"),
         (40, "right+square"), (12, "cross"), (40, "down+square"), (30, "square")]
STEPS = ROUND * 4
TICKS = sum(n for n, _ in STEPS)
PAD_NAMES = "; ".join(f"{n} {b}" for n, b in STEPS)
PAD_BITS = "; ".join(f"{n} {sum(BITS[x] for x in b.split('+')):x}" for n, b in STEPS)
ENV = dict(os.environ, OD_WORLD_DIGEST="10", OD_WORLD_DIGEST_DETAIL="1")
PLAYER, ANIMATED, ANIMATED_END = 0xa71d20, 0xa6e3a0, 0xa6e3a0 + 32 * 0x16c


def prepare(build, run):
    os.makedirs(os.path.join(run, "saves"), exist_ok=True)
    for f in os.listdir(build):
        if f.endswith(".dll") or f == "opendash.exe":
            shutil.copy2(os.path.join(build, f), run)
    if not os.path.isdir(os.path.join(run, "assets")):
        shutil.copytree(os.path.join(build, "assets"), os.path.join(run, "assets"))
    skip = ("net", "net_port", "net_name", "net_key", "net_join", "texture_pack", "aa")
    lines = [l for l in open(os.path.join(build, "opendash.ini"), encoding="utf-8", errors="replace")
             if l.split("=", 1)[0].strip() not in skip]
    open(os.path.join(run, "opendash.ini"), "w", encoding="utf-8").writelines(lines + ["net=off\naa=none\n"])


def blocks(log):
    out, cur = {}, []
    for line in open(log, encoding="utf-8", errors="replace"):
        if line.startswith("wd "):
            cur.append(line.strip())
        else:
            m = re.search(r"worlddigest: tick (\d+) hash (\w+)", line)
            if m:
                out[int(m[1])] = (m[2], cur)
                cur = []
    return out


def fought(b):
    player, enemies = set(), {}
    for _, lines in b.values():
        for l in lines:
            m = re.search(r"body (\w+) pos \w+ \w+ \w+ yaw -?\d+ hp (-?\d+)", l)
            if not m:
                continue
            a = int(m[1], 16)
            if a == PLAYER:
                player.add(int(m[2]))
            elif ANIMATED <= a < ANIMATED_END:
                enemies.setdefault(a, set()).add(int(m[2]))
    return sum(len(v) > 1 for v in enemies.values()), len(player)


def world(run, save, st, sb):
    exe = os.path.join(run, "opendash.exe")
    rc = subprocess.run([exe, "--world", st, sb, "--world-ticks", str(TICKS), "--world-unpaced", "--world-pad", PAD_BITS,
                         "--story-save", save], env=ENV, cwd=run, timeout=300).returncode
    return rc, os.path.join(run, f"opendash-world-ST{int(st, 16):02X}-{sb}.log")


def compare(run, save, st, sb):
    exe = os.path.join(run, "opendash.exe")
    subprocess.run([exe, "--stage", st, "--sub", sb, "--classic", "1", "--audio", "0", "--story-save", save,
                    "--playscript", PAD_NAMES, "--shot", os.path.join(run, "shot.bmp"),
                    "--shot-frames", str(TICKS * 2 + 40)], env=ENV, cwd=run, timeout=300)
    a = blocks(os.path.join(run, "opendash.log"))
    rc, log = world(run, save, st, sb)
    b = blocks(log)
    common = sorted(set(a) & set(b))
    first = next((k for k in common if a[k][0] != b[k][0]), None)
    enemies, player = fought(b)
    print(f"ST{st}-{sb}: world rc {rc}, {len(common)} common digests over {TICKS} ticks, first difference {first}; "
          f"enemies hurt {enemies}, player life values {player}")
    if first is not None:
        x, y = a[first][1], b[first][1]
        for l, q in [(l, q) for l, q in zip(x, y) if l != q][:8]:
            print(f"  windowed {l}\n  world    {q}")
    return first is None and bool(common)


def sweep(run, save):
    def one(room):
        rc, log = world(run, save, f"{room[0]:02X}", str(room[1]))
        return room, rc, fought(blocks(log)) if rc == 0 else (0, 0)
    rooms = [(st, sb) for st in range(0x5d) for sb in range(4)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
        results = list(ex.map(one, rooms))
    ran = [r for r in results if r[1] == 0]
    hurt = sorted((r for r in ran if r[2][0] or r[2][1] > 1), key=lambda r: -sum(r[2]))
    print(f"{len(rooms)} candidates: {len(ran)} ran; hit points changed in {len(hurt)}")
    print(", ".join(f"{s:02X}:{b}" for (s, b), _, _ in hurt[:24]))


def all_digests(run, save):
    def one(room):
        key = f"{room[0]:02X}:{room[1]}"
        rc, log = world(run, save, *key.split(":"))
        return key, {str(k): v[0] for k, v in blocks(log).items()} if rc == 0 else None
    rooms = [(st, sb) for st in range(0x5d) for sb in range(4)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
        return dict(ex.map(one, rooms))


def check(run, save, path):
    base = json.load(open(path, encoding="utf-8"))
    now = all_digests(run, save)
    bad = []
    for key, want in base.items():
        got = now.get(key)
        if want == got:
            continue
        if want is None or got is None:
            bad.append((key, "ran" if want is None else "no longer runs"))
            continue
        ticks = sorted(set(want) | set(got), key=int)
        bad.append((key, f"tick {next(t for t in ticks if want.get(t) != got.get(t))}"))
    print(f"{sum(v is not None for v in base.values())} rooms in the baseline, {len(bad)} differ")
    for key, why in bad[:40]:
        print(f"  ST{key}: {why}")
    return 0 if not bad else 1


def main():
    args = [a for a in sys.argv[1:] if a != "--sweep"]
    build, run, save, *rooms = args
    prepare(build, run)
    if "--sweep" in sys.argv:
        sweep(run, save)
        return 0
    if rooms[:1] == ["--record"]:
        json.dump(all_digests(run, save), open(rooms[1], "w", encoding="utf-8"))
        return 0
    if rooms[:1] == ["--check"]:
        return check(run, save, rooms[1])
    ok = [compare(run, save, *r.split(":")) for r in rooms]
    return 0 if all(ok) else 1


if __name__ == "__main__":
    sys.exit(main())
