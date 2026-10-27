// The Servbots of the Saul Kada pirate attack (ST29 / 2A / 2B / 3B / 3C / 48; pool 0x20 type 0xb; hypothesis names).
//
// Sub 0, the Servbot on foot (exe 0x45f240). States [0x88fa24]: 0 bind 0x45f3c0, 1 run 0x45f550 (its hit reactions,
//   its sub-state [0x88fa30], ledges, freed off screen, lifted by the player), 2 carried 0x460350 ([0x88faa0]: 0 in the
//   hands 0x460370, 1 thrown 0x460470). Sub-states: 0 fall 0x45f670, 1 decide 0x45f6c0, 2 run around the player
//   0x45f760, 3 topple 0x45f8a0, 4 knocked back (a hit from the front) 0x45f9a0, 5 get up 0x45fae0, 6 knocked over (a
//   hit from behind) 0x45fb20, 7 get up 0x45fc90, 8 flung (killed) 0x45fcd0, 9 flung again (no spark) 0x45fde0, 10
//   lying beaten (smoke puffs) 0x45fe40, 11 ledge hop 0x45fee0, 12 ledge drop 0x460060, 13 the intro of variant 1
//   0x460130 ([0x88fa98] by its step: 0 watch the player 0x460150, 1 jump 0x460270, 2 / 3 the carried ones).
//   Its variant & 0x7f: 0 / 3 falls in with a random yaw (3: its spawner told when it is freed), 1 the intro, 2 popped
//   out of a bomb-lobbing Servbot (the spawner sets its yaw / speed); & 0x80: only one (another live type 0xb frees it).
// Sub 1, the bomb-lobbing Servbot (exe 0x464270, ST2A). States [0x88fcf4]: 0 bind 0x464360, 1 run 0x464430 (its
//   sub-state [0x88fd00]: 0 wait for the player in front 0x464500, 1 lob volleys of three bombs 0x4645a0, 2 a taunt
//   0x464860), 2 pop out 0x464920 (a Servbot on foot thrown out when beaten, dropped in place when its floor opens).
// The shadows (exe 0x5f20f0) are render output.
#include "content/saul_kada/pirate_servbots.hpp"
#include "content/saul_kada/pirate_servbots_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lobbed_shot.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/stage_smoke.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace pirate_servbots_state;

namespace {

constexpr uint16_t kHurtSound = 0x8d, kThudSound = 0xab;
constexpr uint8_t kSparkLoopFlags = kActorSweepGroup2;
constexpr uint32_t kFaceNormal = 0, kFaceHurt = 6; // its texture cells
constexpr int8_t kOffScreenTicks = 0x5a;
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void setFace(AnimatedActor& actor, uint32_t cell) { setTexturePageCell(actor.texturePage, cell); } // exe 0x5e32c0
void spark(const AnimatedActor& actor) { // exe 0x5e5400
    spawnHitSparkWithoutSize(kSparkLoopFlags, actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole);
}
// its hit entry at bone 0
template <class Box> void submitAtRoot(const AnimatedActor& actor, Box& box, uint32_t ownerShape, uint32_t attack) {
    const ShortVec4& root = bonePoint(actor, 0, "0x45f240 / 0x464270: bone 0 past its blocks"); // exe 0x5e1810
    const int16_t point[3] = {root.x, root.y, root.z};
    hitListSubmit(point, box, ownerShape, attack); // exe 0x5e4120
}
enum class HitReaction : int { None = 0, Hurt = 1, Killed = 2, Flung = 4 };

// ==== sub 0: the Servbot on foot (exe 0x45f240) ====================================================================
enum class ServbotState : uint8_t { Bind = 0, Run = 1, Carried = 2 };
enum class ServbotSub : uint8_t {
    Fall = 0, Decide = 1, RunAround = 2, Topple = 3, KnockedBack = 4, GetUpBack = 5, KnockedOver = 6, GetUpFront = 7,
    Flung = 8, FlungAgain = 9, Beaten = 0xa, LedgeHop = 0xb, LedgeDrop = 0xc, Intro = 0xd
};
enum class IntroStep : uint8_t { Watch = 0, Jump = 1, Carried = 2, Thrown = 3 };
enum class CarriedSub : uint8_t { Hands = 0, Thrown = 1 };
enum class ServbotAnim : uint8_t {
    Stand = 0, Turn = 1, Run = 4, FallBack = 5, LieBack = 6, GetUpBack = 7, FallForward = 9, LieFront = 0xa,
    GetUpFront = 0xb, Held = 0xd, HeldBeaten = 0xe, Tumble = 0x10, LandBeaten = 0x11, Jump = 0x14, Hop = 0x15,
    HopLand = 0x16, HopFall = 0x17
};
// its collision result
constexpr uint8_t kContactWall = 0x01, kContactBounced = 0x02, kContactLedge = 0x04, kContactLeftGrid = 0x08;
// its flags
constexpr uint8_t kServbotNoHitEntry = 0x01, kServbotNoHitTest = 0x02, kServbotNoReaction = 0x04,
                  kServbotBouncing = 0x08, kServbotHurtsWhenThrown = 0x10, kServbotWatching = 0x20;
constexpr uint8_t kServbotDown = kServbotNoHitEntry | kServbotNoHitTest | kServbotNoReaction;
constexpr uint8_t kServbotTumbling = kServbotNoHitTest | kServbotNoReaction | kServbotBouncing;
// what a ledge under it does
enum class LedgeAction : uint8_t { None = 0, Hop = 1, Drop = 2 };
constexpr uint8_t kServbotVariantMask = 0x7f;
constexpr uint8_t kFallsInVariant = 0, kIntroVariant = 1, kPoppedOutVariant = 2;
const DataTable<ScalarTable<int8_t, 16>> runTimes{TableId::PirateServbotsRunTimes}, lieTimes{TableId::PirateServbotsLieTimes}, lieDownTimes{TableId::PirateServbotsLieDownTimes};
constexpr int16_t kServbotScale = 0x233;
constexpr int16_t kGravity = 0x40;
constexpr uint8_t kBeatenSmokeVariant = 6; // the stage smoke's variant of a beaten one

void enterSub(AnimatedActor& actor, ServbotSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
void setAnim(AnimatedActor& actor, ServbotAnim anim) { actor.anim = uint8_t(anim); }
// its frees: the variant that reports it marks its spawner's byte first
void freeServbot(AnimatedActor& actor) {
    if ((actor.variant & kServbotVariantMask) == kServbotReportsGone)
        *classState<ServbotFields>(actor).goneFlag = 1;
    freeActor(actor); // exe 0x5e1490
}
// exe 0x4606e0: its hit reaction. Alive: damaged -> flashed, sound 0x8d, the hit's yaw, killed or hurt. Beaten: a hit
// -> hurt (the hit's yaw), flung by a hit whose heights were compared and which moved it
HitReaction takeHit(AnimatedActor& actor, ServbotFields& servbot) {
    if (actor.hitPoints >= 0) {
        const uint32_t damage = servbot.hitBox.result & kHitDamageMask;
        if (damage == 0) return HitReaction::None;
        actor.tint = kTintFlash;
        sound(kHurtSound, actor);
        servbot.faceYaw = servbot.hitBox.contact.x;
        return hitPointsAdd(actor, -int(damage)) ? HitReaction::Killed : HitReaction::Hurt; // exe 0x5e52f0
    }
    const uint32_t hit = servbot.hitBox.result;
    if (!(hit & kHitDamageMask)) return HitReaction::None;
    servbot.faceYaw = servbot.hitBox.contact.x;
    if (!(hit & (kHitLevelAbove | kHitLevelWithin | kHitLevelBelow))) return HitReaction::Hurt;
    if (servbot.hitBox.contact.y == 0) return HitReaction::None;
    sound(kHurtSound, actor);
    return HitReaction::Flung;
}
// exe 0x460600: the walls (blocked -> back; a wall noted; bouncing: turned off it), then the ground: standing more than
// 0x10 above it -> a ledge (true); else snapped to it. Airborne and not bouncing: a floor or ceiling push lands it.
bool collide(const PlayerEnv& env, AnimatedActor& actor, ServbotFields& servbot) {
    uint32_t pushed = 0;
    servbot.contact = 0;
    const uint32_t move = actorTestMove(env, actor); // exe 0x40f570
    if (move & kMoveNoCell) servbot.contact |= kContactLeftGrid;
    if (int32_t(move) < 0) {
        pushed = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(pushed) < 0) actor.pos = actor.prevPos;
        if (pushed & kWallOnSides) servbot.contact = kContactWall;
        if ((servbot.flags & kServbotBouncing) && reflectRotationOffWalls(actor.rotation, pushed) != 0) // 0x410740
            servbot.contact |= kContactBounced;
    }
    const int ground = actorGround(env, actor); // exe 0x40f190
    if (actor.airborne == 0) {
        if (ground - actor.pos.y.whole > 0x10) {
            servbot.contact |= kContactLedge;
            return true;
        }
        actor.pos.y.whole = int16_t(ground);
        return false;
    }
    if (!(servbot.flags & kServbotBouncing) && (pushed & kWallOnY)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(ground);
        servbot.contact |= kContactBounced;
    }
    return false;
}
// exe 0x460580(actor, yaw): bounced (a thud, its fall's speed halved and turned up, its speed halved, upright,
// airborne) -> true; else (a wall: a thud) moved along the yaw under its gravity
bool fly(AnimatedActor& actor, const ServbotFields& servbot, int yaw) {
    const uint8_t contact = servbot.contact;
    if (contact & kContactBounced) {
        sound(kThudSound, actor);
        actor.velocity.y = int16_t(-int32_t(actor.velocity.y) >> 1);
        actor.velocity.z = int16_t(int32_t(actor.velocity.z) >> 1);
        actor.rotation.x = 0;
        actor.airborne = 1;
        return true;
    }
    if (contact & kContactWall) sound(kThudSound, actor);
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    return false;
}
// the bounce's one more step (its first bounce)
void stepAfterBounce(AnimatedActor& actor) {
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
}
// exe 0x4600d0: falling; the first bounce one more step, the second lands it (topple)
void fallOn(AnimatedActor& actor, ServbotFields& servbot) {
    if (!fly(actor, servbot, actor.rotation.y)) return;
    if (actor.stepCounter == 0) {
        stepAfterBounce(actor);
        actor.stepCounter = 1;
        return;
    }
    actor.airborne = 0;
    servbot.flags &= uint8_t(~kServbotBouncing);
    enterSub(actor, ServbotSub::Topple);
}
// a random time of the table
int16_t randomTime(const DataTable<ScalarTable<int8_t, 16>>& table) { return table[rngStep() & 0xf]; }
// the lying's count down (the topples' last step): up -> its get-up; beaten -> lying beaten
void lieCountdown(AnimatedActor& actor, ServbotFields& servbot, ServbotSub getUp) {
    servbot.timer -= 1u;
    if (servbot.timer == 0) enterSub(actor, getUp);
    if (actor.hitPoints < 0) enterSub(actor, ServbotSub::Beaten);
}

// ---- the sub-states [0x88fa30] -------------------------------------------------------------------------------------
void topple(AnimatedActor& actor, ServbotFields& servbot);
void runAround(AnimatedActor& actor, ServbotFields& servbot);
void flung(AnimatedActor& actor, ServbotFields& servbot);

// exe 0x45f670: falling in
void fallIn(AnimatedActor& actor, ServbotFields& servbot) {
    if (actor.step == 0) {
        setAnim(actor, ServbotAnim::Tumble);
        setFace(actor, kFaceHurt);
        servbot.ledge = uint8_t(LedgeAction::None);
        servbot.flags |= kServbotBouncing;
        actor.acceleration.y = kGravity;
        actor.airborne = 1;
        actor.enterStep(1);
    }
    fallOn(actor, servbot);
}
// exe 0x45f6c0: topple (now and then, when it has not reacted yet) or run around the player on a random side
void decide(AnimatedActor& actor, ServbotFields& servbot) {
    servbot.flags = 0;
    servbot.ledge = uint8_t(LedgeAction::Hop);
    actor.restartStep();
    const uint32_t r = rngStep();
    if (int32_t(0x14a000u << (r & 0x1f)) < 0 && servbot.reacted == 0) {
        actor.subState = uint8_t(ServbotSub::Topple);
        topple(actor, servbot);
        return;
    }
    const uint32_t r2 = rngStep();
    servbot.side = uint8_t((0xaaaaaaaau >> (r2 & 0x1f)) & 1u);
    actor.subState = uint8_t(ServbotSub::RunAround);
    runAround(actor, servbot);
}
// exe 0x45f760: running sideways around the player a random time (turning back at walls)
void runAround(AnimatedActor& actor, ServbotFields& servbot) {
    switch (actor.step) {
    case 0:
        setAnim(actor, ServbotAnim::Run);
        actor.velocity.setXY(0, 0);
        actor.velocity.z = int16_t(0xff40);
        servbot.timer = randomTime(runTimes);
        setFace(actor, kFaceNormal);
        servbot.flags = 0;
        servbot.ledge = uint8_t(LedgeAction::Hop);
        servbot.reacted = 0;
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        if (servbot.contact != 0) {
            actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800u));
            servbot.wallTimer = 0x1e;
            actor.enterStep(2);
        } else {
            const int angle = angleOf(player().pos, actor.pos); // exe 0x5e3bc0
            turnToward(actor.rotation.y, int(angle + (servbot.side ? 0x600 : -0x600)), 0x20);
        }
        break;
    case 2: {
        const uint8_t t = uint8_t(servbot.wallTimer - 1u);
        servbot.wallTimer = t;
        if (t == 0) {
            actor.enterStep(1);
            servbot.side ^= 1u;
        }
        if (servbot.contact != 0) enterSub(actor, ServbotSub::Topple);
        break;
    }
    default: break;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    servbot.timer -= 1u;
    if (servbot.timer == 0) enterSub(actor, ServbotSub::Decide);
}
// exe 0x45f8a0: toppling over backward, lying a random time (beaten: lying beaten)
void topple(AnimatedActor& actor, ServbotFields& servbot) {
    switch (actor.step) {
    case 0:
        setAnim(actor, ServbotAnim::FallBack);
        setFace(actor, kFaceHurt);
        sound(kThudSound, actor);
        servbot.flags = actor.hitPoints >= 0 ? 0 : kServbotDown;
        servbot.ledge = uint8_t(LedgeAction::Drop);
        servbot.reacted = 1;
        actor.enterStep(1);
        return;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, ServbotAnim::LieBack);
            actor.enterStep(2);
        }
        return;
    case 2:
        if (actor.animEnded()) {
            actor.enterStep(3);
            servbot.timer = randomTime(lieTimes);
        }
        return;
    case 3: lieCountdown(actor, servbot, ServbotSub::GetUpBack); return;
    default: return;
    }
}
// the knock-downs' flight: its flags, speed and gravity; airborne, reacted
void launch(AnimatedActor& actor, ServbotFields& servbot, int16_t vy, int16_t vz) {
    servbot.flags = kServbotTumbling;
    servbot.ledge = uint8_t(LedgeAction::None);
    actor.velocity.y = vy;
    actor.velocity.z = vz;
    actor.acceleration.y = kGravity;
}
// the knock-downs' landing
void landKnocked(AnimatedActor& actor, ServbotFields& servbot) {
    servbot.flags &= uint8_t(~kServbotTumbling);
    actor.airborne = 0;
    servbot.ledge = uint8_t(LedgeAction::Drop);
    actor.enterStep(2);
}
// exe 0x45f9a0: knocked back by a hit from the front, lying, then up
void knockedBack(AnimatedActor& actor, ServbotFields& servbot) {
    switch (actor.step) {
    case 0:
        setAnim(actor, ServbotAnim::FallBack);
        setFace(actor, kFaceHurt);
        launch(actor, servbot, int16_t(0xfe80), int16_t(0xfe80));
        actor.rotation.y = servbot.faceYaw;
        actor.airborne = 1;
        servbot.reacted = 1;
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        if (!fly(actor, servbot, actor.rotation.y)) return;
        setAnim(actor, ServbotAnim::LieBack);
        sound(kThudSound, actor);
        servbot.flags &= uint8_t(~kServbotTumbling);
        actor.airborne = 0;
        servbot.ledge = uint8_t(LedgeAction::Drop);
        actor.enterStep(2);
        return;
    case 2:
        if (actor.animEnded()) {
            actor.enterStep(3);
            servbot.timer = randomTime(lieDownTimes);
        }
        return;
    case 3: lieCountdown(actor, servbot, ServbotSub::GetUpBack); return;
    default: return;
    }
}
// exe 0x45fae0 / 0x45fc90: getting up -> decide
void getUp(AnimatedActor& actor, ServbotFields& servbot, ServbotAnim anim) {
    if (actor.step == 0) {
        setAnim(actor, anim);
        servbot.flags = 0;
        servbot.ledge = uint8_t(LedgeAction::Drop);
        actor.step = 1;
        return;
    }
    if (actor.animEnded()) enterSub(actor, ServbotSub::Decide);
}
// exe 0x45fb20: knocked over forward by a hit from behind, lying, then up
void knockedOver(AnimatedActor& actor, ServbotFields& servbot) {
    switch (actor.step) {
    case 0:
        setAnim(actor, ServbotAnim::FallForward);
        setFace(actor, kFaceHurt);
        servbot.flags = kServbotTumbling;
        servbot.ledge = uint8_t(LedgeAction::None);
        servbot.reacted = 1;
        actor.velocity.y = int16_t(0xfe80);
        actor.velocity.z = 0x180;
        actor.acceleration.y = kGravity;
        actor.airborne = 1;
        actor.enterStep(1);
        actor.rotation.y = int16_t(uint16_t(uint16_t(servbot.faceYaw) + 0x800u));
        [[fallthrough]];
    case 1:
        if (!fly(actor, servbot, actor.rotation.y)) break;
        setAnim(actor, ServbotAnim::LieFront);
        sound(kThudSound, actor);
        actor.velocity.z = 0;
        landKnocked(actor, servbot);
        break;
    case 2:
        if (!actor.animEnded()) break;
        actor.enterStep(3);
        servbot.timer = randomTime(lieDownTimes);
        break;
    case 3: lieCountdown(actor, servbot, ServbotSub::GetUpFront); break;
    default: break;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
}
// exe 0x45fcd0: killed -- a spark, flung along the hit's yaw, then lying beaten
void flung(AnimatedActor& actor, ServbotFields& servbot) {
    switch (actor.step) {
    case 0:
        setAnim(actor, ServbotAnim::Tumble);
        setFace(actor, kFaceHurt);
        servbot.flags = kServbotTumbling;
        servbot.ledge = uint8_t(LedgeAction::None);
        servbot.reacted = 1;
        actor.velocity.y = int16_t(0xfe00);
        actor.velocity.z = int16_t(0xff00);
        actor.acceleration.y = kGravity;
        actor.rotation.y = servbot.faceYaw;
        actor.airborne = 1;
        spark(actor);
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        if (!fly(actor, servbot, actor.rotation.y)) return;
        if (actor.stepCounter == 0) {
            stepAfterBounce(actor);
            actor.stepCounter = 1;
            return;
        }
        setAnim(actor, ServbotAnim::LandBeaten);
        actor.airborne = 0;
        servbot.ledge = uint8_t(LedgeAction::Drop);
        servbot.flags &= uint8_t(~kServbotTumbling);
        actor.enterStep(2);
        return;
    case 2:
        if (actor.animEnded()) enterSub(actor, ServbotSub::Beaten);
        return;
    default: return;
    }
}
// exe 0x45fde0: a beaten one hit again: flung lower, no spark
void flungAgain(AnimatedActor& actor, ServbotFields& servbot) {
    setAnim(actor, ServbotAnim::Tumble);
    setFace(actor, kFaceHurt);
    servbot.flags = kServbotTumbling;
    servbot.ledge = uint8_t(LedgeAction::None);
    servbot.reacted = 1;
    actor.velocity.y = int16_t(0xff40);
    actor.velocity.z = int16_t(0xff00);
    actor.acceleration.y = kGravity;
    actor.rotation.y = servbot.faceYaw;
    actor.airborne = 1;
    actor.subState = uint8_t(ServbotSub::Flung);
    actor.enterStep(1);
    flung(actor, servbot);
}
// exe 0x45fe40: lying beaten; now and then (on screen) a smoke puff
void lieBeaten(AnimatedActor& actor, ServbotFields& servbot) {
    servbot.flags = kServbotBouncing;
    servbot.ledge = uint8_t(LedgeAction::Drop);
    if (gameState().frame & 7) return;
    if (servbot.offScreen != kOffScreenTicks) return;
    const uint32_t r = rngStep();
    if (int32_t(0x15c200u << (r & 0x1f)) >= 0) return;
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& smoke = staticActorAt(slot);
    smoke.flags = kActorLive;
    smoke.type = kSmokeType;
    smoke.variant = kBeatenSmokeVariant;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.pos.x = actor.pos.x;
    smoke.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x20u);
    smoke.pos.z = actor.pos.z;
    SmokeParams params(actorHeaderAt(slot));
    params.driftX = 0;
    params.driftZ = 0;
    params.life = 1;
}
// exe 0x45fee0: hopping down a ledge; landing upright (decide) or falling over (-> up / lying beaten)
void ledgeHop(AnimatedActor& actor, ServbotFields& servbot) {
    switch (actor.step) {
    case 0:
        setAnim(actor, ServbotAnim::Hop);
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(0xfe80);
        actor.velocity.z = int16_t(0xff80);
        actor.airborne = 1;
        setFace(actor, kFaceNormal);
        servbot.flags = 0;
        servbot.ledge = uint8_t(LedgeAction::None);
        servbot.reacted = 0;
        actor.enterStep(1);
        [[fallthrough]];
    case 1: {
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + 0x40u));
        if (actor.airborne != 0) return;
        const uint32_t r = rngStep();
        if (int32_t(0x24249048u << (r & 0x1f)) >= 0 && actor.hitPoints >= 0) {
            setAnim(actor, ServbotAnim::HopLand);
            actor.enterStep(2);
            servbot.ledge = uint8_t(LedgeAction::Drop);
            return;
        }
        setAnim(actor, ServbotAnim::HopFall);
        actor.enterStep(3);
        setFace(actor, kFaceHurt);
        return;
    }
    case 2:
        if (actor.animEnded()) enterSub(actor, ServbotSub::Decide);
        return;
    case 3:
        if (!actor.animEnded()) return;
        if (actor.hitPoints < 0) {
            enterSub(actor, ServbotSub::Beaten);
            return;
        }
        servbot.timer = randomTime(lieTimes);
        actor.enterStep(4);
        return;
    case 4:
        servbot.timer -= 1u;
        if (servbot.timer == 0) enterSub(actor, ServbotSub::GetUpFront);
        return;
    default: return;
    }
}
// exe 0x460060: dropping off a ledge (no hop), then the fall
void ledgeDrop(AnimatedActor& actor, ServbotFields& servbot) {
    if (actor.step == 0) {
        setAnim(actor, ServbotAnim::Tumble);
        setFace(actor, kFaceHurt);
        servbot.ledge = uint8_t(LedgeAction::None);
        servbot.flags |= kServbotBouncing;
        actor.velocity.y = 0x40;
        actor.acceleration.y = kGravity;
        actor.velocity.z = 0;
        actor.airborne = 1;
        servbot.reacted = 0;
        actor.enterStep(1);
    }
    fallOn(actor, servbot);
}

// ---- the intro of variant 1 (its steps [0x88fa98]) and the carried state ------------------------------------------
// exe 0x460150: facing the player; on screen a while -> a jump; near -> decide
void watchPlayer(AnimatedActor& actor, ServbotFields& servbot) {
    if (actor.stepCounter == 0) {
        setAnim(actor, ServbotAnim::Stand);
        actor.velocity.setXY(0, 0);
        actor.velocity.setZSpare(0, 0);
        setFace(actor, kFaceNormal);
        servbot.flags = kServbotWatching;
        servbot.ledge = uint8_t(LedgeAction::Hop);
        servbot.reacted = 0;
        servbot.timer = 0x3c;
        actor.stepCounter = 1;
    }
    const int angle = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle));
    if (uint32_t(d + 0x80) > 0x100u) {
        servbot.faceYaw = int16_t(angle);
        setAnim(actor, ServbotAnim::Turn);
    } else {
        servbot.faceYaw = actor.rotation.y;
    }
    if (turnYawTo(actor.rotation.y, uint32_t(int32_t(servbot.faceYaw)), 0x10) == 0) // exe 0x5e3df0
        setAnim(actor, ServbotAnim::Stand);
    if (pointVisibleOrNear(actor.pos, 0) != 0) { // exe 0x5e59a0: off screen
        servbot.timer += 1u;
        if (servbot.timer > 0x3c) servbot.timer = 0x3c;
        return;
    }
    servbot.timer -= 1u;
    if (servbot.timer == 0) {
        servbot.faceYaw = 0;
        actor.enterStep(uint8_t(IntroStep::Jump));
    }
    if (uint32_t(distance3(player().pos, actor.pos)) < 0x400u) enterSub(actor, ServbotSub::Decide); // exe 0x5e3a60
}
// exe 0x460270: a jump in place (its face yaw set: higher, then decide; else back to watching)
void jumpInPlace(AnimatedActor& actor, ServbotFields& servbot) {
    switch (actor.stepCounter) {
    case 0:
        setAnim(actor, ServbotAnim::Jump);
        actor.velocity.z = 0;
        actor.acceleration.y = kGravity;
        if (servbot.faceYaw == 0) {
            actor.velocity.y = int16_t(0xfe80);
            setFace(actor, kFaceNormal);
        } else {
            actor.velocity.y = int16_t(0xfd80);
            setFace(actor, kFaceHurt);
        }
        servbot.flags = kServbotWatching | kServbotBouncing;
        servbot.ledge = uint8_t(LedgeAction::None);
        servbot.reacted = 0;
        actor.stepCounter = 1;
        return;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, ServbotAnim::Hop);
            actor.stepCounter = 2;
            actor.airborne = 1;
        }
        return;
    case 2:
        if (fly(actor, servbot, actor.rotation.y)) {
            actor.stepCounter = 3;
            actor.airborne = 0;
            setAnim(actor, ServbotAnim::HopLand);
        }
        return;
    case 3:
        if (!actor.animEnded()) return;
        if (servbot.faceYaw != 0) actor.subState = uint8_t(ServbotSub::Decide);
        actor.restartStep();
        return;
    default: return;
    }
}
void thrown(const PlayerEnv& env, AnimatedActor& actor, ServbotFields& servbot);
// exe 0x460370: in the player's hands until thrown (along his aim) or dropped
void carried(const PlayerEnv& env, AnimatedActor& actor, ServbotFields& servbot) {
    servbot.contact = 0;
    if (actor.step == 0) {
        actor.step = 1;
        setAnim(actor, actor.hitPoints >= 0 ? ServbotAnim::Held : ServbotAnim::HeldBeaten);
        setFace(actor, kFaceHurt);
        servbot.thrownHit = 0;
    }
    carryAtHands(actor, 0, 0, 0);                 // exe 0x573ab0
    const CarryRelease release = carryReleased(); // exe 0x573950
    if (release == CarryRelease::Held) return;
    if (release == CarryRelease::Thrown) {
        int32_t aim[3];
        playerAimVector(0, -0x100, -0x400, aim); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(aim[1]);
        actor.velocity.z = int16_t(aim[2]);
        servbot.flags = kServbotTumbling | kServbotHurtsWhenThrown;
    } else {
        actor.velocity.y = int16_t(0xfe00);
        actor.velocity.z = int16_t(0xff00);
        servbot.flags = kServbotTumbling | kServbotNoHitEntry;
    }
    actor.acceleration.y = kGravity;
    actor.rotation.y = player().rotation.y;
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    servbot.ledge = uint8_t(LedgeAction::None);
    setFace(actor, kFaceHurt);
    setAnim(actor, ServbotAnim::Tumble);
    actor.subState = uint8_t(CarriedSub::Thrown);
    actor.restartStep();
    thrown(env, actor, servbot);
}
// exe 0x460470: thrown; hitting something turns it around and beats it (a spark); its first bounce costs it the
// row's hit size in hit points (a spark when that kills it), its second lands it (topple)
void thrown(const PlayerEnv& env, AnimatedActor& actor, ServbotFields& servbot) {
    if (servbot.hitBox.result & kHitAttackSolid) {
        actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + 0x800u));
        servbot.thrownHit = 1;
        if (actor.hitPoints >= 0) {
            spark(actor);
            actor.hitPoints = -1;
            servbot.flags |= kServbotNoHitEntry;
        }
    }
    if (fly(actor, servbot, actor.rotation.y)) {
        if (actor.stepCounter == 0) {
            stepAfterBounce(actor);
            if (actor.hitPoints >= 0 && hitPointsAdd(actor, -int(animatedStats(actor.type).hitSize)))
                spark(actor);
            actor.stepCounter += 1u;
            collide(env, actor, servbot);
            return;
        }
        actor.enterState(uint8_t(ServbotState::Run));
        actor.subState = uint8_t(ServbotSub::Topple);
        actor.airborne = 0;
        servbot.flags &= uint8_t(~kServbotBouncing);
    }
    collide(env, actor, servbot);
}
// exe 0x460130: the intro by its step, its off-screen ticks reset
void intro(const PlayerEnv& env, AnimatedActor& actor, ServbotFields& servbot) {
    switch (IntroStep(actor.step)) { // [0x88fa98]
    case IntroStep::Watch: watchPlayer(actor, servbot); break;
    case IntroStep::Jump: jumpInPlace(actor, servbot); break;
    case IntroStep::Carried: carried(env, actor, servbot); break;
    case IntroStep::Thrown: thrown(env, actor, servbot); break;
    default: notPortedAt(kStoryPort, "0x460130: step past [0x88fa98]", actor.step);
    }
    servbot.offScreen = kOffScreenTicks;
}
void runSub(const PlayerEnv& env, AnimatedActor& actor, ServbotFields& servbot) { // [0x88fa30]
    switch (ServbotSub(actor.subState)) {
    case ServbotSub::Fall: fallIn(actor, servbot); break;
    case ServbotSub::Decide: decide(actor, servbot); break;
    case ServbotSub::RunAround: runAround(actor, servbot); break;
    case ServbotSub::Topple: topple(actor, servbot); break;
    case ServbotSub::KnockedBack: knockedBack(actor, servbot); break;
    case ServbotSub::GetUpBack: getUp(actor, servbot, ServbotAnim::GetUpBack); break;
    case ServbotSub::KnockedOver: knockedOver(actor, servbot); break;
    case ServbotSub::GetUpFront: getUp(actor, servbot, ServbotAnim::GetUpFront); break;
    case ServbotSub::Flung: flung(actor, servbot); break;
    case ServbotSub::FlungAgain: flungAgain(actor, servbot); break;
    case ServbotSub::Beaten: lieBeaten(actor, servbot); break;
    case ServbotSub::LedgeHop: ledgeHop(actor, servbot); break;
    case ServbotSub::LedgeDrop: ledgeDrop(actor, servbot); break;
    case ServbotSub::Intro: intro(env, actor, servbot); break;
    default: notPortedAt(kStoryPort, "0x45f550: sub-state past [0x88fa30]", actor.subState);
    }
}
// another live Servbot of the field (the one-at-a-time variant)
bool anotherServbotLive(const AnimatedActor& actor) {
    uint32_t live = actorSystem().animatedLive;
    for (uint32_t i = 0; i < kAnimatedFieldSlots; ++i, live <<= 1) {
        const AnimatedActor& other = animatedPool()[i];
        if (int32_t(live) < 0 && other.type == kPirateServbotType && other.slot != actor.slot) return true;
    }
    return false;
}
// exe 0x45f3c0
void bindServbot(AnimatedActor& actor, ServbotFields& servbot) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeServbot(actor);
        return;
    }
    if ((actor.variant & kServbotSingle) && anotherServbotLive(actor)) {
        freeServbot(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.runFlags |= kActorWarpKept;
    setAnim(actor, ServbotAnim::Stand);
    actor.prevAnim = 0xff;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(TableId::PirateServbotsServbotBox);
    actor.interactionBox.pointAt(TableId::PirateServbotsServbotLiftBox);
    actor.interaction = kInteractLift;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.scale.z = kServbotScale;
    actor.scale.y = kServbotScale;
    actor.scale.x = kServbotScale;
    actor.clearClassBlock(); // exe 0x40dc40
    servbot.offScreen = kOffScreenTicks;
    actor.enterState(uint8_t(ServbotState::Run));
    switch (actor.variant & kServbotVariantMask) {
    case kFallsInVariant:
    case kServbotReportsGone: {
        const uint32_t r = rngStep();
        actor.velocity.y = int16_t(0xfd00);
        actor.rotation.y = int16_t((r & 0xff) << 4);
        actor.velocity.z = int16_t(0xff00);
        return;
    }
    case kIntroVariant: actor.subState = uint8_t(ServbotSub::Intro); return;
    default: return; // kPoppedOutVariant: its spawner set it
    }
}
// exe 0x45f550: its hit reaction, its sub-state, ledges, freed a while off screen, lifted by the player
void runServbot(const PlayerEnv& env, AnimatedActor& actor, ServbotFields& servbot) {
    if (!(servbot.flags & kServbotNoHitTest)) {
        const HitReaction reaction = takeHit(actor, servbot);
        if (!(servbot.flags & kServbotNoReaction)) {
            if (reaction == HitReaction::Hurt) {
                const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)),
                                        uint32_t(int32_t(servbot.faceYaw)));
                enterSub(actor, uint32_t(d + 0x400) < 0x800u ? ServbotSub::KnockedBack : ServbotSub::KnockedOver);
            } else if (reaction == HitReaction::Killed) {
                enterSub(actor, ServbotSub::Flung);
            } else if (reaction == HitReaction::Flung) {
                enterSub(actor, ServbotSub::FlungAgain);
            }
        }
    }
    runSub(env, actor, servbot);
    if (collide(env, actor, servbot)) {
        if (LedgeAction(servbot.ledge) == LedgeAction::Hop) enterSub(actor, ServbotSub::LedgeHop);
        else if (LedgeAction(servbot.ledge) == LedgeAction::Drop) enterSub(actor, ServbotSub::LedgeDrop);
    }
    if (pointVisibleOrNear(actor.pos, 0x200) != 0) { // exe 0x5e59a0: off screen and far
        const int8_t left = int8_t(servbot.offScreen - 1);
        servbot.offScreen = left;
        if (left < 0) freeServbot(actor);
    } else {
        servbot.offScreen = kOffScreenTicks;
    }
    if ((actor.flags & kActorLive) && playerLiftTargetCheck(actor, 0)) { // exe 0x573a60: lifted
        actor.enterState(uint8_t(ServbotState::Carried));
        servbot.flags |= kServbotNoHitEntry;
        actor.runFlags |= kActorRunMarked;
    }
}
void servbotOnFoot(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x45f240
    ServbotFields& servbot = classState<ServbotFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    actor.tint = actor.hitPoints >= 0 ? kTintNeutral : kTintSpent;
    switch (ServbotState(actor.state)) { // [0x88fa24]
    case ServbotState::Bind: bindServbot(actor, servbot); break;
    case ServbotState::Run: runServbot(env, actor, servbot); break;
    case ServbotState::Carried: // exe 0x460350: [0x88faa0]
        switch (CarriedSub(actor.subState)) {
        case CarriedSub::Hands: carried(env, actor, servbot); break;
        case CarriedSub::Thrown: thrown(env, actor, servbot); break;
        default: notPortedAt(kStoryPort, "0x460350: sub-state past [0x88faa0]", actor.subState);
        }
        break;
    default: notPortedAt(kStoryPort, "0x45f240: state past [0x88fa24]", actor.state);
    }
    if (actor.flags & kActorLive) {
        if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
        else animSetPose(actor, actor.anim, 0);                // exe 0x5e1c30
        servbot.hitBox.previousX = actor.prevPos.x.whole;
        servbot.hitBox.previousZ = actor.prevPos.z.whole;
        servbot.hitBox.result = 0;
        const uint8_t flags = servbot.flags;
        if (!(flags & kServbotNoHitEntry)) {
            const AnimatedActorStats stats = animatedStats(actor.type);
            const uint32_t owner = hitOwner(actor.slot);
            const uint32_t thrownShape =
                owner | hitGroup(kHitGroupPlayerAttacks) | kHitNoLockOn | kHitContactMotion | hitRadius(0x30);
            if (!(flags & kServbotHurtsWhenThrown))
                submitAtRoot(actor, servbot.hitBox,
                             owner | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) | kHitCompareHeights |
                                 kHitNoLockOn | kHitContactMotion | hitRadius(0x20),
                             kHitAttackSolid);
            else if (servbot.thrownHit == 0) // thrown: it hurts what it hits
                submitAtRoot(actor, servbot.hitBox, thrownShape, uint32_t(stats.bodyAttack) | kHitAttackByPlayer);
            else
                submitAtRoot(actor, servbot.hitBox, thrownShape, kHitAttackBump);
        }
        // drawn unless carried: the shadow (exe 0x5f20f0(actor, 0x30, 0x100, 0)) -- render output
    }
    if (servbot.contact & kContactLeftGrid) freeServbot(actor);
}

// ==== sub 1: the bomb-lobbing Servbot (exe 0x464270) ===============================================================
enum class BomberState : uint8_t { Bind = 0, Run = 1, PopOut = 2 };
enum class BomberSub : uint8_t { Wait = 0, Lob = 1, Taunt = 2 };
enum class BomberAnim : uint8_t { Idle = 0, Lob = 0xc };
// its spawn parameter: its voice (the sounds' tables' index)
struct BomberParams {
    explicit BomberParams(const AnimatedActor& actor) : BomberParams(const_cast<AnimatedActor&>(actor)) {}
    explicit BomberParams(AnimatedActor& actor) : voice(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> voice;
};
constexpr uint8_t kBomberStatsRow = 2; // its type's row of spawn byte 2 (hit points, the bombs' damage)
const DataTable<ScalarTable<int16_t, 4>> lobDistances{TableId::PirateServbotsLobDistances};
constexpr uint8_t kThrowKey = 0xb;         // the lob animation's key that lets the bomb go
constexpr int16_t kYawRange = 0x1a0;
constexpr int16_t kBombSize = 0x30;

uint16_t voiceSound(const AnimatedActor& actor, uint32_t table) {
    return dataScalar<uint16_t>(table + uint32_t(int32_t(BomberParams(actor).voice)) * 2);
}
// exe 0x4649e0: damaged -> flashed, sound 0x8d; killed or hurt
HitReaction bomberHit(AnimatedActor& actor, BomberFields& bomber) {
    const uint32_t damage = bomber.hitBox.result & kHitDamageMask;
    if (damage == 0) return HitReaction::None;
    actor.tint = kTintFlash;
    sound(kHurtSound, actor);
    return hitPointsAdd(actor, -int(damage)) ? HitReaction::Killed : HitReaction::Hurt;
}
// exe 0x464360
void bindBomber(AnimatedActor& actor, BomberFields& bomber) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.runFlags |= kActorWarpKept;
    actor.anim = uint8_t(BomberAnim::Idle);
    actor.prevAnim = 0xff;
    const int16_t hp = hasAnimatedStats(actor.type)
                           ? int16_t(animatedStats(actor.type, kBomberStatsRow).hitPoints)
                           : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) & 0xfffu);
    actor.scale.z = kServbotScale;
    actor.scale.y = kServbotScale;
    actor.scale.x = kServbotScale;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(TableId::PirateServbotsBomberBox);
    actor.interactionBox.pointAt(TableId::PirateServbotsBomberLiftBox);
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    bomber.homeYaw = actor.rotation.y;
    actor.enterState(uint8_t(BomberState::Run));
}
// exe 0x464500: idle; the player within 0x600 and in front (+-0x200) -> the lob (nearly always)
void waitForPlayer(AnimatedActor& actor) {
    if (actor.step == 0) {
        setFace(actor, kFaceNormal);
        actor.anim = uint8_t(BomberAnim::Idle);
        ++actor.step;
    }
    if (planarDistance(actor.pos, player().pos) > 0x600) return; // exe 0x5e3b00
    const int angle = angleOf(player().pos, actor.pos);
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angle));
    if (uint32_t(d + 0x200) > 0x400u) return;
    const uint32_t r = rngStep();
    if ((0x857efe51u << (r & 0x1f)) == 0) return;
    actor.subState = uint8_t(BomberSub::Lob);
    actor.restartStep();
}
// the volley's bomb at the target (lobbed from 0x40 above it)
void lobBomb(const AnimatedActor& actor, const int32_t target[3]) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& bomb = projectileActorAt(slot);
    LobbedShotParams params(projectileActorAt(slot));
    bomb.flags = kActorLive;
    bomb.type = kLobbedShotStageType;
    bomb.variant = 0;
    bomb.pos.x = actor.pos.x;
    bomb.pos.y = int32_t(uint32_t(actor.pos.y) - 0x400000u);
    bomb.pos.z = actor.pos.z;
    bomb.rotation.x = 0;
    bomb.rotation.y = actor.rotation.y;
    bomb.rotation.z = 0;
    params.size = kBombSize;
    int16_t vy = 0, vz = 0;
    const int32_t from[3] = {bomb.pos.x, bomb.pos.y, bomb.pos.z};
    lobVelocity(from, target, -0x80, 0x40, 0x400, vy, vz); // exe 0x5e3f50
    bomb.velocity.x = 0; // the exe leaves it as the slot had it
    bomb.velocity.y = vy;
    bomb.velocity.z = int16_t(-vz);
    params.blastSize = 0; // the exe leaves it as the slot had it
    bomb.acceleration.y = 0x40;
    params.damage = int16_t(animatedStats(actor.type, kBomberStatsRow).attack);
    params.launchSound = -1;
}
// exe 0x4645a0: volleys of three bombs lobbed around / at the player, turning within kYawRange of its home yaw; a
// 0x2d..0x3c tick pause between volleys
void lobVolley(AnimatedActor& actor, BomberFields& bomber) {
    switch (actor.step) {
    case 0:
        bomber.bombsLeft = 3;
        ++actor.step;
        [[fallthrough]];
    case 1:
        actor.anim = uint8_t(BomberAnim::Lob);
        actor.prevAnim = 0xff;
        ++actor.step;
        return;
    case 2: {
        turnYawTo(actor.rotation.y, uint32_t(angleOf(player().pos, actor.pos)), 0x20); // exe 0x5e3df0
        const int home = bomber.homeYaw;
        if (actor.rotation.y > home + kYawRange) actor.rotation.y = int16_t(home + kYawRange);
        if (actor.rotation.y < home - kYawRange) actor.rotation.y = int16_t(home - kYawRange);
        if (actor.keyIndex != kThrowKey) return;
        sound(voiceSound(actor, stateAddress(TableId::PirateServbotsLobVoices)), actor);
        bomber.bombsLeft -= 1u;
        int16_t dx = 0, dz = 0, dy;
        if (bomber.bombsLeft == 0) { // the last one at the player
            int d = planarDistance(actor.pos, player().pos);
            if (d < 0x30) d = 0x30;
            angleVector(actor.rotation.y, int16_t(d), dx, dz); // exe 0x5e40c0
            dy = -100;
        } else { // the others a random distance ahead
            angleVector(actor.rotation.y, lobDistances[rngStep() & 3], dx, dz);
            dy = 0;
        }
        const Player& p = player();
        const int32_t target[3] = {int32_t((uint32_t(int32_t(dx)) << 16) + uint32_t(p.pos.x)),
                                   int32_t((uint32_t(int32_t(dy)) << 16) + uint32_t(p.pos.y)),
                                   int32_t((uint32_t(int32_t(dz)) << 16) + uint32_t(p.pos.z))};
        lobBomb(actor, target);
        ++actor.step;
        return;
    }
    case 3:
        if (!actor.animEnded()) return;
        actor.step = 1;
        if (bomber.bombsLeft != 0) return;
        actor.anim = uint8_t(BomberAnim::Idle);
        bomber.wait = uint16_t((rngStep() & 0xf) + 0x2d);
        actor.step = 4;
        return;
    case 4:
        bomber.wait -= 1u;
        if (bomber.wait == 0) actor.step = 0;
        return;
    default: return;
    }
}
// exe 0x464860: a taunt (one of two animation pairs), its face 4 / 6, a spark
void taunt(AnimatedActor& actor) {
    switch (actor.step) {
    case 0: {
        const uint32_t pair = rngStep() & 1;
        actor.stepCounter = uint8_t(pair);
        actor.anim = dataScalar<uint8_t>(stateAddress(TableId::PirateServbotsTauntAnims) + pair * 2);
        ++actor.step;
        const uint32_t r = rngStep();
        setFace(actor, ((~r & 1u) | 2u) << 1);
        spark(actor);
        return;
    }
    case 1:
        if (actor.animEnded()) {
            ++actor.step;
            actor.anim = dataScalar<uint8_t>(stateAddress(TableId::PirateServbotsTauntAnims) + 1 + uint32_t(actor.stepCounter) * 2);
        }
        return;
    default: return;
    }
}
void popOut(AnimatedActor& actor, BomberFields& bomber);
// exe 0x464430: beaten -> a Servbot thrown out; its floor's segment broken open (exe 0x58d570 < 0) -> one dropped in
// place; else its sub-state [0x88fd00]
void runBomber(AnimatedActor& actor, BomberFields& bomber) {
    if (actor.hitPoints >= 0 && bomberHit(actor, bomber) == HitReaction::Killed) {
        spark(actor);
        bomber.thrownOut = 1;
        actor.enterState(uint8_t(BomberState::PopOut));
        popOut(actor, bomber);
        return;
    }
    const GridCell cell(gridCellAtIndex(uint32_t(gridColumn(actor.pos.x.whole)), uint32_t(gridColumn(actor.pos.z.whole))));
    if (int16_t(uint16_t(cellSegmentStrength(cell.head))) < 0) {
        bomber.thrownOut = 0;
        actor.enterState(uint8_t(BomberState::PopOut));
        popOut(actor, bomber);
        return;
    }
    switch (BomberSub(actor.subState)) { // [0x88fd00]
    case BomberSub::Wait: waitForPlayer(actor); break;
    case BomberSub::Lob: lobVolley(actor, bomber); break;
    case BomberSub::Taunt: taunt(actor); break;
    default: notPortedAt(kStoryPort, "0x464430: sub-state past [0x88fd00]", actor.subState);
    }
}
// exe 0x464920: a spark and its voice once, then a Servbot on foot in its place: thrown out along its yaw or dropped;
// this one freed (it retries while the pool is full)
void popOut(AnimatedActor& actor, BomberFields& bomber) {
    if (actor.subState == 0) {
        spark(actor);
        ++actor.subState;
        sound(voiceSound(actor, stateAddress(TableId::PirateServbotsPopVoices)), actor);
    }
    const uint32_t slot = allocActor20(); // exe 0x5e1160
    if (!slot) return;
    AnimatedActor& servbot = animatedActorAt(slot);
    servbot.loopFlags |= kActorSweepGroup2;
    servbot.flags = kActorLive;
    servbot.type = kPirateServbotType;
    servbot.subtype = kServbotOnFootSubtype;
    servbot.pos = actor.pos;
    if (bomber.thrownOut == 0) {
        servbot.variant = kFallsInVariant;
        freeActor(actor); // exe 0x5e1490
        return;
    }
    servbot.variant = kPoppedOutVariant;
    servbot.rotation.y = actor.rotation.y;
    servbot.velocity.y = 0;
    servbot.velocity.z = int16_t(0xff00);
    freeActor(actor);
}
void bombServbot(AnimatedActor& actor) { // exe 0x464270
    BomberFields& bomber = classState<BomberFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (BomberState(actor.state)) { // [0x88fcf4]
    case BomberState::Bind: bindBomber(actor, bomber); break;
    case BomberState::Run: runBomber(actor, bomber); break;
    case BomberState::PopOut: popOut(actor, bomber); break;
    default: notPortedAt(kStoryPort, "0x464270: state past [0x88fcf4]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);                // exe 0x5e1c30
    bomber.hitBox.previousX = actor.prevPos.x.whole;
    bomber.hitBox.previousZ = actor.prevPos.z.whole;
    bomber.hitBox.result = 0;
    submitAtRoot(actor, bomber.hitBox,
                 hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x20),
                 kHitAttackSolid);
    // the shadow (exe 0x5f20f0(actor, 0x30, 0x100, 0)) -- render output
}

} // namespace

void pirateServbotUpdate(const PlayerEnv& env, uint32_t actor) { servbotOnFoot(env, animatedActorAt(actor)); }

void bombServbotUpdate(const PlayerEnv&, uint32_t actor) { bombServbot(animatedActorAt(actor)); }

} // namespace od::port
