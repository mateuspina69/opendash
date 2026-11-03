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


def tiled(model, rgb, dev, tile=256, pad=32):
    """rgb (h, w, 3) through the model by tiles: a whole room's floor does not fit the GPU"""
    h, w = rgb.shape[:2]
    t = torch.from_numpy(rgb / 255).permute(2, 0, 1)[None].float()
    out = torch.zeros(1, 3, h * 4, w * 4)
    with torch.no_grad():
        for y in range(0, h, tile):
            for x in range(0, w, tile):
                y0, x0, y1, x1 = max(y - pad, 0), max(x - pad, 0), min(y + tile + pad, h), min(x + tile + pad, w)
                r = model(t[:, :, y0:y1, x0:x1].to(dev)).clamp(0, 1).cpu()
                ey, ex = min(y + tile, h), min(x + tile, w)
                out[:, :, y * 4:ey * 4, x * 4:ex * 4] = r[:, :, (y - y0) * 4:(ey - y0) * 4, (x - x0) * 4:(ex - x0) * 4]
    return out, t


def corners_uv(corners, a, b):
    """a tile's (u, v) at (a, b) in 0..1 across it, from its four corners {(right, far): (u, v)}"""
    top = corners[0, 0] * (1 - a)[..., None] + corners[1, 0] * a[..., None]
    bottom = corners[0, 1] * (1 - a)[..., None] + corners[1, 1] * a[..., None]
    return top * (1 - b)[..., None] + bottom * b[..., None]


def floors(model, dev, dump, pack, only):
    """The floor tiles again, each next to its neighbours on the floor (a page's tiles are not): every room's floor as
    one image through the model, its tones moved back; each tile's image is the mean of its places on the floors.
    Only those rects of the pages' images change. floors/ is what the game wrote with the dump."""
    P = 31  # a tile's texels across: its UVs span 31 (engine/room parseFloor)
    acc, cached = {}, {}
    grid = (np.arange(P) + 0.5) / P
    big = (np.arange(P * 4) + 0.5) / (P * 4)

    def page_of(name):
        if name not in cached:
            p = dump / f'{name}.png'
            cached[name] = np.asarray(Image.open(p).convert('RGB')) if p.exists() else None
        return cached[name]

    for f in sorted((dump / 'floors').glob('*.txt')):
        if only and not any(f.stem.startswith(s) for s in only):
            continue
        cells = []
        for line in f.read_text().splitlines():
            w = line.split()
            c = {(int(w[3 + i * 4]), int(w[4 + i * 4])): (int(w[5 + i * 4]), int(w[6 + i * 4])) for i in range(4)}
            if len(c) == 4 and page_of(w[2]) is not None:
                cells.append((int(w[0]), int(w[1]), w[2], {k: np.array(v, np.float32) for k, v in c.items()}))
        if not cells:
            continue
        gx0, gz0 = min(c[0] for c in cells), min(c[1] for c in cells)
        W, H = max(c[0] for c in cells) - gx0 + 1, max(c[1] for c in cells) - gz0 + 1
        rgb, have = np.zeros((H * P, W * P, 3), np.uint8), np.zeros((H * P, W * P), bool)
        A, B = np.meshgrid(grid, grid)
        for gx, gz, name, c in cells:
            uv = np.floor(corners_uv(c, A, B)).astype(int).clip(0, 255)
            x, y = (gx - gx0) * P, (gz - gz0) * P
            rgb[y:y + P, x:x + P] = page_of(name)[uv[..., 1], uv[..., 0]]
            have[y:y + P, x:x + P] = True
        rgb = spread(rgb, have)
        out, page = tiled(model, rgb, dev)
        # a tile repeats next to different tiles, so no one image of it meets them all: at its edges it goes back to
        # the floor's own texels, smooth, which both sides of an edge share
        edge = np.minimum(np.arange(P * 4), np.arange(P * 4)[::-1]) / 12
        edge = edge.clip(0, 1)
        edge = torch.from_numpy(np.minimum(*np.meshgrid(edge, edge))).float()
        edge = (edge * edge * (3 - 2 * edge)).repeat(H, W)[None, None]
        out = tone(out, page) * edge + F.interpolate(page, scale_factor=4, mode='bicubic').clamp(0, 1) * (1 - edge)
        out = out[0].permute(1, 2, 0).numpy() * 255
        A, B = np.meshgrid(big, big)
        for gx, gz, name, c in cells:
            uv = np.floor(corners_uv(c, A, B) * 4).astype(int).clip(0, 1023)
            sum_, n = acc.setdefault(name, (np.zeros((1024, 1024, 3), np.float32), np.zeros((1024, 1024), np.float32)))
            x, y = (gx - gx0) * P * 4, (gz - gz0) * P * 4
            np.add.at(sum_, (uv[..., 1], uv[..., 0]), out[y:y + P * 4, x:x + P * 4])
            np.add.at(n, (uv[..., 1], uv[..., 0]), 1)
        print(f'{f.stem}: {len(cells)} tiles', flush=True)
    for name, (sum_, n) in acc.items():
        p = pack / f'{name}.png'
        if not p.exists():
            continue
        img = np.array(Image.open(p).convert('RGBA'))
        done = n > 0
        img[done, :3] = (sum_[done] / n[done][:, None] + 0.5).astype(np.uint8)
        tmp = pack / f'{name}.tmp.png'
        Image.fromarray(img).save(tmp)
        tmp.replace(p)
    print(f'{len(acc)} floor pages', flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', required=True)
    ap.add_argument('--dump', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--floors', nargs='*', help="after the pages, the floors' tiles (of the geometry files starting so; none: all)")
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
    if o.floors is not None:
        floors(model, dev, dump, pack, o.floors)


if __name__ == '__main__':
    main()
