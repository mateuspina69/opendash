// The ST48 charger (pool 0x20 type 6 sub 0, exe 0x4538c0; hypothesis names): a wheeled enemy of the Saul Kada pirate
// attack.
//
// States [0x88ec4c]: 0 bind 0x4539c0, 1 run 0x453a60 (its mode [0x88ec5c]), 2 death 0x454d10 ([0x88ed18] by its
// sub-state), 3 nothing (0x52ef00). The run first: the wall check on and a wall within +-0x3e0 of its yaw (0x455190)
// -> the wall bounce; alive, a heavy hit (0x455200) with the knock-down on -> knocked over; after the mode: its ground
// lost (0x4550f0) -> airborne, the hop.
// Modes: 0 circle 0x453ae0 ([0x88ec80] by its step: spin in place until the player is within 0xfa0 or it was hit
//   0x453b00, boost-turn to him (two jets) 0x453bc0, take off 0x453cd0 -> the hop); 1 charge 0x453d60 ([0x88ec8c]:
//   run 15..23 ticks then he is in front -> again, aside -> curve 0x453ef0, behind -> the skid round 0x453dc0); he is
//   nearer than 0x4b0 -> up close, farther than 0x3a98 -> brake and spin; 2 skid round 0x453fd0 ([0x88eca4]: start
//   0x454030, turn 0x4540e0, skid 0x454120, settle 0x4541c0, brake 0x454250; a wall in front -> the wall bounce, another
//   -> brake); 3 wall bounce 0x4542e0 ([0x88ecb8]: hit 0x454330, turn 0x454420, bounce 0x454480; its speed toward
//   -0x400); 4 up close 0x4544c0 ([0x88ecc4]: sidestep 0x454520, slide 0x4545d0, dodge arc 0x454620, settle 0x454730,
//   brake 0x454250, run off 0x4547c0); 5 hop 0x454860 (two bounces, then the charge, or the circle after a hit while
//   spinning); 6 brake and spin 0x4549e0 (jets freed) -> the circle; 7 knocked over 0x454ae0 ([0x88ed08]: start 0x454b20,
//   wait 0x454b60, airborne 0x454b80, get up 0x454be0: dead -> the death, alive -> overturned); 8 overturned 0x454cc0
//   (its four wheels hit) until dead.
// Its death ([0x88ed18]): 0 the defeat counted 0x454d40, 1 ten rounds of 4 ticks with a blast at a random bone 0x454d70,
// 2 a big blast, the row's drops, freed 0x454e80. Its row: animatedStats(type, spawn byte). The shadow is render
// output.
#include "content/saul_kada/st48_charger.hpp"
#include "content/saul_kada/st48_charger_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/glow_trails.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/step_dust.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st48_charger_state;

namespace {

enum class ChargerState : uint8_t { Bind = 0, Run = 1, Death = 2, Idle = 3 };
enum class ChargerMode : uint8_t {
    Circle = 0, Charge = 1, SkidRound = 2, WallBounce = 3, CloseUp = 4, Hop = 5, BrakeSpin = 6, KnockedOver = 7,
    Overturned = 8
};
enum class CircleStep : uint8_t { Spin = 0, Boost = 1, TakeOff = 2 };
enum class ChargeStep : uint8_t { Run = 0, Curve = 1 };
enum class SkidStep : uint8_t { Start = 0, Turn = 1, Skid = 2, Settle = 3, Brake = 4 };
enum class BounceStep : uint8_t { Hit = 0, Turn = 1, Bounce = 2 };
enum class CloseStep : uint8_t { Sidestep = 0, Slide = 1, Arc = 2, Settle = 3, Brake = 4, RunOff = 5 };
enum class KnockStep : uint8_t { Start = 0, Wait = 1, Airborne = 2, GetUp = 3 };
enum class DeathStep : uint8_t { Count = 0, Blasts = 1, End = 2 };
enum class ChargerAnim : uint8_t {
    Spin = 0, Run = 1, SwerveRight = 2, SwerveRightRun = 3, SwerveLeft = 4, SwerveLeftRun = 5, BounceLeft = 6,
    KnockedOver = 7, Righting = 8, FallBack = 9, Recover = 0xa, Overturned = 0xb, SideRight = 0xd, BounceRight = 0xe,
    SideLeft = 0x10
};
constexpr uint8_t kHitWhileSpinning = 0x80;
const DataTable<ScalarTable<uint8_t, 16>> wallAngles{TableId::St48ChargerWallAngles}; // by the wall bits >> 8 (<< 4: the angle)
const DataTable<ScalarTable<uint16_t, 8>> runTimes{TableId::St48ChargerRunTimes}; // the charge's runs
const DataTable<ScalarTable<int8_t, 16>> runOffTimes{TableId::St48ChargerRunOffTimes}; // the run off's
const DataTable<ScalarTable<int8_t, 8>> brakeSpins{TableId::St48ChargerBrakeSpins}; // the brake's spin (+ 0x800 >> 5)
constexpr uint8_t kJetSize = 4;
constexpr uint8_t kSparkVariant = 3;
constexpr uint16_t kTintBlastFlash = 0x421f;
constexpr uint16_t kHeavyHitSound = 0x8e, kHurtSound = 0x8d, kJetSound = 0xca, kHopSound = 0xcb, kLandSound = 0xcc,
                   kWheelSound = 0xc9, kRattleSound = 0xcd, kRightingSound = 0xce;
constexpr uint8_t kEventWheel = 0x01, kEventRattle = 0x40, kEventGround = 0x80; // its animation's key events

AnimatedActorStats chargerStats(const AnimatedActor& actor) {
    if (!hasAnimatedStats(actor.type))
        notPortedAt(kStoryPort, "0x4538c0: no type row (the exe reads near address 0), type", actor.type);
    return animatedStats(actor.type, actor.spawnByte);
}
void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void setAnim(AnimatedActor& actor, ChargerAnim anim) { actor.anim = uint8_t(anim); }
void enterMode(AnimatedActor& actor, ChargerMode mode) {
    actor.subState = uint8_t(mode);
    actor.restartStep();
}
template <class Step> void enterStep(AnimatedActor& actor, Step step) { actor.enterStep(uint8_t(step)); }
// exe 0x5e36f0 along its heading
void moveAlongHeading(AnimatedActor& actor, const ChargerFields& charger, int y, int z) {
    moveAlongYaw(actor.pos, charger.heading, 0, y, z);
}
int angleToPlayer(const AnimatedActor& actor) { return angleOf(player().pos, actor.pos); } // exe 0x5e3bc0
int turnToPlayer(const AnimatedActor& actor) {
    return angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleToPlayer(actor)));
}
int playerDistance(const AnimatedActor& actor) { return planarDistance(actor.pos, player().pos); } // exe 0x5e3b00
void dustOnOddFrames(ChargerFields& charger) {
    if (gameState().frame & 1) charger.dust = 1;
}
void addToYaw(AnimatedActor& actor, int16_t turn) {
    actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint16_t(turn)));
}
void addToHeading(ChargerFields& charger, int16_t turn) {
    charger.heading = int16_t(uint16_t(uint16_t(charger.heading) + uint16_t(turn)));
}
// the skid's sound: every 10 ticks while it slides faster than 0x80
void skidSound(const AnimatedActor& actor, ChargerFields& charger) {
    const uint8_t c = charger.skidSoundTimer;
    if (c != 0) {
        charger.skidSoundTimer = uint8_t(c - 1u);
        return;
    }
    const int v = actor.velocity.z;
    if ((v < 0 ? -v : v) > 0x80) {
        sound(kJetSound, actor);
        charger.skidSoundTimer = 0xa;
    }
}

// ---- its helpers ---------------------------------------------------------------------------------------------------
// exe 0x4551e0(walls): the wall's angle
uint32_t wallAngle(uint32_t walls) { return uint32_t(wallAngles[(walls >> 8) & 0xf]) << 4; }
// exe 0x455190: a wall within +-0x3e0 of its yaw -> its angle (the target), 1; another wall -1; none 0
int wallAhead(const AnimatedActor& actor, ChargerFields& charger) {
    const uint32_t walls = charger.walls;
    if (walls == 0) return 0;
    const uint32_t angle = wallAngle(walls);
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), angle);
    if (uint32_t(d + 0x3e0) > 0x7c0u) return -1;
    charger.target = int16_t(angle);
    return 1;
}
// exe 0x455200: the damage of its hit box (the player's or a thrown attack) and its body's (a thrown or strong push:
// 0x20 or more -> sound 0x8e, 1); dead -> 2; damaged -> flashed, sound 0x8d, a hit while spinning noted
int takeHit(AnimatedActor& actor, ChargerFields& charger) {
    int32_t damage = 0;
    int heavy = 0;
    uint32_t hit = charger.hitBox.result;
    if ((hit & kHitDamageMask) && (hit & (kHitAttackByPlayer | kHitAttackThrown))) damage = int32_t(hit & kHitDamageMask);
    hit = charger.bodyBox.result;
    const int32_t bodyDamage = int32_t(hit & kHitDamageMask);
    if (bodyDamage != 0 && (hit & (kHitAttackThrown | kHitAttackStrongPush))) {
        damage += bodyDamage;
        if (bodyDamage >= 0x20) {
            sound(kHeavyHitSound, actor);
            heavy = 1;
        }
    }
    if (hitPointsAdd(actor, -damage)) heavy = 2; // exe 0x5e52f0
    if (damage != 0) {
        actor.tint = kTintFlash;
        sound(kHurtSound, actor);
        const uint8_t f = charger.hitWhileSpinning;
        if (f) charger.hitWhileSpinning = uint8_t(f | kHitWhileSpinning);
    }
    return heavy;
}
// exe 0x4550f0: the walls (blocked -> back; their bits noted) and the ground: on it, more than 0x10 above it with the
// ledge check -> true (it falls); else snapped. Airborne: a floor / ceiling push lands it.
bool collide(const PlayerEnv& env, AnimatedActor& actor, ChargerFields& charger) {
    uint32_t pushed = 0;
    if (!(actor.flags & kActorLive)) return false;
    if (actorTestMove(env, actor) != 0) { // exe 0x40f570
        pushed = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(pushed) < 0) actor.pos = actor.prevPos;
    }
    charger.walls = pushed & kWallOnSides;
    const int ground = actorGround(env, actor); // exe 0x40f190
    if (actor.airborne == 0) {
        if (ground - actor.pos.y.whole > 0x10 && charger.ledgeCheck != 0) return true;
    } else {
        if (!(pushed & kWallOnY)) return false;
        actor.airborne = 0;
    }
    actor.pos.y.whole = int16_t(ground);
    return false;
}
// exe 0x455430: its two jets freed (when alive), their slots cleared
void freeJets(ChargerFields& charger) {
    for (size_t i = 0; i < 2; ++i) {
        const uint8_t slot = charger.jets[i];
        if (slot == 0) continue;
        EffectActor& jet = effectActorAt(effectPoolSlot(int32_t(slot)));
        if (jet.flags & kActorLive) freeActor(jet); // exe 0x5e1580
    }
    charger.jetsWord = 0;
}
// exe 0x4553a0: two jets (glow trails riding it at their points); their slots kept (a failed allocation clears the
// second slot, then the first)
void spawnJets(const AnimatedActor& actor, ChargerFields& charger) {
    size_t kept = 0;
    uint32_t point = stateAddress(TableId::St48ChargerJetPoints);
    for (int n = 1;; --n) {
        const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (slot) {
            EffectActor& jet = effectActorAt(slot);
            GlowTrailFields& trail = classState<GlowTrailFields>(effectActorAt(slot));
            const ShortVec3 at = dataShortVec3(point);
            jet.flags = kActorLive;
            jet.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            jet.type = kGlowTrailStageType;
            jet.variant = uint8_t(GlowTrailVariant::Trail);
            trail.size = kJetSize;
            trail.mode = 0;
            trail.parent = &actor;
            jet.pos.x = at.x;
            jet.pos.y = at.y;
            jet.pos.z = at.z;
            trail.bone = 0;
            charger.jets[kept] = jet.slot;
            ++kept;
            point += 8;
        } else {
            charger.jets[size_t(n)] = 0;
        }
        if (n == 0) break;
    }
}
// exe 0x4552b0: dust at bone 8 (pushed out from its centre by a random -3..1 / 16), and 1 in 4 its ground's step dust
void raiseDust(const AnimatedActor& actor) {
    const ShortVec4& wheel = bonePoint(actor, 8, "0x4552b0: bone 8 past its blocks"); // exe 0x5e1810
    rngStep();
    const int32_t k = int32_t(uint16_t(rngLastValue()) % 5u) - 3;
    const int32_t cx = actor.pos.x.whole, cz = actor.pos.z.whole;
    const int32_t ox = ((int32_t(wheel.x) - cx) >> 4) * k;
    const int32_t oz = ((int32_t(wheel.z) - cz) >> 4) * k;
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (slot) {
        EffectActor& spark = effectActorAt(slot);
        spark.flags = kActorLive;
        spark.loopFlags |= kActorSweepGroup2;
        spark.type = typeByte(GlobalEffectType::HitSpark);
        spark.variant = kSparkVariant;
        spark.pos.x = int16_t(uint16_t(ox + cx));
        spark.pos.y = actor.pos.y.whole;
        classState<HitSparkFields>(effectActorAt(slot)).size = kHitSparkSize;
        spark.pos.z = int16_t(uint16_t(oz + cz));
    }
    if ((rngStep() & 3) != 0) return;
    const int32_t kind = groundKindUnder(actor.pos); // exe 0x5e6240
    spawnStepDust(int16_t(uint16_t(ox + cx)), actor.pos.y.whole, int16_t(uint16_t(oz + cz)), kind); // exe 0x5e6300
}
// exe 0x454f00: its hit entries: bone 0 (0x30 up) the hit box, bone 7 (0x40 down) its body's; with the wheels' request
// its four wheels (bones 2 / 3 / 5 / 6) on the hit box; the boxes' previous x / z: bone 0's / bone 7's
void submitHits(const AnimatedActor& actor, ChargerFields& charger) {
    charger.hitBox.result = 0;
    charger.bodyBox.result = 0;
    uint32_t kind, damage;
    if (actor.hitPoints >= 0) {
        kind = kHitAttackSolid;
        damage = chargerStats(actor).attack;
    } else {
        kind = kHitAttackBump;
        damage = 0;
    }
    const uint32_t lockOn = charger.noLockOn != 0 ? kHitNoLockOn : 0u;
    const uint32_t owner = hitOwner(actor.slot) | hitGroup(kHitGroupBodies);
    const uint32_t attack = damage | kind;
    ShortVec4 p = bonePoint(actor, 0, "0x454f00: bone 0 past its blocks"); // exe 0x5e1810
    int16_t pos[3] = {p.x, int16_t(uint16_t(uint16_t(p.y) - 0x30u)), p.z};
    hitListSubmit(pos, charger.hitBox, owner | lockOn | hitRadius(0x50), attack); // exe 0x5e4120
    p = bonePoint(actor, 7, "0x454f00: bone 7 past its blocks");
    pos[0] = p.x;
    pos[1] = int16_t(uint16_t(uint16_t(p.y) + 0x40u));
    pos[2] = p.z;
    hitListSubmit(pos, charger.bodyBox, owner | kHitNoLockOn | hitRadius(0x60), attack);
    charger.bodyBox.previousX = pos[0];
    charger.bodyBox.previousZ = pos[2];
    if (charger.wheelHits != 0) {
        for (uint32_t bone : {2u, 3u, 5u, 6u}) {
            const ShortVec4& w = bonePoint(actor, bone, "0x454f00: a wheel's bone past its blocks");
            const int16_t wheel[3] = {w.x, w.y, w.z};
            hitListSubmit(wheel, charger.hitBox, owner | kHitNoLockOn | hitRadius(0x50),
                          uint32_t(chargerStats(actor).strongAttack) | kind);
        }
        charger.wheelHits = 0;
    }
    p = bonePoint(actor, 0, "0x454f00: bone 0 past its blocks");
    charger.hitBox.previousX = p.x;
    charger.hitBox.previousZ = p.z;
}

// ---- the circle (exe 0x453ae0, its steps [0x88ec80]) ---------------------------------------------------------------
// exe 0x453b00: spinning in place until the player is within 0xfa0 or it was hit
void spin(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.stepCounter) {
    case 0:
        actor.stepCounter = 1;
        setAnim(actor, ChargerAnim::Spin);
        actor.acceleration.z = int16_t(0xfff8);
        charger.wallCheck = 0;
        charger.knockDown = 1;
        charger.ledgeCheck = 1;
        charger.hitWhileSpinning = 1;
        [[fallthrough]];
    case 1:
        if (accelerateToward(actor, 0x200)) { // exe 0x5e5450
            actor.velocity.z = int16_t(0xfe00);
            actor.stepCounter = 2;
        }
        break;
    default: break;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
    addToYaw(actor, 0x20);
    charger.heading = actor.rotation.y;
    if (playerDistance(actor) >= 0xfa0 && !(charger.hitWhileSpinning & kHitWhileSpinning)) return;
    charger.target = int16_t(angleToPlayer(actor));
    enterStep(actor, CircleStep::Boost);
}
// exe 0x453bc0: turning to the target over 0x20 ticks, the jets lit
void boost(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.stepCounter) {
    case 0: {
        actor.stepCounter = 1;
        setAnim(actor, ChargerAnim::SwerveRight);
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(charger.target)));
        actor.spin.y = int16_t(uint16_t(uint32_t(d + 0x1000) >> 5));
        actor.acceleration.z = int16_t(0xfff8);
        charger.timer = 0x20;
        charger.hitWhileSpinning = 0;
        if (int16_t(charger.jetsWord) == 0) spawnJets(actor, charger); // exe 0x4553a0
        sound(kJetSound, actor);
        [[fallthrough]];
    }
    case 1: {
        const int16_t turn = actor.spin.y;
        addToYaw(actor, turn);
        addToHeading(charger, int16_t(turn >> 1));
        charger.timer -= 1u;
        if (charger.timer == 0) {
            actor.stepCounter = 2;
            actor.anim += 1u;
        }
        break;
    }
    case 2:
        if (actor.animEnded()) enterStep(actor, CircleStep::TakeOff);
        break;
    default: break;
    }
    decelerateToward(actor, 0x100); // exe 0x5e54a0
    moveAlongHeading(actor, charger, 0, actor.velocity.z);
    dustOnOddFrames(charger);
}
// exe 0x453cd0: pitched up, accelerating to 0x400 -> airborne, the hop
void takeOff(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.stepCounter == 0) {
        actor.stepCounter = 1;
        setAnim(actor, ChargerAnim::Run);
        actor.velocity.z = 0;
        actor.acceleration.z = int16_t(0xffd0);
        actor.rotation.x = int16_t(0xff60);
        charger.heading = actor.rotation.y;
    }
    if (accelerateToward(actor, 0x400)) {
        actor.airborne = 1;
        enterMode(actor, ChargerMode::Hop);
        actor.rotation.x = 0;
    }
    moveAlongHeading(actor, charger, 0, actor.velocity.z);
    dustOnOddFrames(charger);
}
void circle(AnimatedActor& actor, ChargerFields& charger) { // exe 0x453ae0
    switch (CircleStep(actor.step)) {
    case CircleStep::Spin: spin(actor, charger); break;
    case CircleStep::Boost: boost(actor, charger); break;
    case CircleStep::TakeOff: takeOff(actor, charger); break;
    default: notPortedAt(kStoryPort, "0x453ae0: step past [0x88ec80]", actor.step);
    }
}

// ---- the charge (exe 0x453d60, [0x88ec8c]) -------------------------------------------------------------------------
// exe 0x453dc0: running a random 15..23 ticks, then looking for the player
void run(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.stepCounter) {
    case 0: {
        actor.stepCounter = 1;
        setAnim(actor, ChargerAnim::Run);
        actor.acceleration.x = 0;
        actor.acceleration.z = int16_t(0xfff8);
        const uint32_t r = rngStep();
        charger.wallCheck = 1;
        charger.timer = int16_t(runTimes[r & 7]);
        charger.knockDown = 1;
        charger.ledgeCheck = 1;
        charger.hitWhileSpinning = 0;
        [[fallthrough]];
    }
    case 1:
        if (accelerateToward(actor, 0x400)) {
            actor.stepCounter = 2;
            actor.velocity.z = int16_t(0xfc00);
        }
        break;
    default: break;
    }
    charger.heading = actor.rotation.y;
    const int distance = playerDistance(actor);
    charger.timer -= 1u;
    if (charger.timer != 0) return;
    const int d = turnToPlayer(actor);
    const uint32_t cone = distance <= 0x3e8 ? 0x200u : 0x80u;
    if (uint32_t(int32_t(cone) + d) > cone * 2) {
        if (uint32_t(d + 0x400) > 0x800u) {
            enterMode(actor, ChargerMode::SkidRound);
            return;
        }
        enterStep(actor, ChargeStep::Curve);
        return;
    }
    actor.stepCounter = 0;
}
// exe 0x453ef0: curving to the player (a turn of the angle / 24, at least 0x18 / 24) for one animation
void curve(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.stepCounter == 0) {
        int d = turnToPlayer(actor);
        if (d > 0) {
            setAnim(actor, ChargerAnim::SwerveRightRun);
            if (d < 0x18) d = 0x18;
        } else {
            setAnim(actor, ChargerAnim::SwerveLeftRun);
            if (d > -0x18) d = -0x18;
        }
        actor.acceleration.setXY(0, 0);
        actor.acceleration.setZSpare(0, 0);
        actor.spin.y = int16_t(d / 24);
        actor.stepCounter = 1;
        charger.wallCheck = 1;
        charger.knockDown = 1;
        charger.ledgeCheck = 1;
    }
    addToYaw(actor, actor.spin.y);
    charger.heading = actor.rotation.y;
    accelerateToward(actor, 0x400);
    if (actor.animEnded()) enterStep(actor, ChargeStep::Run);
    charger.heading = actor.rotation.y;
}
// exe 0x453d60: then the player near (< 0x4b0) -> up close, far (> 0x3a98) -> brake and spin
void charge(AnimatedActor& actor, ChargerFields& charger) {
    switch (ChargeStep(actor.step)) {
    case ChargeStep::Run: run(actor, charger); break;
    case ChargeStep::Curve: curve(actor, charger); break;
    default: notPortedAt(kStoryPort, "0x453d60: step past [0x88ec8c]", actor.step);
    }
    moveAlongHeading(actor, charger, 0, actor.velocity.z);
    const int d = playerDistance(actor);
    if (d < 0x4b0) enterMode(actor, ChargerMode::CloseUp);
    if (d > 0x3a98) enterMode(actor, ChargerMode::BrakeSpin);
}

// ---- the skid round (exe 0x453fd0, [0x88eca4]) ---------------------------------------------------------------------
// exe 0x4540e0: turning by its spin for 0x10 ticks
void skidTurn(AnimatedActor& actor, ChargerFields& charger) {
    addToYaw(actor, actor.spin.y);
    charger.target -= 1u;
    if (charger.target <= 0) {
        enterStep(actor, SkidStep::Skid);
        charger.skidSoundTimer = 0;
    }
    charger.dust = 1;
}
// exe 0x454030: turning to the player's side, +-0x40 a tick; the skid's time from the angle
void skidStart(AnimatedActor& actor, ChargerFields& charger) {
    enterStep(actor, SkidStep::Turn);
    const int d = turnToPlayer(actor);
    int32_t t;
    if (d > 0) {
        setAnim(actor, ChargerAnim::SwerveRight);
        actor.spin.y = 0x40;
        actor.spin.x = 0x20;
        t = d - 0x400;
    } else {
        t = -0x400 - d;
        setAnim(actor, ChargerAnim::SwerveLeft);
        actor.spin.y = int16_t(0xffc0);
        actor.spin.x = int16_t(0xffe0);
    }
    charger.timer = int16_t(t / 32);
    actor.acceleration.z = int16_t(0xffe0);
    charger.target = 0x10;
    charger.heading = actor.rotation.y;
    charger.wallCheck = 1;
    charger.knockDown = 1;
    charger.ledgeCheck = 1;
    skidTurn(actor, charger);
}
// exe 0x454120: the skid: its yaw +-0x30 a tick, its heading by its heading spin, slowing to 0x180
void skid(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.stepCounter == 0) {
        actor.stepCounter = 1;
        actor.spin.y = actor.spin.y <= 0 ? int16_t(0xffd0) : int16_t(0x30);
    }
    addToYaw(actor, actor.spin.y);
    addToHeading(charger, actor.spin.x);
    decelerateToward(actor, 0x180);
    charger.timer -= 1u;
    if (charger.timer <= 0) enterStep(actor, SkidStep::Settle);
    dustOnOddFrames(charger);
    skidSound(actor, charger);
}
// its heading's turn to its yaw: the difference / 24 a tick
int16_t headingStep(const AnimatedActor& actor, const ChargerFields& charger) {
    return int16_t(angleDiff(uint32_t(int32_t(charger.heading)), uint32_t(int32_t(actor.rotation.y))) / 24);
}
// exe 0x4541c0: its heading eased to its yaw -> the charge
void skidSettle(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.stepCounter == 0) {
        const int16_t step = headingStep(actor, charger);
        actor.stepCounter = 1;
        actor.anim += 1u;
        actor.acceleration.z = int16_t(0xfff8);
        actor.spin.y = step;
    } else {
        addToHeading(charger, actor.spin.y);
        accelerateToward(actor, 0x400);
        if (actor.animEnded()) enterMode(actor, ChargerMode::Charge);
    }
    if ((gameState().frame & 3) == 0) charger.dust = 1;
}
// exe 0x454250: braking to 0x100, backing off at -0x100 -> the charge (the skid round's / up close's brake)
void brake(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.stepCounter) {
    case 0:
        actor.stepCounter = 1;
        setAnim(actor, actor.spin.y > 0 ? ChargerAnim::SwerveRightRun : ChargerAnim::SwerveLeftRun);
        actor.acceleration.z = int16_t(0xff80);
        charger.heading = actor.rotation.y;
        return;
    case 1:
        if (decelerateToward(actor, 0x100)) {
            actor.stepCounter = 2;
            actor.velocity.z = int16_t(0xff00);
        }
        [[fallthrough]];
    case 2:
        if (actor.animEnded()) enterMode(actor, ChargerMode::Charge);
        return;
    default: return;
    }
}
// exe 0x453fd0: a wall in front -> the wall bounce (another -> brake) in its first three steps
void skidRound(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.step < uint8_t(SkidStep::Settle)) {
        const int wall = wallAhead(actor, charger);
        if (wall == -1) enterStep(actor, SkidStep::Brake);
        else if (wall == 1) enterMode(actor, ChargerMode::WallBounce);
    }
    switch (SkidStep(actor.step)) {
    case SkidStep::Start: skidStart(actor, charger); break;
    case SkidStep::Turn: skidTurn(actor, charger); break;
    case SkidStep::Skid: skid(actor, charger); break;
    case SkidStep::Settle: skidSettle(actor, charger); break;
    case SkidStep::Brake: brake(actor, charger); break;
    default: notPortedAt(kStoryPort, "0x453fd0: step past [0x88eca4]", actor.step);
    }
    moveAlongHeading(actor, charger, 0, actor.velocity.z);
}

// ---- the wall bounce (exe 0x4542e0, [0x88ecb8]) --------------------------------------------------------------------
// exe 0x454420: turning by its spin for its timer, then the animation's end -> the charge
void bounceTurn(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.stepCounter == 0) {
        addToYaw(actor, actor.spin.y);
        charger.timer -= 1u;
        if (charger.timer == 0) {
            actor.stepCounter = 1;
            actor.anim += 1u;
        }
    } else if (actor.animEnded()) {
        enterMode(actor, ChargerMode::Charge);
    }
    charger.heading = actor.rotation.y;
}
// exe 0x454330: the wall at the target: glancing -> turning away; head-on -> bouncing back
void bounceHit(AnimatedActor& actor, ChargerFields& charger) {
    charger.heading = actor.rotation.y;
    charger.wallCheck = 0;
    charger.knockDown = 1;
    charger.ledgeCheck = 0;
    actor.airborne = 0;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(charger.target)));
    if (uint32_t(d + 0x200) > 0x400u) {
        enterStep(actor, BounceStep::Turn);
        const int v = d <= 0 ? 0x20 : -0x20;
        setAnim(actor, v > 0 ? ChargerAnim::SwerveRight : ChargerAnim::SwerveLeft);
        actor.spin.y = int16_t(v);
        charger.timer = 0x20;
        bounceTurn(actor, charger);
        return;
    }
    enterStep(actor, BounceStep::Bounce);
    const int v = d <= 0 ? 0x600 : -0x600;
    setAnim(actor, v > 0 ? ChargerAnim::BounceRight : ChargerAnim::BounceLeft);
    actor.spin.y = int16_t(int32_t(int16_t(v)) / 24);
    sound(kJetSound, actor);
}
// exe 0x454480: its heading turned by its spin until the animation ends -> the charge along the heading
void bounceBack(AnimatedActor& actor, ChargerFields& charger) {
    addToHeading(charger, actor.spin.y);
    const int16_t heading = charger.heading;
    if (actor.animEnded()) {
        actor.restartStep();
        actor.subState = uint8_t(ChargerMode::Charge);
        actor.rotation.y = heading;
        setAnim(actor, ChargerAnim::Run);
    }
}
// exe 0x4542e0: its speed toward -0x400 by its acceleration
void wallBounce(AnimatedActor& actor, ChargerFields& charger) {
    switch (BounceStep(actor.step)) {
    case BounceStep::Hit: bounceHit(actor, charger); break;
    case BounceStep::Turn: bounceTurn(actor, charger); break;
    case BounceStep::Bounce: bounceBack(actor, charger); break;
    default: notPortedAt(kStoryPort, "0x4542e0: step past [0x88ecb8]", actor.step);
    }
    stepTowardLimit(actor.velocity.z, uint16_t(actor.acceleration.z), 0x400); // exe 0x5e5510
    moveAlongHeading(actor, charger, 0, actor.velocity.z);
}

// ---- up close (exe 0x4544c0, [0x88ecc4]) ---------------------------------------------------------------------------
// exe 0x4545d0: slowing to 0x300 for 8 ticks
void slide(AnimatedActor& actor, ChargerFields& charger) {
    decelerateToward(actor, 0x300);
    if (actor.animEnded()) actor.prevAnim = 0;
    charger.timer -= 1u;
    if (charger.timer == 0) {
        enterStep(actor, CloseStep::Arc);
        charger.skidSoundTimer = 0;
    }
    charger.dust = 1;
}
// exe 0x454520: the player nearly in front -> a 0x60 side jump
void sidestep(AnimatedActor& actor, ChargerFields& charger) {
    enterStep(actor, CloseStep::Slide);
    const int d = turnToPlayer(actor);
    setAnim(actor, d > 0 ? ChargerAnim::SideRight : ChargerAnim::SideLeft);
    actor.spin.y = int16_t(d);
    if (uint32_t(d + 0x40) < 0x80u) {
        if (d > 0) {
            addToYaw(actor, -0x60);
            setAnim(actor, ChargerAnim::SideLeft);
        } else {
            addToYaw(actor, 0x60);
            setAnim(actor, ChargerAnim::SideRight);
        }
    }
    actor.acceleration.z = int16_t(0xfff8);
    charger.timer = 8;
    charger.heading = actor.rotation.y;
    charger.wallCheck = 0;
    charger.knockDown = 1;
    charger.ledgeCheck = 1;
    slide(actor, charger);
}
// exe 0x454620: an arc (its yaw +-0x60 then +-0x20 a tick, its heading by its heading spin) for 0x40 ticks
void arc(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.stepCounter) {
    case 0:
        actor.stepCounter = 1;
        charger.timer = 0x800;
        if (actor.spin.y > 0) {
            actor.spin.y = 0x60;
            actor.spin.x = 0x20;
        } else {
            actor.spin.y = int16_t(0xffa0);
            actor.spin.x = int16_t(0xffe0);
        }
        charger.target = 0x500;
        [[fallthrough]];
    case 1:
        charger.target -= 0x60u;
        if (charger.target <= 0) {
            actor.stepCounter = 2;
            actor.spin.y = actor.spin.y <= 0 ? int16_t(0xffe0) : int16_t(0x20);
        }
        break;
    default: break;
    }
    addToYaw(actor, actor.spin.y);
    addToHeading(charger, actor.spin.x);
    decelerateToward(actor, 0x300);
    if (actor.animEnded()) actor.prevAnim = 0;
    charger.timer -= 0x20u;
    if (charger.timer <= 0) enterStep(actor, CloseStep::Settle);
    dustOnOddFrames(charger);
    skidSound(actor, charger);
}
// exe 0x454730: its heading eased to its yaw -> the run off
void closeSettle(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.stepCounter == 0) {
        actor.stepCounter = 1;
        setAnim(actor, actor.spin.y > 0 ? ChargerAnim::SwerveRightRun : ChargerAnim::SwerveLeftRun);
        actor.acceleration.z = int16_t(0xfff8);
        actor.spin.y = headingStep(actor, charger);
    }
    addToHeading(charger, actor.spin.y);
    accelerateToward(actor, 0x400);
    if (actor.animEnded()) enterStep(actor, CloseStep::RunOff);
    if ((gameState().frame & 3) == 0) charger.dust = 1;
}
// exe 0x4547c0: running on a random 15..45 ticks -> the charge
void runOff(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.stepCounter) {
    case 0: {
        charger.heading = actor.rotation.y;
        actor.stepCounter = 1;
        setAnim(actor, ChargerAnim::Run);
        actor.acceleration.z = int16_t(0xfff8);
        const uint32_t r = rngStep();
        charger.timer = runOffTimes[r & 0xf];
        charger.wallCheck = 1;
        [[fallthrough]];
    }
    case 1:
        if (accelerateToward(actor, 0x400)) {
            actor.stepCounter = 2;
            actor.velocity.z = int16_t(0xfc00);
        }
        [[fallthrough]];
    case 2:
        charger.timer -= 1u;
        if (charger.timer == 0) enterMode(actor, ChargerMode::Charge);
        return;
    default: return;
    }
}
// exe 0x4544c0: a wall (its first three steps) -> brake; its wheels hit on its animation's wheel events
void closeUp(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.step < uint8_t(CloseStep::Settle) && wallAhead(actor, charger) != 0) enterStep(actor, CloseStep::Brake);
    switch (CloseStep(actor.step)) {
    case CloseStep::Sidestep: sidestep(actor, charger); break;
    case CloseStep::Slide: slide(actor, charger); break;
    case CloseStep::Arc: arc(actor, charger); break;
    case CloseStep::Settle: closeSettle(actor, charger); break;
    case CloseStep::Brake: brake(actor, charger); break;
    case CloseStep::RunOff: runOff(actor, charger); break;
    default: notPortedAt(kStoryPort, "0x4544c0: step past [0x88ecc4]", actor.step);
    }
    moveAlongHeading(actor, charger, 0, actor.velocity.z);
    charger.wheelHits = uint8_t(actor.animKey.events & kEventWheel);
}

// ---- the hop (exe 0x454860; its steps by the jump table 0x4549c0) --------------------------------------------------
// up, landing, a bounce at half speed, landing again -> the charge (the circle after a hit while spinning)
void hop(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.step) {
    case 0:
        actor.enterStep(1);
        actor.anim = dataScalar<uint8_t>(stateAddress(TableId::St48ChargerHopAnims) + uint32_t(actor.anim));
        actor.velocity.y = int16_t(0xfe80);
        charger.wallCheck = 0;
        charger.knockDown = 1;
        charger.ledgeCheck = 0;
        actor.rotation.x = 0;
        sound(kHopSound, actor);
        [[fallthrough]];
    case 1:
        actor.velocity.y += 0x40u;
        if (actor.velocity.y > 0x200) actor.enterStep(2);
        [[fallthrough]];
    case 2:
        if (actor.airborne == 0) {
            const int16_t v = int16_t(actor.velocity.y >> 1);
            actor.enterStep(3);
            actor.velocity.y = int16_t(-v);
            actor.airborne = 1;
            sound(kLandSound, actor);
        }
        break;
    case 3:
        actor.velocity.y += 0x40u;
        if (actor.velocity.y > 0x200) actor.enterStep(4);
        [[fallthrough]];
    case 4:
        if (actor.airborne == 0) {
            const uint8_t anim = actor.anim;
            actor.enterStep(5);
            if (anim == uint8_t(ChargerAnim::SwerveRight)) setAnim(actor, ChargerAnim::SwerveRightRun);
            if (actor.anim == uint8_t(ChargerAnim::SwerveLeft)) setAnim(actor, ChargerAnim::SwerveLeftRun);
            charger.wallCheck = 1;
            charger.ledgeCheck = 1;
            sound(kLandSound, actor);
        }
        break;
    case 5:
        if (actor.animEnded())
            enterMode(actor, charger.hitWhileSpinning == 0 ? ChargerMode::Charge : ChargerMode::Circle);
        break;
    default: break;
    }
    moveAlongHeading(actor, charger, actor.velocity.y, actor.velocity.z);
}

// ---- brake and spin (exe 0x4549e0) ---------------------------------------------------------------------------------
void brakeSpin(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.step) {
    case 0: {
        setAnim(actor, ChargerAnim::SwerveRight);
        actor.enterStep(1);
        const uint32_t r = rngStep();
        actor.spin.y = int16_t((int32_t(brakeSpins[r & 7]) + 0x800) >> 5);
        charger.timer = 0x20;
        charger.wallCheck = 0;
        charger.knockDown = 1;
        charger.ledgeCheck = 1;
        if (charger.jetsWord != 0) freeJets(charger); // exe 0x455430
        [[fallthrough]];
    }
    case 1:
        actor.velocity.z += 0x20u;
        addToYaw(actor, actor.spin.y);
        if (actor.velocity.z > 0) actor.velocity.z = 0;
        charger.timer -= 1u;
        if (charger.timer == 0) {
            actor.enterStep(2);
            actor.anim += 1u;
        }
        break;
    case 2:
        if (actor.animEnded()) enterMode(actor, ChargerMode::Circle);
        break;
    default: break;
    }
    moveAlongHeading(actor, charger, actor.velocity.y, actor.velocity.z);
}

// ---- knocked over (exe 0x454ae0, [0x88ed08]) -----------------------------------------------------------------------
// exe 0x454b80: airborne: rising, landing (a thud), the animation's end -> getting up
void knockedAirborne(AnimatedActor& actor) {
    switch (actor.stepCounter) {
    case 0:
        actor.velocity.y += 0x40u;
        if (actor.velocity.y > 0x200) actor.stepCounter = 1;
        [[fallthrough]];
    case 1:
        if (actor.airborne == 0) {
            actor.stepCounter = 2;
            actor.velocity.y = 0;
            sound(kLandSound, actor);
        }
        return;
    case 2:
        if (actor.animEnded()) enterStep(actor, KnockStep::GetUp);
        return;
    default: return;
    }
}
// exe 0x454b20: on the ground -> wait, airborne -> the airborne step
void knockedStart(AnimatedActor& actor, ChargerFields& charger) {
    setAnim(actor, ChargerAnim::KnockedOver);
    actor.rotation.x = 0;
    charger.wallCheck = 0;
    charger.knockDown = 0;
    charger.ledgeCheck = 0;
    if (actor.airborne == 0) {
        enterStep(actor, KnockStep::Wait);
        return;
    }
    enterStep(actor, KnockStep::Airborne);
    knockedAirborne(actor);
}
// exe 0x454be0: righting itself (jets freed; a sound on the wheel event), falling back; dead -> the death, alive ->
// overturned
void getUp(AnimatedActor& actor, ChargerFields& charger) {
    switch (actor.stepCounter) {
    case 0:
        actor.stepCounter = 1;
        setAnim(actor, ChargerAnim::Righting);
        freeJets(charger); // exe 0x455430
        break;
    case 1:
        if (actor.animKey.events & kEventWheel) sound(kRightingSound, actor);
        if (actor.animEnded()) actor.stepCounter = 2;
        break;
    case 2:
        actor.stepCounter = 3;
        setAnim(actor, ChargerAnim::FallBack);
        [[fallthrough]];
    case 3:
        actor.velocity.z += 0x20u;
        if (charger.walls == 0 && actor.velocity.z < 0) break;
        if (actor.hitPoints >= 0) {
            actor.stepCounter = 4;
            setAnim(actor, ChargerAnim::Recover);
            actor.velocity.z = 0;
        } else {
            actor.enterState(uint8_t(ChargerState::Death));
        }
        break;
    case 4:
        if (actor.animEnded()) enterMode(actor, ChargerMode::Overturned);
        break;
    default: break;
    }
    dustOnOddFrames(charger);
}
void knockedOver(AnimatedActor& actor, ChargerFields& charger) { // exe 0x454ae0
    switch (KnockStep(actor.step)) {
    case KnockStep::Start: knockedStart(actor, charger); break;
    case KnockStep::Wait: // exe 0x454b60
        if (actor.animEnded()) enterStep(actor, KnockStep::GetUp);
        break;
    case KnockStep::Airborne: knockedAirborne(actor); break;
    case KnockStep::GetUp: getUp(actor, charger); break;
    default: notPortedAt(kStoryPort, "0x454ae0: step past [0x88ed08]", actor.step);
    }
    moveAlongHeading(actor, charger, actor.velocity.y, actor.velocity.z);
}
// exe 0x454cc0: overturned (its four wheels hit) until dead
void overturned(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.step == 0) {
        actor.step = 1;
        setAnim(actor, ChargerAnim::Overturned);
        charger.wallCheck = 0;
        charger.knockDown = 0;
        charger.ledgeCheck = 0;
    }
    charger.wheelHits = 1;
    if (actor.hitPoints < 0) actor.enterState(uint8_t(ChargerState::Death));
}

// ---- its death (exe 0x454d10, [0x88ed18] by its sub-state) ---------------------------------------------------------
// exe 0x454d40: ten rounds, the defeat counted
void deathCount(AnimatedActor& actor, ChargerFields& charger) {
    actor.subState = uint8_t(DeathStep::Blasts);
    actor.restartStep();
    setAnim(actor, ChargerAnim::FallBack);
    charger.target = 0xa;
    countDefeat(); // exe 0x5e53e0
}
// exe 0x454d70: every 4 ticks a blast at a random bone, a red flash; no lock-on once fewer than 0x19 ticks are left
void deathBlasts(AnimatedActor& actor, ChargerFields& charger) {
    if (actor.step == 0) {
        actor.step = 1;
        const uint32_t bone = rngStep() & 7;
        const uint32_t slot = allocActor60(0); // exe 0x5e1300
        if (slot) {
            const ShortVec4& p = bonePoint(actor, bone, "0x454d70: a bone past its blocks"); // exe 0x5e1810
            StaticActor& blast = staticActorAt(slot);
            blast.flags = kActorLive;
            blast.loopFlags |= kActorSweepGroup2;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.variant = 0;
            blast.pos.x.whole = p.x;
            blast.pos.y.whole = p.y;
            blast.pos.z.whole = p.z;
            const uint32_t r = rngStep();
            BlastParams params(actorHeaderAt(slot));
            params.attack = 1;
            params.size = uint16_t(((r & 3) + 4) << 4);
            actor.tint = kTintBlastFlash;
        }
        charger.timer = 4;
    }
    if (int32_t(charger.timer) + int32_t(charger.target) * 4 < 0x19) charger.noLockOn = 1;
    charger.timer -= 1u;
    if (charger.timer != 0) return;
    charger.target -= 1u;
    if (charger.target != 0) {
        actor.step = 0;
        return;
    }
    actor.subState = uint8_t(DeathStep::End);
    actor.restartStep();
}
// exe 0x454e80: the big blast, the row's drops, freed
void deathEnd(AnimatedActor& actor) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (slot) {
        StaticActor& blast = staticActorAt(slot);
        blast.flags = kActorLive;
        blast.loopFlags |= kActorSweepGroup2;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 1;
        blast.pos = actor.pos;
        BlastParams params(actorHeaderAt(slot));
        params.size = 0x100;
        params.attack = 4;
    }
    spawnDrops(actor.pos, chargerStats(actor).dropTable); // exe 0x5f1270
    freeActor(actor);                                     // exe 0x5e1490
}

// ---- its states [0x88ec4c] -----------------------------------------------------------------------------------------
// exe 0x4539c0
void bind(AnimatedActor& actor, ChargerFields& charger) {
    if (!bindActorModel(actor, actor.spawnByte)) { // exe 0x5e0bc0(actor, &spawnByte)
        freeActor(actor);                          // exe 0x5e1490
        return;
    }
    actor.prevAnim = 0xff;
    setAnim(actor, ChargerAnim::Run);
    const int16_t hp =
        hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type, actor.spawnByte).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    charger.noLockOn = 0;
    actor.collisionBox.pointAt(TableId::St48ChargerBox);
    actor.velocity.z = int16_t(0xfe00);
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(ChargerState::Run));
}
// exe 0x453a60
void runModes(const PlayerEnv& env, AnimatedActor& actor, ChargerFields& charger) {
    if (charger.wallCheck != 0 && wallAhead(actor, charger) > 0) enterMode(actor, ChargerMode::WallBounce);
    if (actor.hitPoints >= 0 && takeHit(actor, charger) != 0 && charger.knockDown != 0)
        enterMode(actor, ChargerMode::KnockedOver);
    switch (ChargerMode(actor.subState)) { // [0x88ec5c]
    case ChargerMode::Circle: circle(actor, charger); break;
    case ChargerMode::Charge: charge(actor, charger); break;
    case ChargerMode::SkidRound: skidRound(actor, charger); break;
    case ChargerMode::WallBounce: wallBounce(actor, charger); break;
    case ChargerMode::CloseUp: closeUp(actor, charger); break;
    case ChargerMode::Hop: hop(actor, charger); break;
    case ChargerMode::BrakeSpin: brakeSpin(actor, charger); break;
    case ChargerMode::KnockedOver: knockedOver(actor, charger); break;
    case ChargerMode::Overturned: overturned(actor, charger); break;
    default: notPortedAt(kStoryPort, "0x453a60: mode past [0x88ec5c]", actor.subState);
    }
    if (collide(env, actor, charger)) {
        actor.airborne = 1;
        enterMode(actor, ChargerMode::Hop);
    }
}
// exe 0x454d10
void death(const PlayerEnv& env, AnimatedActor& actor, ChargerFields& charger) {
    actor.tint = kTintNeutral;
    switch (DeathStep(actor.subState)) { // [0x88ed18]
    case DeathStep::Count: deathCount(actor, charger); break;
    case DeathStep::Blasts: deathBlasts(actor, charger); break;
    case DeathStep::End: deathEnd(actor); break;
    default: notPortedAt(kStoryPort, "0x454d10: step past [0x88ed18]", actor.subState);
    }
    collide(env, actor, charger);
}
void chargerTick(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4538c0
    ChargerFields& charger = classState<ChargerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (ChargerState(actor.state)) { // [0x88ec4c]
    case ChargerState::Bind: bind(actor, charger); break;
    case ChargerState::Run: runModes(env, actor, charger); break;
    case ChargerState::Death: death(env, actor, charger); break;
    case ChargerState::Idle: break; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x4538c0: state past [0x88ec4c]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);                // exe 0x5e1c30
    if ((actor.animKey.events & kEventGround) && actor.airborne == 0) sound(kWheelSound, actor);
    if (actor.animKey.events & kEventRattle) sound(kRattleSound, actor);
    // the shadow (exe 0x5f20f0(actor, 0xc0, 0x80, 0)) -- render output
    submitHits(actor, charger);
    if (charger.dust != 0) {
        raiseDust(actor);
        charger.dust = 0;
    }
}

} // namespace

void chargerUpdate(const PlayerEnv& env, uint32_t actor) { chargerTick(env, animatedActorAt(actor)); }

} // namespace od::port
