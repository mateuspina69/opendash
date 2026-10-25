// The fire-ruin guard's lobbed second shot (ST28 pool 0x40 type 0x1b, exe 0x5ca4f0; hypothesis names). States
// [0x8d1560]: 0 bind 0x5ca510 (its model by variant, its box, its speed 0x5a0 / (8 - distance / 0x100) + 0x1e0 along its
// yaw, lobbed at the player under gravity 0x40), 1 run 0x5ca5e0 (exe 0x5ca630: a hit or a wall push of bits 0x3f00 ->
// the burst; else by sub-state [0x8d1570]: 0 = 0x5ca6a0 its hit entry and its gravity), 2 burst 0x5ca710 (a defeat
// counted, a blast), 3 freed, 4 = 0x5ca6a0. The shadow (exe 0x5f20f0) is render output.
#include "content/saul_kada/st28_guard_shot.hpp"
#include "content/saul_kada/st28_guard_shot_state.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st28_guard_shot_state;

namespace {

enum class ShotState : uint8_t { Bind = 0, Run = 1, Burst = 2, Free = 3, Fly = 4 };
constexpr uint32_t kHitEnds = 0xff0000;   // any of the other's attack kinds ends it
constexpr uint32_t kShotAttack = kHitAttackHarmless | kHitAttackStrong | 0x14;

// exe 0x5ca510: its model (none: freed), its box, its speed by the player's distance, lobbed at him
void bind(ProjectileActor& actor, ShotFields& shot) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, &variant)
        freeActor(actor);                        // exe 0x5e1500
        return;
    }
    actor.flags |= kActorDrawn;
    ++actor.state;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.collisionBox.pointAt(stateAddress(TableId::St28GuardShotBoxes) + uint32_t(actor.variant) * 12);
    shot.clearBytes(); // exe 0x40dc40
    shot.hitBox.previousX = actor.pos.x.whole;
    shot.hitBox.previousZ = actor.pos.z.whole;
    int d = distance3(actor.pos, player().pos); // exe 0x5e3a60
    if (d > 0x7ff) d = 0x7ff;
    const int speed = 0x5a0 / (8 - (d >> 8)) + 0x1e0;
    actor.velocity.x = 0;
    actor.velocity.z = int16_t(-speed);
    actor.velocity.y = int16_t(launchSpeed(actor.pos, player().pos, speed, 0x40, 0xffffffffu)); // exe 0x5e3ee0
}
// exe 0x5ca630: a hit or a wall push of bits 0x3f00 -> the burst (true); else its hit box cleared
bool collide(const PlayerEnv& env, ProjectileActor& actor, ShotFields& shot) {
    if (!(shot.hitBox.result & kHitEnds) &&
        (int32_t(actorTestMove(env, actor)) >= 0 || !(actorPushOut(env, actor) & 0x3f00))) { // exe 0x40f570 / 0x410960
        shot.hitBox.result = 0;
        shot.hitBox.previousX = actor.prevPos.x.whole;
        shot.hitBox.previousZ = actor.prevPos.z.whole;
        return false;
    }
    actor.enterState(uint8_t(ShotState::Burst));
    return true;
}
// exe 0x5ca6a0: its hit entry, its gravity, its move
void fly(ProjectileActor& actor, ShotFields& shot) {
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, shot.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x30), kShotAttack); // 0x5e4120
    actor.velocity.y += 0x40u;
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
}
// exe 0x5ca5e0
void run(const PlayerEnv& env, ProjectileActor& actor, ShotFields& shot) {
    actor.savePreviousPose();
    if (collide(env, actor, shot)) return;
    switch (actor.subState) { // [0x8d1570]
    case 0: fly(actor, shot); return;
    default: notPortedAt(kStoryPort, "0x5ca5e0 sub-state past [0x8d1570]", actor.subState);
    }
}
// exe 0x5ca710: a defeat counted, a blast 0x14 up (0xc8 / 0x14), then freed
void burst(ProjectileActor& actor) {
    actor.state = uint8_t(ShotState::Free);
    countDefeat();                         // exe 0x5e53e0
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& blast = staticActorAt(slot);
    blast.flags = kActorLive;
    blast.type = typeByte(GlobalStaticType::Blast);
    blast.variant = 0;
    blast.loopFlags |= kActorSweepGroup2;
    blast.pos.x.whole = actor.pos.x.whole;
    blast.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x14u);
    blast.pos.z.whole = actor.pos.z.whole;
    BlastParams params(actorHeaderAt(slot));
    params.size = 0xc8;
    params.attack = 0x14;
}
void guardShot(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5ca4f0 ([0x8d1560])
    ShotFields& shot = classState<ShotFields>(actor);
    switch (ShotState(actor.state)) {
    case ShotState::Bind: bind(actor, shot); return;
    case ShotState::Run: run(env, actor, shot); return;
    case ShotState::Burst: burst(actor); return;
    case ShotState::Free: freeActor(actor); return; // 0x5d0bc0
    case ShotState::Fly: fly(actor, shot); return;   // 0x5ca6a0
    default: notPortedAt(kStoryPort, "0x5ca4f0 state past [0x8d1560]", actor.state);
    }
}

} // namespace

void guardShotUpdate(const PlayerEnv& env, uint32_t actor) { guardShot(env, projectileActorAt(actor)); }

} // namespace od::port
