// The opening's effect class 0x435960 (see opening_effects.hpp).
#include "content/intro/opening_effects.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/field_alias.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/vectors.hpp"

namespace od::port {


namespace {

// the variant's first tick: state 1, the tail cleared (exe 0x40dc40 over +0x18..+0x27: the class block)
bool firstTick(EffectActor& effect) {
    if (OpeningEffectState(effect.state) != OpeningEffectState::Start) return false;
    effect.enterState(uint8_t(OpeningEffectState::Running));
    effect.clearClassBlock();
    return true;
}

// ---- the fuse (variant 0; its fields: opening_effects.hpp FuseFields)
// ---------------------------------------------------------------------------------------------
// the flash it leaves (effect type 7 variant 0, late loop) and its parameter word (+0xc)
constexpr uint8_t kFuseFlashType = 7;
struct FuseFlashFields {
    explicit FuseFlashFields(const EffectActor& actor) : FuseFlashFields(const_cast<EffectActor&>(actor)) {}
    explicit FuseFlashFields(EffectActor& actor) : param(actor.params, 0) {}
    BytesField<uint16_t, 4> param;
};
constexpr uint32_t kAnyLayers = 0xffff;

// exe 0x435980 (variant 0)
void fuse(const PlayerEnv& env, EffectActor& e) {
    FuseFields& f = classState<FuseFields>(e);
    if (firstTick(e)) {
        f.x = uint32_t(int32_t(f.fromX)) << 16;
        f.z = uint32_t(int32_t(f.fromZ)) << 16;
        const int32_t n = f.steps;
        if (n == 0) notPorted(kStoryPort, "effect 0x435980: 0 steps (the exe divides by zero)");
        f.stepX = int16_t(((int32_t(f.toX) - f.fromX) * 16) / n);
        f.age = 0;
        f.stepZ = int16_t(((int32_t(f.toZ) - f.fromZ) * 16) / n);
        f.flashWord = f.flashParam;
    } else {
        f.x = f.x + (uint32_t(int32_t(f.stepX)) << 12);
        f.z = f.z + (uint32_t(int32_t(f.stepZ)) << 12);
    }
    if ((e.flags & kActorDrawn) && !(uint16_t(f.age) & 1)) {
        if (const uint32_t at = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
            EffectActor& flash = effectActorAt(at);
            flash.flags = kActorLiveDrawn;
            flash.type = kFuseFlashType;
            flash.loopFlags |= kActorLateLoop;
            flash.variant = 0;
            flash.pos.x = int16_t(int32_t(f.x) >> 16);
            const int16_t ground[3] = {int16_t(int32_t(f.x) >> 16), 0, int16_t(int32_t(f.z) >> 16)};
            flash.pos.y = int16_t(probeGroundAt(env, ground, CollisionBoxRef::record(0), 0, kAnyLayers)); // exe 0x40f1c0
            flash.pos.z = int16_t(int32_t(f.z) >> 16);
            FuseFlashFields(flash).param = f.flashWord;
        }
    }
    f.age += 1u;
    if (f.age == int16_t(f.life)) freeActor(e); // exe 0x5e1580
}

// ---- the sparkle (variant 1; its fields: opening_effects.hpp SparkleFields)
// ------------------------------------------------------------------------------------------
constexpr int32_t kSparkleSprites = 0x10, kSparkleSpriteStep = 4;

// exe 0x435ab0 (variant 1)
void sparkle(EffectActor& e) {
    SparkleFields s(e);
    if (firstTick(e)) {
        s.age = 0;
        s.life = int16_t(int32_t(s.lifeParam));
        s.yaw = int16_t(int32_t(s.direction) << 4);
    }
    if (!(e.flags & kActorDrawn)) return;
    int live = 0;
    for (int32_t k = 0; k < kSparkleSprites; k += kSparkleSpriteStep)
        if (int32_t(s.age) + k < int32_t(s.life)) ++live;
    if (live == 0) {
        freeActor(e); // exe 0x5e1580
        return;
    }
    s.age += 1u;
}

// ---- the rider spark (variant 2; its fields: opening_effects.hpp RiderSparkFields)
// --------------------------------------------------------------------------------------
constexpr uint16_t kRiderSparkSound = 0xb1;
constexpr int16_t kRiderSparkDefaultTicks = 2;
constexpr uint32_t kRiderColourShift = 20;

// exe 0x435ce0 (variant 2)
void riderSpark(EffectActor& e) {
    RiderSparkFields& s = classState<RiderSparkFields>(e);
    if (firstTick(e)) {
        if (uint8_t(s.ownerSlot) != 0) s.owner = actorBySlot(uint32_t(int32_t(s.ownerSlot))); // exe 0x5e0fa0
        s.age = 0;
        s.ticks = uint8_t(s.ticksParam) != 0 ? int16_t(int32_t(s.ticksParam)) : kRiderSparkDefaultTicks;
        s.drawWord = (uint32_t(s.colour) << kRiderColourShift) | uint16_t(s.drawParam);
    }
    if (e.flags & kActorDrawn) {
        soundPlayAtPosS16(kRiderSparkSound, e.pos); // exe 0x5d5420
        // the textured quad and the flat quad: render output
    }
    s.age += 1u;
    if (s.ticks > 0 && s.age == s.ticks) {
        freeActor(e); // exe 0x5e1580
        return;
    }
    if (uint8_t(s.ownerSlot) != 0) {
        const Actor& o = actorLayoutAt(s.owner);
        moveAlongYaw16(e.pos, o.rotation.y, o.velocity.x, o.velocity.y, o.velocity.z); // exe 0x5e3750
    }
}

// ---- the chip (variant 3; its fields: opening_effects.hpp ChipFields)
// ---------------------------------------------------------------------------------------------
constexpr int kChipRiseYaw = 0xe0;
constexpr uint8_t kChipBaseRise = 4;
constexpr uint8_t kRising = 0; // the chip's sub-state while it rises

// exe 0x435ed0 (variant 3)
void chip(EffectActor& e) {
    ChipFields& c = classState<ChipFields>(e);
    if (firstTick(e)) {
        c.age = 0;
        c.life = int16_t(int32_t(c.lifeParam));
        c.yaw = int16_t(int32_t(int8_t(c.direction)) << 4);
        c.riseTick = 0;
        c.riseTicks = uint8_t((uint8_t(fixedSin(int(c.direction & 0x7fu))) & 3u) + kChipBaseRise);
        c.lift = int16_t(int32_t(c.liftParam) << 5);
        c.reach = int16_t(int32_t(c.reachParam) << 5);
        const uint32_t y = uint32_t(int32_t(c.clutParam));
        c.clutWord = ((y & 0xff00u) << 12) | (y & 0xffu);
    }
    if (c.age == c.life) {
        freeActor(e); // exe 0x5e1580
        return;
    }
    if (e.subState == kRising) {
        moveAlongYaw16(e.pos, kChipRiseYaw, 0, c.lift >> 5, c.reach >> 3); // exe 0x5e3750
        const uint8_t t = uint8_t(c.riseTick + 1u);
        c.riseTick = t;
        if (t == c.riseTicks) ++e.subState;
    } else {
        moveAlongYaw16(e.pos, c.yaw, 0, c.lift, c.reach);
        c.age += 1u;
    }
}

// ---- the engine flame (variant 4)
// -------------------------------------------------------------------------------------
// (its fields: opening_effects.hpp EngineFlameFields)

// exe 0x436080 (variant 4)
void engineFlame(EffectActor& e) {
    EngineFlameFields& f = classState<EngineFlameFields>(e);
    if (firstTick(e)) f.owner = actorBySlot(uint32_t(int32_t(f.ownerSlot)));
    const Actor& o = actorLayoutAt(f.owner);
    if (o.flags == 0) {
        freeActor(e); // exe 0x5e1580
        return;
    }
    if (f.follows != 0) {
        e.pos.x = o.pos.x.whole;
        e.pos.y = o.pos.y.whole;
        e.pos.z = o.pos.z.whole;
        f.yaw = o.rotation.y;
    }
}

// ---- the streaks (variants 5 / 6)
// -------------------------------------------------------------------------------------
// (their fields and kinds: opening_effects.hpp StreakFields, StreakKind)

// exe 0x436340 (variant 5) / 0x436560 (variant 6, lifts)
void streak(EffectActor& e, bool lifts) {
    StreakFields s(e);
    if (firstTick(e)) {
        s.age = 0;
        s.life = int16_t(int32_t(s.lifeParam) << 2);
    }
    if (lifts) {
        const int16_t d = liftStreakKinds[size_t(int(s.speed))].speed;
        moveAlongYaw16(e.pos, s.yaw, 0, d, d);
    } else {
        moveAlongYaw16(e.pos, s.yaw, 0, 0, streakKinds[size_t(int(s.speed))].speed);
    }
    // the streak's sprites: render output (content/intro/opening_effects_draw)
    s.age += 1u;
    if (s.age == s.life) freeActor(e);
}

// ---- the puffs (variants 7..10)
// ---------------------------------------------------------------------------------------
// (their fields: opening_effects.hpp OwnerPuffFields, ThrownPuffFields)
// A kind of thrown puff (exe table 0x88cc20, 8 bytes): its speed (0, up, ahead), its size and its growth per tick
struct PuffKind : DataRecord {
    int16_t up = 0;
    int16_t ahead = 0;
    uint16_t size = 0;
    uint16_t growth = 0;
    static PuffKind load(const TableReader& r) {
        PuffKind rec;
        rec.up = r.s16(0);
        rec.ahead = r.s16(2);
        rec.size = r.u16(4);
        rec.growth = r.u16(6);
        return rec;
    }
};
const DataTable<RecordTable<PuffKind, 8, 8>> puffKinds{TableId::OpeningEffectsPuffKinds};
constexpr int kOwnerPuffBack = -0x400, kBurstPuffBack = -0x200, kBurstGrowth = 0x10, kBonePuffBack = -0x620;
constexpr int16_t kOwnerPuffTicks = 0x10, kBurstPuffTicks = 0x20, kThrownBonePuffTicks = 8, kBonePuffTicks = 0x14;
constexpr int kBoneThrowSpread = 0x200, kBoneThrowMask = 0x3ff, kBoneThrowBack = 0x480;
constexpr int kBoneThrowTicks = 8;
constexpr int kRngStepsPerThrow = 3;
constexpr int kOwnerBone = 0;

// out = Ry Rx Rz(the owner's rotation) * (x, y, z) (the integer parts)
void ownerRotated(const Actor& owner, int x, int y, int z, ShortVec3& out) {
    int32_t r[3];
    rotateVector(owner.rotation, x, y, z, r);
    out.x = int16_t(uint32_t(r[0]) >> 16);
    out.y = int16_t(uint32_t(r[1]) >> 16);
    out.z = int16_t(uint32_t(r[2]) >> 16);
}

void bonePuff(EffectActor& e) { // exe 0x436900
    OwnerPuffFields& p = classState<OwnerPuffFields>(e);
    if (firstTick(e)) {
        const Actor& o = actorLayoutAt(actorBySlot(uint32_t(int32_t(p.ownerSlot))));
        for (int i = 0; i < kRngStepsPerThrow; ++i) rngStep();
        const uint32_t r = rngLastValue();
        const int d = kBoneThrowSpread - int(r & kBoneThrowMask);
        ownerRotated(o, d, d, int(r & kBoneThrowMask) - kBoneThrowBack, p.velocity);
        p.age = 0;
    }
    const uint32_t ownerAt = actorBySlot(uint32_t(int32_t(p.ownerSlot)));
    const AnimatedActor& o = animatedActorAt(ownerAt);
    int32_t r[3];
    rotateVector(o.rotation, 0, 0, kBonePuffBack, r);
    const BoneBlock* block = boneBlockOf(o, kOwnerBone); // exe 0x5e1780
    if (!block) notPorted(kStoryPort, "the effect owner has no bone block");
    const ShortVec4& bone = block->points[kOwnerBone];
    e.pos.x = int16_t(int16_t(uint32_t(r[0]) >> 16) + bone.x);
    e.pos.y = int16_t(int16_t(uint32_t(r[1]) >> 16) + bone.y);
    e.pos.z = int16_t(int16_t(uint32_t(r[2]) >> 16) + bone.z);
    if (p.thrown != 0) { // thrown: (8 - age) / 8 of the throw on top (rounded toward zero)
        auto thrown = [&](int16_t& lane, int16_t v) {
            const int t = (kBoneThrowTicks - int(p.age)) * int(v);
            lane = int16_t(lane + int16_t((t + ((t >> 31) & 7)) >> 3));
        };
        thrown(e.pos.x, p.velocity.x);
        thrown(e.pos.y, p.velocity.y);
        thrown(e.pos.z, p.velocity.z);
    }
    p.age += 1u;
    if (p.age == (p.thrown != 0 ? kThrownBonePuffTicks : kBonePuffTicks)) freeActor(e);
}

void ownerPuff(EffectActor& e, bool burst) { // exe 0x4367b0 / 0x436ba0
    OwnerPuffFields& p = classState<OwnerPuffFields>(e);
    if (firstTick(e)) {
        const Actor& o = actorLayoutAt(actorBySlot(uint32_t(int32_t(p.ownerSlot))));
        ownerRotated(o, 0, 0, burst ? kBurstPuffBack : kOwnerPuffBack, p.velocity);
        if (burst) ownerRotated(o, 0, 0, kBurstGrowth, p.growth);
        p.age = 0;
    } else {
        e.pos.x += uint16_t(p.velocity.x);
        e.pos.y += uint16_t(p.velocity.y);
        e.pos.z += uint16_t(p.velocity.z);
    }
    // the sprites: render output
    p.age += 1u;
    if (p.age == (burst ? kBurstPuffTicks : kOwnerPuffTicks)) freeActor(e);
}

void thrownPuff(EffectActor& e) { // exe 0x436d50
    ThrownPuffFields& p = classState<ThrownPuffFields>(e);
    const PuffKind kind = puffKinds[size_t(int(p.kind))];
    if (firstTick(e)) {
        p.age = 0;
        p.life = int16_t(int32_t(p.lifeParam) << 3);
        const int16_t rot[3] = {0, p.yaw, 0};
        int32_t v[3];
        rotateVector(rot, 0, kind.up, kind.ahead, v);
        p.velocity.x = int16_t(uint32_t(v[0]) >> 16);
        p.velocity.y = int16_t(uint32_t(v[1]) >> 16);
        p.velocity.z = int16_t(uint32_t(v[2]) >> 16);
        p.size = int16_t(kind.size);
    } else {
        e.pos.x += uint16_t(p.velocity.x);
        e.pos.y += uint16_t(p.velocity.y);
        e.pos.z += uint16_t(p.velocity.z);
        p.size += uint16_t(kind.growth);
    }
    p.age += 1u;
    if (uint16_t(p.age) == uint16_t(p.life)) freeActor(e);
}

// ---- the flash ring (variant 11)
// --------------------------------------------------------------------------------------
// its position projected once onto the screen; 8 ticks (the ring: render output)

// exe 0x436f00 (variant 11)
void flashRing(EffectActor& e) {
    if (OpeningEffectState(e.state) == OpeningEffectState::Start) {
        const int16_t p[3] = {e.pos.x, e.pos.y, e.pos.z};
        int16_t sxy[2];
        projectOnScreen(p, sxy);
        e.pos.x = sxy[0];
        e.pos.y = sxy[1];
        ++e.state;
    }
    if (e.subState == kFlashRingTicks) {
        freeActor(e);
        return;
    }
    ++e.subState;
}

} // namespace

void spawnStreak(int16_t x, int16_t y, int16_t z, uint32_t param) {
    const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (at == 0) return;
    EffectActor& e = effectActorAt(at);
    StreakFields s(e);
    e.flags = kActorLiveDrawn;
    e.type = kOpeningEffectType;
    e.variant = uint8_t(OpeningEffect::Streak);
    e.pos.x = x;
    e.pos.y = y;
    e.pos.z = z;
    s.yaw = kHalfTurn;
    s.speed = 0;
    s.lifeParam = 0x20;
    s.side = uint8_t((param >> 7) & 1);
    s.length = uint8_t(param & 0xf);
}

void spawnEngineFlame(const Actor& owner) {
    const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (at == 0) return;
    EffectActor& e = effectActorAt(at);
    EngineFlameFields& f = classState<EngineFlameFields>(e);
    e.flags = kActorLiveDrawn;
    e.type = kOpeningEffectType;
    e.variant = uint8_t(OpeningEffect::EngineFlame);
    e.pos.x = owner.pos.x.whole;
    e.pos.y = owner.pos.y.whole;
    e.pos.z = owner.pos.z.whole;
    f.yaw = owner.rotation.y;
    f.ownerSlot = int8_t(owner.slot);
    f.follows = 1;
    f.drawParams[0] = 0;
    f.drawParams[1] = 0;
}

EffectActor* allocThrownPuff(uint8_t flags) {
    const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (at == 0) return nullptr;
    EffectActor& e = effectActorAt(at);
    e.flags = flags;
    e.type = kOpeningEffectType;
    e.variant = uint8_t(OpeningEffect::ThrownPuff);
    return &e;
}

void placeThrownPuff(EffectActor& e, const int16_t point[3], uint16_t yaw, uint8_t kind, uint8_t lifeParam) {
    ThrownPuffFields& p = classState<ThrownPuffFields>(e);
    e.pos.x = point[0];
    e.pos.y = point[1];
    e.pos.z = point[2];
    p.yaw = int16_t(yaw);
    e.setParamsWord(uint32_t(kind) | uint32_t(lifeParam) << 8);
}

void spawnFlagStreak(const int16_t point[3], uint8_t flags, uint8_t speed) {
    const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (at == 0) return;
    EffectActor& e = effectActorAt(at);
    StreakFields s(e);
    e.flags = kActorLiveDrawn;
    e.type = kOpeningEffectType;
    e.pos.x = point[0];
    e.pos.y = point[1];
    e.pos.z = point[2];
    e.variant = uint8_t((flags & kStreakRises) ? OpeningEffect::LiftStreak : OpeningEffect::Streak);
    s.yaw = kHalfTurn;
    s.speed = int8_t(speed);
    s.lifeParam = 0x20;
    s.side = uint8_t(flags >> 7);
    s.length = uint8_t(flags & 0xf);
}

void openingEffectUpdate(const PlayerEnv& env, uint32_t address) {
    EffectActor& e = effectActorAt(address);
    switch (OpeningEffect(e.variant)) {
    case OpeningEffect::Fuse: fuse(env, e); return;               // exe 0x435980
    case OpeningEffect::Sparkle: sparkle(e); return;              // exe 0x435ab0
    case OpeningEffect::RiderSpark: riderSpark(e); return;        // exe 0x435ce0
    case OpeningEffect::Chip: chip(e); return;                    // exe 0x435ed0
    case OpeningEffect::EngineFlame: engineFlame(e); return;      // exe 0x436080
    case OpeningEffect::Streak: streak(e, false); return;         // exe 0x436340
    case OpeningEffect::LiftStreak: streak(e, true); return;      // exe 0x436560
    case OpeningEffect::OwnerPuff: ownerPuff(e, false); return;   // exe 0x4367b0
    case OpeningEffect::BonePuff: bonePuff(e); return;            // exe 0x436900
    case OpeningEffect::BurstPuff: ownerPuff(e, true); return;    // exe 0x436ba0
    case OpeningEffect::ThrownPuff: thrownPuff(e); return;        // exe 0x436d50
    case OpeningEffect::FlashRing: flashRing(e); return;          // exe 0x436f00
    default: notPortedAt(kStoryPort, "effect 0x435960 variant", e.variant);
    }
}

} // namespace od::port
