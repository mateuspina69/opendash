// The charge glow class 0x42ef70 (see charge_glow.hpp). +8 the state (0 the start: the sparks and the sound, 8 ticks;
// 1 grow 7 ticks (the radius + 2 a tick); 2 held while its owner sets +0xc each tick; 3 shrink 8 ticks; 4 freed), +9
// the state entered, the header's step its phase (its bolts' seed) and its step counter the state's ticks left.
#include "game/actor_state.hpp"
#include "game/charge_glow_state.hpp"
#include "game/charge_glow.hpp"

#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
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
// shift: one world point of a bolt
void boltPoint(const EffectActor& a, const Matrix3& r, int16_t vx, int16_t vy, int16_t vz, int16_t out[3]) {
    const int sh = stage().coordinateShift & 0x1f;
    const int v[3] = {vx, vy, vz};
    const int16_t pos[3] = {a.pos.x, a.pos.y, a.pos.z};
    for (int i = 0; i < 3; ++i) {
        const int t = (r.e[i * 3] * v[0] + r.e[i * 3 + 1] * v[1] + r.e[i * 3 + 2] * v[2]) >> 12;
        out[i] = wrap16(int(uint16_t(uint16_t(pos[i]) + uint16_t(t))) << sh);
    }
}
// the draw's game state: 3 jagged bolts whose RNG use stops at the first segment off the screen (the projected tip
// 0x6d0120 vs the next 0x6d0134); a crackle sound on 1 of 4; then one RNG step for the disc's rotation. The bolts'
// quads, the rings and the disc are OT packets (render output).
void chargeDraw(const EffectActor& a, const ChargeGlowFields& f, ChargeGlowState st) {
    const int16_t c0[3] = {a.pos.x, a.pos.y, a.pos.z};
    const CameraPoint c = cameraPoint(c0); // the effect's own position, not shifted (as the exe)
    if ((c.view[2] >> 2) >= int(stage().depthRange)) return;
    if (st == ChargeGlowState::Grow || st == ChargeGlowState::Held) {
        const int len5 = int(f.radius) * 16;
        const int step = len5 / 7;
        for (uint32_t p = 0; p < 3; ++p) {
            const uint32_t k = uint32_t(a.step) + p;
            const int16_t ax = wrap16(((k & 7) >= 4 ? 0x800 : 0) - int(rngStep() & 0x3ff) + 0x400);
            const int16_t ay = wrap16(int((k & 3) << 10) - int(rngStep() & 0x1ff) + 0x300);
            const int16_t az = wrap16(0x80 - int(rngStep() & 0xff));
            const Matrix3 r1 = rotationYXZ(ax, ay, az);
            int16_t pt[3];
            boltPoint(a, r1, 0, 0, 0x20, pt); // the exe's 0x6d00e0, projected into 0x6d0120
            CameraPoint q = cameraPoint(pt);
            int16_t px = q.sx, py = q.sy;
            for (int i = 0; i < 6; ++i) {
                const uint32_t sh = uint32_t(i >> 1);
                const int16_t bx = wrap16(int((uint32_t(0x80 - (rngStep() & 0xff)) >> sh) + uint32_t(uint16_t(ax))));
                const int16_t by = wrap16(int((uint32_t(0x40 - (rngStep() & 0x7f)) >> sh) + uint32_t(uint16_t(ay))));
                const int16_t bz = wrap16(int((uint32_t(0x20 - (rngStep() & 0x3f)) >> sh) + uint32_t(uint16_t(az))));
                const Matrix3 r2 = rotationYXZ(bx, by, bz);
                int16_t len;
                if (i == 5) len = wrap16(len5);
                else len = wrap16((i + 1) * step - int(rngStep() & 0xf) + 0x28);
                rngStep(); // the segment's width (RNG & 7) + 5 + i
                boltPoint(a, r2, 0, 0, len, pt); // the exe's 0x6d0108, projected into 0x6d0134
                q = cameraPoint(pt);
                if (py < q.sy) {
                    if (py > 0xf0) break;
                } else if (py < 0) {
                    break;
                }
                if (px < q.sx) {
                    if (px > 0x140) break;
                } else if (px < 0) {
                    break;
                }
                // the segment's quads (OT packets): render output
                px = q.sx;
                py = q.sy;
            }
            // the 8 ring segments of this bolt: render output (no RNG)
        }
        if ((rngStep() & 3) == 0) {
            soundPointAt(a);
            soundPlayAtPos(kSoundCrackle, scratch().soundPoint);
        }
    }
    rngStep(); // the glow disc's rotation (exe 0x6d016c); the disc (16 segments) is render output
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
