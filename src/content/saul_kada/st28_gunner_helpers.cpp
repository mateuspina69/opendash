// The fire-ruin gunner boss's two helpers (ST28 pool 0x20 type 0xb sub 1, exe 0x4617a0; hypothesis names). The boss's bind
// spawns them (its side 0 / 1, the boss's address in their block): Servbot-like helpers standing at its side.
// States [0x88fb64]: 0 bind / wait 0x461860 (the hit points of its row, registered as an enemy, then 0x3c / 0x32 ticks
// of waiting, sound 0x21a + its side), 1 run 0x4619a0 (its sub-state [0x88fb70]), 2 freed 0x468fa0; the table goes on
// with the sub-states as states 3..19.
// Sub-states: 0 idle 0x461a70 (facing the player; alerted -> the attack; away from the boss -> back to it; the player
// within 0x600 for 0x5a ticks -> a throw), 1 back to the boss 0x461b20 (toward its slot beside the boss's bone 1; helper
// 1 gives way to neighbours), 2 attack 0x461bc0 ([0x88fb9c]: 0 start 0x461be0, 1 run at the player 0x461c00, 2 throw
// 0x461c50, 3 give way 0x461d40), 3 throw 0x461dd0, 4 / 5 nothing, 6 / 7 knocked out from the front / behind 0x461e30 /
// 0x461f40 (flung, lying 0x258 ticks, up with full hit points), 8 landed after a throw 0x462060, 9 lifted by the player
// 0x4620f0 ([0x88fbac]: 0 carried 0x462110, 1 thrown 0x4621b0), 10 give way 0x462210. A throw (exe 0x462760, at its
// animation's key 0xb) is three lobbed shots. The boss's signal (its beam caught the player) alerts them (the ticks
// counted; past 0x258 back to idle). The shadow is render output.
#include "content/saul_kada/st28_gunner_helpers.hpp"
#include "content/saul_kada/st28_gunner_helpers_state.hpp"

#include "content/saul_kada/st28_gunner_boss.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lobbed_shot.hpp"
#include "game/matrix.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace st28_gunner_helpers_state;

namespace {

enum class HelperState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class HelperSub : uint8_t {
    Idle = 0, Return = 1, Attack = 2, Throw = 3, Rest1 = 4, Rest2 = 5, KnockedFront = 6, KnockedBack = 7, Landed = 8,
    Lifted = 9, GiveWay = 10
};
enum class AttackStep : uint8_t { Start = 0, Run = 1, Throw = 2, GiveWay = 3, Carried = 4, Thrown = 5 };
enum class EntryKind : uint8_t { None = 0, Standing = 1, KnockedOut = 2, Thrown = 3 };
// its parameters it keeps its timers in: its give-way ticks, its alerted ticks
struct HelperTimers {
    explicit HelperTimers(const AnimatedActor& actor) : HelperTimers(const_cast<AnimatedActor&>(actor)) {}
    explicit HelperTimers(AnimatedActor& actor) : giveWayTicks(actor.params[1]), alertedTicks(actor.params, 2) {}
    uint8_t& giveWayTicks;
    BytesField<uint16_t, 4> alertedTicks;
};
constexpr uint8_t kHelperKnockedOut = 0x01; // liftable
constexpr uint8_t kHelperBounce = 0x02;     // turned off the walls
constexpr uint8_t kHelperVulnerable = 0x04;
constexpr uint8_t kHelperVulnerable2 = 0x08;
constexpr uint8_t kHelperLifted = 0x20;     // carried: no collision
constexpr uint8_t kHelperAlerted = 0x40;
constexpr uint8_t kHelperBindKept = 0x9e, kHelperBindSet = 0x1e; // its flags at the bind
constexpr uint8_t kContactWall = 0x01, kContactLanded = 0x02, kContactLedge = 0x04, kContactOffGrid = 0x08;
constexpr uint32_t kTestMoveOffGrid = 0x20000000; // the move test's bit noted as kContactOffGrid
constexpr uint16_t kBindSound = 0x21a, kAlertSound = 0x21c, kThrowSound = 0x21e, kKnockedSound = 0x220;
constexpr uint8_t kThrowKey = 0xb;
constexpr int16_t kAlertLimit = 0x258;

AnimatedActorStats helperStats(const AnimatedActor& actor, const char* who) {
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, who);
    return animatedStats(actor.type, actor.variant);
}
const AnimatedActor& bossOf(const HelperFields& helper) { // the exe dereferences it without a test
    if (helper.boss == 0) notPorted(kStoryPort, "0x4617a0: no boss actor in +0x14c (the exe dereferences address 0)");
    return *slotByIdentityWord<AnimatedActor>(helper.boss);
}
int8_t sideOf(const AnimatedActor& actor) { return GunnerHelperParams(actor).side; }
uint16_t sideSound(const AnimatedActor& actor, int base) { return uint16_t(int(sideOf(actor)) + base); }
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void enterSub(AnimatedActor& actor, HelperSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}

// exe 0x462720(actor, point): the planar distance to a point's integer x / z (at its own height)
int distanceTo(const AnimatedActor& actor, const Fixed16Vec3& point) {
    const int32_t at[3] = {int32_t(uint32_t(uint16_t(point.x.whole)) << 16), int32_t(uint32_t(uint16_t(actor.pos.y.whole)) << 16),
                           int32_t(uint32_t(uint16_t(point.z.whole)) << 16)};
    return planarDistance(actor.pos, at);
}
// its yaw toward an angle by at most 0x40 (the tails of exe 0x462670 / 0x4626c0)
void yawStep(AnimatedActor& actor, int angle) {
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle));
    if (d >= 0x40) actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x40u));
    else if (d <= -0x40) actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) - 0x40u));
    else actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint32_t(d)));
}
// exe 0x462670: turned toward the player
void facePlayer(AnimatedActor& actor) { yawStep(actor, int16_t(angleOf(player().pos, actor.pos))); }
// exe 0x4626c0: turned toward the player's angle offset 0x80 (helper 1) / -0x80 (helper 0)
void faceSide(AnimatedActor& actor) {
    const int offset = sideOf(actor) ? 0x80 : -0x80;
    yawStep(actor, (angleOf(player().pos, actor.pos) + offset) & 0xfff);
}
// exe 0x4625e0: the yaw snapped toward its slot beside the boss (the boss's bone 1 at the side's offset, at its own
// height)
void faceSlot(AnimatedActor& actor, const HelperFields& helper) {
    const ShortVec3 slot = dataShortVec3(stateAddress(TableId::St28GunnerHelpersSlotOffsets) + uint32_t(int32_t(sideOf(actor))) * 4); // x, z
    const int16_t in[3] = {slot.x, 0, slot.y};
    int16_t out[3];
    bonePointOffset(animatedActorAt(bossOf(helper).at), 1, in, out); // exe 0x5e1970
    const int32_t point[3] = {int32_t(uint32_t(int32_t(out[0])) << 16), actor.pos.y, int32_t(uint32_t(int32_t(out[2])) << 16)};
    const int angle = int16_t(angleOf(point[0], point[2], actor.pos.x, actor.pos.z));
    actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle)))));
}
// exe 0x462760: three lobbed shots from the point (0, -0x300, -0x200) along its rotation, yaws -0x100 / 0 / 0x100 off
// its own, lobbed (speed 0x200, gravity 0x30, at most 0x800) at the player 0x5a above his feet
void throwShots(const AnimatedActor& actor) {
    const int16_t rot[3] = {actor.rotation.x, actor.rotation.y, actor.rotation.z};
    int32_t o[3];
    rotateVector(rot, 0, -0x300, -0x200, o); // exe 0x5e3970
    const Player& p = player();
    const int32_t target[3] = {p.pos.x, int32_t((uint32_t(p.pos.y) & 0xffffu) | uint32_t(uint16_t(uint16_t(p.pos.y.whole) - 0x5au)) << 16),
                               p.pos.z};
    for (int k = -1; k < 2; ++k) {
        const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (!slot) continue;
        ProjectileActor& shot = projectileActorAt(slot);
        LobbedShotParams params(projectileActorAt(slot));
        shot.flags = kActorLive;
        shot.type = kLobbedShotStageType;
        shot.variant = 0;
        shot.loopFlags |= kActorSweepGroup2;
        shot.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
        shot.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
        shot.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
        shot.rotation.x = 0;
        shot.rotation.z = 0;
        shot.rotation.y = int16_t(uint16_t(uint32_t(k << 8) + uint16_t(actor.rotation.y)));
        params.size = 0x30;
        shot.velocity.x = 0;
        const int32_t from[3] = {shot.pos.x, shot.pos.y, shot.pos.z};
        shot.velocity.y = int16_t(launchSpeed(from, target, 0x200, 0x30, 0x800)); // exe 0x5e3ee0
        shot.velocity.z = int16_t(0xfe00);
        shot.acceleration.y = 0x30;
        params.damage = int16_t(helperStats(actor, "0x462760: null type row (the exe faults)").strongAttack);
        params.launchSound = -1;
    }
}
// exe 0x4623d0: a hit of the player's: flashed, its hit points; knocked out -> knocked from the front (within 0x400 of
// its yaw) / behind, liftable, its knocked-out entry, sound 0x220 + its side, -1; the hit's yaw kept
int takeHit(AnimatedActor& actor, HelperFields& helper) {
    const uint32_t result = helper.hitBox.result;
    if (!(result & kHitAttackByPlayer)) return 0;
    actor.tint = kTintFlash;
    if (hitPointsAdd(actor, -int(result & kHitDamageMask))) { // exe 0x5e52f0
        helper.hitYaw = uint16_t(helper.hitBox.contact.x);
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(int16_t(helper.hitYaw))));
        actor.subState = uint8_t(uint32_t(d + 0x400) < 0x800u ? HelperSub::KnockedFront : HelperSub::KnockedBack);
        actor.restartStep();
        helper.entryKind = uint8_t(EntryKind::KnockedOut);
        helper.flags |= kHelperKnockedOut;
        sound(sideSound(actor, kKnockedSound), actor);
        return -1;
    }
    helper.hitYaw = uint16_t(helper.hitBox.contact.x);
    return 0;
}
// exe 0x4622a0: its hit entry by its kind: standing (0x30 lower), knocked out, thrown (hurting the enemies)
void submitHit(const AnimatedActor& actor, HelperFields& helper) {
    helper.hitBox.previousX = actor.prevPos.x.whole;
    helper.hitBox.previousZ = actor.prevPos.z.whole;
    helper.hitBox.result = 0;
    int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const char* who = "0x4622a0: null type row (the exe faults)";
    uint32_t attack, level, group, kind, immunity, extra;
    switch (EntryKind(helper.entryKind)) {
    case EntryKind::Standing:
        attack = helperStats(actor, who).attack;
        level = helperStats(actor, who).hitHeight;
        p[1] = int16_t(uint16_t(p[1] - 0x30));
        group = hitGroup(kHitGroupBodies);
        kind = kHitAttackSolid | kHitAttackThrown;
        immunity = hitImmunity(kHitGroupPlayer);
        extra = kHitCompareHeights;
        break;
    case EntryKind::KnockedOut:
        attack = helperStats(actor, who).attack;
        level = helperStats(actor, who).hitHeight;
        group = hitGroup(kHitGroupBodies);
        kind = kHitAttackSolid;
        immunity = 0;
        extra = kHitNoLockOn;
        break;
    case EntryKind::Thrown:
        attack = helperStats(actor, who).shotAttack;
        level = 0;
        group = hitGroup(kHitGroupPlayerAttacks);
        kind = kHitAttackByPlayer;
        immunity = hitImmunity(kHitGroupPlayer);
        extra = kHitNoLockOn;
        break;
    default: return;
    }
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(level) | extra | group | kHitContactMotion | hitRadius(0x30);
    hitListSubmit(p, helper.hitBox, shape, attack | immunity | kind); // exe 0x5e4120
}
// exe 0x462490: the walls (blocked: back; a wall noted; bouncing: turned off it, landed noted), then the ground: more
// than 0x10 above it -> a ledge (true); else on it. Airborne and not bouncing: a floor / ceiling push -> landed
bool collide(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper) {
    uint32_t pushed = 0;
    helper.contact = 0;
    const uint32_t result = actorTestMove(env, actor); // exe 0x40f570
    if (result & kTestMoveOffGrid) helper.contact |= kContactOffGrid;
    if (int32_t(result) < 0) {
        pushed = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(pushed) < 0) actor.pos = actor.prevPos;
        if (pushed & 0xf00) helper.contact = kContactWall;
        if ((helper.flags & kHelperBounce) && reflectRotationOffWalls(actor.rotation, pushed) != 0) // exe 0x410740
            helper.contact |= kContactLanded;
    }
    const int ground = actorGround(env, actor); // exe 0x40f190
    if (actor.airborne == 0) {
        if (ground - actor.pos.y.whole > 0x10) {
            helper.contact |= kContactLedge;
            return true;
        }
        actor.pos.y.whole = int16_t(ground);
        return false;
    }
    if (!(helper.flags & kHelperBounce) && (pushed & kWallOnY)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(ground);
        helper.contact |= kContactLanded;
    }
    return false;
}
// exe 0x462580(actor, yaw): landed -> bounced (vy = -vy / 2, vz / 2, pitch 0, airborne), true; else moved along the yaw
// by (0, vy, vz), its gravity added
bool fly(AnimatedActor& actor, const HelperFields& helper, int yaw) {
    if (helper.contact & kContactLanded) {
        actor.velocity.y = int16_t(uint16_t(uint32_t(-int32_t(actor.velocity.y) >> 1)));
        actor.velocity.z = int16_t(uint16_t(uint32_t(int32_t(actor.velocity.z) >> 1)));
        actor.rotation.x = 0;
        actor.airborne = 1;
        return true;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    return false;
}
// the give-way of exe 0x461d40 / 0x462210 (its flag byte given): its neighbour classified once (0x1e ticks), turned away
// from it (when the bits read before the classification say opposite), walking; true once the time is up or the
// neighbour is gone
bool giveWay(AnimatedActor& actor, HelperFields& helper, uint8_t& flag) {
    HelperTimers timers(actor);
    const Actor& other = neighbourActor(uint16_t(uint32_t(int32_t(int16_t(helper.neighbour))))); // exe 0x5e5cb0
    const uint32_t bits = uint32_t(int32_t(int16_t(helper.neighbour)));
    if (flag == 0) {
        neighbourClassify(actor, helper.neighbour); // exe 0x5e5f70
        flag = 1;
        timers.giveWayTicks = 0x1e;
    }
    if (bits & kNeighbourOpposite)
        turnYawTo(actor.rotation.y, uint32_t(headingFromOther(actor, other, neighbourSide(helper.neighbour), 0x40)),
                  0x100); // exe 0x5e5d90 / 0x5e3df0
    integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
    timers.giveWayTicks -= 1u;
    return timers.giveWayTicks == 0 || other.flags == 0;
}

// ---- the sub-states [0x88fb70] ---------------------------------------------------------------------------------------
// exe 0x461a70: idle beside the boss
void idle(AnimatedActor& actor, HelperFields& helper) {
    facePlayer(actor);
    if (helper.flags & kHelperAlerted) {
        actor.subState = uint8_t(HelperSub::Attack);
        sound(sideSound(actor, kAlertSound), actor);
        actor.restartStep();
        return;
    }
    if (distanceTo(actor, bossOf(helper).pos) >= 0x200) {
        enterSub(actor, HelperSub::Return);
        return;
    }
    if (distanceTo(actor, player().pos) < 0x600) {
        helper.counter += 1u;
        if (helper.counter > 0x5a) {
            enterSub(actor, HelperSub::Throw);
            helper.counter = 0;
        }
        return;
    }
    helper.counter = 0;
}
// exe 0x461b20: back to the boss (speed 0x100 toward its slot)
void returnToBoss(AnimatedActor& actor, HelperFields& helper) {
    switch (actor.step) {
    case 0:
        actor.anim = 2;
        actor.velocity.z = int16_t(0xff00);
        actor.step = 1;
        break;
    case 1:
        if (distanceTo(actor, bossOf(helper).pos) < 0x200) {
            actor.anim = 0;
            enterSub(actor, HelperSub::Idle);
            return;
        }
        if (sideOf(actor) != 0 && neighbourFind(actor, helper.neighbour, 0) != 0) // exe 0x5e5e00
            enterSub(actor, HelperSub::GiveWay);
        break;
    default: break;
    }
    faceSlot(actor, helper);
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z); // exe 0x5e36f0
}
// exe 0x461be0: the attack starts (speed 0x280)
void attackStart(AnimatedActor& actor) {
    actor.velocity.z = int16_t(0xfd80);
    actor.anim = 2;
    actor.step = uint8_t(AttackStep::Run);
    actor.stepCounter = 0;
}
// exe 0x461c00: running at the player's side until within 0x100
void attackRun(AnimatedActor& actor) {
    faceSide(actor);
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, actor.velocity.z); // exe 0x5e36f0
    if (distanceTo(actor, player().pos) < 0x100) {
        actor.step = uint8_t(AttackStep::Throw);
        actor.stepCounter = 0;
    }
}
// exe 0x461c50: facing the player, the throw (sound 0x21e + its side, animation 0xc, the shots at its key 0xb); after
// 0x258 alerted ticks back to idle, else again (the player near) or running again
void attackThrow(AnimatedActor& actor, HelperFields& helper) {
    actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) +
                                        uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)),
                                                           uint32_t(int32_t(int16_t(angleOf(player().pos, actor.pos))))))));
    switch (actor.stepCounter) {
    case 0:
        sound(sideSound(actor, kThrowSound), actor);
        actor.anim = 0xc;
        actor.stepCounter = 1;
        return;
    case 1:
        if (uint8_t(actor.animKey.frame) != kThrowKey) return;
        throwShots(actor);
        actor.stepCounter = 2;
        return;
    case 2: {
        if (!actor.animEnded()) return;
        HelperTimers timers(actor);
        if (int16_t(timers.alertedTicks.get()) > kAlertLimit) {
            actor.anim = 0;
            enterSub(actor, HelperSub::Idle);
            helper.flags &= uint8_t(~kHelperAlerted);
            timers.alertedTicks = 0;
            return;
        }
        if (distanceTo(actor, player().pos) < 0x100) {
            actor.prevAnim = 0xff;
            actor.stepCounter = 0;
            return;
        }
        actor.step = uint8_t(AttackStep::Start);
        actor.stepCounter = 0;
        return;
    }
    default: return;
    }
}
// exe 0x461d40: giving way to a neighbour, then running again
void attackGiveWay(AnimatedActor& actor, HelperFields& helper) {
    if (!giveWay(actor, helper, actor.stepCounter)) return;
    HelperTimers(actor).giveWayTicks = 0;
    actor.step = uint8_t(AttackStep::Start);
    actor.stepCounter = 0;
}
void thrown(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper);
// exe 0x462110: carried by the player; let go -> thrown along the player's aim (exe 0x5e3e50(0, -0x100, -0x400)), its
// thrown entry
void carried(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper) {
    carryAtHands(actor, 0, 0, 0);                         // exe 0x573ab0
    if (carryReleased() == CarryRelease::Held) return; // exe 0x573950
    int32_t aim[3];
    playerAimVector(0, -0x100, -0x400, aim); // exe 0x5e3e50
    actor.velocity.x = 0;
    actor.velocity.y = int16_t(aim[1]);
    actor.velocity.z = int16_t(aim[2]);
    actor.acceleration.y = 0x40;
    actor.rotation.y = player().rotation.y;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 1;
    helper.ledgeNext = 0;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.step = 1;
    actor.anim = 0x10;
    helper.entryKind = uint8_t(EntryKind::Thrown);
    thrown(env, actor, helper);
}
// exe 0x4621b0: thrown (turned round by a solid hit); landed -> landed after the throw (knocked out, bouncing,
// vulnerable)
void thrown(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper) {
    if (helper.hitBox.result & kHitAttackSolid) actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800u));
    if (fly(actor, helper, actor.rotation.y)) {
        enterSub(actor, HelperSub::Landed);
        helper.entryKind = uint8_t(EntryKind::KnockedOut);
        helper.flags = uint8_t((helper.flags & ~kHelperLifted) | kHelperVulnerable | kHelperVulnerable2);
    }
    collide(env, actor, helper);
}
// exe 0x461bc0: [0x88fb9c] by its step
void attack(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper) {
    switch (AttackStep(actor.step)) {
    case AttackStep::Start: attackStart(actor); return;
    case AttackStep::Run: attackRun(actor); return;
    case AttackStep::Throw: attackThrow(actor, helper); return;
    case AttackStep::GiveWay: attackGiveWay(actor, helper); return;
    case AttackStep::Carried: carried(env, actor, helper); return;
    case AttackStep::Thrown: thrown(env, actor, helper); return;
    default: notPortedAt(kStoryPort, "0x461bc0 index past [0x88fb9c]", actor.step);
    }
}
// exe 0x461dd0: a throw on the spot
void throwOnSpot(AnimatedActor& actor) {
    facePlayer(actor);
    switch (actor.stepCounter) {
    case 0:
        actor.anim = 0xc;
        actor.stepCounter = 1;
        return;
    case 1:
        if (uint8_t(actor.animKey.frame) != kThrowKey) return;
        throwShots(actor);
        actor.stepCounter = 2;
        return;
    case 2:
        if (!actor.animEnded()) return;
        actor.anim = 0;
        enterSub(actor, HelperSub::Idle);
        return;
    default: return;
    }
}
// the lying-down of the knock-outs: after its timer, its full hit points back, not liftable, its standing entry, its
// texture cell 0, the get-up animation
bool lieDown(AnimatedActor& actor, HelperFields& helper, uint8_t upAnim, uint8_t next) {
    helper.timer -= 1u;
    if (helper.timer != 0) return false;
    actor.hitPoints = actor.maxHitPoints;
    helper.flags &= uint8_t(~kHelperKnockedOut);
    helper.entryKind = uint8_t(EntryKind::Standing);
    setTexturePageCell(actor.texturePage, 0); // exe 0x5e3280(actor, 0)
    actor.anim = upAnim;
    actor.step = next;
    return true;
}
void getUp(AnimatedActor& actor) {
    if (!actor.animEnded()) return;
    actor.anim = 0;
    enterSub(actor, HelperSub::Idle);
}
// exe 0x461e30 / 0x461f40 ([0x461f30] / [0x462044]): knocked out from the front (animations 5 / 6 / 7, flung back along
// the hit's yaw) / from behind (9 / 0xa / 0xb, along the hit's yaw + 0x800)
void knocked(AnimatedActor& actor, HelperFields& helper, bool front) {
    switch (actor.step) {
    case 0:
        actor.anim = front ? 5 : 9;
        setTexturePageCell(actor.texturePage, 6); // exe 0x5e3280(actor, 6)
        helper.ledgeNext = 0;
        actor.velocity.y = int16_t(0xfe80);
        actor.velocity.z = int16_t(front ? 0xfe80 : 0x180);
        actor.acceleration.y = 0x40;
        actor.rotation.y = int16_t(front ? helper.hitYaw : uint16_t(helper.hitYaw + 0x800u));
        actor.airborne = 1;
        actor.hitPoints = 1;
        actor.step = 1;
        [[fallthrough]];
    case 1:
        if (!fly(actor, helper, actor.rotation.y)) return;
        actor.anim = front ? 6 : 0xa;
        helper.ledgeNext = 1;
        actor.airborne = 0;
        helper.timer = 0x258;
        actor.step = 2;
        return;
    case 2: lieDown(actor, helper, front ? 7 : 0xb, 3); return;
    case 3: getUp(actor); return;
    default: return;
    }
}
// exe 0x462060: landed after a throw (animation 0x11, 0x64 ticks)
void landed(AnimatedActor& actor, HelperFields& helper) {
    switch (actor.step) {
    case 0:
        actor.anim = 0x11;
        actor.hitPoints = 1;
        helper.timer = 0x64;
        actor.step = 1;
        [[fallthrough]];
    case 1: lieDown(actor, helper, 7, 2); return;
    case 2: getUp(actor); return;
    default: return;
    }
}
// exe 0x4620f0: [0x88fbac] by its step
void lifted(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper) {
    switch (actor.step) {
    case 0: carried(env, actor, helper); return;
    case 1: thrown(env, actor, helper); return;
    default: notPortedAt(kStoryPort, "0x4620f0 index past [0x88fbac]", actor.step);
    }
}
// exe 0x462210: giving way to a neighbour, then back to the boss
void giveWaySub(AnimatedActor& actor, HelperFields& helper) {
    if (!giveWay(actor, helper, actor.step)) return;
    HelperTimers(actor).giveWayTicks = 0;
    enterSub(actor, HelperSub::Return);
}
void runSub(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper, uint32_t n) { // [0x88fb70]
    switch (n) {
    case 0: idle(actor, helper); return;
    case 1: returnToBoss(actor, helper); return;
    case 2: attack(env, actor, helper); return;
    case 3: throwOnSpot(actor); return;
    case 4:
    case 5: return; // exe 0x52ef00
    case 6: knocked(actor, helper, true); return;
    case 7: knocked(actor, helper, false); return;
    case 8: landed(actor, helper); return;
    case 9: lifted(env, actor, helper); return;
    case 10: giveWaySub(actor, helper); return;
    // the table runs on into [0x88fb9c] / [0x88fbac]
    case 11: attackStart(actor); return;
    case 12: attackRun(actor); return;
    case 13: attackThrow(actor, helper); return;
    case 14: attackGiveWay(actor, helper); return;
    case 15: carried(env, actor, helper); return;
    case 16: thrown(env, actor, helper); return;
    default: notPortedAt(kStoryPort, "0x4617a0 sub-state past [0x88fb70]", n);
    }
}
// exe 0x461860: its model, the hit points of its row, its boxes, liftable, registered as an enemy; then its wait
void bind(AnimatedActor& actor, HelperFields& helper) {
    if (actor.step == 0) {
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1490
            return;
        }
        actor.flags |= kActorDrawn;
        actor.anim = 0;
        actor.prevAnim = 0xff;
        const uint16_t hp = hasAnimatedStats(actor.type) ? animatedStats(actor.type, actor.variant).hitPoints : 0;
        actor.maxHitPoints = int16_t(hp);
        actor.hitPoints = int16_t(hp);
        actor.velocity.setXY(0, 0);
        actor.velocity.setZSpare(0, 0);
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.collisionBox.pointAt(TableId::St28GunnerHelpersHelperBox);
        actor.interactionBox.pointAt(TableId::St28GunnerHelpersHelperLiftBox);
        actor.interaction = kInteractLift;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        helper.hitYaw = 0;
        helper.entryKind = uint8_t(EntryKind::None);
        helper.ledgeNext = 0;
        helper.counter = int16_t(sideOf(actor) ? 0x32 : 0x3c);
        HelperTimers timers(actor);
        timers.alertedTicks = 0;
        timers.giveWayTicks = 0;
        helper.flags = uint8_t((helper.flags & kHelperBindKept) | kHelperBindSet);
        registerEnemy(actor.slot, 0); // exe 0x5e5c10
        ++actor.step;
        return;
    }
    helper.counter -= 1u;
    if (helper.counter >= 0) return;
    actor.state = uint8_t(HelperState::Run);
    actor.subState = uint8_t(HelperSub::Return);
    actor.restartStep();
    sound(sideSound(actor, kBindSound), actor);
    helper.entryKind = uint8_t(EntryKind::Standing);
    helper.counter = 0;
}
// exe 0x4619a0
void run(const PlayerEnv& env, AnimatedActor& actor, HelperFields& helper) {
    if (actor.hitPoints >= 0 && (helper.flags & kHelperVulnerable) && !(helper.flags & kHelperAlerted)) takeHit(actor, helper);
    runSub(env, actor, helper, actor.subState);
    if (GunnerBossParams(bossOf(helper)).caught != 0) helper.flags |= kHelperAlerted;
    if (helper.flags & kHelperAlerted) HelperTimers(actor).alertedTicks += 1u;
    const uint8_t flags = helper.flags;
    if (flags & kHelperLifted) return;
    if ((flags & kHelperKnockedOut) && !(flags & kHelperAlerted) && playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        actor.subState = uint8_t(HelperSub::Lifted);
        actor.step = 0;
        actor.anim = 0xe;
        helper.entryKind = uint8_t(EntryKind::None);
        helper.flags = uint8_t((helper.flags & 0xf3) | kHelperLifted);
    }
    if (!collide(env, actor, helper)) return;
    if (helper.ledgeNext == 1) enterSub(actor, HelperSub::Rest1);
    else if (helper.ledgeNext == 2) enterSub(actor, HelperSub::Rest2);
}
void bossHelper(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4617a0
    HelperFields& helper = classState<HelperFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    const uint32_t state = actor.state;
    switch (HelperState(state)) { // [0x88fb64]
    case HelperState::Bind: bind(actor, helper); break;
    case HelperState::Run: run(env, actor, helper); break;
    case HelperState::Free: freeActor(actor); break; // 0x468fa0
    default:
        if (state > 19) notPortedAt(kStoryPort, "0x4617a0 state past [0x88fb64]", state);
        runSub(env, actor, helper, state - 3); // the table continues with the sub-states
        break;
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);                // exe 0x5e1c30
    submitHit(actor, helper);
    // drawn and not lifted: the shadow (exe 0x5f20f0(actor, 0x30, 0xc0, 0)) -- render output
}

} // namespace

void gunnerHelperUpdate(const PlayerEnv& env, uint32_t actor) { bossHelper(env, animatedActorAt(actor)); }

} // namespace od::port
