# Texture pack

**Experimental.** A pack does not cover the whole game: a page the dump never met is drawn nearest, and a page is still found
by its decode, so a palette or a route the dump did not see has no image. Expect pixelated characters and outlines with
steps in places, and floors with cuts in the rooms whose floor pages were not made.

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

The game never clears VRAM, so a page's unused texels hold what earlier stages left, another page by each route to a room.
A page with no image of its own is looked up again as the room's own load makes it (the boot's files, the room's and what
play moved since), the texels that differ drawn nearest from the page as it is.

A tile of a floor repeats beside different tiles, so no image of a tile meets them all. A room's floor can have pages of its
own: each cell in a slot of 32 texels, its UVs reading the 31 between the slot's half texels, named and made like any page.
A room uses them when the pack has them all, and the tiles' pages otherwise. They are never decoded again, so an animated
floor must not have them.
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
   meet (cutscenes, menus, a stage reached from another) are dumped by playing with `texture_dump=` set. With `--save` it
   also opens the rooms whose texture file counts on what earlier stages left, with `--vram-before` naming those files.
   The dump also gets each room's floor pages and `floors/<geometry file>_<sub>.txt`, the slot of each cell.

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
   - `--floors [geometry file prefixes]`: after the pages, each room's floor through the model as one image, its tones
     moved back, each cell's part going to its slot of the floor pages. The PS1's tiles connect as the floor lays them, so
     the image does too. Two floor pages with the same texels are one page, with one image.
