// The effect actors the player spawns (see player_effects.hpp).
#include "game/actor_state.hpp"
#include "game/player/player_effects.hpp"
#include "game/water_splash_state.hpp"

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/dust_puff.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

// (The water burst -- pool 0x60 type 0x48, a stage class: content/nino/water -- is spawned with its state,
// game/water_splash_state.hpp. The dust puff, the skid sparks and the hurt sparks are game classes: their views are in
// their own files.)
constexpr uint8_t kSplashType = 0x48; // a stage table type (per class: game/global_class_types.hpp)
constexpr uint8_t kSparkVariant = 0x0e;
constexpr uint8_t kSplashFollowsActor = 0x40, kSplashAtPoint = 0x80; // the splash's variant
constexpr uint8_t kEffectLoopFlags = kActorSweepGroup1 | kActorLateLoop;
constexpr uint16_t kDustSize = 0xa0, kDustSizeStep = 0x60;
constexpr uint8_t kDustKind = 10, kDustKindStep = 6; // the puff's life
constexpr int32_t kHitSparkHeight = 0x580000; // 0x58 above the position (16.16)
constexpr uint8_t kSplashKind = 7;
constexpr uint8_t kSplashLifeBase = 0x18, kSplashSizeBase = 4;   // + the RNG's low bits
constexpr uint8_t kSprayLifeBase = 0x10, kSpraySize = 8;
constexpr int16_t kSprayHeight = 0x10;  // the spray's offset over the foot's point
constexpr int16_t kSpraySpeed = 0x200;  // thrown backwards

// a new static of the stage class table (exe 0x5e12f0): live, the effect loop flags, its type and variant
uint32_t stageStatic(uint8_t type, uint8_t variant) {
    const uint32_t a = allocActor60(kActorStageClass);
    if (a == 0) return 0;
    StaticActor& s = staticActorAt(a);
    s.flags = kActorLive;
    s.loopFlags |= kEffectLoopFlags;
    s.type = type;
    s.variant = variant;
    return a;
}

} // namespace

void dustPuff(Player& p, int k) {
    const uint32_t a = allocActorA0(0);
    if (a == 0) return;
    EffectActor& e = effectActorAt(a);
    DustPuffFields& dust = classState<DustPuffFields>(effectActorAt(a));
    e.flags = kActorLive;
    e.loopFlags |= kEffectLoopFlags;
    e.type = typeByte(GlobalEffectType::DustPuff);
    if (k == kDustAtFeet) {
        e.pos.x = p.pos.x.whole;
        e.pos.z = p.pos.z.whole;
        dust.airborne = p.onPlatform;
    } else {
        const int shift = stage().coordinateShift & 0x1f;
        const ShortVec3& hips = p.bonePoints[kHipsBone];
        e.pos.x = int16_t(int32_t(hips.x) >> shift);
        dust.airborne = 1;
        e.pos.z = int16_t(int32_t(hips.z) >> shift);
    }
    e.pos.y = p.pos.y.whole;
    dust.size = uint16_t(k * kDustSizeStep + kDustSize);
    dust.ticks = int8_t(uint8_t(k * kDustKindStep + kDustKind));
}

void skidSparks(Player& p) {
    const uint32_t phase = gameState().frame & 3;
    if (phase != 0 && phase != 2) return;
    const uint32_t a = allocActorA0(0);
    if (a == 0) return;
    EffectActor& e = effectActorAt(a);
    const ShortVec3& foot = p.bonePoints[phase == 0 ? kLeftFootBone : kRightFootBone];
    e.flags = kActorLive;
    e.runFlags |= kActorWarpKept;
    e.type = typeByte(GlobalEffectType::SkidSparks);
    e.loopFlags |= kEffectLoopFlags;
    e.variant = kSparkVariant;
    e.pos = foot;
    classState<SkidSparkFields>(effectActorAt(a)).yaw = uint16_t(uint16_t(p.rotation.y) & kAngleMask);
}

void waterSplash(Player& p, int k, int16_t x, int16_t y, int16_t z) {
    const uint32_t a = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (a == 0) return;
    StaticActor& s = staticActorAt(a);
    WaterSplashFields& splash = classState<WaterSplashFields>(s);
    s.flags = kActorLive;
    s.type = kSplashType;
    s.loopFlags |= kEffectLoopFlags;
    if (k < 0) {
        s.variant = kSplashFollowsActor;
        splash.followed = &p;
    } else {
        s.variant = kSplashAtPoint;
        splash.point = &p.bonePoints[size_t(k)];
        splash.offsetX = x;
        splash.offsetY = y;
        splash.offsetZ = z;
    }
    s.rotation.x = 0;
    s.rotation.y = p.rotation.y;
    s.rotation.z = 0;
    s.velocity.z = 0;
    splash.sizeMask = kSplashKind;
    splash.particleLife = int8_t((rngStep() & 0xf) + kSplashLifeBase);
    splash.count = int8_t((rngStep() & 3) + kSplashSizeBase);
}

void splashes(Player& p, int n) {
    for (int i = 0; i < n; ++i) splash(p);
}

void skateSpray(Player& p) {
    for (uint32_t foot : {kLeftFootBone, kRightFootBone}) {
        const uint32_t a = stageStatic(kSplashType, kSplashAtPoint);
        if (a == 0) continue;
        StaticActor& s = staticActorAt(a);
        WaterSplashFields& splash = classState<WaterSplashFields>(s);
        splash.point = &p.bonePoints[foot];
        splash.offsetX = 0;
        splash.offsetY = kSprayHeight;
        splash.offsetZ = 0;
        s.rotation.x = 0;
        s.rotation.z = 0;
        s.rotation.y = int16_t(uint16_t(uint16_t(p.rotation.y) - kHalfTurn));
        s.velocity.z = kSpraySpeed;
        splash.sizeMask = kSplashKind;
        const uint32_t r = rngStep();
        splash.count = kSpraySize;
        splash.particleLife = int8_t((r & 3) + kSprayLifeBase);
    }
}

uint32_t statusMarker(GlobalStaticType type, uint8_t variant) {
    const uint32_t a = allocActor60(0);
    if (!a) return 0;
    StaticActor& s = staticActorAt(a);
    s.flags = kActorLive;
    s.runFlags |= kActorWarpKept;
    s.type = typeByte(type);
    s.loopFlags |= kActorLateLoop;
    s.variant = variant;
    return a;
}

void hitSparks(Player& p, int angle) {
    const uint32_t a = allocActor60(0); // exe 0x5e1300 (inlined by the exe)
    if (a == 0) return;
    StaticActor& s = staticActorAt(a);
    auto&& sparks = classState<HurtSparksFields>(staticActorAt(a));
    s.flags = kActorLive;
    s.type = typeByte(GlobalStaticType::HurtSparks);
    s.loopFlags |= kEffectLoopFlags;
    s.variant = 0;
    s.pos.x = p.pos.x;
    s.pos.y = int32_t(uint32_t(p.pos.y) - uint32_t(kHitSparkHeight));
    s.pos.z = p.pos.z;
    sparks.pitch = 0;
    sparks.yaw = uint16_t(angle);
}

} // namespace od::port
