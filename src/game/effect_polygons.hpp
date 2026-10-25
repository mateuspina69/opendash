// The ordering-table polygons of the projected effects (the status markers, the flash burst, the energy orb, the
// sparkle, the shock wave, the charge's sphere, the drill's cone, the thrown crystal): the exe projects a point and
// offsets a polygon's corners from it in screen pixels (a screen anchor), or turns world corners in a local frame and
// projects them. The port keeps the corners' camera-input points and hands the presentation side channel
// (game/draw_commands) the pixel offsets brought back to the point's depth (camera-plane units), so the polygons are
// drawn through the app's camera.
#pragma once

#include <cstdint>

#include "game/draw_commands.hpp"
#include "game/matrix.hpp"
#include "game/projection.hpp"

namespace od::port {

// a screen point (x | y << 16) of the 64-step circle around the screen point c: (c.x + cos * r >> 12, c.y + sin * r
// >> 12) (the exe table's 64-step sine / cosine)
uint32_t screenAround(uint32_t c, uint32_t i, int r);

// A projected point the corners of a screen-space polygon are offset from: its camera-input point, its projection and
// the camera-input units one pixel spans at its depth
struct ScreenAnchor {
    int16_t pt[3];
    CameraPoint c;
    float unitsPerPixel;
};
// the anchor at a camera-input point
ScreenAnchor screenAnchorAt(const int16_t pt[3]);
// the anchor at a point in game units, shifted by the stage shift as exe 0x5f2dd0 shifts it
ScreenAnchor shiftedScreenAnchor(const int16_t p[3]);
// a POLY_GT4 / POLY_FT4 (drawn as its gouraud form) / POLY_G4 (an additive draw mode before it) with screen corners
// xy (x | y << 16) around the anchor
void anchoredGT4(uint32_t owner, const ScreenAnchor& an, const dc::GouraudTexturedQuad& q, const uint32_t xy[4]);
void anchoredFT4(uint32_t owner, const ScreenAnchor& an, const dc::TexturedQuad& q, const uint32_t xy[4]);
void anchoredG4(uint32_t owner, const ScreenAnchor& an, const uint32_t c[4], const uint32_t xy[4]);
// a POLY_GT4 / an additive POLY_G4 with world corners (camera-input points: no offset)
void worldGT4(uint32_t owner, const dc::GouraudTexturedQuad& q, const int16_t v[4][3]);
void worldG4(uint32_t owner, const uint32_t c[4], const int16_t v[4][3]);

// World points in a local frame: exe 0x40c070's Rz Ry Rx of an angle triple around an origin (the exe composes the
// camera with it on the geometry unit; the port draws the world points through its own camera)
struct LocalFrame {
    Matrix3 rotation;
    int16_t origin[3];
    // the point (x, y, z) of the frame (each truncated to a short) turned (exe 0x40bb30) and moved to the origin
    void point(int x, int y, int z, int16_t out[3]) const;
};
LocalFrame localFrame(int16_t x, int16_t y, int16_t z, const int16_t origin[3]);

} // namespace od::port
