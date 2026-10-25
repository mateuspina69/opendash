// The shell the ruin flyer hangs dormant in (ST27 / ST34 pool 0x20 type 0x67 sub 0, exe 0x4ec580; hypothesis names).
// States [0x89666c]: 0 bind 0x4ec780, 1 run 0x4ec840, 2 freed 0x468fa0; the table runs on into its sub-states. Its
// sub-states [0x896678]: 0 dormant 0x4ec980 (hanging, then the drop), 1 fight 0x4eca50, 2 dying 0x4ece00, 3 lifted
// 0x4ecfb0 ([0x896688]: carried 0x4ecfd0, thrown 0x4ed090). Its row: animatedStats(type, spawn byte + variant * 2).
#include "content/saul_kada/st34_flyer_shell.hpp"
#include "content/saul_kada/st34_flyer_shell_state.hpp"

#include "content/saul_kada/breath_puff.hpp"
#include "content/saul_kada/st34_ruin_flyer.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st34_flyer_shell_state;

namespace {

enum class ShellState : uint8_t { Bind = 0, Run = 1, Free = 2 };
// its routines: the sub-states [0x896678] and the lifted ones [0x896688] after them
enum class ShellRoutine : uint8_t { Dormant = 0, Fight = 1, Dying = 2, Lifted = 3, Carried = 4, Thrown = 5 };
enum class ShellAnim : uint8_t {
    Idle = 0, TurnLeft = 2, TurnLeftEnd = 3, TurnRight = 4, TurnRightEnd = 5, Hang = 6, Land = 7, Aim = 8, Back = 9,
    Fire = 0xa, Lifted = 0xb, Dying = 0xc
};
constexpr uint8_t kShellLifted = 0x01, kShellThrown = 0x02, kShellDormant = 0x08, kShellDying = 0x10,
                  kShellWoken = 0x20;
constexpr uint8_t kThrownSlot = 0x10; // the slot id its walls are tested as while thrown (the shots' class)
constexpr uint16_t kLandSound = 0x201, kReleaseSound = 0x202, kHurtSound = 0x8d;
constexpr uint32_t kThrownStops = 0x5; // the push's axis bits that end the throw

AnimatedActorStats shellStats(const AnimatedActor& actor) {
    return animatedStats(actor.type, uint8_t(actor.spawnByte + actor.variant * 2));
}
void setAnim(AnimatedActor& actor, ShellAnim anim) { actor.anim = uint8_t(anim); }
int playerDistance(const AnimatedActor& actor) { return planarDistance(actor.pos, player().pos); }
uint32_t angleToPlayer(const AnimatedActor& actor) { return uint32_t(angleOf(player().pos, actor.pos)); }
// the dying's start
void startDying(AnimatedActor& actor, ShellFields& shell) {
    actor.subState = uint8_t(ShellRoutine::Dying);
    actor.step = 0;
    setAnim(actor, ShellAnim::Dying);
    shell.flags |= kShellDying;
    shell.dyingTimer = 0x2d;
}

// exe 0x4ed110: the walls (as a shot's slot while thrown); a floor nearer than 0x20 below
uint32_t collide(const PlayerEnv& env, AnimatedActor& actor, ShellFields& shell) {
    uint8_t slot = 0;
    if (shell.flags & kShellThrown) {
        slot = actor.slot;
        actor.slot = kThrownSlot;
    }
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        r = actorPushOut(env, actor);              // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & kWallOnSides) shell.wallHit |= 1u;
    }
    const int g = actorGround(env, actor); // exe 0x40f190
    if (g - actor.pos.y.whole <= 0x1f) actor.pos.y.whole = int16_t(g - 0x20);
    if (shell.flags & kShellThrown) actor.slot = slot;
    return r;
}
// exe 0x4ec980: hanging until the player is within 0x700 (or woken), falling, landing
void dormant(const PlayerEnv& env, AnimatedActor& actor, ShellFields& shell) {
    switch (actor.step) {
    case 0:
        if (playerDistance(actor) < 0x700 || (shell.flags & kShellWoken)) {
            actor.velocity.y = 0;
            actor.acceleration.y = 0x40;
            ++actor.step;
            actor.airborne = 1;
        }
        return;
    case 1:
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        if (actorPushOut(env, actor) & kWallOnY) { // exe 0x410960
            shell.flags &= uint8_t(~(kShellDormant | kShellWoken));
            setAnim(actor, ShellAnim::Land);
            ++actor.step;
            soundPlayAtPos(kLandSound, actor.pos);
        }
        return;
    case 2:
        if (actor.animEnded()) {
            actor.step = 0;
            setAnim(actor, ShellAnim::Idle);
            ++actor.subState;
        }
        return;
    default: return;
    }
}
// a breath puff from bone 0's point 0x80 ahead
void firePuff(const AnimatedActor& actor, const AnimatedActorStats& stats) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& puff = projectileActorAt(slot);
    BreathPuffParams params(projectileActorAt(slot));
    puff.flags = kActorLive;
    puff.loopFlags |= kActorSweepGroup2;
    puff.type = kBreathPuffType;
    puff.variant = 0;
    const int16_t v[3] = {0, 0, int16_t(-0x80)};
    int16_t o[3];
    bonePointOffset(actor, 0, v, o); // exe 0x5e1970
    puff.pos.x.whole = o[0];
    puff.pos.y.whole = o[1];
    puff.pos.z.whole = o[2];
    puff.rotation.x = actor.rotation.x;
    puff.rotation.y = actor.rotation.y;
    puff.rotation.z = actor.rotation.z;
    params.damage = stats.strongAttack;
    params.life = 0x18;
    params.frame = uint8_t((gameState().frame >> 1) & 1u);
    puff.velocity.z = int16_t(0xfd80);
    params.spin = 0x10;
    params.size = 0x30;
}
// exe 0x4eca50 (its jump table 0x4ecddc): circling, turning to the player near, backing off, firing; every step ends
// in the collision
void fight(const PlayerEnv& env, AnimatedActor& actor, ShellFields& shell) {
    const AnimatedActorStats stats = shellStats(actor);
    switch (actor.step) {
    case 0:
        setAnim(actor, ShellAnim::Idle);
        ++actor.step;
        break;
    case 1:
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) - 0x10u));
        integrateVelocity(actor, 0, 0, -0x22);
        if (playerDistance(actor) < 0x400) {
            ++actor.step;
            if (facingWithin(actor, player().pos, 0x40)) {
                setAnim(actor, ShellAnim::Aim);
                break;
            }
            const int16_t before = actor.rotation.y;
            turnYawTo(actor.rotation.y, angleToPlayer(actor), 0x40);
            setAnim(actor, int(before) - int(actor.rotation.y) < 0 ? ShellAnim::TurnLeft : ShellAnim::TurnRight);
            ++actor.step;
        }
        break;
    case 2:
        if (actor.animEnded()) {
            setAnim(actor, actor.anim == uint8_t(ShellAnim::TurnLeft) ? ShellAnim::TurnLeftEnd : ShellAnim::TurnRightEnd);
            ++actor.step;
        }
        break;
    case 3:
        if (playerDistance(actor) < 0x600) {
            if (!facingWithin(actor, player().pos, 0x40)) {
                turnYawTo(actor.rotation.y, angleToPlayer(actor), 0x40);
            } else {
                setAnim(actor, ShellAnim::Aim);
                ++actor.step;
            }
        }
        break;
    case 4:
        turnYawTo(actor.rotation.y, angleToPlayer(actor), 0x40);
        if (actor.animEnded()) {
            setAnim(actor, ShellAnim::Fire);
            ++actor.step;
        } else {
            integrateVelocity(actor, 0, 0, -0x23);
        }
        break;
    case 5:
        if (playerDistance(actor) >= 0x600) {
            setAnim(actor, ShellAnim::Back);
            ++actor.step;
            break;
        }
        turnYawTo(actor.rotation.y, angleToPlayer(actor), 0x40);
        if (actor.animEnded()) firePuff(actor, stats);
        break;
    case 6:
        if (actor.animEnded()) {
            setAnim(actor, ShellAnim::Idle);
            actor.step = 1;
        } else {
            integrateVelocity(actor, 0, 0, -0xc0);
        }
        break;
    default: break;
    }
    collide(env, actor, shell); // exe 0x4ed110
}
// exe 0x4ece00: at its time's end the flyer released (rising out of it), freed; sparks every 4th tick
void dying(const AnimatedActor& actor, AnimatedActor& self, ShellFields& shell) {
    if (self.step != 0) return;
    if (shell.dyingTimer != 0) {
        shell.dyingTimer -= 1u;
    } else {
        self.state = uint8_t(ShellState::Free);
        soundPlayAtPos(kReleaseSound, actor.pos);
        if (const uint32_t slot = allocActor20()) { // exe 0x5e1160
            AnimatedActor& flyer = animatedActorAt(slot);
            flyer.flags = kActorLive;
            flyer.type = kRuinFlyerType;
            flyer.subtype = kRuinFlyerSubtype;
            flyer.variant = 1;
            flyer.spawnByte = actor.spawnByte;
            flyer.loopFlags |= kActorSweepGroup2;
            flyer.pos = actor.pos;
            flyer.rotation = actor.rotation;
            RuinFlyerParams(animatedActorAt(slot)).risesFirst = 1;
        }
    }
    if (shell.dyingTimerLow & 3) return;
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& spark = effectActorAt(slot);
    spark.flags = kActorLive;
    spark.type = typeByte(GlobalEffectType::HitSpark);
    spark.subtype = 0;
    spark.variant = kSparkVariantHit;
    spark.runFlags |= kActorWarpKept;
    const int16_t v[3] = {0, 0, 0};
    int16_t o[3];
    bonePointOffset(actor, 0, v, o); // exe 0x5e1970
    int16_t* lanes[3] = {&spark.pos.x, &spark.pos.y, &spark.pos.z};
    for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t r = rngStep();
        *lanes[k] = int16_t(uint16_t(((((r & 0x7f) * 0x180u) - 0x6000u) >> 8) + uint16_t(o[k])));
    }
    const uint32_t r = rngStep();
    effectActorAt(slot).params[0] = uint8_t((r & 0x7f) + 0x40u); // its size's low byte (HitSparkFields::size)
}
// exe 0x4ed090: flying; a hit, a wall or the floor kills it
void thrown(const PlayerEnv& env, AnimatedActor& actor, ShellFields& shell) {
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    if (!(shell.hitBox.result & kHitAttackSolid) && !(shell.coreBox.result & kHitAttackSolid) &&
        !(actorPushOut(env, actor) & kThrownStops))
        return;
    startDying(actor, shell);
}
// exe 0x4ecfd0: in the player's hands until thrown (along his aim) or dropped
void carried(const PlayerEnv& env, AnimatedActor& actor, ShellFields& shell) {
    carryAtHands(actor, 0, 0, 0);                 // exe 0x573ab0
    const CarryRelease release = carryReleased(); // exe 0x573950
    if (release == CarryRelease::Held) return;
    if (release == CarryRelease::Thrown) {
        int32_t aim[3];
        playerAimVector(0, -0x280, -0x400, aim); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(aim[1]);
        actor.velocity.z = int16_t(aim[2]);
    } else {
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
    }
    actor.acceleration.y = 0x40;
    actor.rotation.y = player().rotation.y;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    carryDropFromPlayer(actor); // exe 0x573990
    ++actor.step;
    shell.flags |= kShellThrown;
    thrown(env, actor, shell);
}
void runRoutine(const PlayerEnv& env, AnimatedActor& actor, ShellFields& shell, uint32_t routine) {
    switch (ShellRoutine(routine)) {
    case ShellRoutine::Dormant: dormant(env, actor, shell); return;
    case ShellRoutine::Fight: fight(env, actor, shell); return;
    case ShellRoutine::Dying: dying(actor, actor, shell); return;
    case ShellRoutine::Lifted: // exe 0x4ecfb0: [0x896688]
        if (actor.step > 1) notPortedAt(kStoryPort, "0x4ecfb0: step past [0x896688]", actor.step);
        runRoutine(env, actor, shell, uint32_t(ShellRoutine::Carried) + actor.step);
        return;
    case ShellRoutine::Carried: carried(env, actor, shell); return;
    case ShellRoutine::Thrown: thrown(env, actor, shell); return;
    default: notPortedAt(kStoryPort, "0x4ec580: routine past its tables", routine);
    }
}
// exe 0x4ec780
void bind(AnimatedActor& actor, ShellFields& shell) {
    if (!bindActorModel(actor, actor.spawnByte)) { // exe 0x5e0bc0(actor, &spawnByte)
        freeActor(actor);                          // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    setAnim(actor, ShellAnim::Hang);
    actor.prevAnim = 0xff;
    const uint16_t hp = shellStats(actor).hitPoints;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St34FlyerShellBox);
    actor.velocity.setXY(0, 0);
    actor.interactionBox.pointAt(TableId::St34FlyerShellLiftBox);
    actor.interaction = kInteractLift;
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.y = 0;
    actor.acceleration.y = 0x40;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    shell.hitBox.result = 0;
    shell.coreBox.result = 0;
    shell.flags = kShellDormant;
    actor.state = uint8_t(ShellState::Run);
    actor.subState = uint8_t(ShellRoutine::Dormant);
    actor.step = 0;
}
// exe 0x4ec840: its hits (dormant: woken) or the lift, then its sub-state
void run(const PlayerEnv& env, AnimatedActor& actor, ShellFields& shell) {
    if (shell.decay != 0 && (gameState().frame & 1)) shell.decay -= 1u;
    if (((shell.hitBox.result & kHitAttackByPlayer) || (shell.coreBox.result & kHitAttackByPlayer)) &&
        !(shell.flags & kShellDying)) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
        uint32_t hit = shell.hitBox.result;
        if (!(hit & kHitAttackByPlayer)) hit = shell.coreBox.result;
        if (shell.flags & kShellDormant) shell.flags |= kShellWoken;
        else if (hitPointsAdd(actor, -int(hit & kHitDamageMask))) startDying(actor, shell); // exe 0x5e52f0
    } else if (!(shell.flags & kShellLifted) && playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        shell.flags |= kShellLifted;
        actor.subState = uint8_t(ShellRoutine::Lifted);
        actor.step = 0;
        setAnim(actor, ShellAnim::Lifted);
    }
    runRoutine(env, actor, shell, actor.subState);
}
void flyerShell(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4ec580
    ShellFields& shell = classState<ShellFields>(actor);
    const AnimatedActorStats stats = shellStats(actor); // read before the state (the exe's EDI)
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint32_t state = actor.state;
    switch (ShellState(state)) { // [0x89666c]
    case ShellState::Bind: bind(actor, shell); break;
    case ShellState::Run: run(env, actor, shell); break;
    case ShellState::Free: freeActor(actor); break; // exe 0x468fa0
    default: runRoutine(env, actor, shell, state - 3); break; // the table runs on into the sub-states
    }
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else animStepPose(actor);                                            // exe 0x5e1e80
    for (HitBoxHead* box : {&shell.hitBox, &shell.coreBox}) {
        box->result = 0;
        box->previousX = actor.prevPos.x.whole;
        box->previousZ = actor.prevPos.z.whole;
    }
    const bool lifted = (shell.flags & kShellLifted) != 0;
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight);
    const uint32_t attack = uint32_t(stats.attack) | (lifted ? kHitAttackByPlayer : kHitAttackSolid);
    const uint32_t liftedShape = hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | hitRadius(0x64);
    const int16_t v[3] = {0, 0, 0};
    int16_t o[3];
    bonePointOffset(actor, 0, v, o); // exe 0x5e1970
    hitListSubmit(o, shell.hitBox, owner | (lifted ? liftedShape : hitGroup(kHitGroupBodies) | hitRadius(0x64)),
                  attack); // exe 0x5e4120
    bonePointOffset(actor, 3, v, o);
    hitListSubmit(o, shell.coreBox,
                  owner | (lifted ? liftedShape : hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x64)), attack);
}

} // namespace

void flyerShellUpdate(const PlayerEnv& env, uint32_t actor) { flyerShell(env, animatedActorAt(actor)); }

} // namespace od::port
