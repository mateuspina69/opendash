// The Birdbot soldier's shot, class 0x5c6950 (see walker_shot.hpp). States [0x8d12e4]: 0 0x5c6990 (set up), 1 0x5c6a10
// (the flight). Its two sprites (draw commands) are render output: only their frame counter is kept.
#include "game/actor_state.hpp"
#include "game/walker_shot_state.hpp"
#include "game/walker_shot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/birdbot.hpp"
#include "game/breakable_walls.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace walker_shot_state;

namespace {

constexpr const char* kWalkerShotPort = "walker shot port"; // the area of its port-gap errors

enum class WalkerShotState : uint8_t { Setup = 0, Fly = 1 };

const DataTable<CollisionBoxRecord> walkerShotBox{TableId::WalkerShotBox};
constexpr uint8_t kLife = 0x78;
constexpr int16_t kRise = -0x70, kSpeed = -0x300, kFall = 4;
constexpr uint16_t kSoundShot = 0x110;
constexpr uint8_t kSparkLoops = 2;
constexpr uint8_t kTrailSparkVariant = 8; // the hit spark's style of the trail
constexpr uint16_t kTrailSparkSize = 0x28;
constexpr uint32_t kShotShape = hitGroup(kHitGroupHeavyThrown) | hitCapsule(1);
constexpr uint16_t kShotRadius = 5;

// its damage: the Birdbot soldier's row (spawn byte 1) of the stats table, its strong attack
uint16_t shotDamage() { return animatedStats(kBirdbotType, 1).strongAttack; }

void fly(const PlayerEnv& env, ProjectileActor& a, WalkerShotFields& f) { // exe 0x5c6a10
    integrateVelocity(a, 0, a.velocity.y, kSpeed);
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(kFall));
    const uint32_t t = actorTestMove(env, a); // exe 0x40f570
    bool end = int32_t(t) < 0;
    if (!end) {
        --f.life;
        end = f.life == 0 || (f.hitBox.result & (kHitAttackBump | kHitAttackSolid));
    }
    if (end) {
        spawnHitSpark(kSparkLoops, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
        if (int32_t(t) < 0) queueBreakableWallHit(t, shotDamage());
        freeActor(a); // exe 0x5e1500
        return;
    }
    const int16_t p0[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const int16_t p1[3] = {a.prevPos.x.whole, a.prevPos.y.whole, a.prevPos.z.whole};
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    hitListSubmitCapsule(p0, kShotRadius, p1, kShotRadius, f.hitBox, hitOwner(a.slot) | kShotShape,
                         uint32_t(shotDamage()) | kHitAttackStrong | kHitAttackThrown); // exe 0x5e4190
    // the trail spark at the midpoint of the move
    const int16_t mx = wrap16(a.pos.x.whole + ((int(a.prevPos.x.whole) - a.pos.x.whole) >> 1));
    const int16_t my = wrap16(a.pos.y.whole + ((int(a.prevPos.y.whole) - a.pos.y.whole) >> 1));
    const int16_t mz = wrap16(a.pos.z.whole + ((int(a.prevPos.z.whole) - a.pos.z.whole) >> 1));
    f.spriteFrame = uint8_t((f.spriteFrame + 1u) & 3u);
    if (const uint32_t address = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& e = effectActorAt(address);
        e.flags = kActorLiveDrawn;
        e.type = typeByte(GlobalEffectType::HitSpark);
        e.variant = kTrailSparkVariant;
        e.loopFlags |= kActorSweepGroup2;
        // x, y, z and the spare word 0: the exe's unwritten stack word
        e.setPositionWords(uint32_t(uint16_t(mx)) | uint32_t(uint16_t(my)) << 16, uint32_t(uint16_t(mz)));
        classState<HitSparkFields>(e).size = kTrailSparkSize;
    }
}

void walkerShot(const PlayerEnv& env, ProjectileActor& a) { // exe 0x5c6950
    WalkerShotFields& f = classState<WalkerShotFields>(a);
    a.savePreviousPose();
    a.wasAirborne = a.airborne;
    switch (WalkerShotState(a.state)) {
    case WalkerShotState::Setup: // exe 0x5c6990
        a.velocity.setXY(0, 0);
        a.velocity.setZSpare(0, 0);
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.interactionBox = nullptr;
        a.interaction = 0;
        a.onPlatform = 0;
        a.hitPoints = 1;
        a.maxHitPoints = 1;
        a.velocity.y = kRise;
        a.collisionBox.pointAt(walkerShotBox.stateAddress());
        a.airborne = 1;
        f.clearBlockBytes();
        f.life = kLife;
        soundPlayAtPos(kSoundShot, a.pos);
        a.enterState(uint8_t(WalkerShotState::Fly));
        fly(env, a, f);
        return;
    case WalkerShotState::Fly: fly(env, a, f); return;
    default: notPortedAt(kWalkerShotPort, "class 0x5c6950 state", a.state);
    }
}

} // namespace

void walkerShotUpdate(const PlayerEnv& env, uint32_t actor) { walkerShot(env, projectileActorAt(actor)); }

} // namespace od::port
