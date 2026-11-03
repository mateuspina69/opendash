#!/usr/bin/env python3
"""Dump the texture pages of every room (docs/texture_pack.md): each stage's sub-rooms played for a few seconds with
--texture-dump, so the room, its actors' and its effects' pages land in the dump folder (tools/upscale_pages.py's
input). With --save (a DASH2-NN.DAT, read only) also each story state's own files, which hold their own characters.
Run it on a copy of the game (its opendash.ini, saves and log are the copy's): it opens a window per room.

  python tools/dump_pages.py --exe <copy>/opendash.exe --dump <dump> [--save DASH2-00.DAT] [--jobs 4]
"""
import argparse, concurrent.futures, pathlib, re, subprocess, tempfile

STAGES = 93  # engine/data_source.hpp kStageCount

# A stage whose files game/stage_files stageDatFiles chooses: (stage, story, flags) of each choice story 0 does not reach
STORY_FILES = [
    (0x0d, 0x0f, ()), (0x0e, 0x0f, (0x5a1,)), (0x0e, 0x0f, (0x5a0,)), (0x10, 2, ()), (0x11, 2, ()), (0x13, 5, ()),
    (0x17, 0, (0x31,)), (0x17, 7, ()), (0x17, 9, (0x2c,)), (0x18, 0, (0x2c,)), (0x18, 9, (0x2c,)), (0x19, 9, (0x2c,)),
    (0x1a, 9, (0x2c,)), (0x1b, 9, (0x2c,)), (0x1d, 2, ()), (0x1f, 8, (0x586,)), (0x1f, 0x11, ()), (0x25, 4, ()),
    (0x26, 0, (0x3ed,)), (0x27, 0, (0x44, 0x45)), (0x29, 0x0d, ()), (0x30, 0, (0x23,)), (0x33, 0, (0x3ed,)),
    (0x35, 0, (0x94,)), (0x39, 0x11, ()), (0x3c, 0x0d, ()), (0x53, 0x0f, (0x5a1,)), (0x53, 0x0f, (0x5a0,)),
]
SAVE_STORY, SAVE_FLAGS = 0x12c, 0x400  # the save image's story byte, its event flags
SAVE_BLOCKS = ((0, 0x5f, 0x5f), (0x80, 0x1f, 0x9f), (0xa0, 0x5f, 0xff), (0x100, 0x1ff, 0x2ff))  # game/save_image kBlocks


def story_save(base, story, flags, path):
    """base at another story with these event flags set, its checksums made again"""
    b = bytearray(base)
    b[SAVE_STORY] = story
    for f in flags:
        b[SAVE_FLAGS + (f >> 3)] |= 1 << (f & 7)
    for first, count, checksum in SAVE_BLOCKS:
        s = sum(int.from_bytes(b[(first + i) * 4:(first + i) * 4 + 4], 'little') for i in range(count))
        b[checksum * 4:checksum * 4 + 4] = (s & 0xffffffff).to_bytes(4, 'little')
    path.write_bytes(b)


def run(exe, dump, stage, sub, ticks, save='none'):
    cmd = [str(exe), '--net', 'off', '--story-save', str(save), '--stage', f'{stage:02X}', '--sub', str(sub),
           '--texture-dump', str(dump), '--playscript', f'{ticks} -']
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, errors='replace', timeout=120, cwd=exe.parent)
        return r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return ''


def rooms(output):
    # sub 0 says how many sub-rooms it has ("ST09 sub 0/1: ..."); a story state's load comes last
    found = re.findall(r'ST[0-9A-F]{2} sub 0/(\d+):', output)
    return int(found[-1]) if found else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--save')
    ap.add_argument('--stories-only', action='store_true')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--ticks', type=int, default=240)
    o = ap.parse_args()
    exe, dump = pathlib.Path(o.exe).resolve(), pathlib.Path(o.dump).resolve()
    dump.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(o.jobs) as pool, tempfile.TemporaryDirectory() as tmp:
        rest = []
        if not o.stories_only:
            firsts = {s: pool.submit(run, exe, dump, s, 0, o.ticks) for s in range(STAGES)}
            for s, f in firsts.items():
                subs = rooms(f.result())
                print(f'ST{s:02X}: {subs} rooms', flush=True)
                rest += [pool.submit(run, exe, dump, s, sub, o.ticks) for sub in range(1, subs)]
        if o.save:
            base = pathlib.Path(o.save).read_bytes()
            firsts = {}
            for i, (s, story, flags) in enumerate(STORY_FILES):
                save = pathlib.Path(tmp) / f'{i}.DAT'
                story_save(base, story, flags, save)
                firsts[(s, story, flags, save)] = pool.submit(run, exe, dump, s, 0, o.ticks, save)
            for (s, story, flags, save), f in firsts.items():
                text = f.result()
                files = re.findall(r'story-state files (\S+) \+ (\S+)', text)
                subs = rooms(text)
                print(f'ST{s:02X} story {story} flags {list(map(hex, flags))}: {files[-1] if files else "?"}, {subs} rooms', flush=True)
                rest += [pool.submit(run, exe, dump, s, sub, o.ticks, save) for sub in range(1, subs)]
        for f in rest:
            f.result()
    print(f'{sum(1 for _ in dump.glob("*.png"))} pages in {dump}')


if __name__ == '__main__':
    main()
