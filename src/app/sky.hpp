// The room's sky: the PS1 sky layers (2D screen-space scroll or on a dome), the sky gradient, the travel map
// (ST01) and the storm backdrop.
#pragma once

#include <cstdint>
#include <vector>

#include "app/room_mesh.hpp"
#include "engine/config.hpp"
#include "engine/vram.hpp"
#include "content/intro/crash_landing_draw.hpp"
#include "render/renderer.hpp"

namespace od::app {

// Native room sky definitions (game/sky_layers); textures and palettes come
// from the stage DAT uploads already applied to the room VRAM.
void loadSky(RoomMesh& mesh, int stage, int sub);

// The sky gradient of the stage / sub (Maybe_SetSkyGradient 0x5f0370's table: game/sky_gradient, the data source's)
// at the game state's story
void loadSkyGradient(RoomMesh& mesh, int stage, int sub);

// Screen-space sky like Maybe_UpdateAndRenderSkyLayers / Maybe_RenderSkyLayerCurved, on a virtual
// 240-line screen widened to the window aspect. X follows yaw (one tilemap width per turn:
// ((speed*frame >> 8) + (base + yaw)*8 & 0x7fff) * W * 0x40), Y follows the horizon (pitch), curved
// layers bend each tile corner by (row * dx^2 * 7) >> 21 (dx from the screen centre).
std::vector<od::RenderVertex> buildSky(const RoomMesh& mesh, const od::Camera& cam, uint64_t frame, float aspect);

// The travel map's 2D layer, drawn by script 7's state 1 (0x5181f0) every tick behind the 3D icons (OT 0xfff / 0xff8):
// FUN_00518c00: four 256x256 quadrants (i = 0..3: page 0x15 + 2i, 8 bit, CLUT row 0x1f0 + i), each 4 x 4 sprites of
// 64x64 (cmd 0x64, colour 0x808080), quadrant origin x = (i odd ? 0 : -0x100) + 0xa0 - cam x, y = cam z - (i < 2 ?
// 0x100 : 0) + 0x78 (cam = block +0x18 / +0x1a); FUN_00518a30: per destination of the map's list except index 2, a
// 16x24 marker (cmd 0x64, u = ((frame >> 1) % 6) * 16, v 0) at (map x - cam x + 0x98, cam z - map z + 0x60) and a
// 16x16 semi-transparent sprite (cmd 0x66, v 0x18) 0x18 lower; page 0x1e, CLUT 0x7fc1 (0x7fc0 for the destination
// whose index is the story marker +0x39). Screen = the PS1 320x240, widened symmetrically like the sky layers; the
// semi-transparent sprite is drawn opaque (the screen-space pass has no blending).
void buildTravelMap(const RoomMesh& mesh, uint64_t frame, float aspect,
                    std::vector<od::RenderVertex>& out);

// The same layers on a sphere around the eye (drawn with the camera transform, no depth), so the
// sky turns and tilts exactly with the world -- the original 2D scroll moves at a different angular
// rate than the 3D view, which is uncomfortable with a free camera at high refresh rates.
// Mapping taken from the game: one tilemap width per turn (a = 2pi / (W*32) rad per pixel, used for
// elevation too), the panorama row at the screen centre of a level camera (hofs + 120) is the
// horizon, and screen centre x = X + 160 with X = (base + yaw) * W/128 px gives the azimuth origin.
std::vector<od::RenderVertex> buildSkyDome(const RoomMesh& mesh, const od::Camera& cam, uint64_t frame);

// The storm's 2D sprites (content/intro/crash_landing_draw: the script-6 backdrop, the panorama of static 0x12) behind the 3D scene, in
// the game's OT order after the gradient / layers. The game draws them in the 320 x 240 frame; the frame is centred
// in the window (240 lines) and the extra sprites left / right of it fill wider windows. Screen sky (dome false):
// clip-space positions; dome: the same screen points pushed onto the sky sphere through `cam` (so they cover the
// same pixels this frame).
void appendStormSky(const RoomMesh& mesh, const od::storm::Frame& storm, const od::Camera& cam, float aspect,
                    bool dome, std::vector<od::RenderVertex>& out);

} // namespace od::app
