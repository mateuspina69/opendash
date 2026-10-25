// The fire ruin's flyer (ST28 pool 0x20 type 0x10 sub 0, exe 0x46c640; hypothesis names).
// States [0x8901f4]: 0 bind 0x46c6f0 (placed around the player like a field encounter), 1 main 0x46c800, 2 freed
// 0x468fa0; its animation runs every tick (even after a free, as the exe). The main state takes the player's hits (dead:
// the explosion; else its stun count down: stunned), then its sub-state [0x890200]: 0 appear behind the player 0x46c960
// (freed over a cell that is no plain floor), 1 fly 0x46ca70, 2 explode 0x46ceb0, 3 stunned 0x46cf60; then its hit
// entry (none once kFlyerNoHit; dead: a bump of no damage -- the exe zeroes the attack in its type's shared row).
// The fly's steps: 0 / 1 sway and bob above the ground (on screen: 600 ticks, then away at a wing beat), over the
// player off a landing spot: 3 the stunned drop (once: its drops), 4 the pickups, 5 / 6 / 2 swooping away and freed.
// Deviation: the low halves of the pickups' launch point (a stack local the exe leaves uninitialised) are 0.
// The shadow (exe 0x5f20f0) is render output.
#include "content/saul_kada/st28_flyer.hpp"
#include "content/saul_kada/st28_flyer_state.hpp"

#include "content/saul_kada/field_reaverbot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/room_records.hpp"

namespace od::port {

using namespace st28_flyer_state;

namespace {

inline constexpr const char* kEnemyPort = "st28 flyer port"; // the area of its port-gap errors

enum class FlyerState : uint8_t { Bind = 0, Main = 1, Free = 2 };
enum class FlyerSub : uint8_t { Appear = 0, Fly = 1, Explode = 2, Stunned = 3 };
enum class FlyStep : uint8_t { Sway = 0, Patrol = 1, Away = 2, Drop = 3, Pickups = 4, Leave = 5, Turn = 6 };
enum class FlyerAnim : uint8_t { Hover = 0, Fly = 1, Stunned = 2, Hit = 3, Bind = 4 };
constexpr uint16_t kFlyerHarmless = 0x001;   // dead: its entry a harmless bump
constexpr uint16_t kFlyerStunned = 0x002;
constexpr uint16_t kFlyerHidden = 0x004;     // not yet appeared: its entry does not count as a near enemy
constexpr uint16_t kFlyerDropped = 0x020;    // its stunned drop done
constexpr uint16_t kFlyerDying = 0x040;
constexpr uint16_t kFlyerLeaving = 0x080;
constexpr uint16_t kFlyerBobFlip = 0x100;
constexpr uint16_t kFlyerNoHit = 0x200;      // no hit entry
constexpr uint8_t kNoAnim = 0xff;
constexpr uint16_t kHurtSound = 0x8d, kStunSound = 0x8e;
constexpr int16_t kSwayStep = 0x66, kBobStep = 0xcc;
constexpr int kPlayerReach = 0x5db;
constexpr uint16_t kExplosionTint = 0x421f;
// the field reaverbot it drops (variant 4, mode 3)
constexpr uint8_t kReaverbotVariant = 4, kReaverbotMode = 3;
const DataTable<ScalarTable<uint8_t, 16>> dropRolls{TableId::St28FlyerDropRolls}; // by a CRT roll: <= 5 pickups, 6 / 7 a reaverbot
const DataTable<ScalarTable<uint8_t, 4>> reaverbotSpawnBytes{TableId::St28FlyerReaverbotSpawnBytes}; // by its own spawn byte
const DataTable<ScalarTable<uint8_t, 16>> pickupKinds{TableId::St28FlyerPickupKinds}; // by the pickups' countdown / 2
// the landing segments' list (exe data at [0x890268]): a count, then the segments' pool indices
// the landing segment list's address (exe data 0x890268; the list is game state, filled at run time)
const DataTable<ScalarTable<uint32_t, 1>> landingSegments{TableId::St28FlyerLandingSegments};

int ground(const PlayerEnv& env, const AnimatedActor& actor) { return actorGround(env, actor); } // exe 0x40f190

// exe 0x46c6f0: the model (its spawn byte's variant), the hit points of its row x 8, the stun count, placed around the
// player as a field encounter
void bind(AnimatedActor& actor, FlyerFields& flyer) {
    if (!bindActorModel(actor, actor.spawnByte)) { // exe 0x5e0bc0(actor, &spawnByte)
        freeActor(actor);
        return;
    }
    actor.anim = uint8_t(FlyerAnim::Bind);
    actor.prevAnim = kNoAnim;
    flyer.animEntry = 0;
    const bool hasRow = hasAnimatedStats(actor.type);
    const uint16_t hp = hasRow ? uint16_t(uint32_t(animatedStats(actor.type).hitPoints) << 3) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    if (!hasRow) notPorted(kEnemyPort, "0x46c6f0: null class row (the exe reads row +8)");
    flyer.stunHits = animatedStats(actor.type).bodyAttack; // the row's byte 8: its stun count
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    flyer.flags = kFlyerHidden;
    flyer.hitBox.result = 0;
    flyer.swayAngle = 0;
    flyer.bobAngle = 0;
    flyer.bobCarry = 0;
    flyer.spin = 0x20;
    int16_t pitch = 0, yaw = 0;
    const int16_t distance = int16_t(uint16_t(uint32_t(encounterFieldBearing(actor, pitch, yaw)))); // exe 0x5e6040
    encounterPlaceAroundPlayer(actor, distance, pitch, yaw);                                        // exe 0x5e6140
    actor.subState = uint8_t(FlyerSub::Appear);
    actor.runFlags |= kActorRunMarked;
    actor.state = uint8_t(FlyerState::Main);
}
// exe 0x46c960: behind the player (3 x 0x7ff0 back, 0x2000 up), facing him; freed over a cell that is no plain floor
void appear(AnimatedActor& actor, FlyerFields& flyer) {
    const Player& p = player();
    int32_t pos[3] = {p.pos.x, p.pos.y, p.pos.z};
    for (int i = 0; i < 3; ++i) { // exe 0x5e36f0
        int32_t d[3];
        rotateByYaw(p.rotation.y, 0, -0x2000, -0x7ff0, d);
        for (int k = 0; k < 3; ++k) pos[k] = int32_t(uint32_t(pos[k]) + uint32_t(d[k]));
    }
    actor.pos.x = pos[0];
    actor.pos.y = pos[1];
    actor.pos.z = pos[2];
    actor.rotation.y = int16_t(uint16_t(int32_t(p.rotation.y) + 0x800));
    const uint32_t cell = gridCellAtPoint(actor.pos.x.whole, actor.pos.z.whole); // exe 0x40f120
    if (cell != 0) {
        const uint16_t word = GridCell(cell).word;
        if (!(word & kCellCollides) || !(word & kCellFloor) || (word & kCellSpecial)) {
            freeActor(actor);
            return;
        }
    }
    actor.subState = uint8_t(FlyerSub::Fly);
    flyer.setAppearVelocity();
    flyer.flags ^= kFlyerHidden;
    actor.flags |= kActorDrawn;
}
// exe 0x46d1c0(segment): the segment's pool index in the landing list -> 0..3 (0x40 / 0x41 / 0x42 its kinds 1..3), else -1
int landingSegmentKind(const SegmentEntry& segment) {
    uint32_t p = landingSegments[0];
    const int8_t n = dataScalar<int8_t>(p);
    ++p;
    for (int8_t c = 0; c < n; ++c, ++p) {
        const uint8_t kind = dataScalar<uint8_t>(p);
        if (segment.poolIndex != kind) continue;
        if (kind == 0x40) return 1;
        if (kind == 0x41) return 2;
        return kind == 0x42 ? 3 : 0;
    }
    return -1;
}
// exe 0x46d130: the cell under it is a landing spot (floor kinds 5..8, or a listed segment)
bool overLandingSpot(const PlayerEnv& env, const AnimatedActor& actor) {
    ground(env, actor); // exe 0x40f190 (its result is the unused y of the probe point)
    const uint32_t cell = gridCellOf(actor.pos.x.whole, actor.pos.z.whole); // exe 0x40f0b0
    if (cell == 0) return false;
    const uint16_t word = GridCell(cell).word;
    if (word & kCellFloor) {
        const uint32_t kind = word & kCellSegmentNumberMask;
        return kind >= 5 && kind < 9;
    }
    return landingSegmentKind(segmentEntry(word)) >= 0;
}
// exe 0x46cfb0: the stunned drop by a CRT roll: pickups (true), or a field reaverbot at it
bool rollDrop(const AnimatedActor& actor) {
    const uint8_t roll = dropRolls[crtRand() & 0xf];
    if (roll <= 5) return true;
    if (roll == 6 || roll == 7) {
        const uint32_t slot = allocActor20(); // exe 0x5e1160
        if (!slot) return false;
        AnimatedActor& reaverbot = animatedActorAt(slot);
        FieldReaverbotParams params(animatedActorAt(slot));
        reaverbot.flags = kActorLive;
        reaverbot.type = kFieldReaverbotType;
        reaverbot.subtype = 0;
        reaverbot.variant = kReaverbotVariant;
        params.mode = kReaverbotMode;
        reaverbot.spawnByte = reaverbotSpawnBytes[actor.spawnByte];
        params.second = 0;
        reaverbot.loopFlags |= kActorSweepGroup2;
        params.flag = kFieldReaverbotNoFlag;
        reaverbot.pos = actor.pos;
    }
    return false;
}
// exe 0x46d0a0(pos, k): a pickup (kind [0x890224 + k]) launched near the point (a CRT roll's 64-step sine / cosine)
void dropPickup(const AnimatedActor& actor, uint32_t k) {
    const uint32_t r = crtRand() & 0xf;
    const Trig64Tables& trig = trig64Tables();
    const int32_t pos[3] = {
        int32_t(uint32_t(uint16_t((trig.sine[r * 4] >> 6) + actor.pos.x.whole)) << 16),
        int32_t(uint32_t(uint16_t(actor.pos.y.whole)) << 16),
        int32_t(uint32_t(uint16_t((trig.cosine[r * 4] >> 6) + actor.pos.z.whole)) << 16),
    };
    spawnLaunchedPickup(pos, 0, 0, 0, pickupKinds[k & 0xff]); // exe 0x5f1400
}
// the sway and the bob (exe 0x46ca70's inline part): the sway angle on, the bob angle (first: down, or the flip),
// moved by their sines and its speed
void swayAndBob(AnimatedActor& actor, FlyerFields& flyer, bool first) {
    const uint32_t sway = (uint32_t(int32_t(flyer.swayAngle)) + uint32_t(kSwayStep)) & 0xfff;
    flyer.swayAngle = int16_t(sway);
    const int16_t sx = fixedSin(int(sway));
    if (first) {
        if (flyer.bobAngle >= 0) flyer.bobAngle = int16_t((uint32_t(int32_t(flyer.bobAngle)) - uint32_t(kSwayStep)) & 0xfff);
        else flyer.flags ^= kFlyerBobFlip;
    }
    const uint8_t carry = uint8_t(flyer.bobCarry + 8u);
    const uint32_t bob = (uint32_t(int32_t(flyer.bobAngle)) + uint32_t(kBobStep)) & 0xfff;
    flyer.bobAngle = int16_t(bob);
    flyer.bobCarry = carry;
    if (carry >= 10) {
        flyer.bobAngle = int16_t(uint16_t(bob + 1u));
        flyer.bobCarry = uint8_t(carry % 10);
    }
    const int16_t sy = fixedSin(int(uint32_t(int32_t(flyer.bobAngle))));
    integrateVelocity(actor, sx >> 4, sy >> 4, actor.velocity.z);
}
// exe 0x46ca70
void fly(const PlayerEnv& env, AnimatedActor& actor, FlyerFields& flyer) {
    if (actor.step <= uint8_t(FlyStep::Patrol)) { // hovering 0x2ee..0x353 above the ground
        const int16_t height = int16_t(uint16_t(ground(env, actor) - actor.pos.y.whole));
        if (height >= 0x2ee) actor.velocity.y = height >= 0x353 ? 0xa0 : 0;
        else actor.velocity.y = int16_t(0xff60);
        integrateVelocity(actor, 0, actor.velocity.y, 0);
    }
    switch (FlyStep(actor.step)) {
    case FlyStep::Sway:
        swayAndBob(actor, flyer, true);
        if (!(actor.runFlags & kActorOffScreen)) {
            actor.runFlags &= uint8_t(~kActorRunMarked);
            flyer.timer = 600;
            ++actor.step;
        }
        return;
    case FlyStep::Patrol:
        if (!(actor.runFlags & kActorOffScreen)) {
            if (flyer.timer != 0) {
                flyer.timer -= 1u;
            } else if (actor.animKey.frame == 10 || actor.animKey.frame == 0x1e) {
                actor.step = uint8_t(FlyStep::Leave);
                flyer.flagsLow |= uint8_t(kFlyerLeaving);
            }
            if (!overLandingSpot(env, actor) && planarDistance(player().pos, actor.pos) <= kPlayerReach) {
                actor.step = uint8_t(FlyStep::Drop);
                actor.anim = uint8_t(FlyerAnim::Hover);
                flyer.flagsLow |= uint8_t(kFlyerStunned);
                flyer.animEntry = 0;
            }
        }
        swayAndBob(actor, flyer, false);
        return;
    case FlyStep::Away:
        if (flyer.timer == 0) {
            actor.state = uint8_t(FlyerState::Free);
            return;
        }
        integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
        actor.velocity.y = int16_t(uint16_t(int32_t(actor.velocity.y) + int32_t(actor.acceleration.y)));
        flyer.timer -= 1u;
        actor.rotation.y = int16_t(uint16_t(int32_t(actor.rotation.y) + int32_t(flyer.spin)));
        if (flyer.spin <= 0x1ff) flyer.spin += 8u;
        return;
    case FlyStep::Drop:
        if (!actor.animEnded()) {
            turnToward(actor.rotation.y, int(angleOf(player().pos, actor.pos)), 0x40);
            return;
        }
        if (!(flyer.flagsLow & kFlyerDropped)) {
            flyer.timer = 0x1e;
            ++actor.step;
            flyer.dropsPickups = rollDrop(actor) ? 1 : 0;
            return;
        }
        actor.velocity.z = 0;
        actor.step = uint8_t(FlyStep::Leave);
        flyer.flagsLow |= uint8_t(kFlyerLeaving);
        flyer.timer = 0x5a;
        if (flyer.flags & kFlyerStunned) flyer.flags ^= kFlyerStunned;
        return;
    case FlyStep::Pickups: {
        flyer.timer -= 1u;
        const uint16_t left = uint16_t(flyer.timer);
        if (left == 0) {
            actor.anim = uint8_t(FlyerAnim::Fly);
            flyer.flagsLow |= uint8_t(kFlyerDropped);
            flyer.animEntry = 0;
            actor.step = uint8_t(FlyStep::Drop);
            return;
        }
        if (flyer.dropsPickups != 0 && (left & 1)) dropPickup(actor, uint32_t(int32_t(int16_t(left)) / 2));
        return;
    }
    case FlyStep::Leave:
        actor.step = uint8_t(FlyStep::Turn);
        actor.velocity.y = int16_t(0xff60);
        actor.velocity.z = 0;
        actor.acceleration.y = int16_t(0xffe0);
        flyer.timer = 0x5a;
        actor.anim = uint8_t(FlyerAnim::Fly);
        flyer.animEntry = 0x19;
        return;
    case FlyStep::Turn:
        actor.rotation.y = int16_t(uint16_t(int32_t(actor.rotation.y) + int32_t(flyer.spin)));
        if (flyer.spin <= 0xff) {
            flyer.spin += 8u;
            return;
        }
        actor.step = uint8_t(FlyStep::Away);
        return;
    default: return;
    }
}
// exe 0x46ceb0: an explosion at its bone 0, a defeat counted; it blinks for 0x1e ticks, then freed
void explode(AnimatedActor& actor, FlyerFields& flyer) {
    if (actor.step == 0) {
        const ShortVec4* bone = bonePointOrNull(actor, 0); // exe 0x5e1810
        if (const uint32_t slot = allocActor60(0)) {        // exe 0x5e1300
            if (!bone) notPorted(kEnemyPort, "0x46ceb0: exe 0x5e1810 returned null (bone 0)");
            StaticActor& explosion = staticActorAt(slot);
            const ShortVec4& point = *bone;
            explosion.flags = kActorLive;
            explosion.type = typeByte(GlobalStaticType::Explosion);
            explosion.variant = 0;
            explosion.loopFlags |= kActorSweepGroup2;
            explosion.pos.x.whole = point.x;
            explosion.pos.y.whole = point.y;
            explosion.pos.z.whole = point.z;
            ExplosionParams params(actorHeaderAt(slot));
            params.spread = 0x60;
            params.flashes = 0x1e;
        }
        ++actor.step;
        flyer.timer = 0x1e;
        countDefeat();
        return;
    }
    if (actor.step != 1) return;
    if (flyer.timerLow & 1) actor.tint = kExplosionTint;
    if (flyer.timer != 0) {
        flyer.timer -= 1u;
        return;
    }
    actor.state = uint8_t(FlyerState::Free);
}
// exe 0x46cf60: stunned until its animation ends; the stun count back from its row
void stunned(AnimatedActor& actor, FlyerFields& flyer) {
    if (!actor.animEnded()) return;
    const uint8_t hits = animatedStats(actor.type).bodyAttack; // the row's byte 8: its stun count
    flyer.flags ^= kFlyerStunned;
    flyer.stunHits = hits;
    actor.anim = uint8_t(FlyerAnim::Bind);
    flyer.animEntry = 0;
    actor.subState = uint8_t(FlyerSub::Fly);
}
// exe 0x46c800
void mainState(const PlayerEnv& env, AnimatedActor& actor, FlyerFields& flyer) {
    if (flyer.hitBox.result & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        if (hitPointsAdd(actor, -int(flyer.hitBox.result & kHitDamageMask))) {
            if (!(flyer.flagsLow & kFlyerDying)) {
                actor.subState = uint8_t(FlyerSub::Explode);
                actor.step = 0;
                flyer.flagsLow |= uint8_t(kFlyerHarmless);
                actor.anim = uint8_t(FlyerAnim::Hit);
                flyer.flags |= kFlyerDying;
                flyer.animEntry = 0;
            }
        } else {
            flyer.stunHits -= 1u;
            uint16_t sound;
            if (int16_t(flyer.stunHits) != 0 || (flyer.flagsLow & kFlyerStunned)) {
                sound = kHurtSound;
            } else {
                actor.anim = uint8_t(FlyerAnim::Stunned);
                flyer.flags |= kFlyerStunned;
                flyer.animEntry = 0;
                actor.subState = uint8_t(FlyerSub::Stunned);
                sound = kStunSound;
            }
            soundPlayAtPos(sound, actor.pos);
        }
    }
    switch (FlyerSub(actor.subState)) {
    case FlyerSub::Appear: appear(actor, flyer); break;
    case FlyerSub::Fly: fly(env, actor, flyer); break;
    case FlyerSub::Explode: explode(actor, flyer); break;
    case FlyerSub::Stunned: stunned(actor, flyer); break;
    default: notPortedAt(kEnemyPort, "0x46c640 state 1 sub-state (table 0x890200 has 4)", actor.subState);
    }
    const uint16_t flags = flyer.flags;
    if (flags & kFlyerNoHit) return;
    AnimatedActorStats stats = animatedStats(actor.type);
    uint32_t attack;
    if (flags & kFlyerHarmless) {
        stats.attack = 0; // the exe zeroes the contact damage in its type's shared row
        attack = kHitAttackBump;
    } else {
        attack = ((flags & kFlyerHidden) ? kHitAttackHarmless : 0) | kHitAttackSolid;
    }
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole - 100)), actor.pos.z.whole};
    flyer.hitBox.previousX = actor.prevPos.x.whole;
    flyer.hitBox.previousZ = actor.prevPos.z.whole;
    flyer.hitBox.result = 0;
    const uint32_t shape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) |
                           kHitContactMotion | hitRadius(0x90);
    hitListSubmit(pos, flyer.hitBox, shape, uint32_t(stats.attack) | attack); // exe 0x5e4120
}
void flyer(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x46c640
    FlyerFields& fields = classState<FlyerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (FlyerState(actor.state)) {
    case FlyerState::Bind: bind(actor, fields); break;
    case FlyerState::Main: mainState(env, actor, fields); break;
    case FlyerState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kEnemyPort, "0x46c640 state (table 0x8901f4 has 3)", actor.state);
    }
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, fields.animEntry); // exe 0x5e1c30
    else animStepPose(actor);                                                           // exe 0x5e1e80
    // the shadow (exe 0x5f20f0(actor, 0x60, 0xc0, 0) when drawn) is render output
}

} // namespace

void flyerUpdate(const PlayerEnv& env, uint32_t actor) { flyer(env, animatedActorAt(actor)); }

} // namespace od::port
