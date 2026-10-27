#!/usr/bin/env python3
"""Fetch the vendor upscaler SDKs into third_party/ (gitignored). Pinned versions; re-running is idempotent.
  python tools/fetch_sdks.py [--only fsr,fidelityfx-vk,dlss,xess,nis]"""
import argparse, io, pathlib, re, subprocess, sys, urllib.request, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent / 'third_party'
GIT = {
    'dlss': ('https://github.com/NVIDIA/DLSS.git', 'v310.9.1'),
    'fidelityfx': ('https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git', 'v2.3.0'),
    # FSR 3.1 for the Vulkan renderer (Linux): the last SDK with a Vulkan backend, built from source
    'fidelityfx-vk': ('https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git', 'v1.1.4'),
    'nis': ('https://github.com/NVIDIAGameWorks/NVIDIAImageScaling.git', 'v1.0.3'),
}
ZIP = {
    'xess': 'https://github.com/intel/xess/releases/download/v3.0.2/XeSS_SDK_3.0.2.zip',
}
ALIASES = {'fsr': 'fidelityfx'}
# source edits after the clone (regex substitutions; idempotent: a patched file no longer matches)
PATCHES = {
    'fidelityfx-vk': [
        # wchar_t is 4 bytes outside Windows, and the FSR 3.1 context (resource names) outgrows the SDK's fixed
        # 128 Ki words: the define becomes overridable (cmake/fidelityfx_vk.cmake sets its own)
        ('sdk/include/FidelityFX/host/ffx_types.h',
         r'(?m)^(?<!#ifndef FFX_SDK_DEFAULT_CONTEXT_SIZE\n)(#define FFX_SDK_DEFAULT_CONTEXT_SIZE \(1024 \* 128\)\n)',
         r'#ifndef FFX_SDK_DEFAULT_CONTEXT_SIZE\n\1#endif\n'),
        # the Vulkan backend carves its arrays out of the scratch buffer 4-byte aligned, after a BackendContext_VK of
        # 17016 bytes; EffectContext is 32-byte aligned and GCC stores to it with aligned SSE (MSVC's stores are
        # unaligned): every array 64-aligned, the slack counted in the scratch size (the app aligns the buffer)
        ('sdk/src/backends/vk/ffx_vk.cpp', r'(ArraySize\s*=\s*FFX_ALIGN_UP\(.*), sizeof\(uint32_t\)\);', r'\1, 64);'),
        ('sdk/src/backends/vk/ffx_vk.cpp', r'return FFX_ALIGN_UP\(sizeof\(BackendContext_VK\) \+',
         r'return FFX_ALIGN_UP(FFX_ALIGN_UP(sizeof(BackendContext_VK), 64) +'),
        ('sdk/src/backends/vk/ffx_vk.cpp', r'uint8_t\* pMem = \(uint8_t\*\)\(\(BackendContext_VK\*\)\(backendContext \+ 1\)\);',
         r'uint8_t* pMem = (uint8_t*)FFX_ALIGN_UP((uintptr_t)(backendContext + 1), 64);'),
        # device-local memory: the backend keeps the last matching type, on RADV the AMD device-coherent / uncached
        # one (no GPU caches: FSR ran at a quarter of the frame rate); those types only when asked for
        ('sdk/src/backends/vk/ffx_vk.cpp',
         r'(for \(uint32_t i = 0; i < memProperties\.memoryTypeCount; i\+\+\) \{\n)(?!\s*if \(\(memProperties\.memoryTypes\[i\]\.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD)',
         r'\1        if ((memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD) && !(requestedProperties & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD))\n            continue;\n'),
        # the reprojection's Lanczos lookup table (an R16_SNORM texture the backend uploads) reads back wrong on
        # Vulkan (RADV): the history is filtered flat and the image blurs. The reference Lanczos (computed in the
        # shader, exact) instead
        ('sdk/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp', r'useLut(\s*)= true;', r'useLut\1= false;'),
    ],
}


def patch(name):
    for rel, pattern, repl in PATCHES.get(name, []):
        f = ROOT / name / rel
        f.write_text(re.sub(pattern, repl, f.read_text()))


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
            patch(n)
    for n, u in ZIP.items():
        if n in want:
            fetch_zip(n, u)
    return 0


if __name__ == '__main__':
    sys.exit(main())
