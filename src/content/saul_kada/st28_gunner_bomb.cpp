// The fire-ruin gunner boss's bomb (ST28 pool 0x40 type 0x18, exe 0x5c92d0; hypothesis names). States [0x8d1460]: 0 bind
// 0x5c92f0 (hidden; five flame seeds: a delay, a size, a phase), 1 run 0x5c93c0 (its sub-state [0x8d146c]: 0 shown
// after 6 ticks 0x5c93e0, 1 the flight 0x5c9420 (its gravity, a spark every odd frame; a wall -> a breakable-wall hit,
// a shake, hidden, the flames), 2 the flames 0x5c9540 (0x1a4 ticks: 13 flame billboards spreading over 16 ticks around
// the landing point, each a hit entry, shrinking past its seed's delay + 0xf0), then freed), 2 freed; the table runs on
// into the sub-states.
#include "content/saul_kada/st28_gunner_bomb.hpp"
#include "content/saul_kada/st28_gunner_bomb_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st28_gunner_bomb_state;

namespace {

enum class BombState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class BombSub : uint8_t { Wait = 0, Flight = 1, Flames = 2 };
// the flames' offsets from the landing point (s16 x / z, times the spread)
struct FlameOffset : DataRecord {
    int16_t x = 0;
    int16_t z = 0;
    static FlameOffset load(const TableReader& r) {
        FlameOffset rec;
        rec.x = r.s16(0);
        rec.z = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<FlameOffset, 13, 4>> flameOffsets{TableId::St28GunnerBombFlameOffsets};
constexpr uint8_t kSparkVariant = 3;
constexpr int16_t kFlameTicks = 0x1a4;

// exe 0x5c9720(actor, size): a spark (the global hit spark, variant 3) at it
void spark(const ProjectileActor& actor, uint16_t size) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& effect = effectActorAt(slot);
    effect.flags = kActorLiveDrawn;
    effect.loopFlags |= kActorSweepGroup2;
    effect.type = typeByte(GlobalEffectType::HitSpark);
    effect.variant = kSparkVariant;
    effect.pos.x = actor.pos.x.whole;
    effect.pos.y = actor.pos.y.whole;
    effect.pos.z = actor.pos.z.whole;
    classState<HitSparkFields>(effectActorAt(slot)).size = size;
}
// exe 0x5c92f0: its model (none: freed), hidden; the five flame seeds
void bind(ProjectileActor& actor, BombFields& bomb) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        actor.state = uint8_t(BombState::Free);
        return;
    }
    actor.setDrawn(false);
    actor.collisionBox.pointAt(TableId::St28GunnerBombBox);
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    bomb.timer = 0;
    for (size_t i = 0; i < 5; ++i) {
        FlameSeed& seed = bomb.seeds[i];
        rngStep();
        seed.delay = uint16_t(uint8_t(rngLastValue()) & 0x1f);
        rngStep();
        seed.size = uint8_t((uint8_t(rngLastValue()) & 0x3f) + 0x70);
        seed.phase = uint8_t(i << 5);
    }
    actor.state = uint8_t(BombState::Run);
    actor.subState = uint8_t(BombSub::Wait);
}
// exe 0x5c93e0: shown after 6 ticks (a spark of 0xc0)
void wait(ProjectileActor& actor, BombFields& bomb) {
    bomb.timer += 1u;
    if (bomb.timer <= 5) return;
    actor.setDrawn(true);
    actor.subState = uint8_t(BombSub::Flight);
    bomb.timer = 0;
    spark(actor, 0xc0);
}
// exe 0x5c9420: the flight along its yaw (its gravity), a spark every odd frame; a wall -> a breakable-wall hit of its
// attack, a shake, hidden, the flames; else its hit entry
void flight(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb) {
    moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    if (gameState().frame & 1) spark(actor, 0x80);
    const int16_t attack = GunnerBombParams(actor).attack;
    bomb.hitBox.previousX = actor.prevPos.x.whole;
    bomb.hitBox.previousZ = actor.prevPos.z.whole;
    bomb.hitBox.result = 0;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const uint32_t result = actorTestMove(env, actor); // exe 0x40f570
    if (int32_t(result) >= 0 && !(bomb.hitBox.result & kHitAttackBump)) {
        actor.prevPos = actor.pos;
        actor.prevRotation = actor.rotation;
        hitListSubmit(pos, bomb.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x40),
                      uint32_t(int32_t(attack)) | kHitAttackStrong | kHitAttackThrown | kHitAttackBurning); // 0x5e4120
        return;
    }
    queueBreakableWallHit(result, attack);                         // exe 0x58d4e0
    cameraShakeAt(0x2000, 0x400, 0x100, 0x1000, actor.pos); // exe 0x40eab0
    actor.setDrawn(false);
    actor.subState = uint8_t(BombSub::Flames);
}
// exe 0x5c9540: the flames: 13 billboards (seed k / 3: its phase steps 0x20 on k % 3 == 0, its size shrinks past its
// delay + 0xf0) at the landing point + their offsets * min(timer, 0x10); each a 0x2c draw command (tpage 0x2f, CLUT
// 0x7cd2) and a hit entry; freed at tick 0x1a4
void flames(ProjectileActor& actor, BombFields& bomb) {
    for (uint32_t k = 0; k < 0xd; ++k) {
        FlameSeed& seed = bomb.seeds[k / 3];
        const uint32_t phase = seed.phase;
        if (k % 3 == 0) {
            seed.phase = uint8_t(phase + 0x20);
            if (seed.size > 1 && bomb.timer > int(seed.delay + 0xf0)) seed.size -= 1u;
        }
        int16_t spread = bomb.timer;
        if (spread > 0x10) spread = 0x10;
        const FlameOffset offset = flameOffsets[k];
        const uint8_t size = seed.size;
        const int16_t p[3] = {int16_t(uint16_t(int(int16_t(uint16_t(int(offset.x) * spread))) + actor.pos.x.whole)),
                              int16_t(uint16_t(int(actor.pos.y.whole) - (size >> 1) - 0x10)),
                              int16_t(uint16_t(int(int16_t(uint16_t(int(offset.z) * spread))) + actor.pos.z.whole))};
        dc::emit({0, 0x2e606060u, (uint32_t(int32_t(p[1])) << 16) | uint16_t(p[0]), phase | 0x7cd24000u, uint16_t(p[2]),
                  (phase + 0x1f) | 0x2f4000u, size, phase | 0x5f00u, 0, (phase + 0x1f) | 0x5f00u},
                 actor.at, false);
        const uint32_t attack = uint32_t(int32_t(GunnerBombParams(actor).attack)) | kHitAttackBurning |
                                (size < 0x40 ? kHitAttackStrong : kHitAttackStrong | kHitAttackThrown);
        hitListSubmit(p, bomb.hitBox, uint32_t(size >> 2) | hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn,
                      attack); // exe 0x5e4120
    }
    if (uint16_t(bomb.timer) == uint16_t(kFlameTicks)) {
        actor.state = uint8_t(BombState::Free);
        return;
    }
    bomb.timer += 1u;
}
void runSub(const PlayerEnv& env, ProjectileActor& actor, BombFields& bomb, uint32_t n) { // [0x8d146c]
    switch (BombSub(n)) {
    case BombSub::Wait: wait(actor, bomb); return;
    case BombSub::Flight: flight(env, actor, bomb); return;
    case BombSub::Flames: flames(actor, bomb); return;
    default: notPortedAt(kStoryPort, "0x5c92d0 sub-state past [0x8d146c]", n);
    }
}
void bossBomb(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5c92d0 ([0x8d1460]; it runs on into the sub-states)
    BombFields& bomb = classState<BombFields>(actor);
    const uint32_t state = actor.state;
    switch (state) {
    case 0: bind(actor, bomb); return;
    case 1: runSub(env, actor, bomb, actor.subState); return;
    case 2: freeActor(actor); return; // 0x5d0bc0
    case 3:
    case 4:
    case 5: runSub(env, actor, bomb, state - 3); return;
    default: notPortedAt(kStoryPort, "0x5c92d0 state past [0x8d1460]", state);
    }
}

} // namespace

void gunnerBombUpdate(const PlayerEnv& env, uint32_t actor) { bossBomb(env, projectileActorAt(actor)); }

} // namespace od::port
