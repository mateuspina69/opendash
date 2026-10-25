// The reaverbot turret class 0x458400 (see reaverbot_turret.hpp). States [0x88ee88]: 0 0x458560 spawn, 1 0x458660 (the
// behaviour [0x88ee94 + variant * 4]: 0 0x458ae0 stand and face, 1 0x458c70 walk out up to its range while the player
// is in its far box, 2 0x458f90 walk toward the player around the walls and neighbours, 3 0x459190 advance while the
// player is in its far box), 2 0x458680 dying. The boxes [0x88eeb8 + variant * 8] (melee) / [0x88eed8 + variant * 8]
// (range) are in the frame of the player seen from the turret (its yaw reference). Its animations are model-bank
// indices keyed by the behaviour; the shadow (exe 0x5f20f0) is render output.
#include "game/actor_state.hpp"
#include "game/reaverbot_turret_state.hpp"
#include "game/reaverbot_turret.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/charge_glow.hpp"
#include "game/charge_glow_state.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/turret_bullet.hpp"

namespace od::port {

using namespace reaverbot_turret_state;

namespace {

constexpr const char* kTurretPort = "reaverbot turret port"; // the area of its port-gap errors

// +0x154
constexpr uint16_t kTurretNoDamage = 0x02, kTurretNoStagger = 0x08, kTurretAttackStarted = 0x10;
constexpr uint16_t kTurretAttackEnded = 0x20, kTurretFire = 0x40, kTurretRelease = 0x80;
constexpr uint16_t kTurretBackToWalk = 0x8000; // after an attack: sub-state 2 (else 0)
constexpr uint8_t kTurretBackToWalkHigh = 0x80;
constexpr uint16_t kTurretWeaponBitsKept = 0xff0f; // the weapon requests cleared

// A box of the player's position in the turret's frame (exe data, s16 x0 z0 x1 z1)
struct TurretBox : DataRecord {
    int16_t x0 = 0;
    int16_t z0 = 0;
    int16_t x1 = 0;
    int16_t z1 = 0;
    static TurretBox load(const TableReader& r) {
        TurretBox rec;
        rec.x0 = r.s16(0);
        rec.z0 = r.s16(2);
        rec.x1 = r.s16(4);
        rec.z1 = r.s16(6);
        return rec;
    }
};
const DataTable<RecordTable<TurretBox, 4, 8>> meleeBoxes{TableId::ReaverbotTurretMeleeBoxes};
const DataTable<RecordTable<TurretBox, 4, 8>> rangeBoxes{TableId::ReaverbotTurretRangeBoxes};
const DataTable<ScalarTable<int16_t, 4>> walkRanges{TableId::ReaverbotTurretWalkRanges};
const DataTable<ScalarTable<uint8_t, 8>> turretModels{TableId::ReaverbotTurretModels};
const DataTable<CollisionBoxRecord> turretBox{TableId::ReaverbotTurretBox};
// the model header's table after its part table (+0xb4): its second word is the bullets' texture word
struct ModelExtraGeometry {
    explicit ModelExtraGeometry(uint32_t pointer) : textureWord(loadedScalar<uint32_t>(pointer + 4)) {}
    uint32_t textureWord;
};
constexpr uint32_t kBulletTextureMode = 0x20;

enum class TurretState : uint8_t { Spawn = 0, Main = 1, Dying = 2 };
enum class TurretBehaviour : uint8_t { Stand = 0, WalkOut = 1, Walk = 2, Advance = 3 };
constexpr uint8_t kTurretSteady = 3;   // the variant that is never staggered
constexpr uint8_t kStagger = 0xff;     // the sub-state of the stagger
constexpr int16_t kTurretWalk = -0xc0, kTurretStride = 0xc;
constexpr int kFireRange = 0x600;
constexpr uint16_t kSoundHit = 0x8d, kSoundStagger = 0x8e, kSoundStep = 0xd3;

AnimatedActorStats statsOf(const AnimatedActor& a) { return animatedStats(a.type, a.spawnByte); }
ShortVec4& bone(const AnimatedActor& a, uint32_t b) { return bonePoint(a, b, kTurretPort); }
void toSub(AnimatedActor& a, uint8_t s) {
    a.subState = s;
    a.enterStep(0);
}
// an attack runs (this tick's or the last tick's lock)
bool attackLocked() { return (encounterState().attackLocksRecent() & kAttackLockAttack) != 0; }
// exe 0x4597b0 (and its inline copies): the planar distance to the player by 32-bit differences of the integer parts
int playerDistance(const AnimatedActor& a) {
    const Player& p = player();
    const int dx = int(p.pos.x.whole) - int(a.pos.x.whole), dz = int(p.pos.z.whole) - int(a.pos.z.whole);
    return isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
}
// the point at `dist` along the angle `u` (the boxes' frame) inside the box
bool inBox(const TurretBox& box, uint32_t u, int dist) {
    const int x = int(fixedSin(int(u))) * dist >> 12, z = int(fixedCos(int(u))) * dist >> 12;
    return box.x0 < x && x < box.x1 && box.z0 < z && z < box.z1;
}
// exe 0x459620(actor, block, box): the player inside the box (the distance exe 0x4597b0, the angle from the reference)
bool playerInBox(const AnimatedActor& a, const TurretFields& f, const TurretBox& box) {
    const int dist = playerDistance(a);
    const uint32_t u = (uint32_t(angleOf(player().pos, a.pos)) - uint32_t(f.yawReference)) & kAngleMask;
    return inBox(box, u, dist);
}
// exe 0x4596a0(actor, block, box): the same from the point the walk-out distance ahead along the reference
bool playerInBoxAhead(const AnimatedActor& a, const TurretFields& f, const TurretBox& box) {
    const Player& p = player();
    const uint32_t yu = f.yawReference & kAngleMask;
    const int d = f.travelled;
    const int16_t hx = wrap16(a.pos.x.whole + wrap16(int(fixedSin(int(yu))) * d >> 12));
    const int16_t hz = wrap16(a.pos.z.whole + wrap16(int(fixedCos(int(yu))) * d >> 12));
    const int32_t px = int32_t((uint32_t(a.pos.x) & 0xffff) | uint32_t(uint16_t(hx)) << 16);
    const int32_t pz = int32_t((uint32_t(a.pos.z) & 0xffff) | uint32_t(uint16_t(hz)) << 16);
    const int dx = int(p.pos.x.whole) - hx, dz = int(p.pos.z.whole) - hz;
    const int dist = isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
    const uint32_t u = (uint32_t(angleOf(p.pos.x, p.pos.z, px, pz)) - uint32_t(f.yawReference)) & kAngleMask;
    return inBox(box, u, dist);
}
const TurretBox meleeBox(const AnimatedActor& a) { return meleeBoxes[a.variant]; }
const TurretBox rangeBox(const AnimatedActor& a) { return rangeBoxes[a.variant]; }

// the facing step of the stand sub-states: snapped within 0x20 (animation 1), else a turn of 0x20 (animation 4)
void face(AnimatedActor& a, TurretFields& f) {
    const int16_t yaw = int16_t(angleOf(player().pos, a.pos));
    f.target = yaw;
    int d = int((uint32_t(uint16_t(a.rotation.y)) - uint32_t(int32_t(yaw))) & kAngleMask);
    if (d > kHalfTurn) d -= kFullTurn;
    if (d < 0) d = -d;
    if (d < 0x21) {
        a.rotation.y = yaw;
        a.anim = 1;
    } else {
        turnToward(a.rotation.y, int(uint32_t(int32_t(yaw))), 0x20);
        a.anim = 4;
    }
}
// exe 0x459510: the melee (animation 3) while the player is in the box; 1 = it left the box (the attack ended)
int meleeEnd(AnimatedActor& a, TurretFields& f) {
    if (a.step == 0) {
        a.step = 1;
        a.anim = 3;
    }
    if (!playerInBox(a, f, meleeBox(a))) {
        f.flags = uint16_t((f.flags & 0xffefu) | kTurretAttackEnded);
        return 1;
    }
    return 0;
}
// exe 0x459560: the fire (animation 2): a bullet every 0x80 ticks; 1 = done
int fire(AnimatedActor& a, TurretFields& f) {
    if (a.step == 0) {
        a.step = 1;
        a.anim = 2;
        f.firing = 1;
        f.timer = 0x80;
    }
    const uint16_t t = f.timer;
    if ((t & 0x7f) == 0) f.setLowFlags(uint8_t(kTurretFire));
    f.timer = uint16_t(t - 1);
    if (uint16_t(t - 1) != 0) {
        if (!playerInBox(a, f, meleeBox(a)) || attackLocked()) {
            if (playerDistance(a) < kFireRange) return 0;
        }
    }
    f.setLowFlags(uint8_t(kTurretRelease));
    f.firing = 0;
    return 1;
}
// exe 0x4595f0: the stagger (animation 7) until its end
int stagger(AnimatedActor& a) {
    if (a.step == 0) {
        a.step = 1;
        a.anim = 7;
    }
    return a.animKey.flags >> 7;
}
// exe 0x4597f0(actor, block): forward toward the target; a wall turns the target by the wall's sides
void walk(AnimatedActor& a, TurretFields& f) {
    uint32_t r = f.pushOut;
    if (int32_t(r) < 0) {
        a.pos = a.prevPos;
        return;
    }
    if (r != 0 && f.turnTicks == 0) {
        r &= 0xf00;
        switch (r) {
        case 0x600: f.target = 0xe00; break;
        case 0x400: f.target = 0; break;
        case 0x100: f.target = 0x400; break;
        case 0x200: f.target = 0xc00; break;
        case 0x500: f.target = 0x200; break;
        case 0x800: f.target = 0x800; break;
        case 0x900: f.target = 0x600; break;
        case 0xa00: f.target = 0xa00; break;
        default: break;
        }
        int d = int((uint32_t(uint16_t(a.rotation.y)) - uint32_t(uint16_t(f.target))) & kAngleMask);
        if (d > kHalfTurn) d -= kFullTurn;
        if (d < 0) d = -d;
        f.turnTicks = int16_t((d + ((d >> 31) & 0x1f)) >> 7);
    }
    const int16_t tgt = f.target;
    int d = int((uint32_t(uint16_t(a.rotation.y)) - uint32_t(int32_t(tgt))) & kAngleMask);
    if (d > kHalfTurn) d -= kFullTurn;
    if (d < 0) d = -d;
    if (d < 0x21) a.rotation.y = tgt;
    else turnToward(a.rotation.y, int(uint32_t(int32_t(tgt))), 0x20);
    integrateVelocity(a, 0, 0, kTurretWalk);
    if (f.turnTicks > 0) f.turnTicks = int16_t(f.turnTicks - 1);
}
// the melee started (the attack's flag)
void startMelee(AnimatedActor& a, TurretFields& f, uint8_t sub) {
    toSub(a, sub);
    f.setLowFlags(uint8_t(kTurretAttackStarted));
}
// after an attack: sub-state 2 when it walked out, else 0
void afterAttack(AnimatedActor& a, TurretFields& f) {
    const uint16_t fl = f.flags;
    a.enterStep(0);
    a.subState = uint8_t((fl >> 14) & 2);
}
// the stand's opening: the stand animation, a random timer
void standStart(AnimatedActor& a, TurretFields& f) {
    if (a.step == 0) {
        a.enterStep(1);
        a.anim = 1;
        f.timer = uint16_t(rngStep() & 0xff);
    }
}
bool walkedOut(const TurretFields& f) {
    return walkRanges[size_t(int32_t(f.rangeIndex))] <= f.travelled;
}

void standBehaviour(AnimatedActor& a, TurretFields& f) { // exe 0x458ae0: stand and face; melee in the box, fire near
    const uint8_t s = a.subState;
    if (s == 0) {
        standStart(a, f);
        face(a, f);
        f.timer = uint16_t(f.timer + 1u);
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) {
            startMelee(a, f, 1);
            return;
        }
        if (playerDistance(a) < kFireRange && (uint8_t(f.timer) & 0x1f) == 0) toSub(a, 2);
    } else if (s == 1) {
        if (meleeEnd(a, f)) toSub(a, 0);
    } else if (s == 2) {
        if (fire(a, f)) toSub(a, 0);
    } else if (stagger(a)) {
        toSub(a, 0);
    }
}
void walkOutBehaviour(AnimatedActor& a, TurretFields& f) { // exe 0x458c70: out up to its range while in range
    switch (a.subState) {
    case 0:
        standStart(a, f);
        f.timer = uint16_t(f.timer + 1u);
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) {
            startMelee(a, f, 3);
            return;
        }
        if (playerInBoxAhead(a, f, rangeBox(a))) {
            toSub(a, 1);
            return;
        }
        break;
    case 1:
        if (a.step == 0) {
            a.enterStep(1);
            a.anim = 0;
            f.timer = 0;
        }
        integrateVelocity(a, 0, 0, kTurretWalk);
        f.travelled = int16_t(uint16_t(f.travelled) + uint16_t(kTurretStride));
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) {
            startMelee(a, f, 3);
            return;
        }
        if (!playerInBoxAhead(a, f, rangeBox(a))) {
            toSub(a, 0);
            return;
        }
        if (walkedOut(f)) {
            toSub(a, 2);
            f.setHighFlags(kTurretBackToWalkHigh);
            return;
        }
        return;
    case 2:
        standStart(a, f);
        face(a, f);
        f.timer = uint16_t(f.timer + 1u);
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) {
            startMelee(a, f, 3);
            return;
        }
        break;
    case 3:
        if (meleeEnd(a, f)) afterAttack(a, f);
        return;
    case 4:
        if (fire(a, f)) afterAttack(a, f);
        return;
    default:
        if (stagger(a)) afterAttack(a, f);
        return;
    }
    // subs 0 / 2
    if (playerDistance(a) < kFireRange && (uint8_t(f.timer) & 0x1f) == 0) toSub(a, 4);
}
void walkBehaviour(AnimatedActor& a, TurretFields& f) { // exe 0x458f90: toward the player around walls / neighbours
    const uint8_t s = a.subState;
    if (s == 0) {
        if (a.step == 0) {
            a.enterStep(1);
            a.anim = 0;
            f.timer = 0;
        }
        if (f.turnTicks == 0) f.target = int16_t(angleOf(player().pos, a.pos));
        walk(a, f);
        f.yawReference = uint16_t(a.rotation.y);
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) {
            startMelee(a, f, 2);
            f.timer = uint16_t(f.timer + 1u);
            return;
        }
        if (neighbourFind(a, f.neighbour, 1) != 0) toSub(a, 1);
    } else if (s == 1) {
        if (a.step == 0) {
            neighbourClassify(a, f.neighbour);
            a.enterStep(1);
        }
        const Actor& o = neighbourActor(uint16_t(f.neighbour));
        if (f.turnTicks == 0) {
            uint16_t y;
            if (!(f.neighbour & kNeighbourOpposite)) y = uint16_t(angleOf(player().pos, a.pos));
            else y = uint16_t(headingFromOther(a, o, neighbourSide(f.neighbour), 0x140) & kAngleMask);
            f.target = int16_t(y);
        }
        walk(a, f);
        f.yawReference = uint16_t(a.rotation.y);
        if (o.flags == 0 || ((uint16_t(o.pos.x.whole) ^ uint16_t(a.pos.x.whole)) & 0xff80) ||
            ((uint16_t(o.pos.z.whole) ^ uint16_t(a.pos.z.whole)) & 0xff80))
            toSub(a, 0);
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) {
            startMelee(a, f, 2);
            f.timer = uint16_t(f.timer + 1u);
            return;
        }
    } else {
        const int done = (s == 2) ? meleeEnd(a, f) : stagger(a);
        if (done) toSub(a, 0);
        return;
    }
    f.timer = uint16_t(f.timer + 1u);
}
void advanceBehaviour(AnimatedActor& a, TurretFields& f) { // exe 0x459190: advance while the player is in its far box
    switch (a.subState) {
    case 0:
        if (a.step == 0) {
            a.enterStep(1);
            a.anim = 1;
            f.timer = 0;
        }
        if (!playerInBoxAhead(a, f, rangeBox(a))) {
            toSub(a, 2);
            return;
        }
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) startMelee(a, f, 1);
        return;
    case 1:
        if (a.step == 0) {
            a.enterStep(1);
            a.anim = 3;
            f.timer = 0;
        }
        if (!playerInBoxAhead(a, f, rangeBox(a))) {
            a.subState = 2;
        } else {
            if (playerInBox(a, f, meleeBox(a))) return;
            a.subState = 0;
        }
        a.enterStep(0);
        f.flags = uint16_t((f.flags & 0xffefu) | kTurretAttackEnded);
        return;
    case 2:
        if (a.step == 0) {
            a.enterStep(1);
            a.anim = 0;
            f.timer = 0;
        }
        integrateVelocity(a, 0, 0, kTurretWalk);
        f.travelled = int16_t(uint16_t(f.travelled) + uint16_t(kTurretStride));
        if (walkedOut(f)) {
            toSub(a, 4);
            f.setHighFlags(kTurretBackToWalkHigh);
            return;
        }
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) startMelee(a, f, 3);
        return;
    case 3:
        if (a.step == 0) {
            a.enterStep(1);
            a.anim = 0;
            f.timer = 0;
        }
        integrateVelocity(a, 0, 0, kTurretWalk);
        f.travelled = int16_t(uint16_t(f.travelled) + uint16_t(kTurretStride));
        if (walkedOut(f)) {
            toSub(a, 4);
            f.flags = uint16_t((f.flags & 0xffefu) | kTurretBackToWalk | kTurretAttackEnded);
            return;
        }
        if (!playerInBox(a, f, meleeBox(a))) {
            toSub(a, 2);
            f.flags = uint16_t((f.flags & 0xffefu) | kTurretAttackEnded);
        }
        return;
    case 4:
        standStart(a, f);
        face(a, f);
        f.timer = uint16_t(f.timer + 1u);
        if (playerInBox(a, f, meleeBox(a)) && !attackLocked()) startMelee(a, f, 5);
        return;
    case 5:
        if (meleeEnd(a, f)) toSub(a, 4);
        return;
    default: return;
    }
}
void spawn(const PlayerEnv& env, AnimatedActor& a, TurretFields& f) { // exe 0x458560 (state 0)
    if (!bindActorModel(a, turretModels[a.spawnByte])) {
        freeActor(a);
        return;
    }
    a.clearClassBlock();
    a.state = uint8_t(TurretState::Main);
    a.subState = 0;
    a.step = 0;
    a.stepCounter = 0;
    a.anim = 1;
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(uint32_t(statsOf(a).hitPoints) << 2) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.collisionBox.pointAt(turretBox.stateAddress());
    a.airborne = 0;
    a.onPlatform = 0;
    const int g = actorGround(env, a); // exe 0x40f190
    const int16_t x = a.pos.x.whole;
    a.pos.y.whole = int16_t(g);
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    f.hitBox.previousX = x;
    f.hitBox.previousZ = a.pos.z.whole;
    f.target = int16_t(angleOf(player().pos, a.pos));
    f.yawReference = uint16_t(a.rotation.y);
    registerEnemy(a.slot, 1);
    if (a.variant == kTurretSteady) f.setLowFlags(uint8_t(kTurretNoStagger));
}
// exe 0x458680 (state 2): an explosion at bone 0, 0xf ticks, the drops
void dying(AnimatedActor& a, TurretFields& f) {
    if (a.step == 0) {
        a.step = 1;
        a.anim = 7;
        f.setLowFlags(uint8_t(kTurretRelease | kTurretAttackEnded | kTurretNoDamage));
        f.timer = 0;
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300: the explosion (game/explosions)
            StaticActor& x = staticActorAt(address);
            ExplosionParams e(x);
            x.flags = kActorLive;
            x.type = typeByte(GlobalStaticType::Explosion);
            x.variant = 0;
            x.loopFlags |= kActorSweepGroup2;
            const ShortVec4& bp = bone(a, 0);
            x.pos.x.whole = bp.x;
            x.pos.y.whole = bp.y;
            x.pos.z.whole = bp.z;
            e.spread = 0x80;
            e.flashes = 0x10;
        }
    }
    a.tint = 0x421f;
    if (int16_t(f.timer) == 0xf) {
        spawnDrops(a.pos, statsOf(a).dropTable);
        countDefeat();
        freeActor(a);
        return;
    }
    f.timer = uint16_t(f.timer + 1u);
}
// exe 0x4589a0(actor, block): the damage (the player's attacks only; below half -> the stagger, its sound; dead ->
// dying), then the body's capsule (bone 1 .. the position, radius 0x10, the contact damage)
void hurtAndHit(AnimatedActor& a, TurretFields& f) {
    if (!(f.lowFlags() & kTurretNoDamage)) {
        const uint32_t v = f.hitBox.result;
        const uint32_t dmg = v & kHitDamageMask;
        if (dmg != 0 && (v & kHitAttackByPlayer)) {
            a.tint = kTintFlash;
            uint16_t id;
            if (hitPointsAdd(a, -int(dmg)) != 0) {
                a.enterState(uint8_t(TurretState::Dying));
                id = kSoundStagger;
            } else if (!(f.lowFlags() & kTurretNoStagger) && a.hitPoints < int16_t(a.maxHitPoints >> 1) &&
                       a.state == uint8_t(TurretState::Main)) {
                a.subState = kStagger;
                a.enterStep(0);
                f.flags = uint16_t((f.flags & 0xffafu) | kTurretRelease | kTurretAttackEnded | kTurretNoStagger);
                id = kSoundStagger;
            } else {
                id = kSoundHit;
            }
            soundPlayAtPos(id, a.pos);
        }
    }
    const int16_t local[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
    const ShortVec4& bp = bone(a, 1);
    const int16_t p0[3] = {bp.x, bp.y, bp.z};
    const AnimatedActorStats atk = statsOf(a);
    const uint32_t param = ((uint32_t(a.slot) << 6 | (f.lowFlags() & kTurretNoDamage)) << 0x12) | 0x402000u;
    const uint32_t param2 = (uint32_t(atk.attack) | 0x8000u) << 2;
    hitListSubmitCapsule(p0, 0x10, local, 0x10, f.hitBox, param, param2); // exe 0x5e4190
    f.hitBox.result = 0;
    f.hitBox.previousX = local[0];
    f.hitBox.previousZ = local[2];
}
// exe 0x458750(actor, block): the charge glow at bone 0 (its damage the strong attack * 4; the attack lock while it
// lives) and the bullet at bone 1 - 0x50 (the shot damage * 2), held 0x20 ticks
void weapons(AnimatedActor& a, TurretFields& f) {
    EncounterState& enc = encounterState();
    bool lockSet = true;
    if (f.chargeSlot == 0) {
        lockSet = false;
        if (f.lowFlags() & kTurretAttackStarted) {
            const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
            if (address != 0) {
                const ShortVec4& bp = bone(a, 0);
                EffectActor& e = effectActorAt(address);
                charge_glow_state::ChargeGlowFields& c = classState<charge_glow_state::ChargeGlowFields>(e);
                e.flags = kActorLiveDrawn;
                e.type = kChargeGlowStageType;
                e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
                e.subtype = 0;
                e.variant = 0;
                c.held = 1;
                e.pos.x = bp.x;
                e.pos.y = bp.y;
                e.pos.z = bp.z;
                c.damage = int16_t(uint32_t(statsOf(a).strongAttack) << 2);
                c.clearHitBoxWords();
                f.chargeSlot = e.slot;
                lockSet = true;
            }
        }
    } else if (f.lowFlags() & kTurretAttackEnded) {
        f.chargeSlot = 0;
        lockSet = false;
    } else {
        const uint32_t address = actorBySlot(f.chargeSlot);
        if (address != 0) {
            const ShortVec4& bp = bone(a, 0);
            EffectActor& e = effectActorAt(address);
            classState<charge_glow_state::ChargeGlowFields>(e).held = 1;
            e.pos.x = bp.x;
            e.pos.y = bp.y;
            e.pos.z = bp.z;
        }
    }
    if (lockSet) enc.markAttackLock(kAttackLockAttack);
    if (f.bulletSlot == 0) {
        if (f.lowFlags() & kTurretFire) {
            f.bulletSlot = 0;
            const uint32_t address = allocActor40(kActorStageClass); // exe 0x5e1280
            if (address != 0) {
                const ShortVec4& bp = bone(a, 1);
                ProjectileActor& s = projectileActorAt(address);
                TurretBulletParams b(s);
                s.flags = kActorLiveDrawn;
                s.type = kTurretBulletStageType;
                s.loopFlags |= kActorSweepGroup2;
                s.subtype = 0;
                s.variant = f.bulletVariant;
                b.trailShrink = 1;
                b.damage = int8_t(uint32_t(statsOf(a).shotAttack) << 1);
                b.trailCounter = 0;
                b.wobbleShift = uint8_t((rngStep() & 1) + 1);
                s.pos.x = wholeToFixed16(bp.x);
                s.pos.y = wholeToFixed16(int32_t(bp.y) - 0x50);
                s.pos.z = wholeToFixed16(bp.z);
                b.texture = ModelExtraGeometry(a.extraGeometry).textureWord | kBulletTextureMode;
                f.flags = uint16_t(f.flags & kTurretWeaponBitsKept);
                f.bulletSlot = s.slot;
                f.bulletHold = 0x20;
                return;
            }
        }
    } else {
        const uint32_t address = actorBySlot(f.bulletSlot);
        if (address != 0) {
            ProjectileActor& s = projectileActorAt(address);
            if ((s.flags & kActorLive) && s.type == kTurretBulletStageType &&
                s.state == uint8_t(TurretBulletState::Held) && !(f.lowFlags() & kTurretRelease)) {
                const ShortVec4& bp = bone(a, 1);
                s.subState = 1;
                s.pos.x = wholeToFixed16(bp.x);
                s.pos.y = wholeToFixed16(int32_t(bp.y) - 0x50);
                s.pos.z = wholeToFixed16(bp.z);
                if (f.bulletHold != 0) {
                    f.flags = uint16_t(f.flags & kTurretWeaponBitsKept);
                    --f.bulletHold;
                    return;
                }
            }
        }
        f.bulletSlot = 0;
        f.bulletHold = 0;
    }
    f.flags = uint16_t(f.flags & kTurretWeaponBitsKept);
}

void turret(const PlayerEnv& env, AnimatedActor& a) { // exe 0x458400
    TurretFields& f = classState<TurretFields>(a);
    a.tint = kTintNeutral;
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (TurretState(a.state)) {
    case TurretState::Spawn: spawn(env, a, f); break;
    case TurretState::Main: // exe 0x458660: [0x88ee94 + variant * 4]
        switch (TurretBehaviour(a.variant)) {
        case TurretBehaviour::Stand: standBehaviour(a, f); break;
        case TurretBehaviour::WalkOut: walkOutBehaviour(a, f); break;
        case TurretBehaviour::Walk: walkBehaviour(a, f); break;
        case TurretBehaviour::Advance: advanceBehaviour(a, f); break;
        default: notPortedAt(kTurretPort, "0x458400 behaviour +6", a.variant);
        }
        break;
    case TurretState::Dying: dying(a, f); break;
    default: notPortedAt(kTurretPort, "0x458400 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (int32_t(actorTestMove(env, a)) < 0) f.pushOut = actorPushOut(env, a); // exe 0x40f570 / 0x410960
    else f.pushOut = 0;
    const int g = actorGround(env, a); // exe 0x40f190: gravity + 3 a tick down to 0x20 above the ground
    bool snap = true;
    if (int(a.pos.y.whole) < g - 0x20) {
        a.velocity.y = int16_t(uint16_t(a.velocity.y) + 3u);
        a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) + uint16_t(a.velocity.y));
        if (int(a.pos.y.whole) <= g - 0x20) snap = false;
    }
    if (snap) {
        a.pos.y.whole = int16_t(g);
        a.velocity.y = 0;
    }
    hurtAndHit(a, f);
    if (a.anim == a.prevAnim) animStepPose(a);
    else animSetPose(a, a.anim, 0);
    weapons(a, f);
    // exe 0x5f20f0(actor, 0x40, 0x100, 0): the shadow (render output)
    const uint8_t anim = a.anim, frame = uint8_t(a.animKey.frame); // the steps' sound by the animation's frame
    if (anim == 0) {
        if (frame == 0xf || frame == 0x1f) soundPlayAtPos(kSoundStep, a.pos);
    } else if (anim == 4 && (frame == 3 || frame == 9)) {
        soundPlayAtPos(kSoundStep, a.pos);
    }
}

} // namespace

void reaverbotTurretUpdate(const PlayerEnv& env, uint32_t actor) { turret(env, animatedActorAt(actor)); }

} // namespace od::port
