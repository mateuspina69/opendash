// The turret's bullet class 0x5c3ee0 (see turret_bullet.hpp): its sub-class 0x5c4250, then the hit entry and the
// billboards. Fields: the header's step (+0xa) the homing phase, its step counter (+0xb) the colour phase; +0xc0 the
// state's timer, +0xc2 kBullet* bits, +0xc3 the trail's length, +0xc4 the hit box, +0xcc.. the trail.
#include "game/turret_bullet.hpp"
#include "game/turret_bullet_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/spark_rings.hpp"

namespace od::port {

using namespace turret_bullet_state;

namespace {

constexpr const char* kBulletPort = "turret bullet port"; // the area of its port-gap errors

constexpr uint8_t kBulletSpent = 0x01, kBulletHit = 0x02, kBulletBlocked = 0x04, kBulletFree = 0x80;
// The draw values the bullet keeps between its routines (the exe's scratch past the shared geometry scratch: the
// bullet's own): its colour, the trail's depth word, the homing's aim point, the billboards' texture words
struct BulletScratch {
    StateArray<uint8_t, 4> colour{}; // r, g, b and the byte the dword clear covers
    uint32_t trailDepth = 0;
    Fixed16Vec3 aim;
    StateArray<uint8_t, 4> uvBytes{}; // u, v, du, dv
    uint32_t texture = 0;

    uint32_t colourWord() const {
        return uint32_t(colour[0]) | uint32_t(colour[1]) << 8 | uint32_t(colour[2]) << 16 | uint32_t(colour[3]) << 24;
    }
    // the colour cleared (a dword), then its three bytes
    void setColour(uint8_t r, uint8_t g, uint8_t b) { colour = {r, g, b, 0}; }
    void clearColour() { colour = {}; }
    // the texels as one word (u | v << 8 | du << 16 | dv << 24)
    uint32_t uvWord() const {
        return uint32_t(uvBytes[0]) | uint32_t(uvBytes[1]) << 8 | uint32_t(uvBytes[2]) << 16 |
               uint32_t(uvBytes[3]) << 24;
    }
    void setUvWord(uint32_t w) {
        for (size_t i = 0; i < 4; ++i) uvBytes[i] = uint8_t(w >> (8 * i));
    }
    // the texture word's high half (the tpage)
    uint16_t textureHigh() const { return uint16_t(texture >> 16); }
};
BulletScratch& scratch() {
    static BulletScratch s;
    return s;
}
// the bullet's sub-class, size and texels by variant (exe tables)
const DataTable<RoutineTable<8>> bulletClasses{TableId::TurretBulletClasses}; // the named routines (engine/routines.csv)
const DataTable<ScalarTable<int16_t, 8>> bulletSizes{TableId::TurretBulletSizes};
const DataTable<ScalarTable<uint32_t, 8>> bulletTexels{TableId::TurretBulletTexels};
constexpr uint32_t kBulletBoxSize = 0xc;
constexpr uint16_t kHeldTicks = 0x20, kHomingTicks = 0x100, kFadeTicks = 0x10;
constexpr uint16_t kSoundBullet = 0xd6;

// a corner's texels (u | v << 8) of the quad scratch
uint32_t quadCorner(const QuadTexels& t, size_t c) {
    return uint32_t(t.uvBytes[2 * c]) | uint32_t(t.uvBytes[2 * c + 1]) << 8;
}

void homing(const PlayerEnv& env, ProjectileActor& a, TurretBulletFields& f) { // exe 0x5c4250
    BulletScratch& s = scratch();
    const uint32_t ph = a.stepCounter;
    switch (TurretBulletState(a.state)) {
    case TurretBulletState::Start: {
        a.state = uint8_t(TurretBulletState::Held);
        a.subState = 1;
        a.step = uint8_t(rngStep() & 0xff);
        a.rotation.z = 0;
        a.rotation.y = 0;
        a.rotation.x = 0;
        a.stepCounter = 0x10;
        a.velocity.z = 0;
        a.collisionBox.pointAt(stateAddress(TableId::TurretBulletBoxes) + uint32_t(a.variant) * kBulletBoxSize);
        a.velocity.y = 0;
        a.velocity.x = 0;
        a.prevPos = a.pos;
        a.airborne = 0;
        a.wasAirborne = 0;
        a.prevRotation = a.rotation;
        f.clearBlockBytes();
        f.timer = kHeldTicks;
        f.trailLength = 4;
        f.hitBox.previousX = a.pos.x.whole;
        f.hitBox.previousZ = a.pos.z.whole;
        f.hitBox.result = 0;
        s.clearColour();
        soundPlayAtPos(kSoundBullet, a.pos);
        break;
    }
    case TurretBulletState::Held: {
        const int k = 0x20 - int(f.timer);
        s.setColour(uint8_t(((int(sin64(ph & 0x3f)) >> 7) + 0x50) * k >> 5),
                    uint8_t(((int(cos64((ph >> 1) & 0x3f)) >> 7) + 0x50) * k >> 5),
                    uint8_t(((int(trig64Tables().sine[ph >> 2]) >> 7) + 0x50) * k >> 5));
        if (a.subState == 0) { // the owner let go
            f.bits |= uint8_t(kBulletFree | kBulletSpent);
            return;
        }
        a.subState = 0;
        if (f.timer == 0) {
            a.state = uint8_t(TurretBulletState::Homing);
            f.timer = kHomingTicks;
        } else {
            f.timer = uint16_t(f.timer - 1u);
        }
        break;
    }
    case TurretBulletState::Homing: {
        s.setColour(uint8_t((uint16_t(sin64(ph & 0x3f)) >> 7) + 0x50),
                    uint8_t((uint16_t(cos64((ph >> 1) & 0x3f)) >> 7) + 0x50),
                    uint8_t((uint16_t(trig64Tables().sine[ph >> 2]) >> 7) + 0x50));
        // the aim point: the player 0x80 up, wobbling by the sine / cosine of the phase >> the wobble shift
        s.aim = player().pos;
        const uint32_t i = (uint32_t(a.step) >> (f.wobbleShift & 0x1f)) & 0x3f;
        s.aim.y.whole = int16_t(uint16_t(s.aim.y.whole) - 0x80u);
        s.aim.x.whole = int16_t(uint16_t(wrap16(cos64(i) >> 6)) + uint16_t(s.aim.x.whole));
        s.aim.z.whole = int16_t(uint16_t(wrap16(sin64(i) >> 6)) + uint16_t(s.aim.z.whole));
        a.rotation.y = int16_t((uint32_t(angleOf(s.aim, a.pos)) - uint32_t(kHalfTurn)) & kAngleMask);
        a.rotation.x = int16_t(pitchOf(s.aim, a.pos)); // exe 0x5e3ca0
        const uint32_t j = (uint32_t(a.step) >> ((3u - f.wobbleShift) & 0x1f)) & 0x3f;
        const int16_t rot[3] = {a.rotation.x, a.rotation.y, a.rotation.z};
        int32_t o[3];
        rotateVector(rot, int(sin64(j)) >> 5, 0, -0xc0, o); // exe 0x5e3970
        std::array<Lane32, 3>& offset = projectionScratch().storedViewPoint;
        offset[0] = o[0];
        offset[1] = o[1];
        offset[2] = o[2];
        a.pos.x = int32_t(uint32_t(a.pos.x) + uint32_t(o[0]));
        a.pos.y = int32_t(uint32_t(a.pos.y) + uint32_t(o[1]));
        a.pos.z = int32_t(uint32_t(a.pos.z) + uint32_t(o[2]));
        if (f.trailLength != 0) { // the trail: the accelerations -(o >> 16), shifted along the entries
            a.acceleration.x = int16_t(-(o[0] >> 16));
            a.acceleration.y = int16_t(-(o[1] >> 16));
            a.acceleration.z = int16_t(-(o[2] >> 16));
            for (int k = int(f.trailLength) - 1; k > 0; --k) {
                BulletTrailEntry& e = f.trail[size_t(k)];
                e.setWord(f.trail[size_t(k - 1)].word());
                e.depth = uint8_t(f.trailCounter - uint8_t(k) - 1u);
            }
            BulletTrailEntry& first = f.trail[0];
            first.step[0] = int8_t(a.acceleration.x);
            first.step[1] = int8_t(a.acceleration.y);
            first.step[2] = int8_t(a.acceleration.z);
            first.depth = uint8_t(f.trailCounter - 1u);
        }
        if (f.timer != 0) {
            f.timer = uint16_t(f.timer - 1u);
        } else {
            a.state = uint8_t(TurretBulletState::Fading);
            f.timer = kFadeTicks;
        }
        break;
    }
    case TurretBulletState::Fading: {
        const int k = f.timer;
        s.setColour(uint8_t(((int(sin64(ph & 0x3f)) >> 6) + 0x60) * k >> 4),
                    uint8_t(((int(cos64((ph >> 1) & 0x3f)) >> 6) + 0x60) * k >> 4),
                    uint8_t(((int(trig64Tables().sine[ph >> 2]) >> 6) + 0x60) * k >> 4));
        if (f.timer != 0) f.timer = uint16_t(f.timer - 1u);
        else f.bits |= uint8_t(kBulletFree | kBulletSpent);
        break;
    }
    default: break;
    }
    s.trailDepth = uint32_t(int32_t(int8_t(f.trailCounter))) << 23;
    ++f.trailCounter;
    if (f.hitBox.result & kHitAttackBump) f.bits |= uint8_t(kBulletFree | kBulletHit);
    else if (int32_t(actorTestMove(env, a)) < 0) f.bits |= kBulletBlocked;
    const uint8_t c = f.bits;
    if (c & kBulletHit) { // the burst (spark rings, variant 5) in the bullet's colour
        const uint32_t address = allocActorA0(kActorStageClass);
        if (!address) return;
        EffectActor& e = effectActorAt(address);
        SparkRingParams& r = classState<SparkRingParams>(e);
        e.flags = kActorLiveDrawn;
        e.type = kSparkRingsStageType;
        e.loopFlags |= kActorSweepGroup2;
        e.variant = 5;
        e.pos.x = a.pos.x.whole;
        e.pos.y = a.pos.y.whole;
        r.clearVelocityWord();
        e.pos.z = a.pos.z.whole;
        r.height = int8_t(0xe8);
        r.angles[0] = uint8_t((uint32_t(uint16_t(a.rotation.x)) - uint32_t(kQuarterTurn)) >> 4);
        r.angles[1] = uint8_t(uint32_t(angleOf(a.pos, player().pos)) >> 4);
        r.angles[2] = 0;
        r.spin = 0;
        r.life = 8;
        r.grow = 0;
        r.targetSize = 0x28;
        r.sizeStep = 0x14;
        r.colour[0] = uint8_t(uint32_t(s.colour[0]) << 1);
        r.colour[1] = uint8_t(uint32_t(s.colour[1]) << 1);
        r.colour[2] = uint8_t(uint32_t(s.colour[2]) << 1);
        r.innerRatio = 0xc;
        r.gradient = 0xc;
        r.rotationOrder = 1;
    } else if ((c & kBulletBlocked) && !(c & kBulletSpent)) {
        a.state = uint8_t(TurretBulletState::Fading);
        f.timer = kFadeTicks;
        f.bits |= kBulletSpent;
    }
}

void bullet(const PlayerEnv& env, ProjectileActor& a) { // exe 0x5c3ee0
    TurretBulletFields& f = classState<TurretBulletFields>(a);
    a.savePreviousPose();
    a.wasAirborne = a.airborne;
    const RoutineRef cls = a.variant < bulletClasses.size() ? bulletClasses[a.variant] : RoutineRef();
    if (!cls.is(RoutineId::TurretBulletHoming)) notPortedAt(kBulletPort, "0x5c3ee0 sub-class", cls.pointerValue());
    homing(env, a, f);
    if (f.bits & kBulletFree) {
        freeActor(a);
        return;
    }
    const int16_t pos[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const uint32_t sz = uint32_t(int32_t(bulletSizes[a.variant]));
    if (!(f.bits & kBulletSpent))
        hitListSubmit(pos, f.hitBox, uint32_t(int32_t(sz | 0x1800000u) >> 2) | hitOwner(a.slot),
                      (uint32_t(int32_t(f.damage)) | kHitAttackThrown) << 1);
    f.bits &= uint8_t(~(kBulletHit | kBulletBlocked));
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    if (a.flags & kActorDrawn) { // the glow billboards (0x2c records, word 0 unwritten) of the bullet and its trail
        BulletScratch& s = scratch();
        ProjectionScratch& ps = projectionScratch();
        LaneVec4 point = ps.quadCorners[0]; // the exe's scratch point (set before the hit submit)
        point.x = a.pos.x.whole;
        point.y = a.pos.y.whole;
        point.z = a.pos.z.whole;
        s.texture = f.texture;
        const uint32_t uv = bulletTexels[a.variant]; // u, v, du, dv
        s.setUvWord(uv);
        for (size_t k = 0; k < 4; ++k) ps.quadTexels.setCorner(k, uint16_t(uv & 0xffff));
        QuadTexels& t = ps.quadTexels;
        t.uvBytes[2] = uint8_t(t.uvBytes[2] + s.uvBytes[2]);
        t.uvBytes[6] = uint8_t(t.uvBytes[6] + s.uvBytes[2]);
        t.uvBytes[5] = uint8_t(t.uvBytes[5] + s.uvBytes[3]);
        t.uvBytes[7] = uint8_t(t.uvBytes[7] + s.uvBytes[3]);
        uint32_t size = sz; // the size, shrinking by trailShrink along the trail
        const auto quad = [&] {
            dc::emit({0, s.colourWord() | 0x2e000000u,
                      uint32_t(uint16_t(point.x)) | uint32_t(int32_t(point.y)) << 16,
                      quadCorner(t, 0) | uint32_t(s.textureHigh()) << 16, uint32_t(int32_t(point.z)),
                      quadCorner(t, 1) | uint32_t(uint16_t(s.texture)) << 16, (size & 0xffff) | s.trailDepth,
                      quadCorner(t, 2), 0, quadCorner(t, 3)},
                     a.at, false);
        };
        quad();
        for (int i = 0; i < int(f.trailLength); ++i) {
            const BulletTrailEntry e = f.trail[size_t(i)];
            size -= uint32_t(int32_t(f.trailShrink));
            point.x = int16_t(uint32_t(uint16_t(point.x)) + uint32_t(int32_t(e.step[0])) * 2);
            point.y = int16_t(uint32_t(uint16_t(point.y)) + uint32_t(int32_t(e.step[1])) * 2);
            point.z = int16_t(uint32_t(uint16_t(point.z)) + uint32_t(int32_t(e.step[2])) * 2);
            s.trailDepth = uint32_t(int32_t(int8_t(e.depth))) << 23;
            s.colour[0] = uint8_t(s.colour[0] >> 1);
            s.colour[1] = uint8_t(s.colour[1] >> 1);
            s.colour[2] = uint8_t(s.colour[2] >> 1);
            quad();
        }
    }
    ++a.step;
    ++a.stepCounter;
}

} // namespace

void turretBulletUpdate(const PlayerEnv& env, uint32_t actor) { bullet(env, projectileActorAt(actor)); }

} // namespace od::port
