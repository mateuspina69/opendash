// The lock-on reticle as the game draws it (see reticle_draw.hpp).
#include "game/reticle_draw.hpp"

#include <algorithm>
#include <cmath>

#include "game/camera_state.hpp"
#include "game/player/player_state.hpp"
#include "game/projection.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"

namespace od::fx {

namespace {

constexpr uint32_t kPlayerStructSize = 0x500;
constexpr int kFlyInSteps = 8; // the point flies in from the camera eye while the lock countdown runs (k / 8)
// the reticle's radii (game units): the outer, the arrowhead's inner point; towns / fields
constexpr float kTownRadius = 128.0f, kTownInner = 96.0f, kFieldRadius = 144.0f, kFieldInner = 112.0f;
constexpr int kArrowSpread = 0x70; // an arrowhead's sides: its angle -+ this (12-bit)
// its gradient rows (relative to rowBase)
constexpr int kRowTown = 0, kRowTownZooming = 1, kRowFieldInRange = 2;
constexpr float kZoomingShade = 0.25f; // B + F / 4 as B + F at a quarter
constexpr uint32_t kReticleObject = 0x50000u;

} // namespace

// ---- the lock-on reticle, exe 0x5f2870 (the draw pass, reads the player only) --------------------------------------
// While the player has a target (its lock-on slot), no script runs and the point passes the camera test below: at the
// target's aim point (moving in from the camera eye while the lock countdown k > 0: t + (eye - t) * k / 8), four
// semi-transparent gouraud quads in the camera plane, one per quarter turn q: the outer radius r at q - 0x70, the inner
// radius at q, r at q and r at q + 0x70 (an arrowhead pointing at the target). Towns (weapon mode 0): r 0x80, inner
// 0x60, colour 0xe0e000 (0xa0a000 while moving in); fields: r 0x90, inner 0x70, 0x00e0e0 in range / 0x0000e0 out of
// range -- the range test (exe 0x573510 range vs exe 0x5e3a60 distance) is not modelled, in range assumed, and its 8 glow
// draw commands are not drawn. Blend: draw mode 0xe1000220 (B + F) or 0xe1000260 (B + F / 4) moving in.
void reticle(int rowBase, std::vector<Prim>& out) {
    const port::Player& p = port::player();
    if (p.lockTarget == 0 ||
        port::scriptState().flags != 0)
        return;
    const int k = p.lockSoundTicks;
    float ctr[3];
    int16_t sh16[3]; // the point << the stage shift as the s16 the game projects
    const int sh = port::stage().coordinateShift & 0x1f;
    const port::CameraVec& eye = port::cameraState().eye;
    const int16_t target[3] = {p.lockAimPoint.x, p.lockAimPoint.y, p.lockAimPoint.z};
    const int32_t eyes[3] = {eye.x, eye.y, eye.z};
    for (uint32_t i = 0; i < 3; ++i) {
        const int t = target[i];
        int v = t;
        if (k >= 1) {
            const int d = (eyes[i] - t) * k;
            v = ((d + ((d >> 31) & (kFlyInSteps - 1))) >> 3) + t;
        }
        ctr[i] = float(v);
        sh16[i] = int16_t(uint16_t(uint32_t(v) << sh));
    }
    // the game's visibility test with its camera: the effects' depth range, in front, on the screen. Not locked, the
    // selection leaves the countdown at 8 (exe 0x5e4b60 sets it with every new candidate; only exe 0x5780e0, the held
    // lock, counts it down), so the point is the camera eye itself (depth 0) and nothing is drawn: the reticle only
    // shows while the lock button is held, flying in from the eye as the countdown runs out
    const port::CameraPoint cp = port::cameraPoint(sh16);
    if (!port::effectDepthVisible(cp) || cp.clipped) return;
    if (!port::effectOnScreen(cp)) return;
    const bool field = p.weaponMode != 0;
    const float r = field ? kFieldRadius : kTownRadius, rin = field ? kFieldInner : kTownInner;
    const int row = rowBase + (field ? kRowFieldInRange : (k > 0 ? kRowTownZooming : kRowTown));
    const float kAng = 6.2831853f / 4096.0f;
    const float shade = k > 0 ? kZoomingShade : 1.0f;
    auto at = [&](int ang, float rad) { // a gradient-row vertex at u 0 (the row's colour)
        Vertex v;
        v.off[0] = std::cos(float(ang) * kAng) * rad;
        v.off[1] = std::sin(float(ang) * kAng) * rad;
        v.u = 0.5f;
        v.v = float(row) + 0.5f;
        v.shade = shade;
        return v;
    };
    for (int q = 0; q < 4; ++q) {
        const int a = q * port::kQuarterTurn;
        Prim pr;
        std::copy(ctr, ctr + 3, pr.centre);
        pr.object = kReticleObject;
        pr.blend = 2;    // PS1 additive
        pr.front = true; // OpenDash: the target's model never covers it
        pr.v[0] = at(a - kArrowSpread, r);
        pr.v[1] = at(a, rin);
        pr.v[2] = at(a, r);
        pr.v[3] = at(a + kArrowSpread, r);
        out.push_back(pr);
    }
}

} // namespace od::fx
