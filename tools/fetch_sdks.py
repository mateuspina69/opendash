#!/usr/bin/env python3
"""Fetch the vendor upscaler SDKs into third_party/ (gitignored). Pinned versions; re-running is idempotent.
  python tools/fetch_sdks.py [--only fsr,dlss,xess,nis]"""
import argparse, io, pathlib, subprocess, sys, urllib.request, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent / 'third_party'
GIT = {
    'dlss': ('https://github.com/NVIDIA/DLSS.git', 'v310.9.1'),
    'fidelityfx': ('https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git', 'v2.3.0'),
    'nis': ('https://github.com/NVIDIAGameWorks/NVIDIAImageScaling.git', 'v1.0.3'),
}
ZIP = {
    'xess': 'https://github.com/intel/xess/releases/download/v3.0.2/XeSS_SDK_3.0.2.zip',
}
ALIASES = {'fsr': 'fidelityfx'}


def fetch_git(name, url, tag):
    dst = ROOT / name
    if dst.exists():
        print(f'{name}: present')
        return
    subprocess.run(['git', 'clone', '--depth', '1', '--branch', tag, url, str(dst)], check=True)
    subprocess.run(['git', '-C', str(dst), 'lfs', 'pull'], check=False)  # DLSS ships its DLLs through LFS


def fetch_zip(name, url):
    dst = ROOT / name
    if dst.exists():
        print(f'{name}: present')
        return
    data = urllib.request.urlopen(url).read()
    zipfile.ZipFile(io.BytesIO(data)).extractall(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default='')
    o = ap.parse_args()
    want = {ALIASES.get(x, x) for x in o.only.split(',') if x} or set(GIT) | set(ZIP)
    ROOT.mkdir(exist_ok=True)
    for n, (u, t) in GIT.items():
        if n in want:
            fetch_git(n, u, t)
    for n, u in ZIP.items():
        if n in want:
            fetch_zip(n, u)
    return 0


if __name__ == '__main__':
    sys.exit(main())
