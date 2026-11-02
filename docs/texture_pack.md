# Texture pack

OpenDash can draw the PS1's texture pages from larger images: its texture pack, one image per page, made offline by
an image model from the pages the game itself dumps. Put the images in `textures/` next to `opendash.exe`; OpenDash
uses the folder when it is there. No pack ships with OpenDash.

```
opendash.exe
textures/
  3f0c9a12d4e5b6a7.png
  ...
```

`texture_pack=` in `opendash.ini` names another folder (a relative path is from the exe's folder).

It works with both data sources (`data=pc` and `data=disc`): a page is found by what it shows, and both sources upload
the same pages.

## What the pack holds

A texture page is what one polygon samples: 256 x 256 texels of VRAM read through one CLUT (4-bit, 8-bit) or directly
(15-bit), with the PS1's semi-transparency bit. Its image is named after the page as the game decodes it
(`Vram::decode`, 256 x 256 RGBA): the XXH3-64 of those bytes, 16 lowercase hex digits, `.png`. The image is the page at
the pack's scale (1024 x 1024 for 4x; the first image's size sets it, an image of another size is skipped), its alpha
as the PS1 page's texels: 0 transparent, 128 the semi-transparency bit (blended on a semi-transparent polygon), 255
opaque.

Every different decode is another page: the same texels through another CLUT (a palette swap, an animated CLUT's
frame, a fog shade), or a page whose VRAM an animation moved. A page with no image is the PS1 page enlarged, nearest.
Images are read when a room needs them and kept in a 512 MB cache, so a room entered again costs nothing. An animated
page's frames (an animated CLUT, VRAM tiles moved) are read by two worker threads and kept on the GPU (a 1 GB pool,
the least recently shown going first), so each frame is read and uploaded once; until a frame's image is read, its
polygons keep the frame before.

## Making it

1. **Dump the pages.** `texture_dump=` in `opendash.ini` (or `--texture-dump <dir>`) names a folder the pages with no
   image in the pack are written to, once each, as `<name>.png` at 256 x 256. `tools/dump_pages.py` plays every
   stage's rooms for a few seconds with it:

   ```
   python tools/dump_pages.py --exe <copy>/opendash.exe --dump pages
   ```

   It opens a window per room; run it on a copy of the game (its log and saves are the copy's). Pages the sweep does
   not meet (cutscenes, menus, a story state's NPCs) are dumped by playing with `texture_dump=` set.

2. **Enlarge them.** `tools/upscale_pages.py` runs the image model over the dump into the pack, skipping the pages
   already there:

   ```
   python tools/upscale_pages.py --model 4xTextures_GTAV_rgt-s_dither.safetensors --dump pages --pack <game>/textures
   ```

   The model is [4xTextures_GTAV_rgt-s_dither](https://openmodeldb.info/models/4x-Textures-GTAV-rgt-s-dither)
   (CC-BY-4.0, game textures with dithering), run through spandrel and PyTorch (CUDA when present: about 2.5 s a page
   on an RTX 3070). The script's header lists the packages.

   - Colour: the page with its transparent texels taking their drawn neighbours' colour first, so no black bleeds
     into the outlines; then the model's output has its tones moved back to the page's, region by region (the model
     shifts them), stopping at the edges between two colours.
   - Alpha: the page's drawn mask through the same model, cut at half. The outlines follow the shapes (a diagonal is
     a line, not the texels' steps) where an enlarged mask would keep the steps. Each drawn texel takes the level
     (128 / 255) of its nearest drawn PS1 texel.
