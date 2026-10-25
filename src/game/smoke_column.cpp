// The smoke column (see smoke_column.hpp).
#include "game/smoke_column.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

inline constexpr const char* kSmokeColumnArea = "smoke column";

enum class SmokeState : uint8_t { Start = 0, Follow = 1, DieOut = 2 }; // exe 0x569890 / 0x569a40 / 0x569b80
constexpr uint8_t kStatusTrail = 2; // the variant of the player's orbit status trails
constexpr int16_t kMaxRings = 0x10;
constexpr uint32_t kRingMask = 0xf;
constexpr uint32_t kSmokeDrawMode = 0xe1000240; // variant 0: subtractive (abr 2); variant 1: additive (abr 1)
constexpr uint32_t kSmokeDrawModeAdditive = 0xe1000220;
constexpr uint32_t kSmokeColourStep = 0x20202, kTrailColourStep = 0x808;
constexpr uint16_t kSmokeSegment = 4, kTrailSegment = 3;
constexpr uint32_t kShade = 0x3a000000; // the quads' black corners (POLY_G4 code)
// the player's look while a trail lives: the idle faces (logic side effect of the draw) and the tint by the length
constexpr int8_t kTrailFace = 4;
constexpr uint32_t kTrailTint = 0x887878, kTrailTintStep = 0x7f7f8;
constexpr uint16_t kStatusTrailsSpawned = 0xf800; // the status word's marker bits the last trail clears

// the player's tint while the trails are long (darker with the length)
uint32_t trailTint(int16_t length) { return kTrailTint - uint32_t(int32_t(length)) * kTrailTintStep; }

// exe 0x569c10(actor, p): the column's draw. While the player is drawn its idle faces are set to 4 (a logic side
// effect). The head ring point (the rings are not shifted by the stage shift) through the game camera must be within
// the effects' depth and on the screen, else nothing. Then for k = length down to 2, ring h = (h - 1) & 0xf, a point
// (ring x + cos * r >> 11, ring y - rise * r, ring z + sin * r >> 11): variants 0 / 1 angle phase - frame * 64 - k *
// 64, r = (length - k) * 2; variant 2 steps the RNG first: angle phase - (RNG & 1) * 256 - frame * 256 - k * 256, r =
// length - k. The first point off depth / screen ends the loop (variant 2: after its RNG step). Each ring's half width
// d = |screen x of (view point + w) - screen x of the view point| << shift (w = the segment width at the head and the
// first ring, then
// + the segment width per ring). Per segment two POLY_G4 (L, C, L', C') and (C, R, C', R') with the DR_MODE word's abr,
// the centre colour = colour step * k | 0x3a000000 (next: - colour step), black at the edges. Port: the corners go
// through dc::otQuadG4 as camera-input points (the ring points) + the screen offset of L / R (+-d pixels, brought back
// to the point's depth). The packet cursor / OT links / depth are presentation only.
void drawColumn(const StaticActor& actor) {
    Player& p = player();
    if (!(p.flags & kActorDrawn)) return;
    p.idleFaces[0] = kTrailFace;
    p.idleFaces[1] = kTrailFace;
    const SmokeColumnFields& smoke = classState<SmokeColumnFields>(actor);
    struct Ring {
        int16_t pt[3];
        CameraPoint c;
        int16_t cx, lx, rx; // the centre's screen x and the edges +- d
    };
    const uint32_t sh = stage().coordinateShift & 0x1f;
    uint32_t w = smoke.segmentWidth;
    const auto edges = [&](Ring& r) {
        const int16_t v[3] = {wrap16(r.c.view[0]), wrap16(r.c.view[1]), wrap16(r.c.view[2])};
        const Projected pc = projectViewPoint(v[0], v[1], v[2]);
        const Projected p1 = projectViewPoint(wrap16(int(uint32_t(r.c.view[0]) + w)), v[1], v[2]);
        int32_t d = int32_t(p1.sx) - int32_t(pc.sx);
        if (d < 0) d = -d;
        d = int32_t(uint32_t(d) << sh);
        r.cx = pc.sx;
        r.lx = wrap16(pc.sx - d);
        r.rx = wrap16(pc.sx + d);
    };
    const auto visible = [](const CameraPoint& c) { return effectDepthVisible(c) && effectOnScreen(c); };
    int32_t h = smoke.head;
    Ring prev{{smoke.ringX[size_t(h)], smoke.ringY[size_t(h)], smoke.ringZ[size_t(h)]}, {}, 0, 0, 0};
    prev.c = cameraPoint(prev.pt);
    if (!visible(prev.c)) return;
    edges(prev);
    const int32_t n = smoke.length;
    const uint32_t step = smoke.colourStep;
    uint32_t cur = (step * uint32_t(n)) | kShade, next = cur - step;
    if (n <= 1) return;
    const uint8_t abr = uint8_t((smoke.drawMode >> 5) & 3); // the DR_MODE word linked before the two quads
    const bool trail = actor.variant == kStatusTrail;
    const uint32_t frame = gameState().frame;
    for (int32_t k = n;;) {
        h = (h - 1) & int32_t(kRingMask);
        int32_t angle, r;
        if (!trail) {
            angle = int32_t(smoke.phase) - int32_t(frame << 6) - int32_t(uint32_t(k) << 6);
            r = (smoke.length - k) * 2;
        } else {
            const uint32_t rn = rngStep();
            angle = int32_t(smoke.phase) - int32_t((rn & 1) << 8) - int32_t(frame << 8) - int32_t(uint32_t(k) << 8);
            r = smoke.length - k;
        }
        const int32_t cs = fixedCos(angle), sn = fixedSin(angle);
        Ring cr{{wrap16(int(int32_t(uint32_t(cs) * uint32_t(r)) >> 11) + smoke.ringX[size_t(h)]),
                 wrap16(smoke.ringY[size_t(h)] - wrap16(smoke.rise * r)),
                 wrap16(int(int32_t(uint32_t(sn) * uint32_t(r)) >> 11) + smoke.ringZ[size_t(h)])},
                {},
                0,
                0,
                0};
        cr.c = cameraPoint(cr.pt);
        if (!visible(cr.c)) return;
        edges(cr);
        const float kp = pixelUnitsAt(prev.c), kc = pixelUnitsAt(cr.c);
        const int16_t v[4][3] = {{prev.pt[0], prev.pt[1], prev.pt[2]},
                                 {prev.pt[0], prev.pt[1], prev.pt[2]},
                                 {cr.pt[0], cr.pt[1], cr.pt[2]},
                                 {cr.pt[0], cr.pt[1], cr.pt[2]}};
        const uint32_t cl[4] = {kShade, cur, kShade, next}; // (L, C, L', C')
        const float ol[4][2] = {{float(wrap16(prev.lx - prev.cx)) * kp, 0.0f},
                                {0.0f, 0.0f},
                                {float(wrap16(cr.lx - cr.cx)) * kc, 0.0f},
                                {0.0f, 0.0f}};
        dc::otQuadG4(actor.at, cl, abr, v, ol);
        const uint32_t crr[4] = {cur, kShade, next, kShade}; // (C, R, C', R')
        const float orr[4][2] = {{0.0f, 0.0f},
                                 {float(wrap16(prev.rx - prev.cx)) * kp, 0.0f},
                                 {0.0f, 0.0f},
                                 {float(wrap16(cr.rx - cr.cx)) * kc, 0.0f}};
        dc::otQuadG4(actor.at, crr, abr, v, orr);
        w += smoke.segmentWidth;
        --k;
        cur -= step;
        next -= step;
        prev = cr;
        if (k <= 1) return;
    }
}

// the player's look ended with the last column (variants 0 / 1 too: the idle faces back to 0)
void clearFaces() {
    player().idleFaces[0] = 0;
    player().idleFaces[1] = 0;
}

} // namespace

void smokeColumnUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& smoke = classState<SmokeColumnFields>(actor);
    Player& p = player();
    const uint8_t variant = actor.variant;
    switch (SmokeState(actor.state)) {
    case SmokeState::Start:
        smoke.head = 0;
        smoke.length = 0;
        if (variant < kStatusTrail) {
            smoke.rise = int16_t((rngStep() & 0xf) + 8);
            smoke.phase = uint16_t(rngStep() & 0xf00);
            smoke.ringX[0] = actor.pos.x.whole;
            smoke.ringY[0] = actor.pos.y.whole;
            smoke.ringZ[0] = actor.pos.z.whole;
            smoke.segmentWidth = kSmokeSegment;
            smoke.drawMode = variant == 0 ? kSmokeDrawMode : kSmokeDrawModeAdditive;
            smoke.colourStep = kSmokeColourStep;
            ++actor.state;
        } else if (variant == kStatusTrail) { // exe 0x5698b9
            smoke.rise = int16_t((rngStep() & 1) + 0xa);
            const uint32_t r = rngStep() & 3; // the low byte of the RNG after the step
            smoke.phase = uint16_t(((r + uint32_t(int32_t(smoke.bone)) * 2) << 8) & kAngleMask);
            const ShortVec3& bone = p.bonePoints[size_t(int32_t(smoke.bone))];
            smoke.ringX[0] = bone.x;
            smoke.ringY[0] = bone.y;
            smoke.drawMode = kSmokeDrawModeAdditive;
            smoke.ringZ[0] = bone.z;
            smoke.colourStep = kTrailColourStep;
            smoke.segmentWidth = kTrailSegment;
            const uint16_t status = p.status;
            if (!(status & (kStatusDrain | kStatusBurn | kStatusDrainFloor | kStatusBurnFloor)))
                p.ambientColour = trailTint(smoke.length);
            // the status still on: Follow (the bone), else straight to DieOut
            actor.state = uint8_t(actor.state + ((status & (kStatusOrbit | kStatusOrbitFloor)) ? 1u : 2u));
        }
        drawColumn(actor);
        return;
    case SmokeState::Follow: { // exe 0x569a40
        smoke.length = int16_t(smoke.length + 1);
        if (smoke.length > kMaxRings) smoke.length = kMaxRings;
        const uint32_t h = uint32_t(uint8_t(uint8_t(smoke.head) + 1)) & kRingMask;
        smoke.head = int16_t(h);
        if (variant < kStatusTrail) {
            smoke.ringX[h] = actor.pos.x.whole;
            smoke.ringY[h] = actor.pos.y.whole;
            smoke.ringZ[h] = actor.pos.z.whole;
            if (smoke.ended != 0) ++actor.state;
        } else if (variant == kStatusTrail) {           // exe 0x569a7e
            if (actor.pos.x != 0 || actor.pos.z != 0) { // a pending x / z shift (the whole parts): the whole trail
                for (int32_t i = 0; i < smoke.length; ++i) {
                    smoke.ringX[size_t(i)] = int16_t(smoke.ringX[size_t(i)] + actor.pos.x.whole);
                    smoke.ringZ[size_t(i)] = int16_t(smoke.ringZ[size_t(i)] + actor.pos.z.whole);
                }
                actor.pos.x = 0;
                actor.pos.z = 0;
            }
            const ShortVec3& bone = p.bonePoints[size_t(int32_t(smoke.bone))];
            smoke.ringX[h] = bone.x;
            smoke.ringY[h] = bone.y;
            smoke.ringZ[h] = bone.z;
            const uint16_t status = p.status;
            if (!(status & (kStatusDrain | kStatusBurn | kStatusDrainFloor | kStatusBurnFloor)))
                p.ambientColour = trailTint(smoke.length);
            // the status ended, the player alive and not knocked back / down: die out
            if (!(status & (kStatusOrbit | kStatusOrbitFloor)) && p.hitPoints >= 0 && p.sub() != PlayerSub::KnockBack &&
                p.sub() != PlayerSub::KnockDown)
                ++actor.state;
        }
        drawColumn(actor);
        return;
    }
    case SmokeState::DieOut: // exe 0x569b80
        if (variant < kStatusTrail) {
            smoke.length = int16_t(smoke.length - 1);
            if (smoke.length < 0) {
                clearFaces();
                freeActor(actor);
                return;
            }
        } else if (variant == kStatusTrail) { // exe 0x569b95
            smoke.length = int16_t(smoke.length - 1);
            if (smoke.length < 0 || (p.status & (kStatusOrbit | kStatusOrbitFloor))) { // gone, or the status again
                p.status &= uint16_t(~kStatusTrailsSpawned); // the marker bits cleared: a new status spawns new trails
                clearFaces();
                freeActor(actor);
                return;
            }
            p.ambientColour = trailTint(smoke.length);
        }
        drawColumn(actor);
        return;
    default: notPortedAt(kSmokeColumnArea, "smoke column state", actor.state);
    }
}

} // namespace od::port
