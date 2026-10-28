// The projection (see projection.hpp).
#include "game/projection.hpp"

#include "game/camera_state.hpp"
#include "game/fixed_point.hpp"
#include "game/projection_state.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

// the effects' screen test: the point within -kScreenMarginX .. kScreenSpan - kScreenMarginX (y likewise)
constexpr uint16_t kScreenMarginX = 0x160, kScreenMarginY = 0x88, kScreenSpan = 0x400;
constexpr int32_t kEffectsNear = 0x30; // the effects' near depth

// a value rounded to a float (the x87's single precision)
float single(double v) { return float(v); }

// the shift of the stage's coordinates (world units -> camera-input units)
uint32_t stageShift() { return stage().coordinateShift & 0x1f; }

} // namespace

Projected projectViewPoint(int32_t x, int32_t y, int32_t z) {
    const ProjectionState& p = projection();
    const int32_t h = p.focal, cx = p.centreX, cy = p.centreY, nearZ = p.nearDepth;
    float fx, fy;
    bool clipped;
    if (z >= nearZ) { // exe 0x40c200
        // (FILD of the 32-bit product is exact; FIDIV rounds once -- computed in double, then to float)
        fx = single(double(single(double(int32_t(uint32_t(h) * uint32_t(x))) / double(z))) + double(cx));
        fy = single(double(single(double(int32_t(uint32_t(h) * uint32_t(y))) / double(z))) + double(cy));
        clipped = z > 0xffff;
    } else if (z <= 0) {
        fx = single(double(single(double(x) + double(x))) + double(cx));
        fy = single(double(single(double(y) + double(y))) + double(cy));
        clipped = true;
    } else {
        const float r = single(double(z) / double(nearZ));
        const float ty = single(double(y) * double(r));
        float ax = single(double(x) * double(r));
        ax = single(double(ax) * double(h));
        ax = single(double(ax) / double(z));
        fx = single(double(ax) + double(cx));
        float ay = single(double(h) * double(ty));
        ay = single(double(ay) / double(z));
        fy = single(double(ay) + double(cy));
        clipped = false;
    }
    // exe 0x40cb10: __ftol (truncation) of the stored floats
    return {wrap16(int(int64_t(fx))), wrap16(int(int64_t(fy))), clipped};
}

Projected projectThrough(const Matrix3& m, const int32_t t[3], const int16_t v[3], int32_t view[3]) {
    int32_t r[3];
    transformVector(m, v, r);
    const int32_t x = r[0] + t[0], y = r[1] + t[1], z = r[2] + t[2];
    if (view) {
        view[0] = x;
        view[1] = y;
        view[2] = z;
    }
    return projectViewPoint(x, y, z);
}

Projected projectThrough(const ViewTransform& t, const int16_t v[3], int32_t view[3]) {
    const int32_t translation[3] = {t.translation[0], t.translation[1], t.translation[2]};
    return projectThrough(t.matrix.get(), translation, v, view);
}

void loadCameraProjection() { projection().camera = cameraTransform().view; }

void loadProjectionMatrix(const ViewTransform& t) { projection().camera.copyMatrixWords(t); }

void loadProjectionTranslation(const ViewTransform& t) { projection().camera.copyTranslation(t); }

void loadProjectionMatrix(const Matrix3& m, int16_t pad) { projection().camera.setMatrixWords(m, pad); }

void loadProjectionTranslation(const int32_t t[3]) {
    for (size_t i = 0; i < 3; ++i) projection().camera.translation[i] = t[i];
}

void loadProjectionPoint(const ShortVec4& p) { projection().point = p; }
void loadProjectionPoint(const LaneVec4& p) { projection().point = p.vec4(); }

void loadProjectionPoint(int16_t x, int16_t y, int16_t z, int16_t spare) {
    projection().point.setWords(uint32_t(uint16_t(x)) | uint32_t(uint16_t(y)) << 16,
                                uint32_t(uint16_t(z)) | uint32_t(uint16_t(spare)) << 16);
}

void transformProjectionPoint() {
    ProjectionState& p = projection();
    const int16_t v[3] = {p.point.x, p.point.y, p.point.z};
    int32_t out[3];
    transformVector(p.camera.matrix, v, out);
    for (size_t i = 0; i < 3; ++i) p.viewPoint[i] = int32_t(uint32_t(out[i]) + uint32_t(p.camera.translation[i]));
}

void storeProjectionPoint(std::array<Lane32, 3>& out) {
    int32_t v[3];
    storeProjectionPoint(v);
    for (size_t i = 0; i < 3; ++i) out[i] = v[i];
}

void storeProjectionPoint(int32_t out[3]) {
    const ProjectionState& p = projection();
    for (size_t i = 0; i < 3; ++i) out[i] = p.viewPoint[i];
}

int projectOnScreen(const int16_t pos[3], int16_t sxy[2]) {
    loadCameraProjection();
    const uint32_t sh = stageShift();
    const int16_t v[3] = {wrap16(pos[0] << sh), wrap16(pos[1] << sh), wrap16(pos[2] << sh)};
    const Projected r = projectThrough(projection().camera, v);
    sxy[0] = r.sx;
    sxy[1] = r.sy;
    return r.clipped ? -1 : 0;
}

int actorOnScreen(const Fixed16Vec3& pos) {
    const int16_t p[3] = {pos.x.whole, pos.y.whole, pos.z.whole};
    int16_t sxy[2];
    return projectOnScreen(p, sxy);
}

CameraPoint cameraPoint(const int16_t v[3]) {
    CameraPoint c{};
    const Projected r = projectThrough(cameraTransform().view, v, c.view);
    c.sx = r.sx;
    c.sy = r.sy;
    c.clipped = r.clipped;
    return c;
}

int32_t cameraPointDepth(const CameraPoint& c) {
    return (c.view[2] >= projection().nearDepth || c.view[2] > 0) ? c.view[2] : 0;
}

int32_t actorCentreViewZ(const int16_t v[3]) {
    const CameraTransform& camera = cameraTransform();
    const int32_t eyeOffset[3] = {camera.eyeOffset[0], camera.eyeOffset[1], camera.eyeOffset[2]};
    int32_t view[3];
    projectThrough(camera.view.matrix.get(), eyeOffset, v, view);
    return view[2];
}

bool modelCentreDrawn(uint8_t flags, const int16_t centre[3]) {
    constexpr uint8_t kNearerThanRange = 0x40; // flags byte 0: drawn at any depth nearer than the range
    constexpr int32_t kModelNear = 0x10;       // the nearest OTZ drawn otherwise
    const CameraTransform& camera = cameraTransform();
    const int32_t eyeOffset[3] = {camera.eyeOffset[0], camera.eyeOffset[1], camera.eyeOffset[2]};
    int32_t view[3];
    const Projected p = projectThrough(camera.view.matrix.get(), eyeOffset, centre, view);
    const int32_t z = (view[2] > 0 ? view[2] : 0) >> 2, far = int32_t(stage().depthRange);
    if ((flags & kNearerThanRange) && z < far) return true;
    return z <= far && z >= kModelNear && uint16_t(p.sx + kScreenMarginX) <= kScreenSpan &&
           uint16_t(p.sy + kScreenMarginY) <= kScreenSpan;
}

bool effectDepthVisible(const CameraPoint& c) {
    return c.view[2] <= int32_t(uint32_t(stage().effectsFar) * 4) && c.view[2] >= kEffectsNear;
}

bool effectOnScreen(const CameraPoint& c) { return effectOnScreen(c.sx, c.sy); }

bool effectOnScreen(int16_t sx, int16_t sy) {
    return uint16_t(sx + kScreenMarginX) <= kScreenSpan && uint16_t(sy + kScreenMarginY) <= kScreenSpan;
}

float pixelUnitsAt(const CameraPoint& c) {
    const int32_t focal = projection().focal;
    return focal != 0 ? float(c.view[2]) / float(focal) : 0.0f;
}

int projectedWidth(const int16_t pos[3], int16_t width, int16_t* depth) {
    const uint32_t sh = stageShift();
    const int16_t v[3] = {wrap16(pos[0] << sh), wrap16(pos[1] << sh), wrap16(pos[2] << sh)};
    CameraPoint c{};
    const Projected p = projectThrough(cameraTransform().view, v, c.view);
    c.sx = p.sx;
    c.sy = p.sy;
    if (!effectDepthVisible(c) || !effectOnScreen(c)) return 0;
    // the view point (16-bit parts) again through the identity (the exe's stack matrix, no translation): the screen x
    // of it and of it + width
    const int32_t none[3] = {0, 0, 0};
    const int16_t a[3] = {wrap16(c.view[0]), wrap16(c.view[1]), wrap16(c.view[2])};
    const int16_t b[3] = {wrap16(width + a[0]), a[1], a[2]};
    const Projected pa = projectThrough(kIdentityMatrix, none, a);
    const Projected pb = projectThrough(kIdentityMatrix, none, b);
    if (depth) *depth = wrap16(c.view[2] >> 2);
    const int d = int(pb.sx) - int(pa.sx);
    return (d < 0 ? -d : d) << sh;
}

int32_t projectQuad(const int16_t v[4][3], int16_t sxy[4][2]) {
    loadCameraProjection();
    const int32_t nearZ = projection().nearDepth;
    int32_t sum = 0;
    for (int i = 0; i < 4; ++i) {
        int32_t view[3];
        const Projected r = projectThrough(projection().camera, v[i], view);
        sxy[i][0] = r.sx;
        sxy[i][1] = r.sy;
        // exe 0x40c200's depth out: z (z >= near, or 0 < z < near), 0 when z <= 0 below the near plane
        sum = int32_t(uint32_t(sum) + uint32_t((view[2] >= nearZ || view[2] > 0) ? view[2] : 0));
    }
    return sum >> 4; // exe 0x40c460 -> 0xa59d24, exe 0x40cad0
}

} // namespace od::port
