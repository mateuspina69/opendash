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
frame, a fog shade), or a page whose VRAM an animation moved. A page with no image is the PS1 page enlarged, nearest,
its transparent texels beside a drawn one taking that one's colour (the filter below reads them).
Pages are made by worker threads (half the CPU's threads) with their mip levels, and kept on the GPU (a 1 GB pool, the
least recently shown going first; the images read also in a 512 MB cache), so each is read and uploaded once. A room's
pages are named from their PS1 decode: one in the pool is copied on the GPU, a room load waits only for the others. An
animated page's frame (an animated CLUT, VRAM tiles moved) goes the same way; until its image is read, its polygons keep
the frame before. An effect's texture met in play takes one of the slices kept free after the room's (32, more reserved
if they run out) and is drawn once its page is there: the room is not touched.

With a pack, a page's colour is filtered (trilinear, anisotropic 16x, mip levels down to the PS1's 256 x 256, biased
by the render size over the output's); its alpha stays the nearest texel's, since 0 / 128 / 255 are meanings, not
levels. The filter's reach stays inside the triangle's UVs: a page packs unrelated pieces side by side, and one's edge
must not read its neighbour. Without a pack the pages are drawn nearest, the PS1's texels.

## Making it

1. **Dump the pages.** `texture_dump=` in `opendash.ini` (or `--texture-dump <dir>`) names a folder the pages with no
   image in the pack are written to, once each, as `<name>.png` at 256 x 256. `tools/dump_pages.py` plays every
   stage's rooms for a few seconds with it:

   ```
   python tools/dump_pages.py --exe <copy>/opendash.exe --dump pages
   ```

   It opens a window per room; run it on a copy of the game (its log and saves are the copy's). With `--save
   DASH2-NN.DAT` (read only) it also opens each story state's own files, which hold their own characters. The game
   never clears VRAM, so a page can carry what the stage before left and be another page: the pages the sweep does not
   meet (cutscenes, menus, a stage reached from another) are dumped by playing with `texture_dump=` set. The dump also
   gets `floors/<geometry file>_<sub>.txt`, how each room's floor lays its tiles.

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
   - `--floors [geometry file prefixes]`: after the pages, each room's floor through the model as one image, so a
     tile is enlarged next to its neighbours on the floor. A tile repeats next to different tiles, so at its edges it
     goes back to the floor's own texels, smooth; each tile's image is the mean of its places. This softens the cut
     between tiles and does not remove it: the PS1's tiles themselves do not connect there.
