// The enemies of the licence exam stage (ST55; they also appear in the first dungeon ST12 / ST13 / ST14 and the
// sub-ruins): see exam_enemies_a.hpp.
//
//   exe 0x48e750  pool 0x20 type 0x2a: the small liftable reaverbot (states [0x892924]: 0 init, 1 appear by its
//                 variant, 2 main (sub-states [0x89293c]), 3 carried / thrown [0x892980], 4 dying, 5 freed)
//   exe 0x47a600  pool 0x20 type 0x19: the shelled reaverbot that flips onto its back (states [0x891044]: 0 init,
//                 1 main (18 sub-states [0x891054]), 2 carried / thrown / rolling [0x891090], 3 dying)
//   exe 0x44e4c0  pool 0x20 type 4: the shield-bearing gunner (states [0x88e98c]: 0 init, 1 main (20 sub-states
//                 [0x88e9a4]), 2 carried / thrown [0x88e9f8], 3 death [0x88ea00]); its shots go to pool 0x40 (the ST55
//                 table: type 0x22 = exe 0x5cc220), the knocked-off shield to static type 5
//   exe 0x5cc220  pool 0x40 ST55 stage type 0x22: the gunner's shot (and its glow billboard)
//   exe 0x592fa0  ST55 static type 5 sub 0: the shield knocked off the gunner
//
// Left out (render output): the shadows (exe 0x5f20f0).
#include "content/manda/exam_enemies_a.hpp"
#include "content/manda/exam_enemies_a_state.hpp"

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/encounter.hpp"
#include "game/enemy_motion.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/line_of_sight.hpp"
#include "game/pickups.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/territory.hpp"
#include "game/vectors.hpp"
#include "game/visibility.hpp"

namespace od::port {


namespace {

constexpr uint16_t kTintHurtBlink = 0x4214, kTintDeathBlink = 0x421f;
constexpr uint16_t kSoundHurt = 0x8d, kSoundKnocked = 0x8e;

// the copies at the top of the classes' updates (the previous pose, the airborne / platform bytes)
void savePrevious(Actor& a) {
    a.savePreviousPose();
    a.savePreviousContact();
}
// the animation tail of the pool-0x20 enemies (exe 0x5e1ea0 / 0x5e1c50, then the pose)
void animateFromKey0(AnimatedActor& a) {
    if (a.anim == a.prevAnim) animStep(a);
    else animSet(a, a.anim, 0);
    animPose(a);
}
// the row of the type's stats by its spawn byte (0x10 bytes each)
AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
// the type's hit points by its spawn byte (0 without a row)
uint16_t rowHitPoints(const AnimatedActor& a) { return hasAnimatedStats(a.type) ? statsOf(a).hitPoints : 0; }
// a death explosion (the global static type 8) at the point
void explosionAt(int16_t x, int16_t y, int16_t z, uint16_t spread, uint16_t flashes) {
    const uint32_t address = allocActor60(0); // exe 0x5e1300
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    e.flags = kActorLive;
    e.type = typeByte(GlobalStaticType::Explosion);
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x.whole = x;
    e.pos.y.whole = y;
    e.pos.z.whole = z;
    ExplosionParams params(e);
    params.spread = spread;
    params.flashes = flashes;
}
// bone k's point (exe 0x5e1810; a null result would crash the exe)
ShortVec4& bonePointOf(const AnimatedActor& a, uint32_t k) { return bonePoint(a, k, kStoryPort); }
int playerAngle(const AnimatedActor& a) { return angleOf(player().pos, a.pos); }       // exe 0x5e3bc0(player, actor)
int playerDistance(const AnimatedActor& a) { return planarDistance(player().pos, a.pos); } // exe 0x5e3b00

// ==== the small liftable reaverbot (exe 0x48e750, pool 0x20 type 0x2a) ==============================================
namespace hopper {

using exam_enemies_a_state::hopper::Fields;

enum class State : uint8_t {
    Init = 0,    // exe 0x48e940
    Appear = 1,  // exe 0x48ea10
    Main = 2,    // exe 0x48eba0
    Carried = 3, // exe 0x48f530
    Dying = 4,   // exe 0x48f7f0
    Free = 5,    // exe 0x468fa0
};
enum class Sub : uint8_t {
    Idle = 0,        // exe 0x48ec90
    Forward = 1,     // exe 0x48ed20
    CurveLeft = 2,   // exe 0x48ed70
    CurveRight = 3,
    Look = 4,        // exe 0x48ee10
    WallTurn = 5,    // exe 0x48f180
    Fall = 6,        // exe 0x48f320
    Approach = 7,    // exe 0x48ef60
    Jump = 8,        // exe 0x48f020
    Bounce = 9,      // exe 0x48f230
    Hurt = 0xa,      // exe 0x48f490
    Knock = 0xb,     // exe 0x48f380
    HurtLevel = 0xc, // exe 0x48f490
};
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1 }; // exe 0x48f550 / 0x48f670

// +0x161
constexpr uint8_t kContactWallX = 0x01, kContactWallZ = 0x02, kContactLanded = 0x04, kContactGroundLost = 0x08;
constexpr uint8_t kContactCarried = 0x10, kContactActor = 0x20;
constexpr uint8_t kContactWalls = kContactWallX | kContactWallZ;
// +0x162
constexpr uint8_t kModeNoHitEntry = 0x01, kModeNoDamage = 0x02, kModeNoWallTurn = 0x04, kModeDeadInAir = 0x08;
constexpr uint8_t kModeThrown = 0x10, kModeStrong = 0x20, kModeBounces = 0x40, kModeNotTarget = 0x80;
// its row: the stats and its hit entries' sizes
struct Row : DataRecord {
    static constexpr uint32_t kBytes = 0xb;
    uint8_t hitPoints = 0; // +0x0
    uint8_t attack = 0; // +0x1
    uint8_t strongAttack = 0; // +0x2
    uint8_t thrownAttack = 0; // +0x3
    uint8_t hitHeight = 0; // +0x4
    uint8_t dropTable = 0; // +0x5
    uint8_t radius = 0; // +0x8
    uint8_t strongRadius = 0; // +0x9
    uint8_t thrownRadius = 0; // +0xa
    template <class R> static Row load(const R& r) {
        Row v;
        v.hitPoints = r.template read<uint8_t>(0x0);
        v.attack = r.template read<uint8_t>(0x1);
        v.strongAttack = r.template read<uint8_t>(0x2);
        v.thrownAttack = r.template read<uint8_t>(0x3);
        v.hitHeight = r.template read<uint8_t>(0x4);
        v.dropTable = r.template read<uint8_t>(0x5);
        v.radius = r.template read<uint8_t>(0x8);
        v.strongRadius = r.template read<uint8_t>(0x9);
        v.thrownRadius = r.template read<uint8_t>(0xa);
        return v;
    }
};
Row rowOf(const AnimatedActor& a) { return dataRecord<Row>(animatedStatRow(a.type, a.spawnByte), Row::kBytes); }

const DataTable<CollisionBoxRecord> box{TableId::HopperBox};
const DataTable<InteractionBoxRecord> liftBox{TableId::HopperLiftBox};
const DataTable<ScalarTable<uint8_t, 0x10>> thinkSubs{TableId::HopperThinkSubs}; // the behaviour picked (by the CRT rand)
const DataTable<ScalarTable<int8_t, 0x10>> wallTurnRates{TableId::HopperWallTurnRates};
const DataTable<ScalarTable<int16_t, 4>> throwSpreads{TableId::HopperThrowSpreads};
constexpr uint16_t kSoundJump = 0x11c;
constexpr uint32_t kVariantAttackFlag = 0x8000; // the attack word's flag of its spawn bytes but 0 (its reader is not
                                                // traced)

void toSub(AnimatedActor& a, Sub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
// exe 0x48fa10: the walls / the territory / the ground; true: the ground lost (more than 0x10 below)
bool collide(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    f.contact = 0;
    uint32_t r;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);             // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0x300) f.contact |= kContactWallX;
        if (r & 0xc00) f.contact |= kContactWallZ;
        if ((f.mode & kModeBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.contact |= kContactLanded;
    } else {
        const int rect = territoryViolated(a.pos); // exe 0x5e6ab0
        r = uint32_t(rect);
        if (rect != 0) {
            r = territoryPushBack(a, rect); // exe 0x5e6b40
            if (r & 0x300) f.contact |= kContactWallX;
            if (r & 0xc00) f.contact |= kContactWallZ;
        }
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne == 0 && a.onPlatform == 0) {
        if (g - a.pos.y.whole > 0x10) {
            f.contact |= kContactGroundLost;
            return true;
        }
        a.pos.y.whole = int16_t(g);
        return false;
    }
    if (!(f.mode & kModeBounces) && (r & kSolidAboveOrBelow)) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        f.contact |= kContactLanded;
    }
    return false;
}
// exe 0x48fb40: the actor-collision result
void actorContact(AnimatedActor& a, Fields& f) {
    const uint32_t v = a.actorHit;
    if (v != 0) {
        if (v & 0xf00) f.contact |= kContactWalls | kContactActor;
        if ((f.mode & kModeBounces) && reflectRotationOffWalls(a.rotation, v) != 0) f.contact |= kContactLanded;
    }
    if (a.onPlatform != 0) f.contact |= kContactCarried;
}
// exe 0x48f790(actor, yaw): the ballistic step along the yaw; landed: a half bounce, true
bool fly(AnimatedActor& a, Fields& f, int yaw) {
    if (f.contact & kContactLanded) {
        const int vy = (-int(a.velocity.y)) >> 1;
        a.velocity.z = int16_t(a.velocity.z >> 1);
        a.velocity.y = int16_t(vy);
        a.rotation.x = 0;
        a.airborne = 1;
        return true;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    return false;
}
// exe 0x48ecd0: every 0x1e ticks a random behaviour
void think(AnimatedActor& a, Fields& f) {
    const uint8_t c = f.thinkTimer;
    f.thinkTimer = uint8_t(c - 1u);
    if (c != 0) return;
    f.thinkTimer = 0x1e;
    const uint32_t r = crtRand();
    a.restartStep();
    a.subState = thinkSubs[r & 0xf];
    f.side = uint16_t(crtRand() & 1);
    a.runFlags &= uint8_t(~kActorRunMarked);
}
// exe 0x48f930: the hits -> the knock-backs, the hit points; dead -> sub-state 0xa
void damage(AnimatedActor& a, Fields& f) {
    const uint32_t v = f.hitBox.result;
    const uint32_t d = v & kHitDamageMask;
    if (d == 0) return;
    uint32_t amount = 0;
    if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
        f.turn = f.hitBox.contact.x;
        a.tint = kTintFlash;
        const uint8_t c = uint8_t(f.hitsLeft - 1u);
        f.hitsLeft = c;
        if (int8_t(c) <= 0) {
            f.hitsLeft = 2;
            toSub(a, Sub::Knock);
        }
        toSub(a, a.airborne != 0 ? Sub::Knock : Sub::Hurt);
        amount = d;
    }
    if (v & (kHitLevelAbove | kHitLevelWithin | kHitLevelBelow)) {
        a.restartStep();
        a.tint = kTintFlash;
        f.turn = f.hitBox.contact.x;
        if ((v & kHitLevelBelow) || a.airborne != 0) a.subState = uint8_t(Sub::Knock);
        else if (v & kHitLevelWithin) a.subState = uint8_t(Sub::HurtLevel);
        amount = d;
    }
    if (hitPointsAdd(a, -int(amount)) != 0) toSub(a, Sub::Hurt); // exe 0x5e52f0
}

// ---- the main state's sub-states [0x89293c] ----
void idle(AnimatedActor& a, Fields& f) {
    f.mode = 0;
    if (a.step == 0) {
        a.step = 1;
        a.anim = 0;
        a.velocity.setXY(0, 0);
        a.velocity.setZSpare(0, 0);
        f.thinkTimer = 5;
    }
    think(a, f);
}
void forward(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 1;
        a.enterStep(1);
    }
    a.velocity.setXY(0, 0);
    a.velocity.z = -0x50;
    a.acceleration.z = 0;
    integrateVelocity(a, 0, 0, -0x50);
    think(a, f);
}
// sub-states 2 / 3: a curve to the left / right
void curve(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 1;
        a.enterStep(1);
        const uint8_t s = a.subState;
        a.velocity.setXY(0, 0);
        a.velocity.z = -0x50;
        a.acceleration.z = 0;
        int d = int((crtRand() & 0x1ff) + 0x200) / 30;
        if (s == uint8_t(Sub::CurveLeft)) d = -d;
        f.turn = int16_t(d);
    }
    a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(f.turn));
    integrateVelocity(a, 0, 0, a.velocity.z);
    think(a, f);
}
// looks around (the animations 9.. / 0xb.. / 0xd.. by the side), then: the player in front -> approach
void look(AnimatedActor& a, Fields& f) {
    switch (a.step) {
    case 0:
        f.mode = 0;
        a.anim = 5;
        a.enterStep(1);
        a.velocity.setXY(0, 0);
        a.velocity.z = 0;
        a.acceleration.z = 0;
        f.timer = 0xc;
        f.turn = int16_t(crtRand() & 1);
        [[fallthrough]];
    case 1:
        --f.timer;
        if (f.timer != 0) return;
        f.timer = 0xf;
        a.enterStep(2);
        a.anim = uint8_t(uint8_t(f.turn) + 9u);
        return;
    case 2:
        --f.timer;
        if (f.timer != 0) return;
        f.timer = 0x14;
        a.enterStep(3);
        a.anim = uint8_t(uint8_t(f.turn) + 0xbu);
        return;
    case 3:
        --f.timer;
        if (f.timer != 0) return;
        a.enterStep(4);
        a.anim = uint8_t(uint8_t(f.turn) + 0xdu);
        return;
    case 4: {
        if (!a.animEnded()) return;
        const int d = (playerAngle(a) - int(uint16_t(a.rotation.y))) & kAngleMask; // exe 0x5e3bc0
        toSub(a, (d > 0x600 && d < 0xa00) ? Sub::Idle : Sub::Approach);
        return;
    }
    default: return;
    }
}
// turns away from the wall at a random rate until free
void wallTurn(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        a.enterStep(1);
        f.mode = kModeNoWallTurn;
        const int diff = int(a.rotation.y) - int(a.prevRotation.y);
        const int c = wallTurnRates[crtRand() & 0xf];
        int v = c;
        if (diff == 0) {
            if (uint8_t(f.side) & 1) v = -c;
        } else if (diff < 0) {
            v = -c;
        }
        f.turn = int16_t(v);
        a.velocity.z = -0x50;
        a.anim = 1;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(f.turn));
    if (f.contact & kContactWalls) return;
    toSub(a, a.rotation.y < 0x800 ? Sub::CurveLeft : Sub::CurveRight);
}
void fall(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        f.mode = kModeBounces | kModeDeadInAir | kModeNoWallTurn;
        a.step = 1;
        a.anim = 1;
        a.acceleration.y = 0x40;
    } else if (s != 1) {
        return;
    }
    if (fly(a, f, a.rotation.y)) {
        a.airborne = 0;
        a.velocity.y = 0;
        toSub(a, Sub::Idle);
    }
}
// turns toward the player 0x5a ticks; facing and near -> the jump attack
void approach(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.enterStep(1);
        a.anim = 2;
        a.velocity.setXY(0, 0);
        a.velocity.z = -0x100;
        a.acceleration.z = 0;
        f.timer = 0x5a;
    }
    const int ang = playerAngle(a);                // exe 0x5e3bc0(player, actor)
    turnYawTo(a.rotation.y, uint32_t(ang), 0x44); // exe 0x5e3df0
    integrateVelocity(a, 0, 0, a.velocity.z);
    const int d = (ang - int(uint16_t(a.rotation.y))) & kAngleMask;
    if ((d <= 0x100 || d >= 0xf00) && uint32_t(playerDistance(a)) < 0x12c) {
        toSub(a, Sub::Jump);
        return;
    }
    --f.timer;
    if (f.timer == 0) toSub(a, Sub::Idle);
}
// crouches, leaps (the sound), flies, lands
void jump(AnimatedActor& a, Fields& f) {
    switch (a.step) {
    case 0:
        f.mode = kModeBounces;
        a.anim = 5;
        a.enterStep(1);
        a.velocity.setXY(0, 0);
        a.velocity.z = 0x50;
        a.acceleration.z = 0;
        return;
    case 1:
        if (a.animEnded()) {
            a.enterStep(2);
            a.velocity.z = 0;
            a.anim = 6;
            f.timer = 0xa;
        }
        integrateVelocity(a, 0, 0, a.velocity.z);
        return;
    case 2:
        --f.timer;
        if (f.timer != 0) return;
        a.enterStep(3);
        a.anim = 7;
        a.airborne = 1;
        integrateVelocity(a, 0, -0x5a0, -0x640);
        soundPlayAtPos(kSoundJump, a.pos);
        return;
    case 3:
        a.enterStep(4);
        a.velocity.y = -0xd5;
        a.acceleration.y = 0x47;
        a.velocity.z = -0x370;
        f.mode |= kModeStrong | kModeBounces;
        [[fallthrough]];
    case 4:
        if (fly(a, f, a.rotation.y)) {
            a.enterStep(5);
            a.anim = 8;
            a.airborne = 0;
            a.velocity.y = 0;
            a.acceleration.y = 0;
            f.mode &= uint8_t(~kModeStrong);
        }
        return;
    case 5:
        if (a.animEnded()) toSub(a, Sub::Idle);
        integrateVelocity(a, 0, 0, -0xa0);
        return;
    default: return;
    }
}
void markDeadInAir(const AnimatedActor& a, Fields& f) {
    f.mode = a.hitPoints < 0 ? uint8_t(f.mode | kModeDeadInAir) : uint8_t(f.mode & ~kModeDeadInAir);
}
// bounced back off a wall (two hops)
void bounce(AnimatedActor& a, Fields& f) {
    switch (a.step) {
    case 0:
        a.enterStep(1);
        f.mode = kModeBounces | kModeDeadInAir;
        a.airborne = 1;
        a.anim = 3;
        a.velocity.x = 0;
        a.velocity.z = -0xf0;
        a.velocity.y = -0x1e0;
        a.acceleration.y = 0x78;
        markDeadInAir(a, f);
        [[fallthrough]];
    case 1:
        if (fly(a, f, a.rotation.y)) {
            a.anim = 4;
            ++a.step;
            f.timer = 2;
        }
        return;
    case 2:
        --f.timer;
        if (f.timer != 0) return;
        a.velocity.z = -0xd5;
        a.velocity.y = -0x140;
        ++a.step;
        a.acceleration.y = 0x6a;
        return;
    case 3:
        if (fly(a, f, a.rotation.y)) toSub(a, Sub::Idle);
        return;
    default: return;
    }
}
// sub-states 0xa / 0xc: pushed back along the hit's direction (the sound); dead -> dying
void hurt(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeNoDamage;
        a.enterStep(1);
        a.anim = 4;
        a.velocity.x = 0;
        a.velocity.z = -0x180;
        a.acceleration.z = 0x40;
        if (a.airborne != 0) a.acceleration.y = 0x40;
        else a.velocity.y = 0;
        soundPlayAtPos(kSoundHurt, a.pos);
    }
    moveAlongYaw(a.pos, f.turn, 0, a.velocity.y, a.velocity.z);
    a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
    if (a.velocity.z >= 0) toSub(a, Sub::Idle);
    if (a.hitPoints < 0) a.enterState(uint8_t(State::Dying));
}
// knocked into the air along the hit's direction (the sound), two bounces
void knock(AnimatedActor& a, Fields& f) {
    switch (a.step) {
    case 0:
        a.enterStep(1);
        f.mode = kModeBounces | kModeNoDamage;
        a.anim = 3;
        a.rotation.y = f.turn;
        a.airborne = 1;
        a.velocity.x = 0;
        a.velocity.z = -0x1f4;
        a.velocity.y = -0x320;
        a.acceleration.y = 0x64;
        soundPlayAtPos(kSoundHurt, a.pos);
        markDeadInAir(a, f);
        [[fallthrough]];
    case 1:
        if (fly(a, f, a.rotation.y)) {
            a.anim = 4;
            ++a.step;
            f.timer = 2;
        }
        return;
    case 2:
        --f.timer;
        if (f.timer != 0) return;
        a.velocity.z = -0x1e0;
        a.velocity.y = -0x200;
        ++a.step;
        a.acceleration.y = 0x66;
        return;
    case 3:
        if (fly(a, f, a.rotation.y)) toSub(a, Sub::Idle);
        return;
    default: return;
    }
}

// ---- the states [0x892924] ----
void init(AnimatedActor& a, Fields& f) {
    if (!bindActorModel(a, a.spawnByte)) { // exe 0x5e0bc0(actor, actor + 7)
        freeActor(a);                      // exe 0x5e1490
        return;
    }
    a.anim = 1;
    a.prevAnim = 0xff;
    a.runFlags |= kActorRunMarked;
    const uint16_t hp = rowHitPoints(a);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(box.stateAddress());
    a.interactionBox.pointAt(liftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock();
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    f.hitsLeft = 2;
    a.enterState(uint8_t(State::Appear));
    registerEnemy(a.slot, 0);
}
enum class Appearance : uint8_t { Grounded = 0, Placed = 1, Scattered = 2, Hidden = 3 };
// by its variant: grounded, placed, scattered (a wall: freed), hidden until its group is marked / it is seen
void appear(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.enterStep(1);
        a.anim = 0;
        a.tint = kTintNeutral;
        switch (Appearance(a.variant)) {
        case Appearance::Grounded:
            f.timer = 1;
            a.pos.y.whole = int16_t(actorGround(env, a));
            break;
        case Appearance::Placed: f.timer = 1; break;
        case Appearance::Scattered: {
            f.timer = uint16_t((crtRand() & 0xf) + 1);
            const int r = int((crtRand() & 0x1ff) << 4);
            const int ang = int(crtRand() & kAngleMask);
            int16_t dx, dz;
            angleVector(ang, r, dx, dz); // exe 0x5e40c0
            integrateVelocity(a, dx, 0, dz);
            break;
        }
        default: break;
        }
    }
    collide(env, a, f);
    switch (Appearance(a.variant)) {
    case Appearance::Scattered:
        if (f.contact & kContactWalls) a.enterState(uint8_t(State::Free));
        [[fallthrough]];
    case Appearance::Grounded:
    case Appearance::Placed:
        --f.timer;
        if (f.timer == 0) a.enterState(uint8_t(State::Main));
        return;
    case Appearance::Hidden: {
        if (a.stepCounter == 0) {
            const int32_t group = enemyGroupFindFor(a, 3);                 // exe 0x5e6cd0
            if (enemyGroupMarks(group, EnemyGroupMarks::Either) != 0) { // exe 0x5e6d60
                ++a.stepCounter;
                f.timer = uint16_t((crtRand() & 7) + 1);
                return;
            }
            if (pointVisibleOrNear(a.pos, 0) != 0) return; // exe 0x5e59a0
            a.enterState(uint8_t(State::Main));
            enemyGroupMark(group, 1);
            return;
        }
        --f.timer;
        if (f.timer == 0) a.enterState(uint8_t(State::Main));
        return;
    }
    default: return;
    }
}
void mainState(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    a.tint = kTintNeutral;
    if (a.actorHit != 0) actorContact(a, f);
    if (a.wasOnPlatform != 0 && a.onPlatform == 0 && !(f.mode & kModeDeadInAir)) toSub(a, Sub::Fall);
    switch (Sub(a.subState)) {
    case Sub::Idle: idle(a, f); break;
    case Sub::Forward: forward(a, f); break;
    case Sub::CurveLeft:
    case Sub::CurveRight: curve(a, f); break;
    case Sub::Look: look(a, f); break;
    case Sub::WallTurn: wallTurn(a, f); break;
    case Sub::Fall: fall(a, f); break;
    case Sub::Approach: approach(a, f); break;
    case Sub::Jump: jump(a, f); break;
    case Sub::Bounce: bounce(a, f); break;
    case Sub::Hurt:
    case Sub::HurtLevel: hurt(a, f); break;
    case Sub::Knock: knock(a, f); break;
    default: notPortedAt(kStoryPort, "class 0x48e750 sub-state", a.subState);
    }
    if (!(f.mode & kModeNoDamage)) damage(a, f);
    if (a.hitPoints < 0) return;
    if (collide(env, a, f)) {
        if (!(f.mode & kModeDeadInAir)) {
            a.subState = uint8_t(Sub::Fall);
            a.airborne = 1;
            a.restartStep();
        }
    } else if ((f.contact & kContactWalls) && !(f.mode & kModeNoWallTurn)) {
        if (a.airborne == 0) {
            a.subState = uint8_t(Sub::WallTurn);
        } else {
            reflectRotationOffWalls(a.rotation, 0);
            a.subState = uint8_t(Sub::Bounce);
        }
        a.restartStep();
    }
    if (playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(State::Carried));
        f.mode |= kModeNoHitEntry;
        a.runFlags |= kActorRunMarked;
    }
}
// exe 0x48f670: thrown / dropped -- the hits, the bounces, the landing (main, or dying when dead)
void thrown(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        if (a.hitPoints >= 0) {
            a.velocity.z = int16_t((-int(a.velocity.z)) >> 1);
            a.hitPoints = -1;
            spawnHitSpark(kActorSweepGroup2, a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole - 0x40)), a.pos.z.whole);
            f.mode |= kModeNoHitEntry;
        }
    } else if (a.actorHit != 0) {
        actorContact(a, f);
        a.hitPoints = -1;
    }
    if (fly(a, f, f.throwYaw)) {
        if (a.stepCounter == 0) {
            a.stepCounter = 1;
            a.anim = 0x11;
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
            f.mode &= uint8_t(~kModeThrown);
        } else {
            a.enterState(uint8_t(a.hitPoints >= 0 ? State::Main : State::Dying));
            a.velocity.y = 0;
            a.airborne = 0;
        }
    }
    collide(env, a, f);
    if (f.contact & kContactWalls) {
        a.anim = 3;
        f.throwYaw = a.rotation.y;
        a.velocity.x = 0;
        a.velocity.z = -0xf0;
        a.velocity.y = -0x1e0;
        a.acceleration.y = 0x78;
    }
}
// exe 0x48f550: at the player's hands until thrown (along the player's aim, spread) / dropped
void held(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeBounces | kModeNoHitEntry;
        a.step = 1;
        a.anim = 0xf;
    }
    carryAtHands(a, 0, 0, 0);                 // exe 0x573ab0
    const CarryRelease r = carryReleased();   // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.mode &= uint8_t(~kModeNoHitEntry);
    a.runFlags &= uint8_t(~kActorRunMarked);
    f.turn = player().rotation.y;
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x200, -0x380, v); // exe 0x5e3e50
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        a.anim = 0x10;
        f.throwYaw = f.turn;
        f.turn = int16_t(uint16_t(f.turn) + uint16_t(throwSpreads[crtRand() & 3]));
        f.mode |= kModeThrown;
    } else {
        a.velocity.y = -0x1e0;
        a.velocity.z = -0x40;
    }
    a.acceleration.y = 0x40;
    a.rotation.y = f.turn;
    a.airborne = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(CarriedSub::Thrown);
    a.restartStep();
    thrown(env, a, f);
}
// exe 0x48f7f0: blinking, an explosion, the drops, freed
void dying(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    a.tint = kTintFlash;
    if (a.subState == 0) {
        a.subState = 1;
        f.timer = 0x14;
        if (a.airborne == 0) {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
        } else {
            f.timer = 8;
        }
        a.acceleration.y = 0x40;
        a.anim = 0;
        explosionAt(a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole) - 0x60u), a.pos.z.whole, 0x30, f.timer);
    }
    if (uint8_t(f.timer) & 1) a.tint = kTintHurtBlink;
    --f.timer;
    if (f.timer == 0) {
        spawnDrops(a.pos, rowOf(a).dropTable); // exe 0x5f1270
        freeActor(a);                          // exe 0x5e1490
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.airborne != 0) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    collide(env, a, f);
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
// exe 0x48e750
void update(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<Fields>(a);
    a.prevAnim = a.anim;
    savePrevious(a);
    switch (State(a.state)) { // [0x892924]
    case State::Init: init(a, f); break;
    case State::Appear: appear(env, a, f); break;
    case State::Main: mainState(env, a, f); break;
    case State::Carried: // [0x892980]
        if (a.subState == uint8_t(CarriedSub::Held)) held(env, a, f);
        else if (a.subState == uint8_t(CarriedSub::Thrown)) thrown(env, a, f);
        else notPortedAt(kStoryPort, "class 0x48e750 carried sub-state", a.subState);
        break;
    case State::Dying: dying(env, a, f); break;
    case State::Free: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "class 0x48e750 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateFromKey0(a);
    const Row row = rowOf(a);
    const uint8_t b = f.mode;
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (!(b & kModeNoHitEntry)) {
        const uint32_t variantFlag = a.spawnByte != 0 ? kVariantAttackFlag : 0u;
        const uint8_t slot = a.slot;
        ShortVec4& point = bonePointOf(a, b & kModeThrown ? 0 : (b & kModeStrong) ? 1 : 0);
        uint32_t owner, attack;
        if (b & kModeThrown) {
            attack = uint32_t(row.thrownAttack) | variantFlag | kHitAttackByPlayer;
            owner = hitRadius(row.thrownRadius) | hitOwner(slot) | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion;
        } else {
            const uint32_t notTarget = uint32_t(b & kModeNotTarget) << 12; // kHitNoLockOn
            if (b & kModeStrong) {
                attack = uint32_t(row.strongAttack) | variantFlag | kHitAttackSolid;
                owner = hitOwner(slot) | hitHeightLevel(row.hitHeight) | hitGroup(kHitGroupBodies) | kHitContactMotion |
                        hitRadius(row.strongRadius) | notTarget;
            } else {
                attack = uint32_t(row.attack) | variantFlag | kHitAttackSolid;
                owner = hitOwner(slot) | hitHeightLevel(row.hitHeight) | hitGroup(kHitGroupBodies) |
                        kHitCompareHeights | kHitContactMotion | hitRadius(row.radius) | notTarget;
            }
        }
        const int16_t p[3] = {point.x, point.y, point.z};
        hitListSubmit(p, f.hitBox, owner, attack); // exe 0x5e4120
    }
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) unless state 3 sub 0: the shadow (render output)
    actorCollisionSubmitMover(a); // exe 0x4140b0
}

} // namespace hopper

// ==== the shelled reaverbot (exe 0x47a600, pool 0x20 type 0x19) =====================================================
// Knocked onto its back it can be lifted and thrown, and it rolls over back onto its feet.
namespace tumbler {

using exam_enemies_a_state::tumbler::Fields;

enum class State : uint8_t {
    Init = 0,    // exe 0x47a760
    Main = 1,    // exe 0x47a840
    Carried = 2, // exe 0x47b2e0
    Dying = 3,   // exe 0x47ba60
};
enum class Sub : uint8_t {
    Reset = 0,      // exe 0x47a920
    Idle = 1,       // exe 0x47a970
    Wander = 2,     // exe 0x47aa40
    TurnAround = 3, // exe 0x47ab10
    Alerted = 4,    // exe 0x47ab80
    Approach = 5,   // exe 0x47ac40
    WindUp = 6,     // exe 0x47ad50
    Charge = 7,     // exe 0x47ae10
    OnBack = 8,     // exe 0x47af20
    FlipOver = 9,   // exe 0x47af50
    FlipUp = 10,    // exe 0x47afe0
    Fall = 11,      // exe 0x47b070
    Flinch = 12,    // exe 0x47b0e0
    KnockBack = 13, // exe 0x47b180
    PushBack = 14,  // exe 0x47b240
    Held = 15,      // the carried state's sub-states run as the main state's
    Thrown = 16,
    Roll = 17,
};
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1, Roll = 2 }; // exe 0x47b340 / 0x47b4b0 / 0x47b710

// +0x160
constexpr uint8_t kContactWallX = 0x01, kContactWallZ = 0x02, kContactLanded = 0x04, kContactGroundLost = 0x08;
constexpr uint8_t kContactCeiling = 0x10;
constexpr uint8_t kContactWalls = kContactWallX | kContactWallZ;
// +0x162
constexpr uint8_t kShellOnBack = 0x01, kShellRollsBack = 0x02, kShellTurned = 0x04, kShellDiesOnLanding = 0x08;
// +0x163
constexpr uint8_t kAlertAlerted = 0x01, kAlertStrongHit = 0x02;
// +0x164
constexpr uint8_t kModeNoHitEntry = 0x01, kModeNoDamage = 0x02, kModeNoWallTurn = 0x04, kModeNoFall = 0x08;
constexpr uint8_t kModeThrown = 0x10, kModeCharge = 0x20, kModeBounces = 0x40, kModeNotTarget = 0x80;

const DataTable<CollisionBoxRecord> box{TableId::TumblerBox};
const DataTable<InteractionBoxRecord> liftBox{TableId::TumblerLiftBox};
constexpr uint16_t kSoundBump = 0x120, kSoundStep = 0x121;
constexpr uint8_t kStunHits = 0x43;

void enter(AnimatedActor& a, Sub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
// the exe's fold of an angle difference (unsigned compares)
uint32_t foldDiff(int32_t d) {
    if (uint32_t(d + 0x800) > 0x1000u) d = (-d ^ 0xfff) + 1;
    return uint32_t(d);
}
// exe 0x47bd50: the walls / the territory / the ground (kept); true: the ground lost. Tumbling in the air, the shell's
// lowest point by its pitch stands on the ground
bool collide(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    f.contactWord = 0;
    uint32_t r;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);             // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0x300) f.contact |= kContactWallX;
        if (r & 0xc00) f.contact |= kContactWallZ;
        if (r & kSolidAboveOrBelow) f.contact |= kContactCeiling;
        if ((f.mode & kModeBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.contact |= kContactLanded;
    } else {
        const int rect = territoryViolated(a.pos); // exe 0x5e6ab0
        r = uint32_t(rect);
        if (rect != 0) {
            r = territoryPushBack(a, rect); // exe 0x5e6b40
            if (r & 0x300) f.contact |= kContactWallX;
            if (r & 0xc00) f.contact |= kContactWallZ;
        }
    }
    const int g = actorGround(env, a); // exe 0x40f190
    f.ground = g;
    if (a.airborne == 0) {
        if (g - a.pos.y.whole > 0x10) {
            f.contact |= kContactGroundLost;
            return true;
        }
        a.pos.y.whole = int16_t(f.ground);
        return false;
    }
    if (!(f.mode & kModeBounces)) {
        if (r & kSolidAboveOrBelow) {
            a.airborne = 0;
            a.pos.y.whole = int16_t(f.ground);
            f.contact |= kContactLanded;
        }
        return false;
    }
    const uint32_t p = uint16_t(a.rotation.x);
    const int sv = fixedSin(int(p)), cv = fixedCos(int(p));
    const int dxv = (sv * 80 < 0 ? -(sv * 80) : sv * 80) >> 12;
    const bool side = uint16_t((p & kAngleMask) - 0x400) < 0x800;
    const int cxv = (side ? (cv * 104 < 0 ? -(cv * 104) : cv * 104) : -(cv * 24 < 0 ? -(cv * 24) : cv * 24)) >> 12;
    const int lim = int(a.pos.y.whole) + int(int16_t(cxv)) + int(int16_t(dxv));
    if (g < lim) {
        a.pos.y.whole = int16_t(uint16_t(uint16_t(f.ground) - uint16_t(cxv) - uint16_t(dxv)));
        f.contact |= kContactLanded;
    }
    return false;
}
// exe 0x47b9f0(actor, yaw): the ballistic step; landed: a half bounce (the sound), true
bool fly(AnimatedActor& a, Fields& f, int yaw) {
    if (f.contact & kContactLanded) {
        const int vy = (-int(a.velocity.y)) >> 1;
        a.velocity.z = int16_t(a.velocity.z >> 1);
        a.velocity.y = int16_t(vy);
        a.airborne = 1;
        soundPlayAtPos(kSoundBump, a.pos);
        return true;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    return false;
}
// exe 0x47bba0: the hits -> the stun and flip counters; on its back the hit points; true: dead (dying)
bool damage(AnimatedActor& a, Fields& f) {
    const uint32_t v = f.hitBox.result;
    const uint32_t d = v & kHitDamageMask;
    if (d == 0) return false;
    const uint8_t m = f.mode;
    if (m & kModeCharge) {
        if (!(v & kHitAttackStrongPush)) return false;
        f.mode = uint8_t(m & ~kModeCharge);
    } else if (m & kModeNoDamage) {
        return false;
    }
    uint8_t shell;
    if (f.shell & kShellOnBack) {
        f.targetYaw = f.hitBox.contact.x;
        a.tint = kTintFlash;
        if (hitPointsAdd(a, -int(d)) != 0) { // exe 0x5e52f0
            a.enterState(uint8_t(State::Dying));
            return true;
        }
        const uint8_t c = uint8_t(f.stun - uint8_t(d));
        f.stun = c;
        if (int8_t(c) > 0 && !(v & kHitAttackStrongPush)) return false;
        enter(a, Sub::FlipUp);
        shell = uint8_t(f.shell & ~kShellOnBack);
    } else {
        uint32_t amount = 0;
        if (f.alert & kAlertAlerted) {
            if (v & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) {
                f.targetYaw = f.hitBox.contact.x;
                a.tint = kTintFlash;
                enter(a, Sub::Flinch);
                amount = d;
            }
            if (v & (kHitLevelAbove | kHitLevelWithin | kHitLevelBelow)) {
                f.targetYaw = f.hitBox.contact.x;
                a.tint = kTintFlash;
                if (v & kHitLevelBelow) enter(a, Sub::KnockBack);
                else if (v & kHitLevelWithin) enter(a, Sub::PushBack);
                amount = d;
            }
        } else {
            enter(a, Sub::Alerted);
            f.alert |= kAlertAlerted;
            amount = d;
        }
        const uint8_t c = uint8_t(f.stun - uint8_t(amount));
        f.stun = c;
        if (int8_t(c) > 0 && !(v & kHitAttackStrongPush)) return false;
        enter(a, Sub::FlipOver);
        shell = uint8_t(f.shell | kShellOnBack);
    }
    const uint8_t alert = f.alert;
    f.shell = shell;
    f.stun = kStunHits;
    f.alert = (v & kHitAttackStrongPush) ? uint8_t(alert | kAlertStrongHit) : uint8_t(alert & ~kAlertStrongHit);
    return false;
}
// exe 0x47a940: still, the scale 1.0
void stop(AnimatedActor& a) {
    a.anim = 0;
    a.velocity.setXY(0, 0);
    a.velocity.z = 0;
    a.acceleration.z = 0;
    resetModelScale(a); // exe 0x4e5cd0
    a.runFlags &= uint8_t(~kActorRunMarked);
}
void alertNow(AnimatedActor& a, Fields& f) {
    enter(a, Sub::Alerted);
    f.alert |= kAlertAlerted;
}
// the planar velocity and acceleration cleared (the exe's two dword stores)
void clearPlanar(AnimatedActor& a) {
    a.velocity.setXY(0, 0);
    a.acceleration.setXY(0, 0);
}

// ---- the main state's sub-states [0x891054] ----
void reset(AnimatedActor& a, Fields& f) {
    stop(a);
    f.mode = 0;
    enter(a, Sub::Idle);
}
// idle; the player in front and near (or a hit) -> alerted; 1 tick in 256 -> wander
void idle(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 0;
        clearPlanar(a);
        a.step = 1;
    }
    const int ang = playerAngle(a) & kAngleMask; // exe 0x5e3bc0
    const uint32_t d = foldDiff(int32_t(uint16_t(a.rotation.y) & kAngleMask) - ang);
    if (d + 0x400 < 0x800) {
        if ((f.hitBox.result & kHitDamageMask) || playerDistance(a) < 0x600) {
            alertNow(a, f);
            return;
        }
    }
    if ((rngStep() & 0xff) == 0) enter(a, Sub::Wander);
}
// wanders forward 0x20 ticks; a wall -> turns round; the player near and ahead -> alerted
void wander(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeNoWallTurn;
        a.anim = 1;
        clearPlanar(a);
        a.velocity.z = -0x100;
        a.enterStep(1);
        f.timer = 0x20;
    }
    integrateVelocity(a, 0, 0, a.velocity.z);
    if (f.contact & kContactWalls) {
        enter(a, Sub::TurnAround);
        return;
    }
    int ang = playerAngle(a);
    if (ang < 0) ang = -ang;
    if (ang < 0x400 && playerDistance(a) < 0x600) {
        alertNow(a, f);
        return;
    }
    --f.timer;
    if (f.timer == 0) enter(a, Sub::Idle);
}
// turns round (0x80 a tick)
void turnAround(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeNoWallTurn;
        a.anim = 2;
        a.enterStep(1);
        clearPlanar(a);
        f.targetYaw = int16_t((uint16_t(a.rotation.y) - 0x800u) & kAngleMask);
    }
    if (turnYawTo(a.rotation.y, uint32_t(int32_t(f.targetYaw)), 0x80) == 0) enter(a, Sub::Idle); // exe 0x5e3df0
}
// alerted: faces the player, steps back 10 ticks (the step's sound at the cycle's end)
void alerted(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeNoWallTurn;
        a.anim = 2;
        a.enterStep(1);
        clearPlanar(a);
        a.velocity.z = 0x180;
        f.timer = 0xa;
        f.targetYaw = a.rotation.y;
    }
    if (a.animEnded()) soundPlayAtPos(kSoundStep, a.pos);
    turnYawTo(a.rotation.y, uint32_t(playerAngle(a)), 0x80);
    moveAlongYaw(a.pos, f.targetYaw, 0, 0, a.velocity.z);
    --f.timer;
    if (f.timer == 0) enter(a, Sub::Approach);
}
// approaches; near -> the wind-up, far -> idle; out of view 0x5a ticks -> idle
void approach(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 1;
        a.enterStep(1);
        clearPlanar(a);
        a.velocity.z = -0x100;
        f.timer = 0x5a;
    }
    const int ang = playerAngle(a);
    turnYawTo(a.rotation.y, uint32_t(ang), 0x40);
    integrateVelocity(a, 0, 0, a.velocity.z);
    const int dist = playerDistance(a);
    int32_t d = ang;
    if (dist < 0x200) {
        enter(a, Sub::WindUp);
    } else if (dist > 0xa00) {
        enter(a, Sub::Idle);
        f.alert &= uint8_t(~kAlertAlerted);
    } else {
        d = int32_t(uint16_t(a.rotation.y) & kAngleMask) - (ang & kAngleMask);
    }
    if (foldDiff(d) + 0x400 < 0x800) {
        f.timer = 0x5a;
        return;
    }
    --f.timer;
    if (f.timer == 0) {
        enter(a, Sub::Idle);
        f.alert &= uint8_t(~kAlertAlerted);
    }
}
// the wind-up: faces the player, the scale pulsing 0x10 ticks, then the charge
void windUp(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = 0;
        a.anim = 2;
        a.enterStep(1);
        clearPlanar(a);
        a.velocity.z = 0;
        f.timer = 0x10;
    }
    turnYawTo(a.rotation.y, uint32_t(playerAngle(a)), 0x80);
    const uint32_t b = uint8_t(f.timer) & 1u;
    a.scale.x = int16_t((b + 0x10) << 5);
    a.scale.y = int16_t(0x200 - (b << 5));
    a.scale.z = int16_t((b + 0x10) << 5);
    --f.timer;
    if (f.timer == 0) {
        enter(a, Sub::Charge);
        resetModelScale(a);
    }
}
// charges 0x20 ticks (the steps' sound); a wall -> the crash (a shake, the sound), alerted
void charge(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeCharge | kModeNoWallTurn | kModeNoDamage;
        a.anim = 2;
        a.enterStep(1);
        clearPlanar(a);
        a.velocity.z = -0x180;
        f.timer = 0x20;
    }
    const uint8_t frame = uint8_t(a.animKey.frame);
    if (frame == 4 || frame == 9) soundPlayAtPos(kSoundStep, a.pos);
    if (f.contact & kContactWalls) {
        soundPlayAtPos(kSoundBump, a.pos);
        cameraShakeAt(0x2000, 0x300, 0x100, 0x1c00, a.pos); // exe 0x40eab0
        padVibrateAt(0x501, 0x412c0, 0x100, 0x1c00, a.pos); // exe 0x55cd80
        enter(a, Sub::Alerted);
        f.mode &= uint8_t(~kModeCharge);
    } else {
        integrateVelocity(a, 0, 0, a.velocity.z);
    }
    a.velocity.z = int16_t(uint16_t(a.velocity.z) - 0x20u);
    if (a.velocity.z < -0x300) a.velocity.z = -0x300;
    --f.timer;
    if (f.timer == 0) {
        enter(a, Sub::Approach);
        f.mode &= uint8_t(~kModeCharge);
    }
}
void onBack(AnimatedActor& a, Fields& f) {
    if (a.step != 0) return;
    f.mode = kModeNoWallTurn;
    a.anim = 4;
    a.step = 1;
    clearPlanar(a);
}
// exe 0x47af50 / 0x47afe0: the hop of a flip (higher after a strong hit), the next sub-state when it ends on the ground
void flipHop(AnimatedActor& a, Fields& f, uint8_t anim, Sub next, uint8_t nextAnim) {
    if (a.step == 0) {
        f.mode = kModeNoWallTurn | kModeNoDamage;
        a.velocity.z = int16_t(a.velocity.z >> 1);
        a.acceleration.z = int16_t(a.acceleration.z >> 1);
        a.anim = anim;
        a.step = 1;
        clearPlanar(a);
        a.velocity.y = (f.alert & kAlertStrongHit) ? -0x300 : -0x240;
        a.acceleration.y = 0x40;
        a.airborne = 1;
    }
    if (a.airborne != 0 && fly(a, f, 0)) {
        a.airborne = 0;
        a.velocity.y = 0;
    }
    if (a.animEnded() && a.airborne == 0) {
        a.anim = nextAnim;
        enter(a, next);
    }
}
void fall(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        f.mode = kModeBounces | kModeNoFall | kModeNoWallTurn;
        a.step = 1;
        a.acceleration.y = 0x40;
        resetModelScale(a);
    } else if (s != 1) {
        return;
    }
    if (!fly(a, f, a.rotation.y)) return;
    a.airborne = 0;
    a.velocity.y = 0;
    a.enterState(uint8_t(State::Main), uint8_t((f.shell & kShellOnBack) ? Sub::OnBack : Sub::Reset));
}
// the shared start of the hit reactions: the sound, the scale 1.0
void hitStart(AnimatedActor& a) {
    soundPlayAtPos(kSoundHurt, a.pos);
    resetModelScale(a);
}
// flinches 2 ticks
void flinch(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        a.enterStep(1);
        f.mode = kModeNoWallTurn | kModeNoDamage;
        a.anim = 0;
        f.timer = 2;
        a.velocity.z = 0;
        if (a.airborne != 0) a.acceleration.y = 0x40;
        else a.velocity.y = 0;
        hitStart(a);
    }
    const uint16_t t = f.timer;
    f.timer = uint16_t(t - 1u);
    if (t == 0 && a.airborne == 0) enter(a, Sub::Approach);
    if (a.airborne != 0 && fly(a, f, a.rotation.y)) {
        a.airborne = 0;
        a.velocity.y = 0;
    }
}
// knocked back along the hit's direction
void knockBack(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        a.enterStep(1);
        f.mode = kModeNoWallTurn | kModeNoDamage;
        a.anim = 0;
        a.rotation.y = f.targetYaw;
        a.velocity.x = 0;
        a.velocity.z = -0x140;
        if (a.airborne != 0) a.acceleration.y = 0x40;
        else a.velocity.y = 0;
        hitStart(a);
        f.timer = 6;
    }
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    if (a.animEnded() && a.airborne == 0) {
        if (f.timer != 0) --f.timer;
        if (f.timer == 0 && a.airborne == 0) enter(a, Sub::Approach);
    }
}
// pushed back along the hit's direction, slowing
void pushBack(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeNoDamage;
        a.enterStep(1);
        a.anim = 0;
        a.velocity.x = 0;
        a.velocity.z = -0x180;
        a.acceleration.z = 0x40;
        if (a.airborne != 0) a.acceleration.y = 0x40;
        else a.velocity.y = 0;
        hitStart(a);
    }
    moveAlongYaw(a.pos, f.targetYaw, 0, a.velocity.y, a.velocity.z);
    a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
    if (a.velocity.z >= 0) enter(a, Sub::Approach);
}

// ---- carried / thrown / rolling over (the carried state's sub-states; also the main state's 15..17) ----
// exe 0x47b4b0: thrown spinning; its bounces, then it lands rolling (sub-state 2) or dies
void thrown(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (f.hitBox.result & kHitAttackSolid) {
        if (a.hitPoints >= 0) {
            f.shell |= kShellDiesOnLanding;
            a.spin.z = int16_t(-int(uint16_t(a.spin.z)));
            a.spin.x = int16_t(-int(uint16_t(a.spin.x)));
            a.velocity.z = int16_t(-int(uint16_t(a.velocity.z)));
            spawnHitSpark(kActorSweepGroup2, a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole - 0x40)), a.pos.z.whole);
            f.mode |= kModeNoHitEntry;
        }
    }
    a.spin.x = int16_t(uint16_t(a.spin.x) + uint16_t(a.spin.z));
    a.rotation.x = int16_t(uint16_t(a.rotation.x) + uint16_t(a.spin.x));
    if (fly(a, f, a.rotation.y)) {
        const uint8_t c = uint8_t(f.bouncesLeft - 1u);
        f.bouncesLeft = c;
        if (c != 0) {
            if (rngStep() & 3) {
                a.spin.z = int16_t(a.spin.z >> 1);
                a.spin.x = int16_t(a.spin.x >> 1);
            }
        } else {
            if (a.hitPoints < 0) {
                a.enterState(uint8_t(State::Dying));
            } else {
                const uint32_t p = uint16_t(a.rotation.x) & kAngleMask;
                const int cv = fixedCos(int(p));
                int v = cv < 0 ? -(cv >> 5) : (cv >> 5);
                v += 2;
                const int lim = int16_t(v) > 0x20 ? 0x20 : v;
                bool neg, back;
                if (p < 0x400) {
                    back = true;
                    neg = true;
                } else if (p < 0x800) {
                    back = false;
                    neg = false;
                } else if (p < 0xc00) {
                    back = false;
                    neg = true;
                } else {
                    back = true;
                    neg = false;
                }
                f.shell = back ? uint8_t(f.shell | kShellRollsBack) : uint8_t(f.shell & ~kShellRollsBack);
                a.spin.x = int16_t(neg ? -v : v);
                a.spin.z = int16_t(neg ? -lim : lim);
                // sub-state 2: the carried state's roll (run as the main state's copy: its sub-state 2, the wander)
                a.subState = uint8_t(CarriedSub::Roll);
                a.restartStep();
            }
            a.velocity.y = 0;
            a.velocity.x = 0;
            a.velocity.z = 0;
            a.airborne = 0;
        }
    }
    collide(env, a, f);
    if (f.contact & kContactCeiling) {
        a.velocity.y = 0;
        a.rotation.x = int16_t((0u - uint16_t(a.rotation.x)) & kAngleMask);
    }
    if (f.contact & kContactWalls) {
        const uint32_t d =
            foldDiff(int32_t(uint16_t(a.rotation.y) & kAngleMask) - int32_t(uint16_t(a.prevRotation.y) & kAngleMask));
        if (d + 0x400 >= 0x800) {
            f.shell ^= kShellTurned;
            a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x800u);
            a.spin.x = int16_t(-int(uint16_t(a.spin.x)));
            a.spin.z = int16_t(-int(uint16_t(a.spin.z)));
            a.velocity.z = int16_t(-int(uint16_t(a.velocity.z)));
        }
    }
}
// exe 0x47b340: at the player's hands until thrown (along the player's aim) / dropped; in water a smaller throw
void held(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        a.tint = kTintNeutral;
        f.mode = kModeBounces | kModeNoHitEntry;
        a.step = 1;
        a.anim = 6;
        resetModelScale(a);
        f.shell = 0;
        a.rotation.x = 0;
        f.bouncesLeft = player().inWater == 1 ? 1 : 3;
    }
    carryAtHands(a, 0, 0, 0);               // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.mode &= uint8_t(~kModeNoHitEntry);
    a.runFlags &= uint8_t(~kActorRunMarked);
    f.targetYaw = player().rotation.y;
    const bool inWater = player().inWater == 1;
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        if (inWater) playerAimVector(0, -0x100, -0x140, v); // exe 0x5e3e50
        else playerAimVector(0, -0x200, -0x280, v);
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.mode |= kModeThrown;
    } else if (!inWater) {
        a.velocity.y = -0x300;
        a.velocity.z = -0x40;
    } else {
        a.velocity.y = -0x180;
        a.velocity.z = -0x20;
    }
    if (player().inWater != 1) {
        a.acceleration.y = 0x40;
        a.spin.x = 0x70;
    } else {
        a.acceleration.y = 0x20;
        a.spin.x = 0x38;
    }
    a.rotation.y = f.targetYaw;
    a.spin.z = 0;
    a.airborne = 1;
    carryDropFromPlayer(a); // exe 0x573990
    // sub-state 1: the carried state's throw (run as the main state's copy: its sub-state 1, the idle)
    a.subState = uint8_t(CarriedSub::Thrown);
    a.restartStep();
    thrown(env, a, f);
}
void rollMove(AnimatedActor& a, int y, int z) { moveAlongYaw(a.pos, a.rotation.y, 0, y, z); } // exe 0x5e36f0
int16_t pitchNow(const AnimatedActor& a) { return int16_t(uint16_t(a.rotation.x) & kAngleMask); }
// exe 0x47b710: rolling over on the ground until upright (the approach) or on its back
void roll(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        f.mode = kModeNoHitEntry | kModeNoFall | kModeThrown;
        a.step = 1;
        a.airborne = 1;
    }
    int v;
    const int cv0 = fixedCos(int(uint16_t(a.rotation.x)));
    if (player().inWater != 1) {
        v = cv0 < 0 ? -(cv0 >> 5) : (cv0 >> 5);
        if (v < 0x40) v = 0x40;
        else if (v > 0xff) v = 0xff;
    } else {
        v = cv0 < 0 ? -(cv0 >> 6) : (cv0 >> 6);
        if (v < 8) v = 8;
        else if (v > 0x20) v = 0x20;
    }
    if (a.spin.z < 0) v = -v;
    const int16_t step = int16_t(v);
    a.spin.z = int16_t(v);
    a.rotation.x = int16_t(uint16_t(a.rotation.x) + uint16_t(step));
    const uint32_t pitch = uint16_t(a.rotation.x), prevPitch = uint16_t(a.prevRotation.x);
    a.spin.x = step;
    const uint8_t b = f.shell;
    const int cd = int16_t(int(fixedCos(int(pitch))) - int(fixedCos(int(prevPitch))));
    const int sd = int16_t(int(fixedSin(int(pitch))) - int(fixedSin(int(prevPitch))));
    const int zLong = int16_t((-80 * cd) >> 8);
    const int yLong = int16_t((80 * sd) >> 8);
    bool flipBack = false, flipUp = false;
    if (b & kShellRollsBack) {
        const int yShort = int16_t((24 * cd) >> 8);
        const int zShort = int16_t((-24 * sd) >> 8);
        if (step > 0) {
            rollMove(a, yShort + yLong, zLong - zShort);
            flipBack = pitchNow(a) < 0x800;
        } else {
            rollMove(a, yShort - yLong, -(zShort + zLong));
            flipBack = pitchNow(a) > 0x800;
        }
    } else {
        const int yShort = int16_t((104 * cd) >> 8);
        const int zShort = int16_t((-104 * sd) >> 8);
        if (step > 0) {
            rollMove(a, yShort - yLong, -(zShort + zLong));
            flipUp = pitchNow(a) > 0x800;
        } else {
            rollMove(a, yShort + yLong, zLong - zShort);
            flipUp = pitchNow(a) < 0x800;
        }
    }
    if (flipBack) { // exe 0x47b8c4
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x800u);
        a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x18u);
        a.rotation.x = 0;
        a.anim = 4;
        a.airborne = 0;
        a.enterState(uint8_t(State::Main), uint8_t(Sub::OnBack));
        f.shell |= kShellOnBack;
        if (f.shell & kShellDiesOnLanding) {
            a.hitPoints = -1;
            a.enterState(uint8_t(State::Dying));
        }
    } else if (flipUp) { // exe 0x47b994
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x800u);
        a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) + 0x68u);
        a.rotation.x = 0;
        a.anim = 0;
        a.airborne = 0;
        a.enterState(uint8_t(State::Main), uint8_t(Sub::Approach));
        f.shell &= uint8_t(~kShellOnBack);
        if (f.shell & kShellDiesOnLanding) {
            a.hitPoints = -1;
            a.enterState(uint8_t(State::Dying));
        }
    }
    a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x18u);
    collide(env, a, f);
    a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) + 0x18u);
}

// ---- the states [0x891044] ----
// exe 0x47ba60: blinking, an explosion, the drops, freed
void dying(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    a.tint = kTintFlash;
    if (a.step == 0) {
        a.step = 1;
        if (a.airborne == 0) {
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = 0;
        }
        a.acceleration.y = 0x40;
        f.timer = 0x14;
        countDefeat(); // exe 0x5e53e0
        explosionAt(a.pos.x.whole, int16_t(uint16_t(a.pos.y.whole) - 0x60u), a.pos.z.whole, 0x30, f.timer);
    }
    if (uint8_t(f.timer) & 1) a.tint = kTintHurtBlink;
    if (int16_t(f.timer) < 5) f.mode |= kModeNotTarget;
    --f.timer;
    if (f.timer == 0) {
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);                            // exe 0x5e1490
        return;
    }
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
    if (a.airborne != 0) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    collide(env, a, f);
    if (a.wasAirborne != 0 && a.airborne == 0) {
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
}
void init(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (!bindActorModel(a, a.spawnByte)) { // exe 0x5e0bc0(actor, actor + 7)
        freeActor(a);                      // exe 0x5e1490
        return;
    }
    a.rotation.x = 0;
    a.rotation.z = 0;
    a.anim = 0;
    a.prevAnim = 0xff;
    a.runFlags |= kActorRunMarked;
    const uint16_t hp = rowHitPoints(a);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(box.stateAddress());
    a.interactionBox.pointAt(liftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock();
    f.hitBox.previousX = a.pos.x.whole;
    f.hitBox.previousZ = a.pos.z.whole;
    f.hitBox.result = 0;
    f.stun = kStunHits;
    f.shell = 0;
    f.alert = 0;
    a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    a.enterState(uint8_t(State::Main));
}
void mainState(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    savePrevious(a);
    if (damage(a, f)) {
        dying(env, a, f);
        return;
    }
    switch (Sub(a.subState)) { // [0x891054]
    case Sub::Reset: reset(a, f); break;
    case Sub::Idle: idle(a, f); break;
    case Sub::Wander: wander(a, f); break;
    case Sub::TurnAround: turnAround(a, f); break;
    case Sub::Alerted: alerted(a, f); break;
    case Sub::Approach: approach(a, f); break;
    case Sub::WindUp: windUp(a, f); break;
    case Sub::Charge: charge(a, f); break;
    case Sub::OnBack: onBack(a, f); break;
    case Sub::FlipOver: flipHop(a, f, 3, Sub::OnBack, 4); break;  // flipped onto its back
    case Sub::FlipUp: flipHop(a, f, 5, Sub::Approach, 1); break; // back onto its feet
    case Sub::Fall: fall(a, f); break;
    case Sub::Flinch: flinch(a, f); break;
    case Sub::KnockBack: knockBack(a, f); break;
    case Sub::PushBack: pushBack(a, f); break;
    case Sub::Held: held(env, a, f); break;
    case Sub::Thrown: thrown(env, a, f); break;
    case Sub::Roll: roll(env, a, f); break;
    default: notPortedAt(kStoryPort, "class 0x47a600 sub-state", a.subState);
    }
    if (collide(env, a, f) && !(f.mode & kModeNoFall)) {
        enter(a, Sub::Fall);
    } else if ((f.contact & kContactWalls) && !(f.mode & kModeNoWallTurn)) {
        enter(a, Sub::Alerted);
    }
    if (a.hitPoints >= 0 && playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        a.enterState(uint8_t(State::Carried));
        f.mode |= kModeNoHitEntry;
        a.runFlags |= kActorRunMarked;
    }
}
void carriedState(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    a.prevAnim = a.anim;
    savePrevious(a);
    switch (CarriedSub(a.subState)) { // [0x891090]
    case CarriedSub::Held: held(env, a, f); break;
    case CarriedSub::Thrown: thrown(env, a, f); break;
    case CarriedSub::Roll: roll(env, a, f); break;
    default: notPortedAt(kStoryPort, "class 0x47a600 carried sub-state", a.subState);
    }
}
// exe 0x47a600
void update(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<Fields>(a);
    switch (State(a.state)) { // [0x891044]
    case State::Init: init(env, a, f); break;
    case State::Main: mainState(env, a, f); break;
    case State::Carried: carriedState(env, a, f); break;
    case State::Dying: dying(env, a, f); break;
    default: notPortedAt(kStoryPort, "class 0x47a600 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateFromKey0(a);
    const AnimatedActorStats stats = statsOf(a);
    const uint8_t b = f.mode;
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    if (b & kModeNoHitEntry) return;
    const uint32_t owner = hitOwner(a.slot), notTarget = uint32_t(b & kModeNotTarget) << 12; // kHitNoLockOn
    uint32_t ownerShape, attack;
    if (a.state == uint8_t(State::Dying)) {
        attack = kHitAttackBump;
        ownerShape = owner | notTarget | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x50);
    } else if (b & kModeThrown) {
        attack = uint32_t(stats.bodyAttack) | kHitAttackByPlayer;
        ownerShape = owner | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x58);
    } else if (b & kModeCharge) {
        attack = uint32_t(stats.strongAttack) | kHitAttackThrown | kHitAttackStrong;
        ownerShape = owner | notTarget | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x58);
    } else {
        attack = uint32_t(stats.attack) |
                 ((f.shell & kShellOnBack) ? kHitAttackSolid : (kHitAttackThrown | kHitAttackSolid));
        ownerShape = owner | notTarget | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x58);
    }
    const ShortVec4& point = bonePointOf(a, 0); // exe 0x5e1810(actor, 0)
    const int16_t p[3] = {point.x, point.y, point.z};
    hitListSubmit(p, f.hitBox, ownerShape, attack); // exe 0x5e4120
    // exe 0x5f20f0(actor, 0x80, 0x100, 0) 0x18 up unless state 2 sub 0: the shadow (render output)
}

} // namespace tumbler

// ==== the shield-bearing gunner (exe 0x44e4c0, pool 0x20 type 4) ====================================================
// It aims and fires bursts (pool-0x40 shots by its spawn byte: 0 type 0x22, 1 / 3 type 0x20, 2 type 5, 4 type 5
// variant 0x80), guards with its shield, loses it to a hit on its weak point (the shield falls off as static type 5),
// can be lifted and thrown.
namespace gunner {

using exam_enemies_a_state::gunner::Fields;
using exam_enemies_a_state::gunner::ShieldParams;
using exam_enemies_a_state::gunner::ShotParams;

enum class State : uint8_t {
    Init = 0,    // exe 0x44e770
    Main = 1,    // exe 0x44e850
    Carried = 2, // exe 0x44f5b0
    Death = 3,   // exe 0x44f7f0
};
enum class Sub : uint8_t {
    Wake = 0,        // exe 0x44e950
    Stand = 1,       // exe 0x44ea30
    StandFacing = 2, // exe 0x44eab0
    Shuffle = 3,     // exe 0x44eb20
    Attack = 4,      // exe 0x44ebc0 (its steps [0x88e9e4])
    Guard = 5,       // exe 0x44ef00
    HoldGuard = 6,   // exe 0x44efd0
    LowerGuard = 7,  // exe 0x44f080
    Turn = 8,        // exe 0x44f0d0
    Far = 9,         // exe 0x44f1d0
    Flinch = 10,     // exe 0x44f280
    KnockBack = 11,  // exe 0x44f2e0
    KnockForward = 12, // exe 0x44f350
    Knocked = 13,    // exe 0x44f3c0
    ShieldLost = 14, // exe 0x44f450
    Fall = 15,       // exe 0x44f500
    Ready = 16,      // the attack's steps run as sub-states
    Aim = 17,
    Sweep = 18,
    Lower = 19,
};
enum class AttackStep : uint8_t { Ready = 0, Aim = 1, Sweep = 2, Lower = 3 };
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1 }; // exe 0x44f5d0 / 0x44f6c0
enum class DeathSub : uint8_t { KnockBack = 0, KnockForward = 1, Fly = 2, Blow = 3 };

// +0x165 low nibble
constexpr uint8_t kModeNoHurt = 0x01, kModeBounces = 0x02, kModeLifted = 0x04, kModeThrown = 0x08;
constexpr uint8_t kModeMask = 0x0f;
// +0x165 high nibble
constexpr uint8_t kShieldRaised = 0x10, kShieldLowered = 0x20, kShieldLost = 0x40, kUnseen = 0x80;
// +0x16b
constexpr uint8_t kContactWall = 0x01, kContactLanded = 0x02, kContactGroundLost = 0x04;

const DataTable<CollisionBoxRecord> box{TableId::GunnerBox};
const DataTable<InteractionBoxRecord> liftBox{TableId::GunnerLiftBox};
const DataTable<ScalarTable<uint8_t, 8>> models{TableId::GunnerModels}; // by the spawn byte (also the shield's model)
const DataTable<ScalarTable<uint8_t, 4>> idleAnims{TableId::GunnerIdleAnims}; // by the shield bits
const DataTable<ScalarTable<uint8_t, 4>> idleStartKeys{TableId::GunnerIdleStartKeys};
constexpr uint8_t kShieldType = 5;
constexpr uint8_t kShieldPart = 3, kBrokenShieldPart = 11;
constexpr uint8_t kShotType5 = 5, kShotType20 = 0x20, kShotType22 = 0x22;
constexpr uint16_t kSoundStep = 0xb2;
constexpr uint32_t kSightBox = 0xff0010;


void shieldIdle(Fields& f) { f.mode = uint8_t(f.mode & ~(kShieldRaised | kShieldLowered)); } // exe 0x44fdb0
void setMode(Fields& f, uint8_t m) { f.mode = uint8_t((f.mode & ~kModeMask) | m); }        // exe 0x44fdc0
void raiseShield(Fields& f) { f.mode = uint8_t((f.mode & ~kShieldLowered) | kShieldRaised); } // exe 0x44fd70
void lowerShield(Fields& f) { f.mode = uint8_t((f.mode & ~kShieldRaised) | kShieldLowered); } // exe 0x44fd90
void enter(AnimatedActor& a, Sub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
// exe 0x44fc60: the walls (bouncing: its flight yaw reflected), the ground; true: the ground lost
bool collide(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    f.contact = 0;
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);             // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0xf00) f.contact = kContactWall;
        if (f.mode & kModeBounces) {
            int16_t pitch = 0, yaw = f.flightYaw; // the exe's stack rotation {0, flight yaw, 0}
            if (reflectRotationOffWalls(pitch, yaw, r) != 0) { // exe 0x410740
                f.flightYaw = yaw;
                f.contact |= kContactLanded;
            }
        }
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne == 0) {
        if (g - a.pos.y.whole > 0x10) {
            f.contact |= kContactGroundLost;
            return true;
        }
        a.pos.y.whole = int16_t(g);
        return false;
    }
    if (!(f.mode & kModeBounces) && ((r & kSolidAboveOrBelow) || !(a.pos.y.whole < g))) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        f.contact |= kContactLanded;
    }
    return false;
}
// exe 0x44fa60(actor, yaw): the ballistic step; landed: a quarter bounce, true
bool fly(AnimatedActor& a, Fields& f, int yaw) {
    if (f.contact & kContactLanded) {
        const int vy = (-int(a.velocity.y)) >> 2;
        a.velocity.z = int16_t(a.velocity.z >> 2);
        a.velocity.y = int16_t(vy);
        a.rotation.x = 0;
        a.airborne = 1;
        return true;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    return false;
}
enum class Hurt : int { None = 0, Hit = 1, WeakPoint = 2, Dead = 3 };
// exe 0x44fbf0: the weak point hit (a special weapon's push on the weak box), a hit, dead
Hurt hurt(AnimatedActor& a, Fields& f) {
    if (f.weakBox.result & kHitAttackStrongPush) return Hurt::WeakPoint;
    const uint32_t v = f.hitBox.result;
    Hurt k = Hurt::None;
    if (v & kHitAttackByPlayer) {
        a.tint = kTintFlash;
        f.hitYaw = f.hitBox.contact.x;
        k = Hurt::Hit;
        if (hitPointsAdd(a, -int(v & kHitDamageMask)) != 0) k = Hurt::Dead; // exe 0x5e52f0
    }
    return k;
}
bool playerArmUp() { return (uint8_t(player().armFlags) & kArmUp) != 0; }
// exe 0x44fb20: the player behind its shield side and attacking -> the guard
bool dodge(AnimatedActor& a, Fields& f) {
    if (f.mode & kShieldLost) return false;
    const Player& p = player();
    const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(int(p.rotation.y) - int(p.directionOffset.y)));
    if (uint32_t(d + 0x400) >= 0x800 && playerArmUp()) {
        enter(a, Sub::Guard);
        return true;
    }
    return false;
}
// exe 0x44fb80: facing the player and near -> stand facing, else stand
void rethink(AnimatedActor& a) {
    enter(a, facingAndRange(a, player(), 0xc0, 0x384) == 0 ? Sub::StandFacing : Sub::Stand);
}
// exe 0x44fac0: off-facing -> the turn, far -> the far wait
int retarget(AnimatedActor& a) {
    int k = 0;
    if (facingAndRange(a, player(), 0xc0, 0x708) & 1) {
        enter(a, Sub::Turn);
        k = 1;
    }
    if (facingAndRange(a, player(), 0x180, 0x708) & 2) {
        enter(a, Sub::Far);
        ++k;
    }
    return k;
}
// exe 0x44fbc0: the idle animation and its first key by the shield bits
void idleAnim(AnimatedActor& a, Fields& f) {
    const uint32_t i = (uint32_t(f.mode) >> 4) & 3;
    a.anim = idleAnims[i];
    f.startKey = int8_t(idleStartKeys[i]);
}
// exe 0x44fde0: the shield (static type 5) falls off at the bone point
void dropShield(const AnimatedActor& a, const ShortVec4& point) {
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return;
    StaticActor& e = staticActorAt(address);
    e.flags = kActorLiveDrawn;
    e.type = kShieldType;
    e.variant = models[a.spawnByte];
    e.loopFlags |= kActorSweepGroup2;
    e.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    e.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
    e.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    e.rotation = a.rotation;
    ShieldParams(e).yaw = uint16_t(uint16_t(a.rotation.y) + 0x800u);
}

// ---- its shots (pool 0x40 through the stage table) ----
// the muzzle: Ry(yaw) * (0, 0, -0x580) and bone 1
ShortVec4& muzzle(const AnimatedActor& a, int32_t& ox, int32_t& oz) {
    int32_t o[3];
    rotateByYaw(a.rotation.y, 0, 0, -0x580, o); // exe 0x5e38d0
    ox = o[0];
    oz = o[2];
    return bonePointOf(a, 1); // exe 0x5e1810(actor, 1)
}
void placeShot(ProjectileActor& e, const ShortVec4& bone, int32_t ox, int32_t oz) {
    e.pos.x.whole = int16_t(uint16_t(ox >> 16) + uint16_t(bone.x));
    e.pos.y.whole = int16_t(uint16_t(bone.y) + 0x10u);
    e.pos.z.whole = int16_t(uint16_t(oz >> 16) + uint16_t(bone.z));
}
// exe 0x44ffc0: a pool-0x40 type 5 shot
ProjectileActor* shotType5(const AnimatedActor& a, int yawAdd, uint8_t side) {
    int32_t ox, oz;
    const ShortVec4& bone = muzzle(a, ox, oz);
    const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!address) return nullptr;
    ProjectileActor& e = projectileActorAt(address);
    ShotParams params(e);
    e.flags = kActorLive;
    e.type = kShotType5;
    e.loopFlags |= kActorSweepGroup2;
    placeShot(e, bone, ox, oz);
    e.rotation.x = 0;
    e.rotation.y = int16_t(uint16_t(yawAdd) + uint16_t(a.rotation.y));
    e.rotation.z = a.rotation.z;
    params.attack = statsOf(a).strongAttack;
    params.side = side;
    params.life = 0xf;
    e.velocity.z = -0x200;
    params.growth = 6;
    params.size = 0x30;
    return &e;
}
// exe 0x44ff80 (variants 1 / 0) / exe 0x4502d0 (0x81 / 0x80): a type 5 shot, the first of a burst variant `first`
void shotType5Burst(const AnimatedActor& a, Fields& f, uint8_t side, uint8_t next, uint8_t first) {
    ProjectileActor* e = shotType5(a, 0, side);
    if (!e) return;
    if (f.shotTimer != 0) {
        e->variant = next;
        return;
    }
    e->variant = first;
    f.shotTimer = 0x18;
}
// exe 0x4500b0: a pool-0x40 type 0x20 shot (the spread)
void shotType20(const AnimatedActor& a, Fields& f, int pitchAdd, int yawAdd, uint8_t side) {
    int32_t ox, oz;
    const ShortVec4& bone = muzzle(a, ox, oz);
    const uint32_t address = allocActor40(kActorStageClass);
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    ShotParams params(e);
    e.flags = kActorLive;
    e.type = kShotType20;
    if (f.shotTimer != 0) {
        e.variant = 1;
    } else {
        e.variant = 0;
        f.shotTimer = 0x10;
    }
    e.loopFlags |= kActorSweepGroup2;
    placeShot(e, bone, ox, oz);
    e.rotation.x = int16_t(uint16_t(a.rotation.x) + uint16_t(pitchAdd));
    e.rotation.y = int16_t(uint16_t(yawAdd) + uint16_t(a.rotation.y));
    e.rotation.z = a.rotation.z;
    params.attack = statsOf(a).strongAttack;
    params.side = side;
    params.life = 0x14;
    e.velocity.z = -0x120;
    const uint32_t n = rngStep();
    params.size = 0x14;
    params.growth = int16_t((n & 1) + 1);
}
// exe 0x4501d0: a pool-0x40 type 0x22 shot (the gunner's own, below)
void shotType22(const AnimatedActor& a, Fields& f, int yawAdd, uint8_t side) {
    int32_t ox, oz;
    const ShortVec4& bone = muzzle(a, ox, oz);
    const uint32_t address = allocActor40(kActorStageClass);
    if (!address) return;
    ProjectileActor& e = projectileActorAt(address);
    ShotParams params(e);
    e.flags = kActorLive;
    e.type = kShotType22;
    if (f.shotTimer != 0) {
        e.variant = 1;
    } else {
        e.variant = 0;
        f.shotTimer = 0x18;
    }
    e.loopFlags |= kActorSweepGroup2;
    placeShot(e, bone, ox, oz);
    e.rotation.x = 0;
    e.rotation.y = int16_t(uint16_t(yawAdd) + uint16_t(a.rotation.y));
    e.rotation.z = a.rotation.z;
    params.attack = statsOf(a).strongAttack;
    params.side = side;
    params.life = 0xf;
    e.velocity.z = -0x200;
    params.growth = 6;
    params.size = 0x30;
}
// exe 0x44fe50: a shot of its spawn byte's kind, every 2nd (or 3rd) frame
void fire(const AnimatedActor& a, Fields& f) {
    const uint32_t t = gameState().frame;
    const uint8_t side = uint8_t((t >> 1) & 1);
    switch (a.spawnByte) {
    case 0:
        if (f.shotTimer != 0) --f.shotTimer;
        if (t & 1) shotType22(a, f, 0, side);
        return;
    case 1:
    case 3: {
        if (f.shotTimer != 0) --f.shotTimer;
        if (t % 3 == 0) return;
        rngStep();
        const int v = int(rngStep() & 0xff) - 0x80;
        shotType20(a, f, v, v, uint8_t((uint8_t(gameState().frame) >> 1) & 1));
        return;
    }
    case 2:
        if (f.shotTimer != 0) --f.shotTimer;
        if (t & 1) shotType5Burst(a, f, side, 1, 0);
        return;
    case 4:
        if (f.shotTimer != 0) --f.shotTimer;
        if (t & 1) shotType5Burst(a, f, side, 0x81, 0x80);
        return;
    default: return;
    }
}

// ---- the main state's sub-states [0x88e9a4] ----
// wakes (the sight probe), then rethinks
void wake(AnimatedActor& a, Fields& f) {
    switch (a.step) {
    case 0:
        a.anim = 5;
        a.enterStep(1);
        setMode(f, 0);
        f.mayFall = 1;
        raiseShield(f);
        return;
    case 1:
        if (a.animEnded()) a.enterStep(2);
        return;
    case 2:
        if (f.mode & kUnseen) return;
        if (facingAndRange(a, player(), 0x100, 0x5dc) == 0) {
            a.anim = 0;
            a.enterStep(3);
        }
        if (playerDistance(a) < 0x180) {
            a.anim = 0;
            a.enterStep(3);
        }
        return;
    case 3:
        if (a.animEnded()) {
            rethink(a);
            shieldIdle(f);
        }
        return;
    default: return;
    }
}
void stand(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        idleAnim(a, f);
        a.restartStep();
        setMode(f, 0);
        f.mayFall = 1;
    }
    const int r = facingAndRange(a, player(), 0xc0, 0x384);
    if (r == 0) enter(a, Sub::StandFacing);
    if (r < 0x3e8) enter(a, Sub::Attack);
    dodge(a, f);
    retarget(a);
}
void standFacing(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        idleAnim(a, f);
        a.restartStep();
        setMode(f, 0);
        f.mayFall = 1;
    }
    const int d = playerDistance(a);
    if (d > 0x384) enter(a, Sub::Stand);
    if (d < 0x3e8) enter(a, Sub::Attack);
    dodge(a, f);
    retarget(a);
}
// shuffles (3 animation cycles)
void shuffle(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = (f.mode & kShieldRaised) ? 0x18 : 0x12;
        f.counter = 3;
        setMode(f, 0);
        f.mayFall = 1;
        a.enterStep(1);
    } else if (s == 1) {
        integrateVelocity(a, 0, 0, 0x40);
        if (a.animEnded()) {
            const uint8_t c = uint8_t(f.counter - 1u);
            f.counter = c;
            if (c == 0) {
                rethink(a);
                dodge(a, f);
                return;
            }
        }
    }
    dodge(a, f);
}
// the attack's steps (the step counter): the shield lowered, the aim; near -> aim, else sweep
void attackReady(AnimatedActor& a, Fields& f) {
    switch (a.stepCounter) {
    case 0:
        if (!(f.mode & kShieldLowered)) {
            a.anim = 1;
            lowerShield(f);
            a.stepCounter = 1;
            dodge(a, f);
            return;
        }
        a.stepCounter = 2;
        break;
    case 1:
        if (a.animEnded()) a.stepCounter = 2;
        break;
    case 2:
        a.anim = 3;
        a.prevAnim = 0xff;
        f.timer = 0x1e;
        a.enterStep(uint8_t(playerDistance(a) < 0x320 ? AttackStep::Aim : AttackStep::Sweep));
        break;
    default: break;
    }
    dodge(a, f);
}
// the end of the aim / the sweep: 0x1e ticks, then far -> lower
void shootTail(AnimatedActor& a, Fields& f, int dist) {
    const uint16_t c = f.timer;
    if (c == 0) {
        if (dist > 0x640) a.enterStep(uint8_t(AttackStep::Lower));
        dodge(a, f);
        return;
    }
    f.timer = uint16_t(c - 1u);
    dodge(a, f);
}
// aims (the yaw - 0x80) and fires
void attackAim(AnimatedActor& a, Fields& f) {
    switch (a.stepCounter) {
    case 0:
        a.anim = 3;
        f.shotTimer = 1;
        a.stepCounter = 1;
        [[fallthrough]];
    case 1: {
        const int ang = playerAngle(a);
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang - 0x80));
        if (uint32_t(d + 0x40) > 0x80) a.stepCounter = 2;
        rngStep();
        rngStep();
        break;
    }
    case 2:
        a.anim = 0x10;
        a.stepCounter = 3;
        [[fallthrough]];
    case 3: {
        const int t = playerAngle(a) - 0x80;
        if (uint32_t(turnToward(a.rotation.y, t, 0x40) + 0x40) <= 0x80) {
            a.rotation.y = int16_t(t);
            a.stepCounter = 0;
        }
        break;
    }
    default: break;
    }
    fire(a, f);
    const int dist = playerDistance(a);
    if (dist > 0x320) a.enterStep(uint8_t(AttackStep::Sweep));
    shootTail(a, f, dist);
}
// sweeps the yaw left / right while firing
void attackSweep(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.stepCounter;
    if (s == 0) {
        a.anim = 0x10;
        a.spin.y = a.spawnByte == 1 ? 0x30 : 0x40;
        const int v = a.spin.y;
        f.counter = uint8_t(0x200 / (v < 0 ? -v : v));
        f.shotTimer = 1;
        a.stepCounter = 1;
    }
    if (s <= 1) {
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(a.spin.y));
        const uint8_t c = uint8_t(f.counter - 1u);
        f.counter = c;
        if (c == 0) {
            const int v = a.spin.y;
            f.counter = uint8_t(0x400 / (v < 0 ? -v : v));
            a.spin.y = int16_t(-int(uint16_t(a.spin.y)));
        }
    }
    fire(a, f);
    const int dist = playerDistance(a);
    if (dist < 0x320) a.enterStep(uint8_t(AttackStep::Aim));
    shootTail(a, f, dist);
}
void attackLower(AnimatedActor& a, Fields& f) {
    if (a.stepCounter == 0) {
        a.anim = 2;
        shieldIdle(f);
        a.stepCounter = 1;
        return;
    }
    if (a.animEnded()) rethink(a);
}
void attack(AnimatedActor& a, Fields& f) {
    switch (AttackStep(a.step)) {
    case AttackStep::Ready: attackReady(a, f); break;
    case AttackStep::Aim: attackAim(a, f); break;
    case AttackStep::Sweep: attackSweep(a, f); break;
    case AttackStep::Lower: attackLower(a, f); break;
    default: notPortedAt(kStoryPort, "class 0x44e4c0 attack step", a.step);
    }
}
// guards against the player's attack
void guard(AnimatedActor& a, Fields& f) {
    switch (a.step) {
    case 0: {
        const uint8_t m = f.mode & (kShieldRaised | kShieldLowered);
        if (m == 0) {
            raiseShield(f);
            a.enterStep(2);
        } else if (m == kShieldRaised) {
            enter(a, Sub::HoldGuard);
        } else if (m == kShieldLowered) {
            a.anim = 2;
            a.enterStep(1);
            shieldIdle(f);
        }
        setMode(f, 0);
        f.mayFall = 1;
        return;
    }
    case 1:
        if (a.animEnded()) a.enterStep(2);
        return;
    case 2:
        a.anim = 6;
        raiseShield(f);
        a.enterStep(3);
        return;
    case 3:
        if (a.animEnded()) enter(a, Sub::HoldGuard);
        return;
    default: return;
    }
}
// holds the guard while the player attacks
void holdGuard(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 6;
        f.startKey = 7;
        a.enterStep(1);
        setMode(f, 0);
        f.mayFall = 1;
    }
    if (s <= 1) {
        if (!playerArmUp()) {
            a.enterStep(2);
            f.counter = 0xa;
        }
    } else if (s == 2) {
        if (playerArmUp()) a.enterStep(1);
        const uint8_t c = uint8_t(f.counter - 1u);
        f.counter = c;
        if (c == 0) enter(a, Sub::LowerGuard);
    }
    if (facingAndRange(a, player(), 0xc0, 0x708) & 1) enter(a, Sub::Turn);
}
void lowerGuard(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        a.anim = 7;
        setMode(f, 0);
        f.mayFall = 1;
        shieldIdle(f);
        a.enterStep(1);
        return;
    }
    if (a.animEnded()) rethink(a);
}
// turns toward the player (0x40 a tick)
void turn(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        f.hitYaw = int16_t(playerAngle(a));
        a.anim = (f.mode & kShieldRaised) ? 8 : 0x10;
        setMode(f, 0);
        f.mayFall = 1;
        a.enterStep(1);
    } else if (s == 1) {
        const int ang = playerAngle(a);
        f.hitYaw = int16_t(ang);
        if (uint32_t(turnToward(a.rotation.y, int(int16_t(uint16_t(ang))), 0x40) + 0x40) <= 0x80) {
            if (f.mode & kShieldRaised) {
                enter(a, Sub::HoldGuard);
            } else {
                a.anim = 2;
                a.enterStep(2);
                shieldIdle(f);
            }
        }
    } else if (s == 2) {
        if (a.animEnded()) rethink(a);
    }
    if (!(f.mode & kShieldRaised)) dodge(a, f);
}
// the player far: waits, then wakes again
void waitFar(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 0x13;
        a.enterStep(1);
        f.counter = 2;
        setMode(f, 0);
        f.mayFall = 1;
        shieldIdle(f);
    } else if (s == 1) {
        if (a.animEnded() && !(f.mode & kShieldLost)) {
            const uint8_t c = uint8_t(f.counter - 1u);
            f.counter = c;
            if (c == 0) enter(a, Sub::Wake);
        }
        if (facingAndRange(a, player(), 0x200, 0x708) == 0) a.enterStep(2);
    } else if (s == 2) {
        if (a.animEnded()) enter(a, Sub::Turn);
    }
}
void flinch(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        a.anim = 9;
        setMode(f, kModeNoHurt | kModeBounces);
        f.mayFall = 1;
        shieldIdle(f);
        soundPlayAtPos(kSoundHurt, a.pos);
        a.enterStep(1);
        return;
    }
    if (a.animEnded()) rethink(a);
}
// knocked: flies, lands, gets up
void knocked(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        if (fly(a, f, f.flightYaw)) {
            a.airborne = 0;
            ++a.anim;
            f.mayFall = 1;
            a.enterStep(1);
        }
    } else if (s == 1) {
        if (a.animEnded()) {
            ++a.anim;
            a.step = uint8_t(s + 1u);
        }
    } else if (s == 2) {
        if (a.animEnded()) rethink(a);
    }
}
// exe 0x44f2e0 / 0x44f350: thrown up along its yaw (no fall), the sound, knocked
void knock(AnimatedActor& a, Fields& f, uint8_t anim, int16_t vz) {
    a.anim = anim;
    a.velocity.x = 0;
    a.velocity.y = -0x100;
    a.velocity.z = vz;
    a.acceleration.y = 0x40;
    f.flightYaw = a.rotation.y;
    a.airborne = 1;
    setMode(f, kModeNoHurt | kModeBounces);
    f.mayFall = 0;
    shieldIdle(f);
    soundPlayAtPos(kSoundHurt, a.pos);
    enter(a, Sub::Knocked);
}
// its shield knocked off (static type 5 at bone 3, a spark, the sound)
void shieldLost(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        a.anim = 0x14;
        setMode(f, kModeNoHurt);
        f.mayFall = 1;
        shieldIdle(f);
        f.mode |= kShieldLost;
        a.parts[kShieldPart].flags |= kPartHidden;
        a.parts[kBrokenShieldPart].flags &= uint8_t(~kPartHidden);
        const ShortVec4& p = bonePointOf(a, 3);
        dropShield(a, p);
        spawnHitSpark(kActorSweepGroup2, p.x, p.y, p.z);
        soundPlayAtPos(kSoundHurt, a.pos);
        a.enterStep(1);
        return;
    }
    if (a.animEnded()) rethink(a);
}
void fall(AnimatedActor& a, Fields& f) {
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 0x16;
        a.velocity.x = 0;
        a.velocity.y = 0x40;
        a.velocity.z = 0;
        f.flightYaw = a.rotation.y;
        a.airborne = 1;
        setMode(f, kModeBounces);
        f.mayFall = 0;
        shieldIdle(f);
        a.enterStep(1);
    }
    if (s <= 1) {
        if (fly(a, f, f.flightYaw)) {
            a.airborne = 0;
            ++a.anim;
            f.mayFall = 1;
            a.enterStep(2);
        }
    } else if (s == 2) {
        if (a.animEnded()) rethink(a);
    }
}

// ---- the states [0x88e98c] ----
void death(const PlayerEnv& env, AnimatedActor& a, Fields& f);
void init(AnimatedActor& a, Fields& f) {
    if (!bindActorModel(a, models[a.spawnByte])) { // exe 0x5e0bc0
        freeActor(a);                              // exe 0x5e1490
        return;
    }
    a.anim = 5;
    a.prevAnim = 0xff;
    f.startKey = 0;
    const uint16_t hp = rowHitPoints(a);
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(box.stateAddress());
    a.interactionBox.pointAt(liftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock();
    f.mode |= kUnseen;
    f.startKey = 0;
    a.parts[kShieldPart].flags &= uint8_t(~kPartHidden);
    a.parts[kBrokenShieldPart].flags |= kPartHidden;
    a.enterState(uint8_t(State::Main));
}
void mainState(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if (!(f.mode & kModeNoHurt)) {
        const Hurt k = hurt(a, f);
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(int32_t(f.hitYaw)));
        if (k == Hurt::Hit) {
            if (uint32_t(d + 0x400) >= 0x800)
                a.subState = uint8_t((f.mode & kShieldLost) ? Sub::KnockBack : Sub::Flinch);
            else
                a.subState = uint8_t(Sub::KnockForward);
            a.restartStep();
        } else if (k == Hurt::WeakPoint) {
            enter(a, Sub::ShieldLost);
        } else if (k == Hurt::Dead) {
            a.enterState(uint8_t(State::Death));
            a.subState = uint8_t(uint32_t(d + 0x400) >= 0x800 ? DeathSub::KnockBack : DeathSub::KnockForward);
            death(env, a, f);
            return;
        }
    }
    switch (Sub(a.subState)) { // [0x88e9a4]
    case Sub::Wake: wake(a, f); break;
    case Sub::Stand: stand(a, f); break;
    case Sub::StandFacing: standFacing(a, f); break;
    case Sub::Shuffle: shuffle(a, f); break;
    case Sub::Attack: attack(a, f); break;
    case Sub::Guard: guard(a, f); break;
    case Sub::HoldGuard: holdGuard(a, f); break;
    case Sub::LowerGuard: lowerGuard(a, f); break;
    case Sub::Turn: turn(a, f); break;
    case Sub::Far: waitFar(a, f); break;
    case Sub::Flinch: flinch(a, f); break;
    case Sub::KnockBack:
        knock(a, f, 0xd, 0x180);
        knocked(a, f);
        break;
    case Sub::KnockForward:
        knock(a, f, 0xa, -0x180);
        knocked(a, f);
        break;
    case Sub::Knocked: knocked(a, f); break;
    case Sub::ShieldLost: shieldLost(a, f); break;
    case Sub::Fall: fall(a, f); break;
    case Sub::Ready: attackReady(a, f); break;
    case Sub::Aim: attackAim(a, f); break;
    case Sub::Sweep: attackSweep(a, f); break;
    case Sub::Lower: attackLower(a, f); break;
    default: notPortedAt(kStoryPort, "class 0x44e4c0 sub-state", a.subState);
    }
    if (collide(env, a, f) && f.mayFall == 1) enter(a, Sub::Fall);
    if (playerLiftTargetCheck(a, 0)) { // exe 0x573a60
        f.mode |= kModeLifted;
        a.enterState(uint8_t(State::Carried));
        shieldIdle(f);
    }
}
// exe 0x44f6c0: thrown -- it tumbles (its pitch), bounces once, lands knocked (or dies)
void thrown(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    if ((f.hitBox.result & kHitAttackSolid) && a.hitPoints >= 0) {
        const uint16_t vz = uint16_t(a.velocity.z);
        a.hitPoints = -1;
        a.velocity.z = int16_t(-int(vz));
        spawnHitSpark(kActorSweepGroup2, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
    }
    if (a.stepCounter == 0) {
        a.rotation.x = int16_t(uint16_t(a.rotation.x) + 0x80u);
        if (a.rotation.x >= 0x200) a.rotation.x = 0x200;
        if (fly(a, f, f.flightYaw)) {
            moveAlongYaw(a.pos, f.flightYaw, 0, a.velocity.y, a.velocity.z);
            a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0xc00u);
            a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
            ++a.stepCounter;
            a.anim = 0xe;
        }
        collide(env, a, f);
        return;
    }
    a.rotation.x = int16_t(uint16_t(a.rotation.x) - 0x100u);
    if (a.rotation.x <= 0) a.rotation.x = 0;
    if (fly(a, f, f.flightYaw)) {
        a.rotation.x = 0;
        if (a.hitPoints >= 0) {
            a.anim = 0xf;
            a.state = uint8_t(State::Main);
            a.subState = uint8_t(Sub::Knocked);
            a.enterStep(2);
        } else {
            a.enterState(uint8_t(State::Death));
            a.subState = uint8_t(DeathSub::Blow);
        }
        a.airborne = 0;
        setMode(f, 0);
    }
    collide(env, a, f);
}
// exe 0x44f5d0: at the player's hands until thrown (along the player's aim) / dropped
void held(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    f.contact = 0;
    if (a.step == 0) {
        a.anim = 0x19;
        a.enterStep(1);
    }
    carryAtHands(a, 0, 0, 0);               // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0xc0, -0x200, v); // exe 0x5e3e50
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        setMode(f, kModeThrown | kModeBounces | kModeNoHurt);
    } else {
        a.velocity.y = -0x180;
        a.velocity.z = -0x100;
        setMode(f, kModeLifted | kModeBounces | kModeNoHurt);
    }
    carryDropFromPlayer(a); // exe 0x573990
    a.acceleration.y = 0x40;
    a.rotation.x = -0x480;
    const int16_t yaw = player().rotation.y;
    a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) - 0x60u);
    a.rotation.y = yaw;
    f.flightYaw = yaw;
    a.airborne = 1;
    enter(a, Sub::Stand);
    thrown(env, a, f);
}
// exe 0x44f900: flies until it lands, then (the animation) blows up
void deathFly(AnimatedActor& a, Fields& f) {
    if (a.step == 0) {
        if (fly(a, f, f.flightYaw)) {
            a.airborne = 0;
            a.enterStep(1);
            ++a.anim;
        }
        return;
    }
    if (a.animEnded()) {
        a.subState = uint8_t(DeathSub::Blow);
        a.restartStep();
    }
}
// exe 0x44f820 / 0x44f890: thrown up along its yaw (the sound), the death's flight
void deathKnock(AnimatedActor& a, Fields& f, uint8_t anim, int16_t vz) {
    a.anim = anim;
    a.velocity.x = 0;
    a.velocity.y = -0x100;
    a.velocity.z = vz;
    a.acceleration.y = 0x40;
    f.flightYaw = a.rotation.y;
    a.airborne = 1;
    setMode(f, kModeNoHurt | kModeBounces);
    shieldIdle(f);
    soundPlayAtPos(kSoundKnocked, a.pos);
    a.subState = uint8_t(DeathSub::Fly);
    a.restartStep();
    deathFly(a, f);
}
// exe 0x44f950: blinking 0x1e ticks (an explosion), the shield dropped, the drops, freed
void deathBlow(AnimatedActor& a, Fields& f) {
    a.tint = kTintNeutral;
    const uint8_t s = a.step;
    if (s == 2) {
        if (!(f.mode & kShieldLost)) dropShield(a, bonePointOf(a, 3));
        spawnDrops(a.pos, statsOf(a).dropTable); // exe 0x5f1270
        freeActor(a);                            // exe 0x5e1490
        return;
    }
    if (s == 0) {
        f.counter = 0x1e;
        const ShortVec4& p = bonePointOf(a, 0);
        explosionAt(p.x, p.y, p.z, 0x60, 0x1e);
        countDefeat(); // exe 0x5e53e0
        a.enterStep(1);
    }
    if (s <= 1) {
        if (f.counter & 1) a.tint = kTintDeathBlink;
        const uint8_t c = uint8_t(f.counter - 1u);
        f.counter = c;
        if (c == 0) a.enterStep(2);
    }
    if (int8_t(f.counter) < 0xf) f.mode |= kUnseen;
}
// exe 0x44f7f0 [0x88ea00]
void death(const PlayerEnv& env, AnimatedActor& a, Fields& f) {
    switch (DeathSub(a.subState)) {
    case DeathSub::KnockBack: deathKnock(a, f, 0xd, 0x180); break;
    case DeathSub::KnockForward: deathKnock(a, f, 0xa, -0x180); break;
    case DeathSub::Fly: deathFly(a, f); break;
    case DeathSub::Blow: deathBlow(a, f); break;
    default: notPortedAt(kStoryPort, "class 0x44e4c0 death step", a.subState);
    }
    if (a.flags & kActorLive) collide(env, a, f);
}
// exe 0x44e4c0
void update(const PlayerEnv& env, AnimatedActor& a) {
    auto&& f = classState<Fields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    savePrevious(a);
    switch (State(a.state)) { // [0x88e98c]
    case State::Init: init(a, f); break;
    case State::Main: mainState(env, a, f); break;
    case State::Carried: // exe 0x44f5b0 [0x88e9f8]
        if (a.subState == uint8_t(CarriedSub::Held)) held(env, a, f);
        else if (a.subState == uint8_t(CarriedSub::Thrown)) thrown(env, a, f);
        else notPortedAt(kStoryPort, "class 0x44e4c0 carried sub-state", a.subState);
        break;
    case State::Death: death(env, a, f); break;
    default: notPortedAt(kStoryPort, "class 0x44e4c0 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStep(a); // exe 0x5e1e80
    else animSet(a, a.anim, f.startKey);   // exe 0x5e1c30
    animPose(a);
    f.startKey = 0;
    if (a.animKey.events & 0x80) soundPlayAtPos(kSoundStep, a.pos);
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    f.hitBox.result = 0;
    const uint8_t cl = f.mode;
    f.weakBox.previousX = a.prevPos.x.whole;
    f.weakBox.previousZ = a.prevPos.z.whole;
    f.weakBox.result = 0;
    const AnimatedActorStats stats = statsOf(a);
    const bool probe = (cl & kUnseen) != 0;
    const uint32_t unseen = probe ? kHitNoLockOn : 0u;
    const int16_t pos[3] = {a.pos.x.whole, int16_t(a.pos.y.whole - 0x64), a.pos.z.whole};
    const uint8_t slot = a.slot;
    if (a.hitPoints < 0) {
        hitListSubmit(pos, f.hitBox,
                      hitOwner(slot) | unseen | hitGroup(kHitGroupBodies) | kHitContactMotion | hitRadius(0x50),
                      kHitAttackBump);
        return;
    }
    if (cl & kModeLifted) return;
    const uint8_t shield = cl & (kShieldRaised | kShieldLost);
    const uint32_t radius = shield != kShieldRaised ? 0x50u : 0x20u;
    if (!(cl & kModeThrown)) {
        hitListSubmit(pos, f.hitBox,
                      hitOwner(slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupBodies) | kHitContactMotion |
                          hitRadius(radius) | unseen,
                      uint32_t(stats.attack) | kHitAttackSolid);
    } else {
        hitListSubmit(pos, f.hitBox,
                      hitOwner(slot) | hitHeightLevel(stats.hitHeight) | hitGroup(kHitGroupPlayerAttacks) |
                          kHitNoLockOn | kHitContactMotion | hitRadius(radius),
                      uint32_t(stats.bodyAttack) | kHitAttackByPlayer);
    }
    if (shield == kShieldRaised) { // the shield's weak point: the second box at bone 3
        const ShortVec4& p = bonePointOf(a, 3);
        const int16_t bp[3] = {p.x, p.y, p.z};
        hitListSubmit(bp, f.weakBox, hitOwner(slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x70),
                      kHitAttackDeflects | 1u);
    }
    if (probe) {
        const uint8_t res = f.sight;
        if (!(res & kSightRunning)) lineOfSightSpawn(a, 2, bonePointOf(a, 0), kSightBox, 0, f.sight, 4); // 0x5e5970
        if (res & kSightDone) {
            if (!(f.sight & 1)) f.mode &= uint8_t(~kUnseen);
            f.sight = 0;
        }
    }
    // exe 0x5f20f0(actor, 0x80, 0x100, 0) unless state 2 sub 0: the shadow (render output)
}

} // namespace gunner

// ==== the gunner's shot (exe 0x5cc220, pool 0x40 ST55 stage type 0x22) ==============================================
// It flies along its rotation sinking, grows; its life out or a wall: it fades. Its glow is a billboard draw command in
// three colour phases.
namespace gunnerShot {

enum class State : uint8_t { Launch = 0, Fly = 1, Fade = 2 };
using exam_enemies_a_state::gunnerShot::Fields;
const DataTable<CollisionBoxRecord> box{TableId::GunnerShotBox};
constexpr uint32_t kShotGroup = 3;
constexpr uint32_t kShotFlag8000 = 0x8000; // a flag of its attack word (its reader is not traced)

// exe 0x5cc220
void update(const PlayerEnv& env, ProjectileActor& a) {
    auto&& f = classState<Fields>(a);
    a.prevPos = a.pos;
    a.wasAirborne = a.airborne;
    a.prevRotation = a.rotation;
    switch (State(a.state)) {
    case State::Launch:
        a.clearClassBlock();
        f.ticks = uint16_t(int32_t(int8_t(f.life)));
        f.radius = f.size;
        f.brightness = 0x80;
        a.velocity.y = 0;
        a.collisionBox.pointAt(box.stateAddress());
        a.enterState(uint8_t(State::Fly));
        [[fallthrough]];
    case State::Fly:
        moveAlongRotation(a.pos, a.rotation, 0, a.velocity.y, a.velocity.z); // exe 0x5e37b0
        a.velocity.y = int16_t(uint16_t(a.velocity.y) - 6u);
        f.radius = int16_t(uint16_t(f.radius) + uint16_t(f.growth));
        --f.ticks;
        if (f.ticks == 0) a.enterState(uint8_t(State::Fade));
        if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
            a.velocity.z = 0;
            a.enterState(uint8_t(State::Fade));
        }
        if (f.side != 0) {
            const int16_t pos[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
            f.hitBox.previousX = a.prevPos.x.whole;
            f.hitBox.previousZ = a.prevPos.z.whole;
            const uint32_t owner = hitRadius(uint32_t(int32_t(f.radius) - 0x20)) | hitGroup(kShotGroup) |
                                   hitOwner(a.slot);
            hitListSubmit(pos, f.hitBox, owner,
                          uint32_t(int32_t(int16_t(f.attack))) | kHitAttackStrong | kShotFlag8000); // exe 0x5e4120
        }
        break;
    case State::Fade:
        integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        a.velocity.y = int16_t(uint16_t(a.velocity.y) - 6u);
        f.radius = int16_t(uint16_t(f.radius) + uint16_t(f.growth));
        f.brightness = int16_t(uint16_t(f.brightness) - 0x10u);
        if (f.brightness <= 0) {
            freeActor(a); // exe 0x5e1500
            return;
        }
        break;
    default: break;
    }
    // the glow billboard (word 0 unwritten), three colour phases
    const int32_t u = f.texelU;
    const int32_t u2 = u + 0x1f;
    uint8_t ph = uint8_t(f.phase + 1u);
    if (ph > 2) ph = 0;
    f.phase = ph;
    const int32_t e = f.brightness;
    int32_t col;
    if (ph == 0) col = (e * 5) << 13 | (e * 5) << 4 | (e * 3) << 20;
    else if (ph == 1) col = (e * 5) << 4 | (e * 3) << 20 | e << 15;
    else col = (e * 5) << 13 | (e * 5) << 5 | (e * 3) << 20;
    col >>= 7;
    dc::Billboard bb;
    bb.colour = dc::billboardColour(uint32_t(col));
    bb.xy = dc::packXY(a.pos.x.whole, a.pos.y.whole);
    bb.z = dc::zWord(a.pos.z.whole);
    bb.clut = 0x7c92;
    bb.tpage = 0x2f;
    bb.uv[0] = dc::texel(uint32_t(u), 0x40);
    bb.uv[1] = dc::texel(uint32_t(u2), 0x40);
    bb.uv[2] = dc::texel(uint32_t(u), 0x5f);
    bb.uv[3] = dc::texel(uint32_t(u2), 0x5f);
    bb.sizeRotation = uint32_t(int32_t(f.radius));
    dc::emit(bb, a.at);
    f.texelU = int16_t((uint32_t(f.texelUByte) + 0x20u) & 0xffu);
}

} // namespace gunnerShot

// ==== the knocked-off shield (exe 0x592fa0, ST55 static type 5 sub 0, states [0x8cdf44]) ==========================
namespace shield {

enum class State : uint8_t { Init = 0, Fly = 1 };   // exe 0x592ff0 / 0x593080
enum class FlySub : uint8_t { Bouncing = 0, Fading = 1 };
using exam_enemies_a_state::shield::Fields;
const DataTable<CollisionBoxRecord> box{TableId::ShieldBox};
constexpr uint8_t kSemiOn = ((1 & 1) << 4 | (1 & 3)) << 1; // exe 0x5e1a70(actor, 1, 1)

// exe 0x592fa0
void update(const PlayerEnv& env, StaticActor& a) {
    auto&& f = classState<Fields>(a);
    savePrevious(a);
    switch (State(a.state)) {
    case State::Init: // exe 0x592ff0
        a.enterState(uint8_t(State::Fly));
        if (!bindActorModel(a, a.variant)) { // exe 0x5e0bc0(actor, actor + 6)
            freeActor(a);                    // exe 0x5e1540
            return;
        }
        a.maxHitPoints = 1;
        a.hitPoints = 1;
        a.velocity.x = 0;
        a.velocity.y = -0x380;
        a.velocity.z = -0x100;
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.airborne = 1;
        a.onPlatform = 0;
        a.collisionBox.pointAt(box.stateAddress());
        a.interactionBox = nullptr;
        a.interaction = 0;
        a.clearClassBlock();
        f.flightYaw = int16_t(f.yaw);
        return;
    case State::Fly: { // exe 0x593080: tumbles off along its yaw, bounces; fades 0x10 ticks after the first bounce
        const int vz = a.velocity.z, vy = a.velocity.y;
        a.rotation.x = int16_t(uint16_t(a.rotation.x) + 0x80u);
        a.rotation.y = int16_t(uint16_t(a.rotation.y) - 0x100u);
        moveAlongYaw(a.pos, f.flightYaw, 0, vy, vz); // exe 0x5e36f0
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + 0x40u);
        if (a.subState == uint8_t(FlySub::Bouncing)) {
            if (f.bounced != 0) {
                a.subState = uint8_t(FlySub::Fading);
                f.fade = 0x10;
                a.semiTransparency = kSemiOn;
            }
        } else if (a.subState == uint8_t(FlySub::Fading)) {
            const int c = int8_t(f.fade);
            a.tint = uint16_t(((c << 5 | c) << 5) | c);
            --f.fade;
            if (f.fade == 0) {
                freeActor(a); // exe 0x5e1540
                return;
            }
        }
        if (!(a.flags & kActorLive)) return;
        f.bounced = 0;
        if (int32_t(actorTestMove(env, a)) >= 0) return; // exe 0x40f570
        const uint32_t r = actorPushOut(env, a);         // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & 0xf00) a.velocity.z = 0;
        if (r & kSolidAboveOrBelow) {
            const int t = (-int(a.velocity.y)) >> 1;
            a.velocity.z = int16_t(a.velocity.z >> 1);
            a.velocity.y = int16_t(t);
            f.bounced = 1;
        }
        return;
    }
    default: notPortedAt(kStoryPort, "class 0x592fa0 state (null entry of 0x8cdf44)", a.state);
    }
}

} // namespace shield

} // namespace

void examHopperUpdate(const PlayerEnv& env, uint32_t actor) { hopper::update(env, animatedActorAt(actor)); }
void examTumblerUpdate(const PlayerEnv& env, uint32_t actor) { tumbler::update(env, animatedActorAt(actor)); }
void examShieldGunnerUpdate(const PlayerEnv& env, uint32_t actor) { gunner::update(env, animatedActorAt(actor)); }
void examGunnerShotUpdate(const PlayerEnv& env, uint32_t actor) {
    gunnerShot::update(env, projectileActorAt(actor));
}
void examShieldUpdate(const PlayerEnv& env, uint32_t actor) { shield::update(env, staticActorAt(actor)); }

} // namespace od::port
