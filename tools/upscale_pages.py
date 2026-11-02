#!/usr/bin/env python3
"""Make OpenDash's texture pack from the pages the game dumped (texture_dump= / --texture-dump, docs/texture_pack.md).

Each <name>.png of the dump folder (a PS1 page as the game decodes it, 256 x 256 RGBA, alpha 0 / 128 STP / 255) is
enlarged 4x by an image model (4xTextures_GTAV_rgt-s_dither, CC-BY-4.0, game textures with dithering:
https://github.com/Phhofm/models/releases/download/4xTextures_GTAV_rgt-s_dither/4xTextures_GTAV_rgt-s_dither.safetensors)
into the pack folder under the same name. The colour: the page with its transparent texels taking their drawn
neighbours' colour (no black bleeding into the edges), then its tones moved back to the page's (the model shifts them).
The alpha: the page's drawn mask through the same model, cut at half (its outlines follow the shapes, not the texel
steps), each drawn texel at the PS1 level of its nearest drawn texel. Pages already in the pack are skipped.

  python -m venv venv && venv/Scripts/pip install torch --index-url https://download.pytorch.org/whl/cu130
  venv/Scripts/pip install spandrel pillow numpy safetensors
  venv/Scripts/python tools/upscale_pages.py --model 4xTextures_GTAV_rgt-s_dither.safetensors --dump <dump> --pack build/Release/textures
"""
import argparse, pathlib, sys, time

import numpy as np
import spandrel
import torch
import torch.nn.functional as F
from PIL import Image


def spread(values, have):
    """values (h, w, c) where `have`; the others take the mean of their nearest `have` neighbours, step by step"""
    values = values.astype(np.float32).copy()
    have = have.copy()
    while not have.all():
        acc = np.zeros_like(values)
        n = np.zeros(have.shape, np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            acc += np.roll(values * have[..., None], (dy, dx), (0, 1))
            n += np.roll(have, (dy, dx), (0, 1))
        new = ~have & (n > 0)
        if not new.any():
            break
        values[new] = acc[new] / n[new][:, None]
        have |= new
    return values


def tone(out, page, reach=8, like=0.06):
    """out (1, 3, 1024, 1024) with its tones moved to page's (1, 3, 256, 256): each texel's difference from the page,
    averaged over the texels of a like colour within `reach` (the correction stops at an edge between two colours)"""
    delta = page - F.avg_pool2d(out, 4)
    acc, weight = torch.zeros_like(page), torch.zeros_like(page[:, :1])
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            k = torch.roll(page, (dy, dx), (2, 3)) - page
            k = torch.exp(-(k * k).sum(1, keepdim=True) / (2 * like * like)) * np.exp(-(dy * dy + dx * dx) / (reach * reach / 2))
            acc += k * torch.roll(delta, (dy, dx), (2, 3))
            weight += k
    return (out + F.interpolate(acc / weight, scale_factor=4, mode='bilinear')).clamp(0, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--pack', required=True)
    o = ap.parse_args()
    dev = 'cuda' if torch.cuda.is_available() else 'cpu'
    model = spandrel.ModelLoader().load_from_file(o.model).eval().to(dev)
    if model.scale != 4:
        sys.exit(f'{o.model}: a {model.scale}x model, the pack is 4x')
    dump, pack = pathlib.Path(o.dump), pathlib.Path(o.pack)
    pack.mkdir(parents=True, exist_ok=True)
    todo = [p for p in sorted(dump.glob('*.png')) if len(p.stem) == 16 and not (pack / p.name).exists()]
    print(f'{len(todo)} pages to enlarge ({dev})', flush=True)

    def tensor(rgb):
        return torch.from_numpy(rgb / 255).permute(2, 0, 1)[None].float().to(dev)

    def enlarge(rgb):
        with torch.no_grad():
            return model(tensor(rgb)).clamp(0, 1)

    def image(t):
        return t[0].permute(1, 2, 0).cpu().numpy()

    t0 = time.time()
    for i, p in enumerate(todo):
        page = np.asarray(Image.open(p).convert('RGBA'))
        drawn = page[..., 3] > 0
        if not drawn.any():
            out = np.zeros((1024, 1024, 4), np.uint8)
        else:
            rgb = spread(page[..., :3], drawn)
            with torch.no_grad():
                colour = (image(tone(enlarge(rgb), tensor(rgb))) * 255 + 0.5).astype(np.uint8)
            mask = np.ones((1024, 1024), bool) if drawn.all() else \
                image(enlarge(np.repeat(drawn[..., None].astype(np.float32) * 255, 3, 2))).mean(-1) >= 0.5
            level = spread(page[..., 3:4], drawn)[..., 0]
            level = np.where(level >= 192, 255, 128).astype(np.uint8).repeat(4, 0).repeat(4, 1)
            out = np.dstack([colour, np.where(mask, level, 0).astype(np.uint8)])
        tmp = pack / (p.stem + '.tmp.png')
        Image.fromarray(out).save(tmp)
        tmp.replace(pack / p.name)
        if (i + 1) % 50 == 0 or i + 1 == len(todo):
            print(f'{i + 1}/{len(todo)} ({time.time() - t0:.0f} s)', flush=True)


if __name__ == '__main__':
    main()
