// The cannon shell of the mine boss (ST2B pool 0x40 type 8, exe 0x5c4d60; hypothesis names). States [0x8d11ec]: 0
// start 0x5c4db0, 1 flight 0x5c4ec0, 2 bounce 0x5c5040 (no state here selects it), 3 burst 0x5c5110. Its gravity by
// variant [0x8d11fc]; the high lob sparks while falling and has no trail.
#include "content/saul_kada/st2b_cannon_shell.hpp"
#include "content/saul_kada/st2b_cannon_shell_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/missile_trail.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/shockwave.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st2b_cannon_shell_state;

namespace {

enum class ShellState : uint8_t { Start = 0, Flight = 1, Bounce = 2, Burst = 3 };
// The shell's spawn parameters: the point it warns near (x / z)
struct ShellParams {
    explicit ShellParams(const ProjectileActor& actor) : ShellParams(const_cast<ProjectileActor&>(actor)) {}
    explicit ShellParams(ProjectileActor& actor) : targetX(actor.params, 0), targetZ(actor.params, 2) {}
    BytesField<uint16_t, 4> targetX;
    BytesField<uint16_t, 4> targetZ;
};
const DataTable<ScalarTable<uint16_t, 4>> shellGravities{TableId::St2bCannonShellGravities}; // by variant
constexpr uint8_t kShellStatsType = 0x14; // the turret's row: its byte 2 the shell's damage
constexpr uint16_t kLaunchSound = 0xaf, kWarningSound = 0xc6;
constexpr uint8_t kTrailVariant = 3, kSparkVariant = 3, kBurstShockVariant = 6;
constexpr uint32_t kHitGroupShells = 6; // its entry's group
constexpr int kWarnReach = 0x1770, kWarnTicks = 0x2d;

// exe 0x5c5230(parent): its trail (the global static type 6, late, variant 3) following it
void spawnTrail(const ProjectileActor& actor) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& trail = staticActorAt(slot);
    trail.flags = kActorLive;
    trail.loopFlags |= kActorLateLoop;
    trail.type = typeByte(GlobalStaticType::MissileTrail);
    trail.variant = kTrailVariant;
    classState<MissileTrailFields>(staticActorAt(slot)).parent = actor.at;
}
// exe 0x5c51e0: the wall test; blocked -> the push-out's result (< 0: back to its previous position) as its wall hit
uint32_t wallTest(const PlayerEnv& env, ProjectileActor& actor) {
    if (int32_t(actorTestMove(env, actor)) >= 0) { // exe 0x40f570
        actor.wallHit = 0;
        return 0;
    }
    const uint32_t result = actorPushOut(env, actor); // exe 0x410960
    if (int32_t(result) < 0) actor.pos = actor.prevPos;
    actor.wallHit = result;
    return result;
}
// exe 0x5c5260: a spark (the global hit spark, variant 3) 0x400 << 12 along its rotation
void spark(const ProjectileActor& actor) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    const int16_t rot[3] = {actor.rotation.x, actor.rotation.y, actor.rotation.z};
    int32_t o[3];
    rotateVector(rot, 0, 0, 0x400, o); // exe 0x5e3970
    EffectActor& effect = effectActorAt(slot);
    effect.flags = kActorLive;
    effect.type = typeByte(GlobalEffectType::HitSpark);
    effect.variant = kSparkVariant;
    effect.loopFlags |= kActorSweepGroup2;
    effect.pos.x = int16_t(int32_t(uint32_t(actor.pos.x) + uint32_t(o[0])) >> 16);
    effect.pos.y = int16_t(int32_t(uint32_t(actor.pos.y) + uint32_t(o[1])) >> 16);
    effect.pos.z = int16_t(int32_t(uint32_t(actor.pos.z) + uint32_t(o[2])) >> 16);
    classState<HitSparkFields>(effectActorAt(slot)).size = 0xc0;
}
// a blast (the global static type 7) at it
void blast(const ProjectileActor& actor, uint16_t size, uint16_t attack) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& b = staticActorAt(slot);
    b.flags = kActorLive;
    b.loopFlags |= kActorSweepGroup2;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = 0;
    b.pos = actor.pos;
    BlastParams params(actorHeaderAt(slot));
    params.size = size;
    params.attack = attack;
}
// exe 0x5c4db0: its model, its box, airborne, its trail (the flat shot: its rise from its speed along its rotation),
// its gravity, its target, the launch sound
void start(ProjectileActor& actor, ShellFields& shell) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1500
        return;
    }
    actor.maxHitPoints = 0xff;
    actor.hitPoints = 0xff;
    actor.flags |= kActorDrawn;
    actor.collisionBox.pointAt(TableId::St2bCannonShellBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 1;
    actor.onPlatform = 0;
    shell.clearBytes(); // exe 0x40dc40
    const uint8_t variant = actor.variant;
    if (variant <= uint8_t(CannonShellVariant::Aimed)) {
        spawnTrail(actor);
    } else if (CannonShellVariant(variant) == CannonShellVariant::Flat) {
        const int16_t rot[3] = {actor.rotation.x, actor.rotation.y, actor.rotation.z};
        int32_t o[3];
        rotateVector(rot, 0, 0, actor.velocity.z, o); // exe 0x5e3970
        actor.velocity.y = int16_t(uint16_t(uint32_t(o[1] >> 12)));
        spawnTrail(actor);
    }
    actor.acceleration.y = int16_t(shellGravities[variant]);
    const ShellParams params(actor);
    shell.targetX = int16_t(params.targetX.get());
    shell.targetZ = int16_t(params.targetZ.get());
    soundPlayAtPos(kLaunchSound, actor.pos);
    actor.enterState(uint8_t(ShellState::Flight));
}
// exe 0x5c4ec0: the flight (rolling 0x40 a tick, its gravity); its pitch along its path (but the high lob); the
// warning shell sounds once when fewer than 0x2d ticks from its target; the high lob sparks every 4th tick falling; a
// wall -> a breakable-wall hit and the burst; else its hit entry
void flight(const PlayerEnv& env, ProjectileActor& actor, ShellFields& shell) {
    actor.rotation.z = int16_t(uint16_t(uint16_t(actor.rotation.z) + 0x40u));
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    const CannonShellVariant variant = CannonShellVariant(actor.variant);
    if (variant != CannonShellVariant::High) {
        actor.rotation.x = int16_t(pitchOf(actor.prevPos, actor.pos) + 0x800); // exe 0x5e3ca0
        if (variant == CannonShellVariant::Warning && shell.warned == 0 &&
            planarDistance(actor.pos, player().pos) < kWarnReach) {
            // its target as a 16.16 point (the exe's stack point: only its integer x / z are set and read)
            const int32_t target[3] = {int32_t(uint32_t(uint16_t(shell.targetX)) << 16), 0,
                                       int32_t(uint32_t(uint16_t(shell.targetZ)) << 16)};
            const int32_t vz = actor.velocity.z;
            if (vz == 0) notPorted(kStoryPort, "0x5c4ec0: division by a zero speed (the exe faults)");
            if (-((planarDistance(actor.pos, target) << 4) / vz) < kWarnTicks) {
                soundPlayFlatForced(kWarningSound); // exe 0x5d5260
                shell.warned = 1;
            }
        }
    } else if ((gameState().frame & 3) == 0 && actor.velocity.y < 0) {
        spark(actor);
    }
    const uint32_t result = actorTestMove(env, actor); // exe 0x40f570
    if (int32_t(result) < 0) {
        queueBreakableWallHit(result, 0xff); // exe 0x58d4e0
        actor.wallHit = wallTest(env, actor);
        actor.enterState(uint8_t(ShellState::Burst));
        return;
    }
    shell.hitBox.result = 0;
    shell.hitBox.previousX = actor.prevPos.x.whole;
    shell.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, shell.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupShells) | hitRadius(0x80),
                  uint32_t(animatedStats(kShellStatsType).strongAttack) | kHitAttackStrong); // exe 0x5e4120
}
// exe 0x5c5040: the bounce (its speed x4, none after a wall), 3 ticks of shrinking blasts, then freed
void bounce(const PlayerEnv& env, ProjectileActor& actor, ShellFields& shell) {
    if (actor.subState == 0) {
        shell.bounceTicks = 3;
        const uint32_t result = actor.wallHit;
        actor.velocity.z = int16_t(uint16_t(uint32_t(uint16_t(actor.velocity.z)) << 2));
        if (result & 0xf00) actor.velocity.z = 0;
        shell.blastSize = 0x140;
        actor.subState = 1;
        actor.flags ^= kActorDrawn;
    }
    blast(actor, shell.blastSize, 0x40);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    wallTest(env, actor);
    if (actor.pos.y < actor.prevPos.y) {
        actor.velocity.y = 0;
        actor.velocity.z = 0;
    }
    shell.blastSize -= 0x50u;
    shell.bounceTicks -= 1u;
    if (shell.bounceTicks == 0) freeActor(actor); // exe 0x5e1500
}
// exe 0x5c5110: the burst: a blast and two shockwaves (one hanging down), freed
void burst(ProjectileActor& actor) {
    blast(actor, 0x100, 0x10);
    for (uint8_t k = 0; k < 2; ++k)
        if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
            ProjectileActor& shock = projectileActorAt(slot);
            ShockwaveParams params(projectileActorAt(slot));
            shock.flags = kActorLive;
            shock.loopFlags |= kActorSweepGroup2;
            shock.type = kShockwaveStageType;
            shock.variant = kBurstShockVariant;
            params.hitMode = kShockHitEdge;
            params.downward = k;
            shock.pos = actor.pos;
        }
    freeActor(actor); // exe 0x5e1500
}
void cannonShell(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5c4d60 (states [0x8d11ec])
    ShellFields& shell = classState<ShellFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (ShellState(actor.state)) {
    case ShellState::Start: start(actor, shell); return;
    case ShellState::Flight: flight(env, actor, shell); return;
    case ShellState::Bounce: bounce(env, actor, shell); return;
    case ShellState::Burst: burst(actor); return;
    default: notPortedAt(kStoryPort, "0x5c4d60 state", actor.state);
    }
}

} // namespace

void cannonShellUpdate(const PlayerEnv& env, uint32_t actor) { cannonShell(env, projectileActorAt(actor)); }

} // namespace od::port
