// The burrowing sand enemy of Saul Kada (ST27 / ST34; pool 0x20 type 0x35, exe 0x4a9eb0; hypothesis names).
// States [0x893e2c]: 0 bind 0x4a9fa0, 1 run 0x4aa100 (its sub-states [0x893e38]), 2 freed 0x468fa0.
// Its hit record lives in its third bone block (+0x60): the body's hit box, the limbs' hit box and the two dust
// points of its dives. Its homes: its spawn point and the point 0x300 ahead of its spawn yaw.
// Sub-states: 0 pace 0x4aa350 (buried wander, or pacing between the homes, then an attack), 1 leap 0x4aa970, 2 shoot
// 0x4aaa40, 3 dive 0x4aadf0, 4 emerge 0x4ab030, 5 charge 0x4ab170, 6 die 0x4ab300, 7 burrow 0x4ab4f0, 8 hidden
// 0x4ab630.
#include "content/saul_kada/sand_burrower.hpp"
#include "content/saul_kada/sand_burrower_state.hpp"

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
#include "game/field_alias.hpp"
#include "game/footstep_dust.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace sand_burrower_state;

namespace {

enum class BurrowerState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class BurrowerSub : uint8_t {
    Pace = 0, Leap = 1, Shoot = 2, Dive = 3, Emerge = 4, Charge = 5, Die = 6, Burrow = 7, Hidden = 8
};
enum class BurrowerAnim : uint8_t {
    Leap = 0, Land = 1, SpitA = 2, SpitB = 3, DiveStart = 4, Dive = 5, FlinchA = 6, FlinchShootA = 7, FlinchDiveA = 8,
    FlinchB = 9, FlinchShootB = 0xa, FlinchDiveB = 0xb, Charge = 0xc, Buried = 0xd
};
// Its spawn parameters: its home's x / z
struct BurrowerParams {
    explicit BurrowerParams(const AnimatedActor& actor) : BurrowerParams(const_cast<AnimatedActor&>(actor)) {}
    explicit BurrowerParams(AnimatedActor& actor) : homeX(actor.params, 0), homeZ(actor.params, 2) {}
    BytesField<int16_t, 4> homeX;
    BytesField<int16_t, 4> homeZ;
};
constexpr uint8_t kBurrowerHitLive = 0x01; // its body's attack strong (it bites)
constexpr uint8_t kBurrowerSway = 0x02;    // its head sways toward the player
constexpr uint8_t kBurrowerFlinch = 0x04;  // its next landing flinches
constexpr uint8_t kBurrowerSecondHome = 0x08;
constexpr uint8_t kBurrowerDead = 0x10;
constexpr uint8_t kBurrowerFreeze = 0x20;  // its pose held
constexpr uint8_t kBurrowerWander = 0x40;  // wandering buried
constexpr uint8_t kBurrowerMarks = 0x80;   // its group's marks kept up
constexpr uint8_t kNoAttack = 0xff;
constexpr uint16_t kHurtSound = 0x8d, kLeapSound = 0x207, kSplashSound = 0x209;
constexpr uint32_t kHomeReach = 0xbb8, kWakeReach = 0xb54;
constexpr uint8_t kShotType = 0; // the stage's pool-0x40 type of its spit
// the motion of its animations by key (y, z << 4)
const DataTable<ScalarTable<uint16_t, 0x1a>> leapRise{TableId::SandBurrowerLeapRise}, leapAhead{TableId::SandBurrowerLeapAhead};
const DataTable<ScalarTable<uint16_t, 0x1e>> diveRise{TableId::SandBurrowerDiveRise}, diveAhead{TableId::SandBurrowerDiveAhead};
const DataTable<ScalarTable<uint16_t, 0x32>> chargeRise{TableId::SandBurrowerChargeRise}, chargeAhead{TableId::SandBurrowerChargeAhead};
// its spit's parameters (the stage's pool-0x40 type 0; its class is not this file's): its ticks, its attack
struct SpitParams {
    explicit SpitParams(const ProjectileActor& actor) : SpitParams(const_cast<ProjectileActor&>(actor)) {}
    explicit SpitParams(ProjectileActor& actor) : ticks(actor.params, 0), attack(actor.params[2]) {}
    BytesField<uint16_t, 4> ticks;
    uint8_t& attack;
};
constexpr size_t kLimbParts = 7, kFirstLimbPart = 4; // its parts 4..10 hidden while buried

uint8_t frame() { return uint8_t(gameState().frame); }
// exe 0x5ad170(actor, variant, point) (variant 0 reads no point)
void dust(const AnimatedActor& actor, uint8_t variant, int x, int y, int z) {
    const int16_t point[3] = {int16_t(x), int16_t(y), int16_t(z)};
    footstepDustAt(actor, variant, point);
}
// exe 0x5e1970(actor, bone, (x, y, z), out)
void bonePt(const AnimatedActor& actor, uint32_t bone, int x, int y, int z, int16_t out[3]) {
    const int16_t v[3] = {int16_t(x), int16_t(y), int16_t(z)};
    bonePointOffset(actor, bone, v, out);
}
const ShortVec4& bone(const AnimatedActor& actor, uint32_t b) { // exe 0x5e1810 (a null result would crash the exe)
    const ShortVec4* p = bonePointOrNull(actor, b);
    if (!p) notPortedAt(kStoryPort, "exe 0x5e1810 returned null, bone", b);
    return *p;
}
void partsHidden(AnimatedActor& actor, size_t first, size_t count, bool hidden) {
    for (size_t i = 0; i < count; ++i) {
        if (hidden) actor.parts[first + i].flags |= kPartHidden;
        else actor.parts[first + i].flags &= uint8_t(~kPartHidden);
    }
}
// the home in use: the second at kBurrowerSecondHome, else the spawn's; y = its own
void home(AnimatedActor& actor, const BurrowerFields& b, int16_t h[3], bool second) {
    const BurrowerParams params(actor);
    h[0] = second ? b.secondHomeX : params.homeX;
    h[1] = actor.pos.y.whole;
    h[2] = second ? b.secondHomeZ : params.homeZ;
}
void self16(const AnimatedActor& actor, int16_t p[3]) {
    p[0] = actor.pos.x.whole;
    p[1] = actor.pos.y.whole;
    p[2] = actor.pos.z.whole;
}
void player16(int16_t p[3]) {
    const Player& pl = player();
    p[0] = pl.pos.x.whole;
    p[1] = pl.pos.y.whole;
    p[2] = pl.pos.z.whole;
}
bool atSecondHome(const BurrowerFields& b) { return (b.flags & kBurrowerSecondHome) != 0; }
// the point 0x4b0 beyond the home opposite to the one in use (exe 0x5e40c0(spawn yaw (+0x800), 0x4b0))
void farPoint(AnimatedActor& actor, const BurrowerFields& b, int16_t p[3]) {
    const bool second = atSecondHome(b);
    const BurrowerParams params(actor);
    int16_t dx, dz;
    angleVector(int(uint16_t(b.spawnYaw + (second ? 0 : 0x800))), 0x4b0, dx, dz);
    p[0] = int16_t(uint16_t(dx + (second ? b.secondHomeX : params.homeX)));
    p[1] = actor.pos.y.whole;
    p[2] = int16_t(uint16_t(dz + (second ? b.secondHomeZ : params.homeZ)));
}
void turnToward(AnimatedActor& actor, const int16_t to[3], const int16_t from[3], int step) {
    turnYawTo(actor.rotation.y, uint32_t(angleOf(to, from)), step); // exe 0x5e3df0
}
void turnTowardPlayer(AnimatedActor& actor) {
    turnYawTo(actor.rotation.y, uint32_t(angleOf(player().pos, actor.pos)), 0x40); // exe 0x5e3bc0
}
void setAnim(AnimatedActor& actor, BurrowerAnim anim) { actor.anim = uint8_t(anim); }

// exe 0x4a9fa0: its model, the hit points of its row, its box, its record, its second home, its enemy group; buried
void bind(const PlayerEnv& env, AnimatedActor& actor, BurrowerFields& b) {
    actor.flags |= kActorDrawn;
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    setAnim(actor, BurrowerAnim::Buried);
    actor.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(actor.type) ? animatedStats(actor.type).hitPoints : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::SandBurrowerBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    b.headSway = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    const uint32_t block = boneBlockAddress(actor, 2); // exe 0x5e1780(actor, 2)
    if (block == 0) notPorted(kStoryPort, "exe 0x5e1780: no such bone block (the exe writes through 0x60)");
    const uint32_t record = block + 0x60;
    b.flags = kBurrowerHitLive;
    b.record.pointAt(record);
    b.timerB = 0;
    int16_t dx, dz;
    angleVector(actor.rotation.y, 0x300, dx, dz); // exe 0x5e40c0
    const BurrowerParams params(actor);
    b.secondHomeX = int16_t(uint16_t(uint16_t(params.homeX) + uint16_t(dx)));
    b.secondHomeZ = int16_t(uint16_t(uint16_t(params.homeZ) + uint16_t(dz)));
    b.spawnYaw = uint16_t(actor.rotation.y);
    b.attack = kNoAttack;
    b.lastAttack = kNoAttack;
    b.repeats = 0;
    b.damageTaken = 0;
    b.group = uint8_t(enemyGroupFindFor(actor, 0)); // exe 0x5e6cd0(actor, 0)
    b.groupMarks = 0;
    BurrowerRecord& rec = b.rec();
    rec.body.result = 0;
    rec.limbs.result = 0;
    partsHidden(actor, kFirstLimbPart, kLimbParts, true);
    b.spawnHeight = actor.pos.y.whole;
    actor.pos.y.whole = int16_t(actorGround(env, actor) + 0x140); // exe 0x40f190
    actor.state = uint8_t(BurrowerState::Run);
    actor.subState = uint8_t(BurrowerSub::Hidden);
    actor.stepCounter = 1;
}

// exe 0x4aa350: surfaced -- the buried wander, or pacing between the homes, then an attack
void pace(AnimatedActor& actor, BurrowerFields& b) {
    BurrowerRecord& rec = b.rec();
    int16_t A[3], B[3];
    if (b.flags & kBurrowerWander) {
        farPoint(actor, b, A);
        integrateVelocity(actor, 0, 0, -0x180);
        self16(actor, B);
        if (planarDistance16(B, A) >= 0x12c) turnToward(actor, A, B, 0x30);
        b.timerB -= 1u;
        home(actor, b, A, atSecondHome(b));
        player16(B);
        const uint32_t d = uint32_t(planarDistance16(B, A));
        if (d > kHomeReach) { // the player left: burrow home
            actor.subState = uint8_t(BurrowerSub::Burrow);
            b.firstTick = 1;
            return;
        }
        if ((frame() & 7) == 0) dust(actor, 0, A[0], A[1], A[2]);
        if (b.timerB != 0) return;
        bool toggle;
        if (actor.variant & 1) toggle = d < kWakeReach;
        else toggle = d < kWakeReach && pointVisibleOrNear(actor.pos, 0) == 0; // exe 0x5e59a0
        if (toggle) b.flags ^= kBurrowerWander;
        else b.timerB = 1;
        return;
    }
    home(actor, b, A, atSecondHome(b));
    self16(actor, B);
    if ((actor.variant & 2) && int8_t(b.attack) < 0) { // the attack picked (no more than 4 repeats of one)
        const uint8_t r = uint8_t(rngStep() & 1);
        b.attack = r;
        if (b.lastAttack == r && b.repeats <= 3) {
            b.repeats += 1u;
        } else {
            if (b.lastAttack == r) b.attack = 1;
            b.repeats = 0;
        }
    }
    const uint32_t reach = ((actor.variant & 0x7f) && b.attack != 0) ? 0xc8 : 0x190;
    if (uint32_t(planarDistance16(B, A)) >= reach) { // back to the home in use
        turnToward(actor, A, B, 0x40);
        integrateVelocity(actor, 0, 0, -0x180);
        actor.pos.y.whole = int16_t(uint16_t(b.spawnHeight) + 0xd0u);
        if ((frame() & 7) == 0) dust(actor, 0, A[0], A[1], A[2]);
        return;
    }
    home(actor, b, A, !atSecondHome(b)); // the other home
    self16(actor, B);
    const int yaw = angleOf(A, B);
    if (uint32_t(angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(yaw)) + 0x40) > 0x80) {
        turnToward(actor, A, B, 0x40);
        return;
    }
    const uint8_t variant = actor.variant;
    if ((variant & 0x7f) && b.attack != 0) {
        if (!(variant & 1) && b.attack != 1) return;
        // the charge toward the other home
        actor.subState = uint8_t(BurrowerSub::Charge);
        b.firstTick = 1;
        b.lastAttack = b.attack;
        b.attack = kNoAttack;
        setAnim(actor, BurrowerAnim::Charge);
        b.timerA = 0xa;
        b.flags ^= kBurrowerHitLive;
        partsHidden(actor, kFirstLimbPart, kLimbParts, false);
        b.flags ^= kBurrowerSecondHome;
        turnToward(actor, A, B, 0x40);
        rec.startPoint.x = actor.pos.x.whole;
        rec.startPoint.y = actor.pos.y.whole;
        rec.startPoint.z = actor.pos.z.whole;
        soundPlayAtPos(kLeapSound, actor.pos);
        return;
    }
    const uint32_t marks = enemyGroupMarks(int32_t(b.group), EnemyGroupMarks::Either) & 3;
    if (marks == 3) { // both marks up: back to the buried wander
        b.timerB = 0x7f;
        b.flags |= kBurrowerWander;
        return;
    }
    // the leap at the player
    actor.subState = uint8_t(BurrowerSub::Leap);
    b.firstTick = 1;
    b.lastAttack = b.attack;
    b.attack = kNoAttack;
    setAnim(actor, BurrowerAnim::Leap);
    soundPlayAtPos(kLeapSound, actor.pos);
    b.flags |= kBurrowerMarks;
    b.groupMarks = (marks & 1) ? 2 : 1;
    integrateVelocity(actor, 0, 0xb00, 0x1300);
    b.flags ^= kBurrowerHitLive;
    partsHidden(actor, kFirstLimbPart, kLimbParts, false);
    turnTowardPlayer(actor);
}

// exe 0x4aa970: the leap's motion by key; landed -> the shooting (or a flinch)
void leap(AnimatedActor& actor, BurrowerFields& b) {
    if (!actor.animEnded()) {
        const size_t key = size_t(int32_t(actor.animKey.frame));
        integrateVelocity(actor, 0, int(leapRise[key]) << 4, int(leapAhead[key]) << 4);
    } else {
        actor.subState = uint8_t(BurrowerSub::Shoot);
        b.flags |= kBurrowerSway;
        if (b.flags & kBurrowerFlinch) {
            b.timerB = 0x12;
            if (rngStep() & 1) {
                setAnim(actor, BurrowerAnim::FlinchA);
                actor.step = 0;
            } else {
                setAnim(actor, BurrowerAnim::FlinchB);
                actor.step = 1;
            }
        } else {
            b.timerB = 0xc;
            setAnim(actor, BurrowerAnim::Land);
            b.timerA = 3;
            actor.step = 0;
        }
        integrateVelocity(actor, 0, 0, -0xc80);
    }
    turnTowardPlayer(actor);
}

// exe 0x4aaa40: facing the player, spitting shots on its animation's event; done (or the player left): the dive
void shoot(AnimatedActor& actor, BurrowerFields& b) {
    if (actor.animEnded()) {
        if (b.flags & kBurrowerFlinch) {
            const uint8_t anim = uint8_t(actor.step == 0 ? BurrowerAnim::FlinchShootA : BurrowerAnim::FlinchShootB);
            if (actor.anim == anim) actor.prevAnim = 0xff;
            actor.anim = anim;
        } else if (b.timerA == 0) {
            if (actor.step == 0) {
                setAnim(actor, BurrowerAnim::SpitA);
                b.timerA = 3;
                ++actor.step;
            } else {
                setAnim(actor, BurrowerAnim::SpitB);
                b.timerA = 3;
                --actor.step;
            }
        } else {
            b.timerA -= 1u;
        }
    }
    if (actor.animKey.events & 1) {
        b.timerB -= 1u;
        if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
            ProjectileActor& spit = projectileActorAt(slot);
            const uint8_t loop = spit.loopFlags;
            spit.flags = kActorLive;
            spit.type = kShotType;
            spit.loopFlags = uint8_t(loop | kActorSweepGroup2);
            const ShortVec4& mouth = bone(actor, 2);
            spit.pos.x.whole = mouth.x;
            spit.pos.y.whole = mouth.y;
            spit.pos.z.whole = mouth.z;
            spit.velocity.setXY(0, 0);
            spit.velocity.z = int16_t(0xfd00);
            // the player, 100 up
            const Player& p = player();
            const int16_t target[3] = {p.pos.x.whole, int16_t(uint16_t(p.pos.y.whole - 0x64)), p.pos.z.whole};
            if (pointVisibleOrNear(actor.pos, 0) == 0) { // exe 0x5e59a0
                spit.rotation.x = int16_t(pitchOf(spit.pos, target) + 0x800);
                spit.rotation.y = actor.rotation.y;
            } else { // off screen: scattered
                rngStep();
                const int pitch = pitchOf(spit.pos, target);
                spit.rotation.x = int16_t(pitch + int(uint8_t(rngLastValue()) & 0x7f) + 0x7c1);
                const uint32_t r = rngStep();
                spit.rotation.y = int16_t(uint16_t((r & 0x7f) + uint16_t(actor.rotation.y) - 0x3fu));
            }
            spit.rotation.z = 0;
            SpitParams params(projectileActorAt(slot));
            params.ticks = 0x96;
            params.attack = animatedStats(actor.type).strongAttack;
        }
    }
    int16_t A[3], B[3];
    home(actor, b, A, false);
    player16(B);
    const uint32_t d = uint32_t(planarDistance16(B, A));
    if (b.timerB != 0 && d <= kHomeReach) {
        turnTowardPlayer(actor);
        if (b.flags & kBurrowerSway) { // its head sways toward the player's bone point 1
            const ShortVec4& head = bone(actor, 2);
            const int to = pitchOf(head, player().bonePoints[1]) + 0x800;
            const int diff = angleDiff(uint32_t(int32_t(b.headSway)), uint32_t(to));
            if (diff + 0x20 > 0x40) b.headSway = int16_t(uint16_t(uint16_t(b.headSway) + (diff > 0 ? 0x20u : uint32_t(-0x20))));
            else b.headSway = int16_t(to);
            const int lim = angleDiff(0, uint32_t(int32_t(b.headSway)));
            if (lim > 0x200) b.headSway = 0x200;
            else if (lim < -0x200) b.headSway = int16_t(0xfe00);
        }
        if ((frame() & 7) == 0) dust(actor, 1, actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole);
        return;
    }
    if (!actor.animEnded()) return;
    // done (or the player left): the dive back under
    actor.subState = uint8_t(BurrowerSub::Dive);
    b.firstTick = 1;
    if (b.flags & kBurrowerFlinch)
        setAnim(actor, actor.anim != uint8_t(BurrowerAnim::FlinchShootA) ? BurrowerAnim::FlinchDiveB
                                                                          : BurrowerAnim::FlinchDiveA);
    else setAnim(actor, BurrowerAnim::DiveStart);
    actor.step = 0;
    b.flags = uint8_t((b.flags | kBurrowerHitLive) ^ (kBurrowerMarks | kBurrowerSway));
    b.headSway = 0;
    b.timerA = 5;
    partsHidden(actor, 8, 3, true);
}

// exe 0x4aadf0: the dive back into the sand, then home and the emergence
void dive(AnimatedActor& actor, BurrowerFields& b) {
    BurrowerRecord& rec = b.rec();
    switch (actor.step) {
    case 0:
        if (actor.animEnded()) {
            setAnim(actor, BurrowerAnim::Dive);
            integrateVelocity(actor, 0, -0x16a0, -0x340);
            ++actor.step;
        }
        return;
    case 1: break;
    default: return;
    }
    if (!actor.animEnded()) {
        const int8_t key = actor.animKey.frame;
        integrateVelocity(actor, 0, int(diveRise[size_t(int32_t(key))]) << 4, int(diveAhead[size_t(int32_t(key))]) << 4);
        if (actor.animKey.frame == 3) {
            rec.sinkPoint.x = actor.pos.x.whole;
            rec.sinkPoint.y = actor.pos.y.whole;
            rec.sinkPoint.z = actor.pos.z.whole;
            soundPlayAtPos(kSplashSound, actor.pos);
        }
        if (actor.animKey.frame < 3 || (frame() & 3) != 0) return;
        dust(actor, 1, rec.sinkPoint.x, int16_t(uint16_t(rec.sinkPoint.y + 0x90)), rec.sinkPoint.z);
        return;
    }
    if (b.timerA != 0) {
        b.flags |= kBurrowerHitLive;
        b.timerA -= 1u;
        integrateVelocity(actor, 0, 0x130, -0x890);
        return;
    }
    partsHidden(actor, kFirstLimbPart, kLimbParts, true);
    b.firstTick = 1;
    setAnim(actor, BurrowerAnim::Buried);
    ++actor.subState;
    b.timerA = 0x42;
    const int32_t group = int32_t(b.group);
    enemyGroupMark(group, uint16_t(b.groupMarks ^ enemyGroupMarks(group, EnemyGroupMarks::Either)));
    b.groupMarks = 0;
    int16_t A[3];
    if (!atSecondHome(b)) {
        actor.rotation.y = int16_t((uint16_t(b.spawnYaw) - 0x800u) & 0xfff);
        home(actor, b, A, false);
    } else {
        actor.rotation.y = int16_t(b.spawnYaw);
        home(actor, b, A, true);
    }
    int32_t o[3];
    rotateByYaw(actor.rotation.y, 0, 0, -0x7fff, o); // exe 0x5e3750(A, yaw, 0, 0, -0x7fff)
    actor.pos.x.whole = int16_t(uint16_t(uint16_t(A[0]) + uint16_t(uint32_t(o[0]) >> 16)));
    actor.pos.z.whole = int16_t(uint16_t(uint16_t(A[2]) + uint16_t(uint32_t(o[2]) >> 16)));
}

// exe 0x4ab030: rising at the home, then pacing
void emerge(AnimatedActor& actor, BurrowerFields& b) {
    if (b.timerA != 0) {
        b.timerA -= 1u;
        return;
    }
    int16_t A[3], B[3];
    home(actor, b, A, atSecondHome(b));
    self16(actor, B);
    turnToward(actor, A, B, 0x40);
    integrateVelocity(actor, 0, -0x130, -0x100);
    const int top = b.spawnHeight + 0xd0;
    if (top > actor.pos.y.whole) {
        actor.subState = uint8_t(BurrowerSub::Pace);
        actor.pos.y.whole = int16_t(top);
        b.firstTick = 1;
        b.timerA = 2;
        if (actor.stepCounter == 0) {
            b.timerB = 0x7f;
            b.flags |= kBurrowerWander;
        } else {
            b.timerB = 1;
            b.flags |= kBurrowerWander;
            actor.stepCounter = 0;
        }
        return;
    }
    if ((frame() & 7) == 0) dust(actor, 0, A[0], A[1], A[2]);
}

// exe 0x4ab170: the lunge, then the emergence
void charge(AnimatedActor& actor, BurrowerFields& b) {
    BurrowerRecord& rec = b.rec();
    if (!actor.animEnded()) {
        const size_t key = size_t(int32_t(actor.animKey.frame));
        integrateVelocity(actor, 0, int(chargeRise[key]) << 4, int(chargeAhead[key]) << 4);
        if (uint8_t(actor.animKey.frame) == 0x18) {
            rec.sinkPoint.x = actor.pos.x.whole;
            rec.sinkPoint.y = actor.pos.y.whole;
            rec.sinkPoint.z = actor.pos.z.whole;
            soundPlayAtPos(kSplashSound, actor.pos);
        }
        if (actor.animKey.frame > 0x13) b.flags |= kBurrowerHitLive;
        if ((frame() & 7) == 0)
            dust(actor, 1, rec.startPoint.x, int16_t(uint16_t(rec.startPoint.y - 0x90)), rec.startPoint.z);
        if (actor.animKey.frame >= 0x18 && (frame() & 7) == 0)
            dust(actor, 1, rec.sinkPoint.x, int16_t(uint16_t(rec.sinkPoint.y - 0x90)), rec.sinkPoint.z);
        return;
    }
    if (b.timerA != 0) {
        integrateVelocity(actor, 0, 0x2f0, -0x290);
        b.timerA -= 1u;
        return;
    }
    partsHidden(actor, kFirstLimbPart, kLimbParts, true);
    actor.subState = uint8_t(BurrowerSub::Emerge);
    actor.step = 0;
    b.firstTick = 1;
    setAnim(actor, BurrowerAnim::Buried);
    b.timerA = 0x42;
}

// exe 0x4ab300: explosions at its bones, then blasts bone by bone (each part hidden), freed
void die(AnimatedActor& actor, BurrowerFields& b) {
    if (actor.step == 0) {
        if (b.timerB == 0) actor.step = 1;
        else b.timerB -= 1u;
        if (actor.stepCounter != 0) return;
        actor.stepCounter = 1;
        for (int i = 0; i < 9; ++i) {
            int16_t o[3];
            bonePt(actor, uint32_t(i), 0, i == 0 ? -0x10 : 8, i == 0 ? -0x48 : 0x14, o); // exe 0x5e1970
            if (const uint32_t slot = allocActor60(0)) {                                    // exe 0x5e1300
                StaticActor& explosion = staticActorAt(slot);
                const uint8_t loop = explosion.loopFlags;
                explosion.flags = kActorLive;
                explosion.type = typeByte(GlobalStaticType::Explosion);
                explosion.variant = 0;
                explosion.loopFlags = uint8_t(loop | kActorSweepGroup2);
                explosion.pos.x.whole = o[0];
                explosion.pos.y.whole = o[1];
                explosion.pos.z.whole = o[2];
                ExplosionParams params(actorHeaderAt(slot));
                params.spread = 0x60;
                params.flashes = 0x28;
            }
        }
        return;
    }
    if (b.timerA != 0) {
        b.timerA -= 1u;
        return;
    }
    const int i = int8_t(b.attack); // the bone the blasts are at
    if (i >= 9) {
        actor.state = uint8_t(BurrowerState::Free);
        countDefeat();
        return;
    }
    int16_t o[3];
    bonePt(actor, uint32_t(i), 0, i == 0 ? -0x10 : 8, i == 0 ? -0x48 : 0x20, o); // exe 0x5e1970
    if (const uint32_t slot = allocActor60(0)) {                                    // exe 0x5e1300
        StaticActor& blast = staticActorAt(slot);
        const uint8_t loop = blast.loopFlags;
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 0;
        blast.loopFlags = uint8_t(loop | kActorSweepGroup2);
        blast.pos.x.whole = o[0];
        blast.pos.y.whole = o[1];
        blast.pos.z.whole = o[2];
        BlastParams params(actorHeaderAt(slot));
        params.size = 0x80;
        params.attack = 0;
    }
    if (i == 0) partsHidden(actor, 0, 3, true);
    else actor.parts[size_t(2 + i)].flags |= kPartHidden;
    b.timerA = 2;
    b.attack = uint8_t(i + 1);
}

// exe 0x4ab4f0: sinking while moving away; deep enough -> hidden
void burrow(AnimatedActor& actor, BurrowerFields& b) {
    int16_t A[3], B[3];
    farPoint(actor, b, A);
    integrateVelocity(actor, 0, 0x130, -0x180);
    self16(actor, B);
    if (planarDistance16(B, A) >= 0x12c) turnToward(actor, A, B, 0x20);
    b.timerB -= 1u;
    const int base = b.spawnHeight, y = actor.pos.y.whole;
    if (base + 0xd0 > y) {
        if ((frame() & 7) != 0) dust(actor, 0, A[0], A[1], A[2]); // (sic: on the other ticks than the rest)
        return;
    }
    if (base + 0x140 <= y) actor.subState = uint8_t(BurrowerSub::Hidden);
}

// exe 0x4ab630: buried; off screen -> freed; the player within 0xbb8 of its home -> the emergence
void hidden(AnimatedActor& actor, BurrowerFields& b) {
    if (actor.runFlags & kActorOffScreen) {
        actor.state = uint8_t(BurrowerState::Free);
        return;
    }
    int16_t A[3], B[3];
    home(actor, b, A, atSecondHome(b));
    player16(B);
    if (planarDistance16(B, A) < int(kHomeReach)) { // (the exe computes it twice)
        actor.subState = uint8_t(BurrowerSub::Emerge);
        actor.step = 0;
        actor.stepCounter = 1;
        setAnim(actor, BurrowerAnim::Buried);
        b.timerA = 0;
    }
}

// exe 0x4aa100
void run(AnimatedActor& actor, BurrowerFields& b) {
    BurrowerRecord& rec = b.rec();
    if ((rec.body.result & kHitAttackByPlayer) && !(b.flags & kBurrowerDead)) { // hit
        actor.tint = kTintFlash;
        const uint16_t before = uint16_t(actor.hitPoints);
        if (hitPointsAdd(actor, -int(rec.body.result & kHitDamageMask)) != 0) { // exe 0x5e52f0: dead
            b.flags |= kBurrowerDead | kBurrowerFreeze;
            actor.subState = uint8_t(BurrowerSub::Die);
            actor.step = 0;
            actor.stepCounter = 0;
            b.timerB = 0x28;
            b.timerA = 0;
            b.attack = 0;
        } else {
            const uint8_t flags = b.flags;
            if (flags & kBurrowerHitLive) {
                if (int16_t(b.damageTaken) > int(animatedStats(actor.type).bodyAttack)) { // enough: flinch
                    b.damageTaken = 0;
                    b.flags = uint8_t(flags | kBurrowerFlinch);
                } else {
                    b.damageTaken = uint16_t(uint16_t(b.damageTaken) - uint16_t(actor.hitPoints) + before);
                }
            }
            soundPlayAtPos(kHurtSound, actor.pos);
        }
    }
    b.firstTick = 0;
    switch (BurrowerSub(actor.subState)) { // [0x893e38]
    case BurrowerSub::Pace: pace(actor, b); break;
    case BurrowerSub::Leap: leap(actor, b); break;
    case BurrowerSub::Shoot: shoot(actor, b); break;
    case BurrowerSub::Dive: dive(actor, b); break;
    case BurrowerSub::Emerge: emerge(actor, b); break;
    case BurrowerSub::Charge: charge(actor, b); break;
    case BurrowerSub::Die: die(actor, b); break;
    case BurrowerSub::Burrow: burrow(actor, b); break;
    case BurrowerSub::Hidden: hidden(actor, b); break;
    default: notPortedAt(kStoryPort, "0x4aa100 sub-state", actor.subState);
    }
    if (BurrowerSub(actor.subState) == BurrowerSub::Hidden) return;
    if (b.flags & kBurrowerMarks) {
        const int32_t group = int32_t(b.group);
        enemyGroupMark(group, uint16_t(b.groupMarks | enemyGroupMarks(group, EnemyGroupMarks::Either)));
    }
    if (b.flags & kBurrowerDead) return;
    rec.body.result = 0;
    rec.limbs.result = 0;
    const AnimatedActorStats stats = animatedStats(actor.type);
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight);
    for (int k = 10; k > 1; --k) { // the hit spheres on bones 10..2 (bone 2: the body, its record's first box)
        int16_t o[3];
        bonePt(actor, uint32_t(k), 0, k == 2 ? -0x10 : 8, k == 2 ? -0x48 : 0x20, o); // exe 0x5e1970
        if (k == 2) {
            if (b.flags & kBurrowerHitLive)
                hitListSubmit(o, rec.body, owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x90),
                              uint32_t(stats.attack) | kHitAttackHarmless | kHitAttackSolid); // exe 0x5e4120
            else
                hitListSubmit(o, rec.body, owner | hitGroup(kHitGroupBodies) | hitRadius(0x90),
                              uint32_t(stats.attack) | kHitAttackSolid);
        } else if (actor.anim != uint8_t(BurrowerAnim::Buried)) {
            hitListSubmit(o, rec.limbs, owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x90),
                          uint32_t(stats.attack) | kHitAttackHarmless | kHitAttackDeflects);
        }
    }
}

void sandBurrower(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4a9eb0
    BurrowerFields& b = classState<BurrowerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BurrowerState(actor.state)) { // [0x893e2c]
    case BurrowerState::Bind: bind(env, actor, b); break;
    case BurrowerState::Run: run(actor, b); break;
    case BurrowerState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4a9eb0 state", actor.state);
    }
    if (actor.anim == actor.prevAnim) {
        if (b.flags & kBurrowerFreeze) animDecodeCurrent(actor); // exe 0x5e1f30
        else animStep(actor);                                     // exe 0x5e1ea0
    } else {
        animSet(actor, actor.anim, 0); // exe 0x5e1c50
    }
    if (b.flags & kBurrowerSway) { // the head's sway added to the decoded frame's entries 2 / 3 / 4 x
        for (size_t k = 2; k <= 4; ++k) {
            LaneVec4 entry = decodedFrame().entries[k];
            entry.x = int16_t(uint16_t(uint16_t(b.headSway) + uint16_t(entry.x)));
        }
    }
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void sandBurrowerUpdate(const PlayerEnv& env, uint32_t actor) { sandBurrower(env, animatedActorAt(actor)); }

} // namespace od::port
