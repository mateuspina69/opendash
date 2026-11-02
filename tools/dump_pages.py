#!/usr/bin/env python3
"""Dump the texture pages of every room (docs/texture_pack.md): each stage's sub-rooms played for a few seconds with
--texture-dump, so the room, its actors' and its effects' pages land in the dump folder (tools/upscale_pages.py's
input). Run it on a copy of the game (its opendash.ini, saves and log are the copy's): it opens a window per room.

  python tools/dump_pages.py --exe <copy>/opendash.exe --dump <dump> [--jobs 4]
"""
import argparse, concurrent.futures, pathlib, re, subprocess

STAGES = 93  # engine/data_source.hpp kStageCount


def run(exe, dump, stage, sub, ticks):
    cmd = [str(exe), '--net', 'off', '--story-save', 'none', '--stage', f'{stage:02X}', '--sub', str(sub),
           '--texture-dump', str(dump), '--playscript', f'{ticks} -']
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=120, cwd=exe.parent)
        return r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--ticks', type=int, default=240)
    o = ap.parse_args()
    exe, dump = pathlib.Path(o.exe).resolve(), pathlib.Path(o.dump).resolve()
    dump.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(o.jobs) as pool:
        # sub 0 of every stage says how many sub-rooms it has ("ST09 sub 0/1: ...")
        firsts = {s: pool.submit(run, exe, dump, s, 0, o.ticks) for s in range(STAGES)}
        rest = []
        for s, f in firsts.items():
            m = re.search(r'ST[0-9A-F]{2} sub 0/(\d+):', f.result())
            subs = int(m.group(1)) if m else 0
            print(f'ST{s:02X}: {subs} rooms', flush=True)
            rest += [pool.submit(run, exe, dump, s, sub, o.ticks) for sub in range(1, subs)]
        for f in rest:
            f.result()
    print(f'{sum(1 for _ in dump.glob("*.png"))} pages in {dump}')


if __name__ == '__main__':
    main()
