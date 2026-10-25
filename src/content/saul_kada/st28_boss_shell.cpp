// The lava boss's lobbed shell (ST28 pool 0x40 type 0x1c, exe 0x5ca770; hypothesis names). States [0x8d1594]: 0 bind
// 0x5ca790, 1 fly 0x5ca7e0 (ballistic, its pitch along its path; its first contact -> a blast of its damage), 2 freed.
#include "content/saul_kada/st28_boss_shell.hpp"

#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

constexpr uint8_t kBossShellType = 0x1c;
enum class ShellState : uint8_t { Bind = 0, Fly = 1, Free = 2 };
// The shell's bytes: its damage (the spawner stores a word, the shell reads its low byte signed), its hit box
struct ShellFields {
    explicit ShellFields(const ProjectileActor& actor) : ShellFields(const_cast<ProjectileActor&>(actor)) {}
    explicit ShellFields(ProjectileActor& actor) : damageWord(actor.params, 0), damage(actor.params[0]), hitBox(actor.hitBox) {}
    BytesField<uint16_t, 4> damageWord;
    FieldAlias<int8_t, uint8_t> damage;
    HitBoxHead& hitBox;
};

// exe 0x5ca7e0
void fly(const PlayerEnv& env, ProjectileActor& actor, ShellFields& shell) {
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    actor.rotation.x = int16_t(uint16_t(0x800 - pitchOf(actor.pos, actor.prevPos)));
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        spawnBlastAt(actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole, 0x80,
                     uint16_t(int16_t(shell.damage)));
        actor.state = uint8_t(ShellState::Free);
        return;
    }
    shell.hitBox.previousX = actor.prevPos.x.whole;
    shell.hitBox.previousZ = actor.prevPos.z.whole;
    shell.hitBox.result = 0;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(p, shell.hitBox,
                  hitOwner(actor.slot) | hitGroup(kHitGroupHeavyThrown) | kHitNoLockOn | hitRadius(0x44),
                  uint32_t(int32_t(shell.damage)) | kHitAttackStrong); // exe 0x5e4120
}
void bossShell(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5ca770: [0x8d1594]
    ShellFields shell(actor);
    switch (ShellState(actor.state)) {
    case ShellState::Bind: { // exe 0x5ca790
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        const uint8_t state = actor.state;
        actor.airborne = 1;
        actor.flags |= kActorDrawn | kActorMeshModel;
        actor.collisionBox.pointAt(TableId::St28BossShellBox);
        actor.interactionBox = nullptr;
        actor.interaction = 0;
        actor.state = uint8_t(state + 1u);
        shell.hitBox.result = 0;
        fly(env, actor, shell);
        return;
    }
    case ShellState::Fly: fly(env, actor, shell); return;
    case ShellState::Free: freeActor(actor); return; // exe 0x5d0bc0
    default: notPortedAt(kStoryPort, "0x5ca770 state past [0x8d1594]", actor.state);
    }
}

} // namespace

void lobBossShell(const AnimatedActor& boss, bool nearShot) { // exe 0x5ca900
    const Player& p = player();
    const int32_t target[3] = {p.pos.x, p.pos.y, p.pos.z};
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& shell = projectileActorAt(slot);
    shell.flags = kActorLive;
    shell.type = kBossShellType;
    shell.variant = 0;
    shell.loopFlags |= kActorSweepGroup2;
    shell.pos = boss.pos;
    shell.rotation.x = 0;
    shell.rotation.y = int16_t(angleOf(target[0], target[2], boss.pos.x, boss.pos.z));
    shell.rotation.z = 0;
    shell.velocity.x = 0;
    const int32_t from[3] = {shell.pos.x, shell.pos.y, shell.pos.z};
    if (nearShot) {
        shell.velocity.y = int16_t(launchSpeed(from, target, 0x300, 0x30, 0xffffffffu)); // exe 0x5e3ee0
        shell.velocity.z = int16_t(0xfd00);
    } else {
        shell.velocity.y = int16_t(launchSpeed(from, target, 0x600, 0x30, 0xffffffffu));
        shell.velocity.z = int16_t(0xfa00);
    }
    shell.acceleration.y = 0x30;
    ShellFields(shell).damageWord = animatedStats(boss.type, boss.variant).shotAttack;
}

void bossShellUpdate(const PlayerEnv& env, uint32_t actor) { bossShell(env, projectileActorAt(actor)); }

} // namespace od::port
