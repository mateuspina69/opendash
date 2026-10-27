// The projection: the exe's emulation of the PS1 geometry unit (exe 0x40c3c0.. : a camera loaded into the projection
// registers, a point through it into view space, its screen x / y and its clip flags), as the routines that project a
// point compute it -- the x87 in single precision (the Direct3D device leaves it there), every step rounded to a
// float, the screen point truncated. The game reads some results back (the on-screen tests, the lock-on selection, the
// effects' depth tests); the presentation side channel takes the camera-input points.
#pragma once

#include <array>
#include <cstdint>

#include "game/field_alias.hpp"
#include "game/geometry_scratch.hpp"
#include "game/matrix.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"
#include "game/view_transform.hpp"

namespace od::port {


// A point's screen position (truncated to shorts) and whether it is clipped (behind the near plane or beyond the far
// limit: the projection's kProjectionClipped)
struct Projected {
    int16_t sx, sy;
    bool clipped;
};

// The projection half of exe 0x40c860 (0x40c200 + 0x40cb10) of a view-space point
Projected projectViewPoint(int32_t x, int32_t y, int32_t z);
// exe 0x40bab0 + 0x40c860: the s16 point v through the matrix m and the translation t into view space (into view
// when given), then projected
Projected projectThrough(const Matrix3& m, const int32_t t[3], const int16_t v[3], int32_t view[3] = nullptr);
// ... through a view transform of the game state (its matrix and its translation)
Projected projectThrough(const ViewTransform& t, const int16_t v[3], int32_t view[3] = nullptr);

// exe 0x40c3c0 / 0x40c410 on the camera 0x6d0014: the game camera loaded into the projection registers
void loadCameraProjection();

// The single-point path of the registers, as the exe's routines store them:
// exe 0x40c3c0 / 0x40c3e0: a transform's matrix image (its padding short included; 5 dwords) / its translation (3
// dwords) loaded
void loadProjectionMatrix(const ViewTransform& t);
void loadProjectionTranslation(const ViewTransform& t);
// ... of a stack transform (a matrix value and its padding short; a translation value)
void loadProjectionMatrix(const Matrix3& m, int16_t pad);
void loadProjectionTranslation(const int32_t t[3]);
// exe 0x40c6a0: an s16 point (its four lanes, two dwords) loaded
void loadProjectionPoint(const ShortVec4& p);
void loadProjectionPoint(const LaneVec4& p);
// ... of the stack (x, y, z and its spare lane)
void loadProjectionPoint(int16_t x, int16_t y, int16_t z, int16_t spare);
// exe 0x40c800: the loaded point through the loaded matrix (exe 0x40bab0: 32-bit sums >> 12) plus the translation into
// the view point
void transformProjectionPoint();
// exe 0x40caa0(out): the view point stored (3 dwords)
void storeProjectionPoint(std::array<Lane32, 3>& out);
void storeProjectionPoint(int32_t out[3]);

// exe 0x5f2dd0(pos16, out): the s16 point (<< the stage shift) projected with the game camera (loaded first): the
// screen x / y to sxy; 0 = in front, -1 = clipped
int projectOnScreen(const int16_t pos[3], int16_t sxy[2]);
// exe 0x5f2dd0 on an actor's position (integer parts): 0 = in front of the camera, -1 = clipped
int actorOnScreen(const Fixed16Vec3& pos);

// The game camera (matrix 0x6d0014, translation 0x6d0028) applied to an s16 point already << the stage shift, read-only
// (the draw pass; exe 0x40c800 -> the view point, exe 0x40c860 -> screen x / y, clipped = exe 0x40cae0 < 0)
struct CameraPoint {
    int32_t view[3];
    int16_t sx, sy;
    bool clipped;
};
CameraPoint cameraPoint(const int16_t v[3]);
// the depth the projection leaves for a camera point (the screen-z the exe's OT depth exe 0x40d010 reads): its view z,
// 0 when it is behind the camera (neither past the near plane nor in front)
int32_t cameraPointDepth(const CameraPoint& c);
// exe 0x5f4160's centre: the camera matrix 0x6d0014 with the eye offset 0x6d003c (exe 0x40c3c0 / 0x40c410): the view z
// of an s16 point (exe 0x40c6a0 / 0x40c860)
int32_t actorCentreViewZ(const int16_t v[3]);
// exe 0x5f4160 (animated models) / exe 0x5f4600.. (bank models): a model is drawn, the whole of it, when its centre
// through the same camera has a depth (OTZ = SZ >> 2) within 0x10 .. the stage's depth range and its screen point
// within -0x160 .. 0x2a0 / -0x88 .. 0x378; with flags byte 0 & 0x40, whenever it is nearer than the range. A model
// pressed against the camera, behind it or far off screen is not drawn (the cutscene cameras inside the rooms)
bool modelCentreDrawn(uint8_t flags, const int16_t centre[3]);
// The effects' depth / screen tests of a camera point (the exe's projected effects): view z within 0x30 .. the stage's
// effects far * 4, the screen point within -0x160 .. 0x2a0 / -0x88 .. 0x378
bool effectDepthVisible(const CameraPoint& c);
bool effectOnScreen(const CameraPoint& c);
// ... of a screen point (an exe 0x5f2dd0 result)
bool effectOnScreen(int16_t sx, int16_t sy);
// the camera-input units one screen pixel spans at a camera point's depth (view z / the projection distance; 0 without
// a projection distance): a pixel offset of the exe brought back to the point's depth
float pixelUnitsAt(const CameraPoint& c);

// exe 0x5f2f20(pos16, width, depthOut): the projected screen width (<< the stage shift) of `width` units at the point
// (<< the stage shift) when it is within the effects' depth range and near the screen, else 0; depth = view z / 4
int projectedWidth(const int16_t pos[3], int16_t width, int16_t* depth);
// The GTE sequence of the OT quads (0x40c3c0 / 0x40c3e0: the game camera loaded; 0x40c770 + 0x40c930 (corners 0..2),
// 0x40c6a0 + 0x40c860 (corner 3), 0x40cbe0 / 0x40cb10 (screen x / y, truncated), 0x40c460 + 0x40cad0): the four
// corners' screen points and the OTZ = (the four view depths, 0 behind) >> 4. The exe's float history 0xa59a58.. /
// 0xa59d28.. is not replicated.
int32_t projectQuad(const int16_t v[4][3], int16_t sxy[4][2]);

// The screen-point scratch of the draws (the geometry scratch's +0x50 / +0x52, game/geometry_scratch: the screen x / y
// of the points a draw projects; the crash landing's 2D backdrop writes its origin there for its draw)
struct ScreenPointScratch {
    Lane16 x = geometryScratch().lane(0x50);
    Lane16 y = geometryScratch().lane(0x52);
};
inline ScreenPointScratch& screenPointScratch() {
    static ScreenPointScratch s;
    return s;
}

} // namespace od::port
