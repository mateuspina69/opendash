// The charge glow class 0x42ef70 (see charge_glow.hpp). +8 the state (0 the start: the sparks and the sound, 8 ticks;
// 1 grow 7 ticks (the radius + 2 a tick); 2 held while its owner sets +0xc each tick; 3 shrink 8 ticks; 4 freed), +9
// the state entered, the header's step its phase (its bolts' seed) and its step counter the state's ticks left.
#include "game/actor_state.hpp"
#include "game/charge_glow_state.hpp"
#include "game/charge_glow.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/spark_rings.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace charge_glow_state;

namespace {

enum class ChargeGlowState : uint8_t { Start = 0, Grow = 1, Held = 2, Shrink = 3, Freed = 4 };
// The draw values the glow keeps (the exe's scratch past the shared geometry scratch: the glow's own): the point its
// sounds play at and its colour
struct ChargeGlowScratch {
    Fixed16Vec3 soundPoint;
    StateArray<uint8_t, 4> colour{}; // r, g, b and the byte the dword store covers
    void setColour(uint32_t c) {
        for (size_t i = 0; i < 4; ++i) colour[i] = uint8_t(c >> (8 * i));
    }
};
ChargeGlowScratch& scratch() {
    static ChargeGlowScratch s;
    return s;
}
constexpr uint32_t kChargeColour = 0x6080c0;
constexpr uint16_t kSoundCharge = 0xd4, kSoundCrackle = 0xd5;
constexpr int kChargeSparks = 3;

// the sound point at the glow's s16 position (as 16.16)
void soundPointAt(const EffectActor& a) {
    ChargeGlowScratch& s = scratch();
    s.soundPoint.x = wholeToFixed16(a.pos.x);
    s.soundPoint.y = wholeToFixed16(a.pos.y);
    s.soundPoint.z = wholeToFixed16(a.pos.z);
}

// exe 0x40c800 with the matrix r (translation 0) + exe 0x40caa0, then the glow's position added and << the stage
// shift: one world point of a bolt / of the disc
void boltPoint(const EffectActor& a, const Matrix3& r, int16_t vx, int16_t vy, int16_t vz, int16_t out[3]) {
    const int sh = stage().coordinateShift & 0x1f;
    const int v[3] = {vx, vy, vz};
    const int16_t pos[3] = {a.pos.x, a.pos.y, a.pos.z};
    for (int i = 0; i < 3; ++i) {
        const int t = (r.e[i * 3] * v[0] + r.e[i * 3 + 1] * v[1] + r.e[i * 3 + 2] * v[2]) >> 12;
        out[i] = wrap16(int(uint16_t(uint16_t(pos[i]) + uint16_t(t))) << sh);
    }
}
// a tip ring point: the bolt's tip (a world point) + the rotated offset (not shifted)
void ringPoint(const int16_t tip[3], const Matrix3& r, int vx, int vy, int16_t out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = wrap16(int(tip[i]) + ((r.e[i * 3] * vx + r.e[i * 3 + 1] * vy) >> 12));
}
void copyPoint(const int16_t from[3], int16_t to[3]) {
    for (int i = 0; i < 3; ++i) to[i] = from[i];
}
// a colour word's channels each less 8 (the bolts' fade per segment; the bytes wrap as the exe's)
uint32_t fadeColour(uint32_t c) {
    return uint32_t(uint8_t((c & 0xff) - 8)) | uint32_t(uint8_t(((c >> 8) & 0xff) - 8)) << 8 |
           uint32_t(uint8_t(((c >> 16) & 0xff) - 8)) << 16;
}
// exe 0x42f152: the pulse colour 0x6d01b0 by the step (+0xa): r sin64[step], g cos64[step / 2], b sin64[step / 4],
// each >> 6 + 0x60
uint32_t pulseColour(uint8_t step) {
    const uint8_t r = uint8_t(uint8_t(fixedSin64(step & 0x3f) >> 6) + 0x60);
    const uint8_t g = uint8_t(uint8_t(fixedCos64((step >> 1) & 0x3f) >> 6) + 0x60);
    const uint8_t b = uint8_t(uint8_t(fixedSin64(step >> 2) >> 6) + 0x60);
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16;
}
// the packets' draw mode (exe 0x40b6d0(p, 1, 1, 0x3e)): semi-transparency mode 1, the G4 / G3 codes semi
constexpr uint8_t kGlowAbr = 1;
constexpr uint32_t kQuadG4 = 0x3a000000, kTriG3 = 0x32000000;
constexpr int kBoltSegments = 6, kRingSegments = 8, kDiscSegments = 16;
constexpr int16_t kDiscDepthPoint = -0x118; // the disc's depth point toward the camera
// the 4-corner OTZ of points already << the stage shift (exe 0x40c460 + 0x40cad0)
int32_t quadOtz(const int16_t a[3], const int16_t b[3], const int16_t c[3], const int16_t d[3]) {
    const int16_t q[4][3] = {{a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {c[0], c[1], c[2]}, {d[0], d[1], d[2]}};
    int16_t sxy[4][2];
    return projectQuad(q, sxy);
}
// a ribbon of a bolt segment: from the previous segment's points to this one's, the side black and the centre line
// in the colour (`colour` the previous centre's, `next` this one's); sideFirst: {side, side, centre, centre}
void boltRibbon(uint32_t owner, const int16_t s0[3], const int16_t s1[3], const int16_t c0[3], const int16_t c1[3],
                uint32_t colour, uint32_t next, bool sideFirst) {
    const float off[4][2] = {};
    int16_t v[4][3];
    uint32_t c[4];
    if (sideFirst) {
        copyPoint(s0, v[0]);
        copyPoint(s1, v[1]);
        copyPoint(c0, v[2]);
        copyPoint(c1, v[3]);
        c[0] = kQuadG4;
        c[1] = 0;
        c[2] = colour;
        c[3] = next;
    } else {
        copyPoint(c0, v[0]);
        copyPoint(c1, v[1]);
        copyPoint(s0, v[2]);
        copyPoint(s1, v[3]);
        c[0] = colour | kQuadG4;
        c[1] = next;
        c[2] = 0;
        c[3] = 0;
    }
    dc::otQuadG4(owner, c, kGlowAbr, v, off);
}

// exe 0x42f1dd..0x42fd43: 3 jagged bolts of 6 segments from the glow out to its radius, each segment 4 POLY_G4
// ribbons (black at the segments' side points, the bolt colour fading 8 a segment on the centre line); a bolt that
// reached its last segment on the screen ends in a ring of 8 POLY_G3 (black rim, the pulse colour at the tip). The RNG
// use stops at the first segment off the screen (the projected centre 0x6d0120 vs the next 0x6d0134), that bolt
// without its ring (exe 0x42fd37).
void chargeBolts(const EffectActor& a, const ChargeGlowFields& f, uint32_t pulse) {
    const float off[3][2] = {};
    const int len5 = int(f.radius) * 16;
    const int step = len5 / 7;
    for (uint32_t p = 0; p < 3; ++p) {
        uint32_t colour = kChargeColour, next = fadeColour(kChargeColour); // 0x6d01a8 / 0x6d01ac
        const uint32_t k = uint32_t(a.step) + p;
        const int16_t ax = wrap16(((k & 7) >= 4 ? 0x800 : 0) - int(rngStep() & 0x3ff) + 0x400);
        const int16_t ay = wrap16(int((k & 3) << 10) - int(rngStep() & 0x1ff) + 0x300);
        const int16_t az = wrap16(0x80 - int(rngStep() & 0xff));
        const Matrix3 r1 = rotationYXZ(ax, ay, az);
        // the first segment's points (0x6d00c0..e0): its sides (-2, 0) (0, -2) (0, 2) (2, 0) and its centre at z 0x20
        int16_t side[4][3], centre[3];
        boltPoint(a, r1, -2, 0, 0x20, side[0]);
        boltPoint(a, r1, 0, -2, 0x20, side[1]);
        boltPoint(a, r1, 0, 2, 0x20, side[2]);
        boltPoint(a, r1, 2, 0, 0x20, side[3]);
        boltPoint(a, r1, 0, 0, 0x20, centre);
        int32_t otz = quadOtz(side[0], side[1], side[2], side[3]); // 0x6d0198
        CameraPoint q = cameraPoint(centre);
        int16_t px = q.sx, py = q.sy;
        int16_t bx = 0, by = 0, bz = 0;
        bool reached = true;
        for (int i = 0; i < kBoltSegments && reached; ++i) {
            const uint32_t sh = uint32_t(i >> 1);
            bx = wrap16(int((uint32_t(0x80 - (rngStep() & 0xff)) >> sh) + uint32_t(uint16_t(ax))));
            by = wrap16(int((uint32_t(0x40 - (rngStep() & 0x7f)) >> sh) + uint32_t(uint16_t(ay))));
            bz = wrap16(int((uint32_t(0x20 - (rngStep() & 0x3f)) >> sh) + uint32_t(uint16_t(az))));
            const Matrix3 r2 = rotationYXZ(bx, by, bz);
            int16_t len;
            if (i == kBoltSegments - 1) len = wrap16(len5);
            else len = wrap16((i + 1) * step - int(rngStep() & 0xf) + 0x28);
            const int16_t w = int16_t((rngStep() & 7) + uint32_t(i) + 5); // the segment's half width
            // the segment's points (0x6d00e8..0x108): sides (-w, 0) (0, -w) (0, w) (w, 0) and its centre at z len
            int16_t nside[4][3], ncentre[3];
            boltPoint(a, r2, wrap16(-w), 0, len, nside[0]);
            boltPoint(a, r2, 0, wrap16(-w), len, nside[1]);
            boltPoint(a, r2, 0, w, len, nside[2]);
            boltPoint(a, r2, w, 0, len, nside[3]);
            boltPoint(a, r2, 0, 0, len, ncentre);
            q = cameraPoint(ncentre);
            if (py < q.sy ? py > 0xf0 : py < 0) reached = false;
            else if (px < q.sx ? px > 0x140 : px < 0) reached = false;
            if (!reached) break;
            const int32_t notz = quadOtz(nside[0], nside[1], nside[2], nside[3]); // 0x6d019c
            if (((otz + notz) >> 1) >= 1) { // exe 0x42f7c3: the 4 ribbons in one OT entry
                boltRibbon(a.at, side[0], nside[0], centre, ncentre, colour, next, true);
                boltRibbon(a.at, side[3], nside[3], centre, ncentre, colour, next, false);
                boltRibbon(a.at, side[1], nside[1], centre, ncentre, colour, next, true);
                boltRibbon(a.at, side[2], nside[2], centre, ncentre, colour, next, false);
            }
            for (int n = 0; n < 4; ++n) copyPoint(nside[n], side[n]);
            copyPoint(ncentre, centre);
            otz = notz;
            colour = next;
            next = fadeColour(next);
            px = q.sx;
            py = q.sy;
        }
        if (!reached) continue;
        // the tip ring (exe 0x42fa62): the last segment's angles, rim points (cos64, -sin64) >> 6 around the tip
        // (0x6d0108), triangles (rim n, tip, rim n + 1)
        const Matrix3 rr = rotationYXZ(bx, by, bz);
        int16_t rim[3];
        ringPoint(centre, rr, fixedCos64(0) >> 6, -(fixedSin64(0) >> 6), rim);
        for (int n = 0; n < kRingSegments; ++n) {
            const uint32_t angle = uint32_t(n * 8 + 8) & 0x3f;
            int16_t nrim[3];
            ringPoint(centre, rr, fixedCos64(angle) >> 6, -(fixedSin64(angle) >> 6), nrim);
            if (quadOtz(rim, centre, nrim, centre) >= 1) {
                const int16_t v[3][3] = {{rim[0], rim[1], rim[2]}, {centre[0], centre[1], centre[2]},
                                         {nrim[0], nrim[1], nrim[2]}};
                const uint32_t c[3] = {kTriG3, pulse, 0};
                dc::otTriG3(a.at, c, kGlowAbr, v, off);
            }
            copyPoint(nrim, rim);
        }
    }
}

// exe 0x42fda1..0x43041e: the disc, every state: facing the camera (the view angles x / y, a random roll), radius
// (radius + 2) << 4, 16 segments each a POLY_G4 rim band {rim black, mid colour, next rim black, next mid colour} and a
// POLY_G3 {mid colour, centre black, next mid colour}, the mids 1/16 of the way to the centre (the exe takes them on
// the screen); the colour the pulse's half, faded in over the start (x (8 - ticks left) / 8) and out over the shrink
// (x ticks left / 8)
void chargeDisc(const EffectActor& a, const ChargeGlowFields& f, ChargeGlowState st, uint32_t pulse) {
    uint32_t ch[3] = {(pulse & 0xff) >> 1, ((pulse >> 8) & 0xff) >> 1, ((pulse >> 16) & 0xff) >> 1};
    for (uint32_t& c : ch) {
        if (st == ChargeGlowState::Start) c = (c * uint32_t(8 - int(a.stepCounter))) >> 3;
        else if (st == ChargeGlowState::Shrink) c = (uint32_t(a.stepCounter) * c) >> 3;
        c &= 0xff;
    }
    const uint32_t colour = ch[0] | ch[1] << 8 | ch[2] << 16;
    const int radius = (int(f.radius) + 2) << 4;
    const ShortVec4& view = cameraState().viewAngles;
    const Matrix3 rd = rotationYXZ(view.x, view.y, int16_t((rngStep() & 0x7ff) << 1)); // 0x6d0168..16c
    const int sh = stage().coordinateShift & 0x1f;
    const int16_t centre[3] = {wrap16(int(a.pos.x) << sh), wrap16(int(a.pos.y) << sh), wrap16(int(a.pos.z) << sh)};
    int16_t depthPoint[3];
    boltPoint(a, rd, 0, 0, kDiscDepthPoint, depthPoint);
    const int32_t depth = cameraPointDepth(cameraPoint(depthPoint)); // 0x6d019c (exe 0x40d010)
    const float offQ[4][2] = {};
    const float offT[3][2] = {};
    auto rimAt = [&](uint32_t angle, int16_t out[3]) {
        boltPoint(a, rd, wrap16((int(fixedCos64(angle)) * radius) >> 12),
                  wrap16(-((int(fixedSin64(angle)) * radius) >> 12)), 0, out);
    };
    int16_t rim[3];
    rimAt(0, rim);
    for (int j = 0; j < kDiscSegments; ++j) {
        int16_t nrim[3];
        rimAt(uint32_t(j * 4 + 4) & 0x3f, nrim);
        if (((quadOtz(rim, centre, nrim, centre) + depth) >> 1) >= 1) {
            int16_t m0[3], m1[3];
            for (int i = 0; i < 3; ++i) {
                m0[i] = wrap16((int(rim[i]) * 15 + int(centre[i])) >> 4);
                m1[i] = wrap16((int(nrim[i]) * 15 + int(centre[i])) >> 4);
            }
            const int16_t vq[4][3] = {{rim[0], rim[1], rim[2]}, {m0[0], m0[1], m0[2]},
                                      {nrim[0], nrim[1], nrim[2]}, {m1[0], m1[1], m1[2]}};
            const uint32_t cq[4] = {kQuadG4, colour, 0, colour};
            dc::otQuadG4(a.at, cq, kGlowAbr, vq, offQ);
            const int16_t vt[3][3] = {{m0[0], m0[1], m0[2]}, {centre[0], centre[1], centre[2]}, {m1[0], m1[1], m1[2]}};
            const uint32_t ct[3] = {colour | kTriG3, 0, colour};
            dc::otTriG3(a.at, ct, kGlowAbr, vt, offT);
        }
        copyPoint(nrim, rim);
    }
}

// the draw (flags bit 2): nothing past the depth range; the bolts and their rings while growing / held, a crackle on
// 1 of 4, then the disc
void chargeDraw(const EffectActor& a, const ChargeGlowFields& f, ChargeGlowState st) {
    const int16_t c0[3] = {a.pos.x, a.pos.y, a.pos.z};
    const CameraPoint c = cameraPoint(c0); // the effect's own position, not shifted (as the exe)
    if ((c.view[2] >> 2) >= int(stage().depthRange)) return;
    const uint32_t pulse = pulseColour(a.step);
    if (st == ChargeGlowState::Grow || st == ChargeGlowState::Held) {
        chargeBolts(a, f, pulse);
        if ((rngStep() & 3) == 0) {
            soundPointAt(a);
            soundPlayAtPos(kSoundCrackle, scratch().soundPoint);
        }
    }
    chargeDisc(a, f, st, pulse);
}

void chargeGlow(EffectActor& a) { // exe 0x42ef70
    ChargeGlowFields& f = classState<ChargeGlowFields>(a);
    ChargeGlowScratch& s = scratch();
    s.setColour(kChargeColour);
    if (a.subState == 0) {
        const ChargeGlowState st = ChargeGlowState(a.state);
        if (st == ChargeGlowState::Start) {
            f.radius = 3;
            a.step = 0x10;
            a.stepCounter = 8;
            for (int n = 0; n < kChargeSparks; ++n) { // 3 spark rings (variant 0)
                const uint32_t address = allocActorA0(kActorStageClass);
                if (!address) continue;
                EffectActor& e = effectActorAt(address);
                SparkRingParams& r = classState<SparkRingParams>(e);
                e.flags = kActorLiveDrawn;
                e.type = kSparkRingsStageType;
                e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
                e.subtype = 0;
                e.variant = 0;
                e.spawnByte = 0;
                r.clearVelocityWord();
                r.angles[0] = uint8_t(rngStep() & 0xff);
                r.angles[1] = uint8_t(rngStep() & 0xff);
                r.angles[2] = uint8_t(rngStep() & 0xff);
                r.spin = 0x10;
                r.life = 8;
                r.grow = 0x50;
                r.targetSize = 8;
                r.sizeStep = 0x24;
                r.colour[0] = uint8_t(s.colour[0] >> 1);
                r.colour[1] = uint8_t(s.colour[1] >> 1);
                r.colour[2] = uint8_t(s.colour[2] >> 1);
                r.innerRatio = 0x18;
                r.gradient = 0xc;
                r.rotationOrder = 0;
                e.copyPositionWords(a);
            }
            f.hitBox.previousX = a.pos.x;
            f.hitBox.previousZ = a.pos.z;
            soundPointAt(a);
            soundPlayAtPos(kSoundCharge, s.soundPoint);
        } else if (st == ChargeGlowState::Grow) {
            a.stepCounter = 7;
        } else if (st == ChargeGlowState::Shrink) {
            a.stepCounter = 8;
        }
        a.subState = 1;
    }
    if (a.flags & kActorDrawn) chargeDraw(a, f, ChargeGlowState(a.state));
    if (a.state == uint8_t(ChargeGlowState::Grow) || a.state == uint8_t(ChargeGlowState::Held)) {
        const int16_t pos[3] = {a.pos.x, a.pos.y, a.pos.z};
        const uint32_t ownerShape =
            uint32_t(int32_t((uint32_t(int32_t(f.radius)) | 0x1a0000u) << 4) >> 2) | hitOwner(a.slot);
        hitListSubmit(pos, f.hitBox, ownerShape, uint32_t(int32_t(f.damage)) | kHitAttackStrong);
        f.hitBox.result = 0;
        f.hitBox.previousX = a.pos.x;
        f.hitBox.previousZ = a.pos.z;
    }
    const ChargeGlowState st = ChargeGlowState(a.state);
    ++a.step;
    if (st == ChargeGlowState::Held) {
        if (f.held == 0) {
            a.state = uint8_t(ChargeGlowState::Shrink);
            a.subState = 0;
        }
    } else if (a.stepCounter == 0) {
        a.subState = 0;
        a.state = uint8_t(uint8_t(st) + 1u);
    } else {
        if (st == ChargeGlowState::Grow) f.radius = int8_t(uint8_t(f.radius) + 2u);
        --a.stepCounter;
    }
    if (a.state != uint8_t(ChargeGlowState::Freed)) {
        f.held = 0;
        return;
    }
    freeActor(a);
}

} // namespace

void chargeGlowUpdate(const PlayerEnv&, uint32_t actor) { chargeGlow(effectActorAt(actor)); }

} // namespace od::port
