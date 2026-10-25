// The falling rocks of the ST33 guardian boss (static ST33 type 0x3b sub 1, exe 0x5afde0; hypothesis names). The boss
// spawns them (its stomp rain: kind 1, its quake: kind 0); a quake rock shatters into 4 fragments (kind 2).
// States [0x8cfa14]: 0 start 0x5afe70, 1 run 0x5affa0, 2 freed 0x5afdd0.
//  - the start: its model (by its variant), its box, the hit points of its row; the small ones a random scale and their
//    bounces (3 for the rain, 2 for the fragments); gravity 0xc0.
//  - the run: a hit on its box shatters it; else its fall (gravity until 0x600) to y >= 0: a quake rock shatters
//    (sound 0x14a), the small ones bounce (half the speed back) their bounces and blink. Only the quake rocks have a
//    hit entry.
//  The shadow (exe 0x5f20f0 at the ground under it, the quake rocks only) is render output; its ground probes (the
//  class's and exe 0x5f20f0's) are kept for their collision-scratch side effects.
#include "content/saul_kada/st33_falling_rock.hpp"
#include "content/saul_kada/st33_falling_rock_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shadows.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st33_falling_rock_state;

namespace {

enum class RockState : uint8_t { Start = 0, Run = 1, Free = 2 };
enum class RockKind : uint8_t { Quake = 0, Rain = 1, Fragment = 2 };
enum class RockPhase : uint8_t { Fall = 0, Bounce = 1 };
// The rock's parameters (its spawner's): its kind and its bounces left
struct RockParams {
    explicit RockParams(const StaticActor& actor) : RockParams(const_cast<StaticActor&>(actor)) {}
    explicit RockParams(StaticActor& actor) : kind(actor.params[0]), bounces(actor.params[1]) {}
    uint8_t& kind;
    uint8_t& bounces;
};
constexpr uint8_t kRockType = 0x3b, kRockSubtype = 1;
const DataTable<ScalarTable<uint32_t, 3>> rockRadii{TableId::St33RockRadii}; // by kind (the hit entry's radius)
constexpr uint16_t kShatterSound = 0x14a;
constexpr int16_t kGravity = 0xc0, kMaxFall = 0x600;

// its row: the type's, by its sub-type; 0 hit points without one
StaticActorStats rockStats(const StaticActor& actor) { return staticStats(actor.type, actor.subtype); }

// exe 0x5b0180: 4 fragments at its integer position, yaws k * 0x400 + a random 0..0x1ff, flung -0x300 up and out
void shatter(const StaticActor& actor) {
    for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (slot == 0) continue;
        StaticActor& piece = staticActorAt(slot);
        RockParams params(staticActorAt(slot));
        piece.flags = kActorLive;
        piece.type = kRockType;
        piece.subtype = kRockSubtype;
        piece.variant = 0;
        piece.spawnByte = 0;
        params.kind = uint8_t(RockKind::Fragment);
        piece.rotation.z = 0;
        piece.rotation.x = 0;
        const uint32_t r = rngStep();
        piece.rotation.y = int16_t((k << 10) + (r & 0x1ff));
        piece.pos.x.whole = actor.pos.x.whole;
        piece.pos.y.whole = actor.pos.y.whole;
        piece.pos.z.whole = actor.pos.z.whole;
        piece.velocity.y = int16_t(0xfd00);
        piece.velocity.z = int16_t(0xfd00);
    }
}
// exe 0x5afe70
void start(StaticActor& actor, RockFields& rock) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                        // exe 0x5e1540
        return;
    }
    RockParams params(actor);
    actor.flags |= kActorDrawn;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(TableId::St33FallingRockBox);
    actor.actorHit = 0;
    const uint16_t hp = hasStaticStats(actor.type) ? rockStats(actor).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    const RockKind kind = RockKind(params.kind);
    if (kind != RockKind::Quake) {
        actor.scale.x = int16_t((rngStep() & 0x1ff) + 0x701);
        actor.scale.y = int16_t((rngStep() & 0x1ff) + 0x701);
        actor.scale.z = int16_t((rngStep() & 0x1ff) + 0x701);
        params.bounces = kind == RockKind::Rain ? 3 : 2;
    }
    actor.step = 0;
    actor.stepCounter = 3;
    if (kind != RockKind::Fragment) actor.velocity.y = 0;
    actor.acceleration.y = kGravity;
    rock.startMark = 0x59;
    rock.hitBox.result = 0;
    ++actor.state;
}
// the fall: its velocity, gravity while under 0x600; true once y >= 0 (landed)
bool fall(StaticActor& actor) {
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    const int16_t vy = actor.velocity.y;
    if (vy < kMaxFall) actor.velocity.y = int16_t(uint16_t(vy + actor.acceleration.y));
    return actor.pos.y.whole >= 0;
}
// exe 0x5b0090
void move(StaticActor& actor) {
    RockParams params(actor);
    if (RockKind(params.kind) != RockKind::Quake) actor.setDrawn((gameState().frame & 3) != 0);
    if (RockPhase(actor.step) == RockPhase::Fall) {
        if (!fall(actor)) return;
        if (RockKind(params.kind) != RockKind::Quake) {
            actor.step = uint8_t(RockPhase::Bounce);
            params.bounces -= 1u;
            actor.velocity.y = int16_t(uint16_t(-(int32_t(actor.velocity.y) / 2)));
            return;
        }
        actor.state = uint8_t(RockState::Free);
        shatter(actor);
        soundPlayAtPos(kShatterSound, actor.pos); // exe 0x5d5380
        return;
    }
    if (!fall(actor)) return;
    if (params.bounces == 0) {
        actor.state = uint8_t(RockState::Free);
        return;
    }
    actor.step = uint8_t(RockPhase::Fall);
}
// exe 0x5affa0
void run(StaticActor& actor, RockFields& rock) {
    const StaticActorStats stats = rockStats(actor);
    const RockParams params(actor);
    const uint32_t damage = RockKind(params.kind) == RockKind::Quake ? uint32_t(stats.damage) : 0u;
    if (rock.hitBox.result & (kHitAttackBump | kHitDamageMask)) {
        shatter(actor); // exe 0x5b0180
        actor.state = uint8_t(RockState::Free);
        return;
    }
    if (actor.subState == 0) move(actor); // [0x8cfa28]
    else notPortedAt(kStoryPort, "0x5afde0 sub-state (table 0x8cfa28)", actor.subState);
    if (RockKind(params.kind) != RockKind::Quake) return;
    rock.hitBox.previousX = actor.prevPos.x.whole;
    rock.hitBox.previousZ = actor.prevPos.z.whole;
    rock.hitBox.result = 0;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupLiftable) |
                           kHitNoLockOn | (rockRadii[size_t(int32_t(int8_t(params.kind)))] >> 2);
    hitListSubmit(pos, rock.hitBox, shape, damage | kHitAttackSolid); // exe 0x5e4120
}
void fallingRock(const PlayerEnv& env, StaticActor& actor) { // exe 0x5afde0
    RockFields& rock = classState<RockFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (RockState(actor.state)) {
    case RockState::Start: start(actor, rock); break;
    case RockState::Run: run(actor, rock); break;
    case RockState::Free: freeActor(actor); break; // 0x5afdd0 -> exe 0x5e1540
    default: notPortedAt(kStoryPort, "0x5afde0 state (table 0x8cfa14)", actor.state); return;
    }
    if (RockParams(actor).kind != uint8_t(RockKind::Quake) || !(actor.flags & kActorDrawn)) return;
    // exe 0x5f20f0(actor, ((y >> 5) & 0x7f) + 0x10, 0xc0, 0) at the ground 0x100 above the class's probe (render
    // output); its y restored after it
    const int16_t y = actor.pos.y.whole;
    const int ground = actorGround(env, actor); // exe 0x40f190
    actor.pos.y.whole = int16_t(ground - 0x100);
    shadowGroundProbe(env, actor);
    actor.pos.y.whole = y;
}

} // namespace

void fallingRockUpdate(const PlayerEnv& env, uint32_t actor) { fallingRock(env, staticActorAt(actor)); }

} // namespace od::port
