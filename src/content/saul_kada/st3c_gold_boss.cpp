// Teisel's fight with the gold statue (ST3C sub 2, the pirate base, story 12; pool 0x20 type 0x73 sub 0, exe 0x4f6e60;
// hypothesis names), spawned at the end of script 0x5d with the statue 0x4fa800 (type 0x73 sub 1) and 0x4f4740 (type
// 0x72 sub 0). The room handler 0x588660 waits for story flag 0x6f, which only its end sets.
// States [0x897398]: 0 bind 0x4f6f30, 1 phase one 0x4f7050 (its sub-states [0x8973a8]), 2 phase two 0x4f7180
// ([0x8973c4]), 3 the end 0x4f72c0 (flag 0x6f, freed).
// It keeps a block of its own in its bone block 4 (+0x20, 0x60 bytes; its address in its +0x14c and, every tick, in the
// global 0xa59e90): four hit boxes, the player's distance, its flags, timers, the two pieces it holds, angles, its hit
// entries' mode, a script's cursor, its rising, a ring and its target point.
// Phase one: idle / turn / throw two pieces / the swing throwing gold / the taunt (the statue hit: voices) / stagger /
// defeat (blasts, a fade, the end). The statue's hit part (static ST3C 0x50) signals in its +0xf: hit, below half its
// hit points (message 0x66), destroyed (the balance -4500 and, unless dying, phase two with a 300-tick timer; message
// 0x68 when it runs out).
// Phase two: message 0x65 and a turn, the leap onto the floor, the chase, the lunge, the ring move (to the player's ring,
// around it), to a target point, three attacks (scripts 0x8975a4 / 0x897648 / 0x897728), a turn, the leap again, the
// recovery (no ring under the player), the defeat.
// Exe 0x5e40c0 / 0x5e4070 leave a word / a dword of their stack's garbage in the last slot they return; the exe copies
// it into the projections' scratch (and from there a spark's spare lane): written as 0 here.
#include "content/saul_kada/st3c_gold_boss.hpp"
#include "content/saul_kada/st3c_gold_boss_state.hpp"

#include "content/saul_kada/st3c_boss_shot.hpp"
#include "content/saul_kada/st3c_gold_piece.hpp"
#include "content/saul_kada/st3c_gold_shockwave.hpp"
#include "content/saul_kada/st3c_minion_spawner.hpp"
#include "content/saul_kada/st3c_statue.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/matrix.hpp"
#include "game/message_triggers.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/room_grid.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/vectors.hpp"
#include "game/visibility.hpp"

using namespace od::port::st3c_gold_boss_state;

namespace od::port {

using namespace st3c_gold_boss_state;

namespace {

enum class BossState : uint8_t { Bind = 0, PhaseOne = 1, PhaseTwo = 2, End = 3 };
enum class OneSub : uint8_t { Idle = 0, Turn = 1, Throw = 2, Swing = 3, Taunt = 4, Stagger = 5, Defeat = 6 };
enum class TwoSub : uint8_t {
    Start = 0, Leap = 1, Chase = 2, Lunge = 3, ToRing = 4, Circle = 5, MoveTo = 6, Attack1 = 7, Attack2 = 8,
    Attack3 = 9, Face = 0xa, Releap = 0xb, Recover = 0xc, Defeat = 0xd
};
enum class BossAnim : uint8_t {
    Idle = 0, Throw = 1, SwingStart = 2, Swing = 3, SwingEnd = 4, Stagger = 7, TauntStart = 8, Taunt = 9,
    TauntEnd = 0xa, DefeatStart = 0xb, DefeatEnd = 0xc, Turn = 0xd, Leap = 0xe, Walk = 0xf, Attack1 = 0x10,
    Attack2 = 0x11, Attack3 = 0x12, LungeBack = 0x13, LungeSwing = 0x14, LungeDive = 0x15
};
// its hit entries' modes
enum class HitMode : uint8_t { BodyAndArm = 0, Body = 1, Low = 2, Strike = 3 };
constexpr uint8_t kStatusTaunting = 0x01, kStatusDying = 0x02;
constexpr uint8_t kStatueDestroyed = 0x01, kStatueHit = 0x02, kStatueHalf = 0x04;
// its block's flags
constexpr uint32_t kTauntPending = 0x4, kHitsOff = 0x8, kNoDamage = 0x10, kStaggered = 0x20, kNoStagger = 0x40,
                   kProvoked = 0x80, kPlayerGrounded = 0x100, kInsideRing = 0x200, kDying = 0x400,
                   kRingMoveUsed = 0x800, kHopAfterMove = 0x1000, kPhaseTwoTimerDone = 0x2000;
constexpr uint8_t kNoRing = 4;
// the exe's global holding the running boss's block
// The row of its type (by its spawn byte) as it reads it
struct BossRow : DataRecord {
    static constexpr uint32_t kBytes = 0xe;
    uint8_t hitPoints = 0; // +0x0
    uint8_t bodyDamage = 0; // +0x1
    uint8_t pieceAttack = 0; // +0x2
    uint8_t goldAttack = 0; // +0x3
    uint8_t shotAttack = 0; // +0x8
    uint8_t swingDamage = 0; // +0x9: attacks 1 / 3, the lunge
    uint8_t leapDamage = 0; // +0xa: attack 2
    uint8_t strikeDamage = 0; // +0xb: the low entry, the other strikes
    uint8_t staggerHalf = 0; // +0xc: x 2: the damage that staggers it
    uint8_t shotBlast = 0; // +0xd
    template <class R> static BossRow load(const R& r) {
        BossRow v;
        v.hitPoints = r.template read<uint8_t>(0x0);
        v.bodyDamage = r.template read<uint8_t>(0x1);
        v.pieceAttack = r.template read<uint8_t>(0x2);
        v.goldAttack = r.template read<uint8_t>(0x3);
        v.shotAttack = r.template read<uint8_t>(0x8);
        v.swingDamage = r.template read<uint8_t>(0x9);
        v.leapDamage = r.template read<uint8_t>(0xa);
        v.strikeDamage = r.template read<uint8_t>(0xb);
        v.staggerHalf = r.template read<uint8_t>(0xc);
        v.shotBlast = r.template read<uint8_t>(0xd);
        return v;
    }
};
BossRow bossRow(const AnimatedActor& actor) { return dataRecord<BossRow>(animatedStatRow(actor.type, actor.spawnByte), BossRow::kBytes); }
// the exe's data
const DataTable<ScalarTable<uint8_t, 8>> chaseRadiusPicks{TableId::St3cGoldBossChaseRadiusPicks},
    crossRadiusPicks{TableId::St3cGoldBossCrossRadiusPicks}, lungeChances{TableId::St3cGoldBossLungeChances},
    hopChances{TableId::St3cGoldBossHopChances};
const DataTable<ScalarTable<int32_t, 4>> pickRadii{TableId::St3cGoldBossPickRadii};
const DataTable<ScalarTable<int32_t, 1>> hopSpeed{TableId::St3cGoldBossHopSpeed};    // exe data 0x897494
const DataTable<ScalarTable<uint32_t, 1>> chainRoot{TableId::St3cGoldBossChainRoot}; // exe data 0x8978c0
const DataTable<ScalarTable<uint32_t, 3>> chainBones{TableId::St3cGoldBossChainBones};
constexpr int16_t kFloorY = 0x9b;
constexpr uint16_t kHurtSound = 0x8d;
constexpr uint32_t kFlagBossDone = 0x6f;
constexpr uint8_t kHitMuteType = 0x18, kHitMuteVariant = 0x7c; // the global e0 hit mute (content/manda/manda_guardian)
constexpr uint8_t kSparkVariant = 7;
constexpr uint8_t kFadeOut = 0x1a;

// the hit mute's spawn parameter (its class is another area's)
struct HitMuteParams {
    explicit HitMuteParams(const ControllerActor& actor) : HitMuteParams(const_cast<ControllerActor&>(actor)) {}
    explicit HitMuteParams(ControllerActor& actor) : ticks(actor.params, 0) {}
    BytesField<uint16_t, 4> ticks;
};

void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380
void setAnim(AnimatedActor& actor, BossAnim anim) { actor.anim = uint8_t(anim); }
template <class Sub> void enterSub(AnimatedActor& actor, Sub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
void nextStep(AnimatedActor& actor) { ++actor.step; }
int ground(const PlayerEnv& env, const Actor& actor) { return actorGround(env, actor); } // exe 0x40f190
void setFlags(BossBlock& b, uint32_t bits) { b.flags = b.flags | bits; }
void clearFlags(BossBlock& b, uint32_t bits) { b.flags = b.flags & ~bits; }
uint16_t randomTicks(uint32_t base, uint32_t mask) { return uint16_t((rngStep() & mask) + base); }

// exe 0x5e40c0(angle, r) into the scratch point: {dx, 0, dz, 0}
void scratchAhead(int angle, int r) {
    int16_t dx, dz;
    angleVector(angle, r, dx, dz);
    LaneVec4& p = projectionScratch().point;
    p.x = dx;
    p.y = 0;
    p.z = dz;
    projectionScratch().pointYaw = 0;
}
// exe 0x5e4070(angle, r) into the scratch's transformed vector: {x, 0, z, 0}
void scratchRadius(int angle, int r) {
    int32_t v[3];
    angleRadiusVector(angle, r, v);
    ProjectionScratch& s = projectionScratch();
    s.transformed[0] = v[0];
    s.transformed[1] = 0;
    s.transformed[2] = v[2];
    s.transformedPad = 0;
}
// the scratch point at the boss's integer x / z, y 0
void scratchAtBoss(const AnimatedActor& actor) {
    LaneVec4& p = projectionScratch().point;
    p.x = actor.pos.x.whole;
    p.y = 0;
    p.z = actor.pos.z.whole;
}
ShortVec3 ringCentre(const BossBlock& b) { return dataShortVec3(stateAddress(TableId::St3cGoldBossRingCentres) + uint32_t(b.ring) * 8); }
LaneVec3 scratchPoint() { return geometryScratch().vec3(0x60); }
// exe 0x40f5a0 on the integer parts of two 16.16 points
uint32_t blocked(const PlayerEnv& env, const int32_t p[3], const int32_t q[3], uint32_t box) {
    const int16_t a[3] = {int16_t(p[0] >> 16), int16_t(p[1] >> 16), int16_t(p[2] >> 16)};
    const int16_t c[3] = {int16_t(q[0] >> 16), int16_t(q[1] >> 16), int16_t(q[2] >> 16)};
    return testMoveBlocked(env, a, c, CollisionBoxRef::record(box), 2);
}
bool insideArena(int32_t x, int32_t z) { return pointInCellBox(int16_t(x >> 16), int16_t(z >> 16), CellBox(stateAddress(TableId::St3cGoldBossArenaCells))); }

// ---- its spawns ----------------------------------------------------------------------------------------------------
// exe 0x4fa7d0(id): a message trigger showing the message at once
void message(uint16_t id) {
    const uint32_t slot = allocActorE0(); // exe 0x5e1440
    if (!slot) return;
    ControllerActor& trigger = controllerActorAt(slot);
    MessageTriggerFields& fields = classState<MessageTriggerFields>(trigger);
    trigger.subtype = 0;
    trigger.variant = kMessageTriggerAtOnce;
    trigger.spawnByte = 0;
    trigger.flags = kActorLive;
    trigger.type = kMessageTriggerType;
    fields.message = int16_t(id);
    fields.lastStory = 0xc;
    fields.flag = kNoMessageFlag;
}
// exe 0x4fa370(actor, variant, offset): a shockwave at its x / z (+ the s16 offset's x / z), y 0
void shockwave(const AnimatedActor& actor, uint8_t variant, const LaneVec4* offset) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& wave = staticActorAt(slot);
    wave.flags = kActorLive;
    wave.variant = variant;
    wave.type = kGoldShockwaveType;
    wave.subtype = 0;
    wave.spawnByte = 0;
    wave.pos.y = 0;
    if (offset) {
        wave.pos.x = int32_t((uint32_t(int32_t(offset->x.get())) << 16) + uint32_t(actor.pos.x));
        wave.pos.z = int32_t((uint32_t(int32_t(offset->z.get())) << 16) + uint32_t(actor.pos.z));
    } else {
        wave.pos.x = actor.pos.x;
        wave.pos.z = actor.pos.z;
    }
}
void shockwaveAhead(const AnimatedActor& actor, uint8_t variant) {
    const LaneVec4 p = projectionScratch().point;
    shockwave(actor, variant, &p);
}
// exe 0x4fa310: a hit spark (late) at the scratch point (its spare lane copied)
void sparkAtScratch() {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& spark = effectActorAt(slot);
    spark.flags = kActorLive;
    spark.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    spark.type = typeByte(GlobalEffectType::HitSpark);
    spark.variant = kSparkVariant;
    spark.setPositionWords(projectionScratch().point);
    classState<HitSparkFields>(effectActorAt(slot)).size = randomTicks(0x60, 0x1f);
}
// a blast (exe 0x5e1300) without its attack; its slot
uint32_t blast() {
    const uint32_t slot = allocActor60(0);
    if (!slot) return 0;
    StaticActor& b = staticActorAt(slot);
    b.flags = kActorLive;
    b.type = typeByte(GlobalStaticType::Blast);
    b.subtype = 0;
    b.variant = 0;
    b.spawnByte = 0;
    return slot;
}
// exe 0x4fa130: a sound, the gold thrown 0x100 above it at a random point near the player (wider while it is off
// screen)
void throwGold(const AnimatedActor& actor) {
    sound(0x2eb, actor);
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& gold = staticActorAt(slot);
    gold.flags = kActorLive;
    gold.type = kGoldPieceType;
    gold.subtype = kThrownGoldSubtype;
    const uint32_t r = rngStep();
    gold.spawnByte = 0;
    gold.loopFlags |= kActorSweepGroup2;
    gold.variant = uint8_t(r % 6);
    ThrownGoldParams(staticActorAt(slot)).attack = bossRow(actor).goldAttack;
    gold.pos.x = actor.pos.x;
    gold.pos.y = int32_t(uint32_t(actor.pos.y) - 0x1000000u);
    gold.pos.z = actor.pos.z;
    const Player& p = player();
    int32_t target[3];
    int32_t dz;
    if (actor.runFlags & kActorOffScreen) {
        const uint32_t q = rngStep();
        target[0] = int32_t((((q & 0x1ff) - 0x100u) << 16) + uint32_t(p.pos.x));
        target[1] = int32_t(((((rngLastValue() >> 9) & 0x1ff) - 0x180u) << 16) + uint32_t(p.pos.y));
        dz = int32_t((rngLastValue() >> 0x12) & 0x1ff) - 0x100;
    } else {
        const uint32_t q = rngStep();
        target[0] = int32_t((((q & 0x7f) - 0x40u) << 16) + uint32_t(p.pos.x));
        target[1] = int32_t(((((rngLastValue() >> 7) & 0x7f) - 0xc0u) << 16) + uint32_t(p.pos.y));
        dz = int32_t((rngLastValue() >> 0xe) & 0x7f) - 0x40;
    }
    target[2] = int32_t((uint32_t(dz) << 16) + uint32_t(p.pos.z));
    const int32_t from[3] = {gold.pos.x, gold.pos.y, gold.pos.z};
    gold.velocity.y = int16_t(launchSpeed(from, target, 0x410, 0x14, 0xffffffffu)); // exe 0x5e3ee0
    gold.velocity.z = int16_t(0xfbf0);
    gold.acceleration.y = 0x14;
    gold.rotation.y = int16_t(angleOf(target[0], target[2], actor.pos.x, actor.pos.z));
}

// ---- its block's routines ------------------------------------------------------------------------------------------
// One entry of its motion scripts: {tick, op, value}
struct ScriptEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint8_t tick = 0; // +0x0
    uint8_t op = 0; // +0x1
    int16_t value = 0; // +0x2
    template <class R> static ScriptEntry load(const R& r) {
        ScriptEntry v;
        v.tick = r.template read<uint8_t>(0x0);
        v.op = r.template read<uint8_t>(0x1);
        v.value = r.template read<int16_t>(0x2);
        return v;
    }
};
enum class ScriptOp : uint8_t { VerticalSpeed = 0, ForwardSpeed = 1, Gravity = 2, ShowPart = 3, HidePart = 4 };
// exe 0x4f9b70(actor, script): the entries of this tick (its cursor) applied; an unknown op leaves without the tick
void runScript(AnimatedActor& actor, BossBlock& b, uint32_t script) {
    uint32_t at = script + uint32_t(b.scriptCursor) * 4;
    if (dataRecord<ScriptEntry>(at, ScriptEntry::kBytes).tick == b.scriptTick) {
        do {
            const ScriptEntry e = dataRecord<ScriptEntry>(at, ScriptEntry::kBytes);
            switch (ScriptOp(e.op)) {
            case ScriptOp::VerticalSpeed: actor.velocity.y = e.value; break;
            case ScriptOp::ForwardSpeed: actor.velocity.z = e.value; break;
            case ScriptOp::Gravity: actor.acceleration.y = e.value; break;
            case ScriptOp::ShowPart: actor.parts[size_t(int32_t(e.value))].flags &= uint8_t(~kPartHidden); break;
            case ScriptOp::HidePart: actor.parts[size_t(int32_t(e.value))].flags |= kPartHidden; break;
            default: return;
            }
            b.scriptCursor += 1u;
            at += 4;
        } while (dataRecord<ScriptEntry>(at, ScriptEntry::kBytes).tick == b.scriptTick);
    }
    b.scriptTick += 1u;
}
void startScript(BossBlock& b) {
    b.scriptCursor = 0;
    b.scriptTick = 0;
}
void scripted(AnimatedActor& actor, BossBlock& b, uint32_t script) {
    runScript(actor, b, script);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
}
// exe 0x4f9c30: its four boxes' largest damage; flashed, a sound; unless taking no damage its hit points (killed: -1)
// and a big enough hit staggers it (unless it may not be); 1 = hit, 0 = none
int takeHits(AnimatedActor& actor, BossBlock& b) {
    clearFlags(b, kStaggered);
    uint32_t damage = b.boxes[0].result & kHitDamageMask;
    const uint32_t any = b.boxes[3].result | b.boxes[2].result | b.boxes[1].result | b.boxes[0].result;
    for (size_t k = 1; k < 4; ++k)
        if (damage < (b.boxes[k].result & kHitDamageMask)) damage = b.boxes[k].result & kHitDamageMask;
    if (!(any & (kHitAttackBump | kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) || damage == 0) return 0;
    actor.tint = kTintFlash;
    sound(kHurtSound, actor);
    if (b.flagsLow & kNoDamage) return 1;
    if (hitPointsAdd(actor, -int(damage))) return -1; // exe 0x5e52f0
    if (int32_t(damage) >= int32_t(uint32_t(bossRow(actor).staggerHalf) << 1) && !(b.flags & kNoStagger))
        setFlags(b, kStaggered);
    return 1;
}
// its arm: a chain of three capsules (radius 0xc) along its chain's bones into boxes 1..3 (the ends alternate in the
// scratch's point pair)
void armChain(const AnimatedActor& actor, BossBlock& b, uint32_t owner, uint32_t attack) {
    ProjectionScratch& s = projectionScratch();
    s.pointPair[1] = bonePoint(actor, chainRoot[0], "0x4f9d10: the arm's root bone past its blocks"); // exe 0x5e1810
    for (uint32_t k = 0; k < 3; ++k) {
        s.pointPair[k & 1] = bonePoint(actor, chainBones[k], "0x4f9d10: an arm bone past its blocks");
        const LaneVec4 from = s.pointPair[(k - 1) & 1], to = s.pointPair[k & 1];
        const int16_t a[3] = {from.x, from.y, from.z}, c[3] = {to.x, to.y, to.z};
        hitListSubmitCapsule(a, 0xc, c, 0xc, b.boxes[k + 1], owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(1),
                             attack); // exe 0x5e4190
    }
}
// exe 0x4f9d10(actor, mode): the boxes reset (the previous x / z), then the mode's entries; its immunities: all but the
// player's group while drawn, else all
void submitHits(const AnimatedActor& actor, BossBlock& b, HitMode mode) {
    b.boxes[0].previousX = actor.prevPos.x.whole;
    const uint32_t immunity = (actor.flags & kActorDrawn) ? hitImmunity(kHitGroupPlayer) : kHitImmunityMask;
    b.boxes[0].previousZ = actor.prevPos.z.whole;
    b.boxes[0].result = 0;
    for (size_t k = 1; k < 4; ++k) {
        b.boxes[k].previousX = actor.prevPos.x.whole;
        b.boxes[k].previousZ = actor.prevPos.z.whole;
        b.boxes[k].result = 0;
    }
    const uint32_t owner = hitOwner(actor.slot);
    const BossRow row = bossRow(actor);
    const uint32_t kinds = immunity | kHitAttackThrown | kHitAttackSolid;
    LaneVec4& point = projectionScratch().point;
    switch (mode) {
    case HitMode::BodyAndArm:
    case HitMode::Body: {
        point.x = actor.pos.x.whole;
        point.y = int16_t(uint16_t(actor.pos.y.whole) - 0x40u);
        point.z = actor.pos.z.whole;
        const ShortVec4& top = bonePoint(actor, 0x10, "0x4f9d10: bone 0x10 past its blocks");
        const uint32_t attack = uint32_t(row.bodyDamage) | kinds;
        const int16_t a[3] = {point.x, point.y, point.z}, c[3] = {top.x, top.y, top.z};
        const uint32_t lockOn = mode == HitMode::BodyAndArm ? 0u : kHitNoLockOn;
        hitListSubmitCapsule(a, 0x20, c, 0x20, b.boxes[0], owner | hitGroup(kHitGroupBodies) | lockOn | hitCapsule(1),
                             attack);
        if (mode == HitMode::BodyAndArm) armChain(actor, b, owner, attack);
        return;
    }
    case HitMode::Low: {
        point.x = actor.pos.x.whole;
        point.y = int16_t(uint16_t(actor.pos.y.whole) - 0xa0u);
        point.z = actor.pos.z.whole;
        const int16_t p[3] = {point.x, point.y, point.z};
        hitListSubmit(p, b.boxes[0], owner | hitGroup(kHitGroupBodies) | hitRadius(0x80), uint32_t(row.strikeDamage) | kinds);
        return;
    }
    case HitMode::Strike: {
        const ShortVec4& fist = bonePoint(actor, 1, "0x4f9d10: bone 1 past its blocks");
        const uint8_t anim = actor.anim;
        uint32_t damage;
        if (anim == uint8_t(BossAnim::Attack1) || anim == uint8_t(BossAnim::Attack3)) damage = row.swingDamage;
        else if (anim == uint8_t(BossAnim::Attack2)) damage = row.leapDamage;
        else damage = row.strikeDamage;
        const uint32_t attack = damage | kinds;
        const int16_t p[3] = {fist.x, fist.y, fist.z};
        hitListSubmit(p, b.boxes[0], owner | hitGroup(kHitGroupBodies) | hitRadius(0xa0), attack);
        armChain(actor, b, owner, attack);
        return;
    }
    default: return;
    }
}
// exe 0x4fa2e0: the player within 0x200, or not standing on anything
bool playerNearOrAir(const PlayerEnv& env, const BossBlock& b) {
    if (b.distance < 0x200) return true;
    return ground(env, player()) == -0x100;
}
// the provoke check closing most phase-one sub-states: provoked (or the player near / in the air) -> no longer; a
// taunt pending -> the taunt
void provokedCheck(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    if (!(b.flagsLow & kProvoked) && !playerNearOrAir(env, b)) return;
    clearFlags(b, kProvoked);
    if (b.flagsLow & kTauntPending) enterSub(actor, OneSub::Taunt);
}
// exe 0x5e3df0 called twice: the second difference, negated when the first was negative
int turnTwice(AnimatedActor& actor, uint32_t angle) {
    const int d = turnYawTo(actor.rotation.y, angle, actor.spin.y);
    const int d2 = turnYawTo(actor.rotation.y, angle, actor.spin.y);
    return d < 0 ? -d2 : d2;
}
// exe 0x4f95c0: the height offset: rising by 0x20 to 0xf0 (then not drawn), else drawn and sinking by 0x20 to 0
void stepHeightOffset(AnimatedActor& actor, BossBlock& b) {
    if (b.rising) {
        b.heightOffset += 0x20u;
        if (b.heightOffset < 0xf0) return;
        actor.setDrawn(false);
        b.heightOffset = 0xf0;
        return;
    }
    actor.setDrawn(true);
    b.heightOffset -= 0x20u;
    if (b.heightOffset <= 0) b.heightOffset = 0;
}
// exe 0x4fa3e0: its forward speed stepped toward its target speed (by the target, 16-bit, as the exe)
void stepSpeed(AnimatedActor& actor, const BossFields& boss) {
    const int16_t v = actor.velocity.z, t = boss.speedTarget;
    if (v < t) {
        const int16_t n = int16_t(uint16_t(v - t));
        actor.velocity.z = n;
        if (n > t) actor.velocity.z = t;
    } else if (v > t) {
        const int16_t n = int16_t(uint16_t(v + t));
        actor.velocity.z = n;
        if (n < t) actor.velocity.z = t;
    }
}
// the shared end of exe 0x4fa420 / 0x4f8950: the height offset onto its y, the speed, dust sparks while drawn, the
// step sound every 0x12 ticks (at its x / z, y 0); dying -> the defeat
void moveEnd(AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    stepHeightOffset(actor, b);
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(b.heightOffset)) << 16));
    stepSpeed(actor, boss);
    if (actor.flags & kActorDrawn) {
        scratchAhead(uint16_t(actor.rotation.y), 0x80);
        const uint32_t r = rngStep();
        LaneVec4& p = projectionScratch().point;
        p.x = int16_t(uint16_t((r & 0x1f) + uint16_t(actor.pos.x.whole) + uint16_t(p.x) - 0x20u));
        p.z = int16_t(uint16_t(((rngLastValue() >> 5) & 0x1f) + uint16_t(actor.pos.z.whole) + uint16_t(p.z) - 0x20u));
        sparkAtScratch();
    }
    if (b.stepSoundTimer == 0) {
        soundPlayAtXyz(0x2f1, actor.pos.x.whole, 0, actor.pos.z.whole);
        b.stepSoundTimer = 0x12;
    } else {
        b.stepSoundTimer -= 1u;
    }
    if (b.flags & kDying) enterSub(actor, TwoSub::Defeat);
}
// exe 0x4fa420: rising unless its move is free and inside the arena; then moveEnd
void moveTail(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    if (actorTestMove(env, actor) == 0 && pointInCellBox(actor.pos, CellBox(stateAddress(TableId::St3cGoldBossArenaCells)))) b.rising = 0; // 0x40f570
    else b.rising = 1;
    moveEnd(actor, boss, b);
}
// exe 0x4f9b40: the first ring cell holding the player (kNoRing: none)
uint8_t playerRing() {
    uint8_t k = 0;
    for (uint32_t box = stateAddress(TableId::St3cGoldBossRingCells); box < stateAddress(TableId::St3cGoldBossRingCells) + 0x10; box += 4, ++k)
        if (pointInCellBox(player().pos, CellBox(box))) break;
    return k;
}
// exe 0x4f9a20: the low entries, walking, then: the player standing -> his ring (none: the recovery), else the ring
// move (inside its ring: within 0x600 of its centre); the player in the air -> the chase
void pickRingMove(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    if (!(b.flags & kPlayerGrounded)) {
        b.hitMode = uint8_t(HitMode::Low);
        setAnim(actor, BossAnim::Walk);
        enterSub(actor, TwoSub::Chase);
        return;
    }
    if (ground(env, player()) == -0x100) {
        b.ring = kNoRing;
        b.hitMode = uint8_t(HitMode::Low);
        setAnim(actor, BossAnim::Walk);
        enterSub(actor, TwoSub::Recover);
        return;
    }
    b.ring = playerRing();
    if (b.ring >= kNoRing) {
        setAnim(actor, BossAnim::Walk);
        enterSub(actor, TwoSub::Recover);
        b.hitMode = uint8_t(HitMode::Low);
        return;
    }
    scratchAtBoss(actor);
    if (planarDistance16(scratchPoint(), ringCentre(b)) <= 0x600) setFlags(b, kInsideRing);
    else clearFlags(b, kInsideRing);
    b.hitMode = uint8_t(HitMode::Low);
    setAnim(actor, BossAnim::Walk);
    enterSub(actor, TwoSub::ToRing);
}
// exe 0x4f98a0(actor, mode): a hop target: its heading (0 / 0x800) and its +-z speed by the mode (0 by its side of x,
// 1 +, 2 -, 3 random), a random x offset; a hop after the move by chance
void hopTarget(const AnimatedActor& actor, BossBlock& b, int mode) {
    b.radiusPick = 2;
    const int32_t sp = hopSpeed[0] >> 5;
    const auto toward = [&](bool negative) {
        b.targetZ = int16_t(negative ? -sp : sp);
        b.targetAngle = negative ? 0x800 : 0;
    };
    switch (mode) {
    case 0:
    case 1:
    case 2:
        toward(mode == 2 || (mode == 0 && actor.pos.x < 0));
        b.targetX = int16_t((rngStep() & 0x7ff) - 0x400u);
        break;
    case 3:
        toward((rngStep() & 1) != 0);
        b.targetX = int16_t(((rngLastValue() >> 1) & 0x7ff) - 0x400u);
        break;
    default: break;
    }
    if (hopChances[rngStep() & 7] == 0) clearFlags(b, kHopAfterMove);
    else setFlags(b, kHopAfterMove);
}
// exe 0x4f9620: the chase's time out: once by chance a lunge (standing free inside the arena); else a ring point around
// the player (toward the boss the first time, then random) free and inside the arena at it and under the player ->
// the target and to it; after 0xc random tries a hop and to it
void chaseTimeout(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    const int32_t pos[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    const int32_t prev[3] = {actor.prevPos.x, actor.prevPos.y, actor.prevPos.z};
    if (!(b.flags & kRingMoveUsed) && lungeChances[rngStep() & 7] != 0 && blocked(env, pos, prev, stateAddress(TableId::St3cGoldBossChaseBox)) == 0 &&
        pointInCellBox(actor.pos, CellBox(stateAddress(TableId::St3cGoldBossArenaCells)))) {
        enterSub(actor, TwoSub::Lunge);
        return;
    }
    b.radiusPick = chaseRadiusPicks[rngStep() & 7];
    if (b.flags & kRingMoveUsed) {
        b.targetAngle = uint16_t(rngStep() & 0xfff);
        scratchRadius(b.targetAngle, pickRadii[b.radiusPick]);
        b.ringMoves += 1u;
    } else {
        b.targetAngle = uint16_t(angleOf(actor.pos, player().pos));
        scratchRadius(b.targetAngle, pickRadii[b.radiusPick]);
    }
    const Player& p = player();
    const ProjectionScratch& s = projectionScratch();
    const int32_t ring[3] = {int32_t(uint32_t(p.pos.x) + uint32_t(s.transformed[0].get())),
                             int32_t(stateAddress(TableId::St3cGoldBossFloorY16)),
                             int32_t(uint32_t(p.pos.z) + uint32_t(s.transformed[2].get()))};
    const int32_t under[3] = {p.pos.x, int32_t(stateAddress(TableId::St3cGoldBossFloorY16)), p.pos.z};
    bool chosen = false;
    if (blocked(env, ring, ring, stateAddress(TableId::St3cGoldBossChaseBox)) == 0 && insideArena(ring[0], ring[2]) &&
        blocked(env, under, under, stateAddress(TableId::St3cGoldBossChaseBox)) == 0 && insideArena(under[0], under[2])) {
        b.targetX = int16_t(ring[0] >> 16);
        b.targetZ = int16_t(ring[2] >> 16);
        b.targetAngle = uint16_t(b.targetAngle + 0x800u);
        chosen = true;
    } else if (b.ringMoves >= 0xc) {
        hopTarget(actor, b, 0);
        chosen = true;
    }
    if (chosen) enterSub(actor, TwoSub::MoveTo);
    setFlags(b, kRingMoveUsed);
}
// exe 0x4fa570: a radius pick and a random angle; the two points at half the radius each side of its ring's centre
// both free -> the target the first, the heading across, 1; else 0
int ringCross(const PlayerEnv& env, BossBlock& b) {
    b.radiusPick = crossRadiusPicks[rngStep() & 7];
    const uint32_t angle = (rngLastValue() & 0x38) << 6;
    scratchRadius(int(angle), pickRadii[b.radiusPick]);
    const ShortVec3 centre = ringCentre(b);
    const uint32_t cx = uint32_t(int32_t(centre.x)) << 16, cz = uint32_t(int32_t(centre.z)) << 16;
    const ProjectionScratch& s = projectionScratch();
    const uint32_t hx = uint32_t(s.transformed[0] >> 1), hz = uint32_t(s.transformed[2] >> 1);
    const int32_t first[3] = {int32_t(cx + hx), int32_t(stateAddress(TableId::St3cGoldBossFloorY16)),
                               int32_t(cz + hz)};
    const int32_t second[3] = {int32_t(cx - hx), int32_t(stateAddress(TableId::St3cGoldBossFloorY16)),
                                int32_t(cz - hz)};
    if (blocked(env, first, first, stateAddress(TableId::St3cGoldBossArenaFloorBox)) != 0) return 0;
    if (blocked(env, second, second, stateAddress(TableId::St3cGoldBossArenaFloorBox)) != 0) return 0;
    b.targetX = int16_t(first[0] >> 16);
    b.targetZ = int16_t(first[2] >> 16);
    b.targetAngle = uint16_t(angle + 0x800);
    return 1;
}
// exe 0x4fa750: within its speed (x / z in 1/16 units) of the target -> snapped onto it, 1
int arrived(AnimatedActor& actor, const BossBlock& b) {
    int32_t v = actor.velocity.z;
    if (v < 0) v = -v;
    const int32_t tx = b.targetX;
    if (uint32_t((actor.pos.x >> 12) - (tx << 4) + v) >= uint32_t(v * 2)) return 0;
    if (uint32_t((actor.pos.z >> 12) - (int32_t(b.targetZ) << 4) + v) >= uint32_t(v * 2)) return 0;
    actor.pos.x = int32_t(uint32_t(tx) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(b.targetZ)) << 16);
    return 1;
}
// exe 0x4fa6f0: turned to the target, forward, arrived?
int moveToTarget(AnimatedActor& actor, const BossBlock& b) {
    turnYawTo(actor.rotation.y,
              uint32_t(planarAngleOf(b.targetX, b.targetZ, actor.pos.x.whole, actor.pos.z.whole)), actor.spin.y);
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    return arrived(actor, b);
}
// onto the ring of radius 0x600 around its centre, from its direction
void ringSnap(AnimatedActor& actor, const ShortVec3& centre) {
    ProjectionScratch& s = projectionScratch();
    s.transformed[0] = int32_t(actor.pos.x.whole) - centre.x;
    s.transformed[1] = 0;
    s.transformed[2] = int32_t(actor.pos.z.whole) - centre.z;
    const int32_t v[3] = {s.transformed[0], s.transformed[1], s.transformed[2]};
    int32_t n[3];
    normalizeVector(v, n); // exe 0x40c320
    for (size_t k = 0; k < 3; ++k) s.normalized[k] = n[k];
    actor.pos.x = int32_t((uint32_t(int32_t(centre.x)) << 16) + ((uint32_t(n[0]) * 3) << 13));
    actor.pos.z = int32_t((uint32_t(int32_t(centre.z)) << 16) + ((uint32_t(n[2]) * 3) << 13));
}
// the attacks' slam: a shockwave 0x82 ahead, a shake, a rumble
void slam(const AnimatedActor& actor) {
    scratchAhead(uint16_t(actor.rotation.y), 0x82);
    shockwaveAhead(actor, 0);
    cameraShakeAt(0x2000, 0x300, 0x100, 0x1c00, actor.pos); // exe 0x40eab0
    padVibrateAt(0x201, 0x208b0, 0x100, 0x1c00, actor.pos); // exe 0x55cd80
}
// a sound at the scratch point ahead of it
void soundAhead(uint16_t id, const AnimatedActor& actor) {
    const LaneVec4 p = projectionScratch().point;
    soundPlayAtXyz(id, int16_t(p.x + actor.pos.x.whole), actor.pos.y.whole, int16_t(p.z + actor.pos.z.whole));
}
void clearMotion(AnimatedActor& actor, BossFields& boss) {
    boss.speedTarget = 0;
    actor.velocity.z = 0;
    actor.acceleration.z = 0;
    actor.velocity.y = 0;
    actor.acceleration.y = 0;
}

// ---- phase one [0x8973a8] ------------------------------------------------------------------------------------------
// exe 0x4f72e0: idle for its timer, then the turn
void idle(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::Idle);
        clearFlags(b, kProvoked);
        nextStep(actor);
        [[fallthrough]];
    case 1:
        b.timer -= 1u;
        if (b.timer == 0) enterSub(actor, OneSub::Turn);
        break;
    default: break;
    }
    provokedCheck(env, actor, b);
}
// exe 0x4f7370: a sound, turned to the player; facing him -> the throw (far) or the swing
void turnToPlayer(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0:
        sound(0x2f5, actor);
        setAnim(actor, BossAnim::Idle);
        clearFlags(b, kProvoked);
        nextStep(actor);
        [[fallthrough]];
    case 1:
        if (turnTwice(actor, b.playerAngle) <= actor.spin.y) nextStep(actor);
        break;
    case 2:
        actor.rotation.y = int16_t(b.playerAngle);
        enterSub(actor, b.distance >= 0xe00 ? OneSub::Throw : OneSub::Swing);
        break;
    default: break;
    }
    provokedCheck(env, actor, b);
}
// exe 0x4f7490: two pieces held, released at keys 0x32 / 0x41 (a sound); then a random idle
void throwPieces(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0:
        sound(0x2f4, actor);
        setFlags(b, kNoStagger);
        setAnim(actor, BossAnim::Throw);
        nextStep(actor);
        for (uint32_t k = 0; k < 2; ++k) {
            const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
            b.pieces[k] = slot;
            if (!slot) continue;
            ProjectileActor& piece = projectileActorAt(slot);
            BossShotParams params(projectileActorAt(slot));
            piece.flags = kActorLive;
            piece.type = kBossShotType;
            piece.subtype = kThrownPieceSubtype;
            piece.variant = 0;
            piece.spawnByte = 0;
            piece.loopFlags = kActorStageClass | kActorSweepGroup2;
            params.attack = int8_t(bossRow(actor).pieceAttack);
            params.side = uint8_t(k);
            ThrownPieceBoss(projectileActorAt(slot)).boss = slotIdentityWord(actor);
        }
        return;
    case 1:
    case 2: {
        const uint32_t k = actor.step == 1 ? 0 : 1;
        if (actor.keyIndex != (k == 0 ? 0x32 : 0x41)) return;
        sound(0x2ed, actor);
        const uint32_t piece = b.pieces[k];
        if (!piece) notPorted(kStoryPort, "0x4f7490: the thrown piece was never allocated (the exe writes to address 0xe)");
        BossShotParams(projectileActorAt(piece)).released = 1;
        b.pieces[k] = 0;
        if (k == 1) clearFlags(b, kNoStagger);
        nextStep(actor);
        return;
    }
    case 3:
        if (actor.animEnded()) {
            b.timer = randomTicks(0x1e, 0x1f);
            enterSub(actor, OneSub::Idle);
        }
        provokedCheck(env, actor, b);
        return;
    default: return;
    }
}
// exe 0x4f76c0: the swing throwing gold at its key 5; the player behind (> 0x300) -> the turn; done -> a random idle
void swing(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::SwingStart);
        clearFlags(b, kProvoked);
        nextStep(actor);
        return;
    case 1:
        provokedCheck(env, actor, b);
        if (actor.animEnded()) {
            b.timer = 0;
            nextStep(actor);
            setAnim(actor, BossAnim::Swing);
        }
        return;
    case 2:
        turnYawTo(actor.rotation.y, b.playerAngle, actor.spin.y);
        if (actor.animEnded()) {
            b.timer += 1u;
            if (b.timer == 5) {
                setAnim(actor, BossAnim::SwingEnd);
                nextStep(actor);
            }
            int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), b.playerAngle); // exe 0x5e3d80 (twice)
            if (d < 0) d = -d;
            if (d > 0x300) enterSub(actor, OneSub::Turn);
            else actor.prevAnim = 0xff;
            provokedCheck(env, actor, b);
        }
        if (actor.keyIndex == 5) throwGold(actor);
        return;
    case 3:
        if (actor.animEnded()) {
            setAnim(actor, BossAnim::Idle);
            b.timer = randomTicks(0x1e, 0x1f);
            enterSub(actor, OneSub::Idle);
            clearFlags(b, kNoStagger);
        }
        return;
    default: return;
    }
}
// exe 0x4f78d0: the taunt (its hit entries off): while the statue is hit a voice every 0x3c..0x5b ticks and its time
// kept up; its time out or no taunt pending -> the turn
void taunt(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    switch (actor.step) {
    case 0:
        b.stepSoundTimer = 0;
        sound(0x2ec, actor);
        boss.status |= kStatusTaunting;
        b.hitMode = uint8_t(HitMode::Body);
        setFlags(b, kHitsOff);
        clearFlags(b, kNoStagger);
        nextStep(actor);
        setAnim(actor, BossAnim::TauntStart);
        return;
    case 1:
        if (actor.animEnded()) {
            setAnim(actor, BossAnim::Taunt);
            b.timer = randomTicks(0x1e, 0x1f);
            nextStep(actor);
        }
        return;
    case 2:
        turnYawTo(actor.rotation.y, b.playerAngle, actor.spin.y);
        if (b.stepSoundTimer != 0) b.stepSoundTimer -= 1u;
        if (boss.statueSignals & kStatueHit) {
            if (b.stepSoundTimer == 0) {
                sound((rngStep() & 1) ? 0x2fd : 0x2fe, actor);
                b.stepSoundTimer = randomTicks(0x3c, 0x1f);
            }
            b.timer = randomTicks(0x1e, 0x1f);
        } else if (playerNearOrAir(env, b)) {
            b.timer = randomTicks(0x1e, 0x1f);
        }
        b.timer -= 1u;
        if (b.timer != 0 && (b.flagsLow & kTauntPending)) return;
        boss.status &= uint8_t(~kStatusTaunting);
        setAnim(actor, BossAnim::TauntEnd);
        nextStep(actor);
        return;
    case 3:
        if (actor.animKey.flags & 8) {
            boss.status &= uint8_t(~kStatusTaunting);
            b.hitMode = uint8_t(HitMode::BodyAndArm);
            clearFlags(b, kHitsOff);
            enterSub(actor, OneSub::Turn);
        }
        return;
    default: return;
    }
}
// exe 0x4f7b20: the stagger (no new one), then the taunt
void stagger(AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0:
        sound(0x2fb, actor);
        setAnim(actor, BossAnim::Stagger);
        setFlags(b, kNoStagger);
        nextStep(actor);
        return;
    case 1:
        if (actor.animEnded()) enterSub(actor, OneSub::Taunt);
        return;
    default: return;
    }
}
// a blast every 4th tick within 0x100 around it and up to 0xff above it
void defeatBlasts(const AnimatedActor& actor, BossFields& boss) {
    boss.defeatTicks += 1u;
    if (boss.defeatTicks & 3) return;
    const uint32_t slot = blast(); // exe 0x5e1300
    if (!slot) return;
    StaticActor& b = staticActorAt(slot);
    const uint32_t r = rngStep();
    b.pos.x = int32_t(((0x100u - (r & 0x1ff)) << 16) + uint32_t(actor.pos.x));
    b.pos.y = int32_t(uint32_t(actor.pos.y) - (((rngLastValue() >> 9) & 0xff) << 16));
    b.pos.z = int32_t(((0x100u - ((rngLastValue() >> 0x11) & 0x1ff)) << 16) + uint32_t(actor.pos.z));
    BlastParams(actorHeaderAt(slot)).size = 0x100;
}
// exe 0x4f7b80: the defeat in phase one, 0x1e ticks, a fade; the fade done -> the end; blasts all along
void defeatOne(AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    switch (actor.step) {
    case 0:
        setAnim(actor, BossAnim::DefeatStart);
        sound(0x2fc, actor);
        nextStep(actor);
        break;
    case 1:
        if (actor.animEnded()) {
            nextStep(actor);
            setAnim(actor, BossAnim::DefeatEnd);
        }
        break;
    case 2:
        if (actor.animEnded()) {
            b.timer = 0x1e;
            nextStep(actor);
        }
        break;
    case 3:
        b.timer -= 1u;
        if (b.timer == 0) {
            scriptFade(kFadeOut, 0); // exe 0x53f6b0
            nextStep(actor);
        }
        break;
    case 4:
        if (fadeState().busy == 0) actor.enterState(uint8_t(BossState::End));
        break;
    default: break;
    }
    defeatBlasts(actor, boss);
}

// ---- phase two [0x8973c4] ------------------------------------------------------------------------------------------
// exe 0x4f7cf0: its hit entries off, message 0x65; then turned toward +z / -z (by the player's side) -> the leap
void startTwo(AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0:
        setFlags(b, kHitsOff);
        nextStep(actor);
        message(0x65);
        [[fallthrough]];
    case 1:
        if (!actor.animEnded()) return;
        b.targetAngle = player().pos.z.whole < 0 ? 0x800 : 0;
        actor.step += 2u;
        setAnim(actor, BossAnim::Turn);
        return;
    case 2:
        if (!actor.animEnded()) return;
        nextStep(actor);
        setAnim(actor, BossAnim::Turn);
        return;
    case 3:
        if (!actor.animEnded()) return;
        if (turnTwice(actor, b.targetAngle) < actor.spin.y) {
            clearFlags(b, kHitsOff);
            b.hitMode = uint8_t(HitMode::BodyAndArm);
            enterSub(actor, TwoSub::Leap);
        }
        return;
    default: return;
    }
}
// the leaps' landing: walking, the low entries, the ring move, rising
void landLeap(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    setAnim(actor, BossAnim::Walk);
    integrateVelocity(actor, 0, 0, -0x1322);
    b.hitMode = uint8_t(HitMode::Low);
    actor.velocity.z = 0;
    pickRingMove(env, actor, b);
    b.rising = 1;
    b.heightOffset = 0xf0;
}
void startLeap(AnimatedActor& actor, BossBlock& b, uint32_t script) {
    startScript(b);
    nextStep(actor);
    setAnim(actor, BossAnim::Leap);
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.acceleration.z = 0;
    actor.acceleration.y = 0;
    scripted(actor, b, script);
}
// exe 0x4f7e40: the leap onto the floor (shockwaves at keys 0xf (a sound around the player) / 0x16 / 0x1d)
void leap(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0: startLeap(actor, b, stateAddress(TableId::St3cGoldBossLeapScript)); return;
    case 1: {
        scripted(actor, b, stateAddress(TableId::St3cGoldBossLeapScript));
        const uint8_t key = actor.keyIndex;
        if (key == 0xf) {
            scratchAhead(uint16_t(actor.rotation.y), 0x100);
            shockwaveAhead(actor, 0);
            const LaneVec4 p = projectionScratch().point;
            const Player& pl = player();
            soundPlayAtXyz(0x2f0, int16_t(p.x + pl.pos.x.whole), pl.pos.y.whole, int16_t(p.z + pl.pos.z.whole));
        } else if (key == 0x16) {
            scratchAhead(uint16_t(actor.rotation.y), 0xa0);
            shockwaveAhead(actor, 1);
        } else if (key == 0x1d) {
            scratchAhead(uint16_t(actor.rotation.y), 0x8c);
            shockwaveAhead(actor, 1);
        }
        if (!actor.animEnded()) return;
        actor.collisionBox.pointAt(TableId::St3cGoldBossArenaFloorBox);
        actor.pos.y = int32_t(uint32_t(ground(env, actor) + 0x9b) << 16);
        landLeap(env, actor, b);
        return;
    }
    default: return;
    }
}
// exe 0x4f7ff0: the chase (its speed toward -0x280 / -0x380, turning 0x40): near (< 0x300) -> the charge, far again
// (> 0x700) -> the chase; its time out -> exe 0x4f9620
void chase(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    actor.pos.y = stateAddress(TableId::St3cGoldBossFloorY16);
    switch (actor.step) {
    case 0:
        b.stepSoundTimer = 0;
        clearFlags(b, kProvoked);
        b.timer = randomTicks(0x80, 0x1f);
        nextStep(actor);
        boss.speedTarget = int16_t(0xfd80);
        actor.acceleration.z = int16_t(0xffb8);
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        actor.spin.y = 0x40;
        [[fallthrough]];
    case 1:
        if (b.distance < 0x300) {
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            nextStep(actor);
            boss.speedTarget = int16_t(0xfc80);
            break;
        }
        turnYawTo(actor.rotation.y, b.playerAngle, actor.spin.y);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (b.timer == 0) chaseTimeout(env, actor, b);
        else b.timer -= 1u;
        break;
    case 2:
        if (b.distance > 0x700) {
            boss.speedTarget = int16_t(0xfd80);
            actor.subState = uint8_t(TwoSub::Chase);
            actor.step = 1;
        }
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        break;
    default: break;
    }
    if (b.flags & kPlayerGrounded) pickRingMove(env, actor, b);
    moveTail(env, actor, boss, b);
}
// exe 0x4f8150: the lunge: backing off, three swings (a sound), then the dive (a shockwave at key 8)
void lunge(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0: {
        actor.setDrawn(true);
        b.hitMode = uint8_t(HitMode::Strike);
        startScript(b);
        nextStep(actor);
        actor.rotation.y = int16_t(b.playerAngle);
        actor.spin.y = 0x40;
        setAnim(actor, BossAnim::LungeBack);
        actor.pos.y = stateAddress(TableId::St3cGoldBossFloorY16);
        scratchRadius(b.playerAngle, -0x1322);
        const ProjectionScratch& s = projectionScratch();
        actor.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(s.transformed[0].get()));
        actor.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(s.transformed[2].get()));
        scripted(actor, b, stateAddress(TableId::St3cGoldBossLungeBackScript));
        scratchAhead(uint16_t(actor.rotation.y), 0x100);
        shockwaveAhead(actor, 1);
        break;
    }
    case 1:
        scripted(actor, b, stateAddress(TableId::St3cGoldBossLungeBackScript));
        if (actor.animEnded()) {
            sound(0x2fa, actor);
            b.timer = 3;
            nextStep(actor);
            setAnim(actor, BossAnim::LungeSwing);
        }
        break;
    case 2:
        turnYawTo(actor.rotation.y, b.playerAngle, actor.spin.y);
        if (actor.animEnded()) {
            b.timer -= 1u;
            if (b.timer == 0) {
                startScript(b);
                actor.pos.y = 0x78d000;
                scripted(actor, b, stateAddress(TableId::St3cGoldBossLungeDiveScript));
                actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
                nextStep(actor);
                setAnim(actor, BossAnim::LungeDive);
            } else {
                actor.prevAnim = 0xff;
            }
        }
        break;
    case 3:
        scripted(actor, b, stateAddress(TableId::St3cGoldBossLungeDiveScript));
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
        if (actor.animEnded()) {
            setAnim(actor, BossAnim::Walk);
            pickRingMove(env, actor, b);
            b.rising = 1;
            b.heightOffset = 0xf0;
        }
        if (actor.keyIndex == 8) {
            scratchAhead(uint16_t(actor.rotation.y), 0x100);
            shockwaveAhead(actor, 0);
        }
        break;
    default: break;
    }
    if (b.flags & kDying) enterSub(actor, TwoSub::Defeat);
}
// the ring moves' start: walking, the speeds, the turn step
void startRingMove(AnimatedActor& actor, BossFields& boss, BossBlock& b, int16_t turn) {
    b.stepSoundTimer = 0;
    clearFlags(b, kProvoked);
    nextStep(actor);
    setAnim(actor, BossAnim::Walk);
    boss.speedTarget = int16_t(0xfc80);
    actor.acceleration.z = int16_t(0xff70);
    actor.velocity.y = 0;
    actor.acceleration.y = 0;
    actor.spin.y = turn;
}
// the player in the air: back to the chase
void chaseIfPlayerAirborne(AnimatedActor& actor, const BossBlock& b) {
    if (b.flags & kPlayerGrounded) return;
    setAnim(actor, BossAnim::Walk);
    enterSub(actor, TwoSub::Chase);
}
// exe 0x4f83f0: to its ring: inside it straight out to 0x600, else to the player until within 0x600; then onto the
// ring, facing along it -> around it
void toRing(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    actor.pos.y = stateAddress(TableId::St3cGoldBossFloorY16);
    switch (actor.step) {
    case 0:
        if (actorTestMove(env, actor) == 0 && pointInCellBox(actor.pos, CellBox(stateAddress(TableId::St3cGoldBossArenaCells)))) { // exe 0x40f570
            b.rising = 0;
            b.heightOffset = 0;
        } else {
            b.rising = 1;
            b.heightOffset = 0xf0;
        }
        b.stepSoundTimer = 0;
        clearFlags(b, kProvoked);
        nextStep(actor);
        setAnim(actor, BossAnim::Walk);
        boss.speedTarget = int16_t(0xfc80);
        actor.acceleration.z = int16_t(0xff70);
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        actor.spin.y = 0x80;
        break;
    case 1: {
        scratchAtBoss(actor);
        const ShortVec3 centre = ringCentre(b);
        bool snap;
        if (b.flags & kInsideRing) {
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            scratchAtBoss(actor);
            snap = planarDistance16(scratchPoint(), centre) > 0x600;
        } else {
            turnYawTo(actor.rotation.y, b.playerAngle, actor.spin.y);
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            scratchAtBoss(actor);
            snap = planarDistance16(scratchPoint(), centre) <= 0x600;
        }
        if (snap) {
            ringSnap(actor, centre);
            actor.rotation.y = int16_t(angleOf(scratchPoint(), centre) - 0x400);
            enterSub(actor, TwoSub::Circle);
        }
        break;
    }
    default: break;
    }
    chaseIfPlayerAirborne(actor, b);
    moveTail(env, actor, boss, b);
}
// exe 0x4f86f0: around its ring (kept at 0x600, facing along it) for 0x78..0xb7 ticks; then 1 in 4 the lunge, else a
// crossing -> to the target
void circle(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    actor.pos.y = stateAddress(TableId::St3cGoldBossFloorY16);
    switch (actor.step) {
    case 0:
        b.stepSoundTimer = 0;
        clearFlags(b, kProvoked);
        b.timer = randomTicks(0x78, 0x3f);
        nextStep(actor);
        setAnim(actor, BossAnim::Walk);
        boss.speedTarget = int16_t(0xfc80);
        actor.acceleration.z = int16_t(0xff70);
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        actor.spin.y = 0x80;
        [[fallthrough]];
    case 1: {
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        const ShortVec3 centre = ringCentre(b);
        ProjectionScratch& s = projectionScratch();
        s.transformed[0] = int32_t(uint32_t(int32_t(centre.x)) << 16);
        s.transformed[1] = 0;
        s.transformed[2] = int32_t(uint32_t(int32_t(centre.z)) << 16);
        const int32_t c[3] = {s.transformed[0], 0, s.transformed[2]};
        if (planarDistance(actor.pos, c) > 0x600) ringSnap(actor, centre);
        scratchAtBoss(actor);
        actor.rotation.y = int16_t(angleOf(centre, scratchPoint()) + 0x400);
        if (b.timer == 0) {
            if ((rngStep() & 3) == 0) enterSub(actor, TwoSub::Lunge);
            else if (ringCross(env, b)) enterSub(actor, TwoSub::MoveTo);
        } else {
            b.timer -= 1u;
        }
        break;
    }
    default: break;
    }
    chaseIfPlayerAirborne(actor, b);
    moveTail(env, actor, boss, b);
}
// exe 0x4f8950: to the target point (turning 0x300); arrived: a hop pending -> the lunge, else its heading and the
// attack of its radius pick; always rising
void moveTo(AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    actor.pos.y = stateAddress(TableId::St3cGoldBossFloorY16);
    switch (actor.step) {
    case 0:
        startRingMove(actor, boss, b, 0x300);
        clearFlags(b, kRingMoveUsed);
        [[fallthrough]];
    case 1:
        if (moveToTarget(actor, b)) {
            if (b.flags & kHopAfterMove) {
                clearFlags(b, kHopAfterMove);
                actor.subState = uint8_t(TwoSub::Lunge);
            } else {
                actor.rotation.y = int16_t(b.targetAngle);
                switch (b.radiusPick) {
                case 0: actor.subState = uint8_t(TwoSub::Attack1); break;
                case 1: actor.subState = uint8_t(TwoSub::Attack2); break;
                case 2: actor.subState = uint8_t(TwoSub::Attack3); break;
                default: break;
                }
            }
            actor.restartStep();
        }
        break;
    default: break;
    }
    b.rising = 1;
    moveEnd(actor, boss, b);
}
// the attacks' start (exe 0x4f8b20 / 0x4f8cf0 / 0x4f8eb0): a sound, the strike entries, drawn, turning 0x80, the
// animation, its height, (forward: moved 0x1322 back along its yaw), the script, a shockwave at it, a sound
void attackStart(AnimatedActor& actor, BossFields& boss, BossBlock& b, uint16_t id, BossAnim anim, int32_t y,
                 bool forward, uint32_t script) {
    sound(id, actor);
    b.hitMode = uint8_t(HitMode::Strike);
    startScript(b);
    actor.setDrawn(true);
    actor.spin.y = 0x80;
    setAnim(actor, anim);
    actor.pos.y = y;
    if (forward) {
        scratchAhead(uint16_t(actor.rotation.y), -0x1322);
        const LaneVec4 p = projectionScratch().point;
        actor.pos.x = int32_t(uint32_t(actor.pos.x) + (uint32_t(int32_t(p.x)) << 12));
        actor.pos.z = int32_t(uint32_t(actor.pos.z) + (uint32_t(int32_t(p.z)) << 12));
    }
    nextStep(actor);
    clearMotion(actor, boss);
    scripted(actor, b, script);
    shockwave(actor, 1, nullptr);
    sound(0x2ee, actor);
}
// the end of attacks 2 / 3: part 0x10 shown, the ring move, rising, not drawn
void attackLand(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    actor.parts[0x10].flags &= uint8_t(~kPartHidden);
    pickRingMove(env, actor, b);
    b.rising = 1;
    b.heightOffset = 0xf0;
    actor.setDrawn(false);
}
// exe 0x4f8b20: attack 1: the slam at key 0x15 (a sound ahead); then the turn
void attack1(AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    switch (actor.step) {
    case 0: attackStart(actor, boss, b, 0x2f6, BossAnim::Attack1, stateAddress(TableId::St3cGoldBossFloorY16), true, stateAddress(TableId::St3cGoldBossAttack1Script)); return;
    case 1:
        scripted(actor, b, stateAddress(TableId::St3cGoldBossAttack1Script));
        if (actor.animEnded()) enterSub(actor, TwoSub::Face);
        if (actor.keyIndex == 0x15) {
            slam(actor);
            soundAhead(0x2ef, actor);
        }
        return;
    default: return;
    }
}
// exe 0x4f8cf0: attack 2 (high): the slam at key 0x18 (a sound ahead)
void attack2(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    switch (actor.step) {
    case 0: attackStart(actor, boss, b, 0x2f7, BossAnim::Attack2, 0x1d00000, false, stateAddress(TableId::St3cGoldBossAttack2Script)); return;
    case 1:
        scripted(actor, b, stateAddress(TableId::St3cGoldBossAttack2Script));
        if (actor.animEnded()) attackLand(env, actor, b);
        if (actor.keyIndex == 0x18) {
            slam(actor);
            soundAhead(0x2f0, actor);
        }
        return;
    default: return;
    }
}
// exe 0x4f8eb0: attack 3: the slam at key 0x19 (a sound at it); at key 0x1c a homing shot from bone 0xe (a sound)
void attack3(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    switch (actor.step) {
    case 0: attackStart(actor, boss, b, 0x2f8, BossAnim::Attack3, stateAddress(TableId::St3cGoldBossFloorY16), true, stateAddress(TableId::St3cGoldBossAttack3Script)); return;
    case 1: {
        scripted(actor, b, stateAddress(TableId::St3cGoldBossAttack3Script));
        if (actor.animEnded()) attackLand(env, actor, b);
        if (actor.keyIndex == 0x19) {
            slam(actor);
            sound(0x2f0, actor);
        }
        if (actor.keyIndex != 0x1c) return;
        const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
        if (!slot) return;
        const ShortVec4& p = bonePoint(actor, 0xe, "0x4f8eb0: bone 0xe past its blocks (the exe faults)"); // 0x5e1810
        ProjectileActor& shot = projectileActorAt(slot);
        BossShotParams params(projectileActorAt(slot));
        shot.flags = kActorLive;
        shot.loopFlags |= kActorSweepGroup2;
        shot.type = kBossShotType;
        shot.subtype = kHomingShotSubtype;
        shot.variant = 0;
        shot.spawnByte = 0;
        shot.pos.x = int32_t(uint32_t(int32_t(p.x)) << 16);
        shot.pos.y = int32_t(uint32_t(int32_t(p.y)) << 16);
        shot.pos.z = int32_t(uint32_t(int32_t(p.z)) << 16);
        const BossRow row = bossRow(actor);
        params.attack = int8_t(row.shotAttack);
        params.blastAttack = int8_t(row.shotBlast);
        soundPlayAtPos(0x2f2, shot.pos);
        return;
    }
    default: return;
    }
}
// exe 0x4f9120: on the floor, still; turned to the player -> the leap again
void face(AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    switch (actor.step) {
    case 0:
        nextStep(actor);
        actor.spin.y = 0x40;
        actor.pos.y = 0;
        clearMotion(actor, boss);
        return;
    case 1:
        if (turnTwice(actor, b.playerAngle) <= actor.spin.y) {
            actor.restartStep();
            actor.subState = uint8_t(TwoSub::Releap);
        }
        return;
    default: return;
    }
}
// exe 0x4f91d0: the leap again (shockwaves at keys 7 / 0xe / 0x15)
void releap(const PlayerEnv& env, AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0: startLeap(actor, b, stateAddress(TableId::St3cGoldBossReleapScript)); return;
    case 1: {
        scripted(actor, b, stateAddress(TableId::St3cGoldBossReleapScript));
        const uint8_t key = actor.keyIndex;
        if (key == 7) {
            scratchAhead(uint16_t(actor.rotation.y), 0x80);
            shockwaveAhead(actor, 0);
        } else if (key == 0xe || key == 0x15) {
            scratchAhead(uint16_t(actor.rotation.y), 0x80);
            shockwaveAhead(actor, 1);
        }
        if (!actor.animEnded()) return;
        landLeap(env, actor, b);
        return;
    }
    default: return;
    }
}
// exe 0x4f9310: no ring under the player: chasing (turning 0x80) to within 0x400, then straight on; past 0x800 from
// the origin -> a hop back and to it
void recover(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    actor.pos.y = stateAddress(TableId::St3cGoldBossFloorY16);
    switch (actor.step) {
    case 0:
        b.stepSoundTimer = 0;
        clearFlags(b, kProvoked);
        b.timer = randomTicks(0x30, 0x1f);
        nextStep(actor);
        boss.speedTarget = int16_t(0xfd80);
        actor.acceleration.z = int16_t(0xffb8);
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
        actor.spin.y = 0x80;
        [[fallthrough]];
    case 1:
        turnYawTo(actor.rotation.y, b.playerAngle, actor.spin.y);
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (b.distance < 0x400) nextStep(actor);
        break;
    case 2: {
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        const int32_t x = actor.pos.x.whole, z = actor.pos.z.whole;
        if (int32_t(uint32_t(x * x) + uint32_t(z * z)) > 0x400000) {
            hopTarget(actor, b, 3);
            enterSub(actor, TwoSub::MoveTo);
        }
        break;
    }
    default: break;
    }
    moveTail(env, actor, boss, b);
}
// exe 0x4f9430: the defeat in phase two: not drawn, a sound, the hit mute, a blast, a shockwave; 0x3c ticks of blasts /
// shockwaves every 8th tick, a fade; the fade done -> the end
void defeatTwo(AnimatedActor& actor, BossBlock& b) {
    switch (actor.step) {
    case 0: {
        actor.setDrawn(false);
        sound(0x2fc, actor);
        if (const uint32_t slot = allocActorE0()) { // exe 0x5e1440
            ControllerActor& mute = controllerActorAt(slot);
            mute.flags = kActorLive;
            mute.type = kHitMuteType;
            mute.subtype = 0;
            mute.variant = kHitMuteVariant;
            mute.spawnByte = 0;
            HitMuteParams(controllerActorAt(slot)).ticks = 0x7fff;
        }
        if (const uint32_t slot = blast()) {
            StaticActor& bl = staticActorAt(slot);
            bl.pos.x = actor.pos.x;
            bl.pos.y = 0;
            BlastParams(actorHeaderAt(slot)).size = 0x100;
            bl.pos.z = actor.pos.z;
        }
        shockwave(actor, 2, nullptr);
        b.timer = 0x3c;
        nextStep(actor);
        return;
    }
    case 1:
        b.timer -= 1u;
        if (b.timer == 0) {
            scriptFade(kFadeOut, 0); // exe 0x53f6b0
            nextStep(actor);
        }
        if (b.timerLow & 7) return;
        if (const uint32_t slot = blast()) {
            StaticActor& bl = staticActorAt(slot);
            const uint32_t r = rngStep();
            bl.pos.y = 0;
            bl.pos.x = int32_t(uint32_t(actor.pos.x) - (r & 0xff) + 0x80u);
            BlastParams(actorHeaderAt(slot)).size = 0x100;
            bl.pos.z = int32_t(uint32_t(actor.pos.z) - ((rngLastValue() >> 8) & 0xff) + 0x80u);
        }
        scratchAhead(int((rngLastValue() >> 16) & 0xfff), 0x80);
        shockwaveAhead(actor, 2);
        return;
    case 2:
        if (fadeState().busy == 0) actor.enterState(uint8_t(BossState::End));
        return;
    default: return;
    }
}

// ---- its states [0x897398] -----------------------------------------------------------------------------------------
// exe 0x4f6f30: its model, hit points, its block in its bone block 4, the boss gauge, the minion spawner, the statue's
// hit part
void bind(AnimatedActor& actor, BossFields& boss) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.prevAnim = 0;
    setAnim(actor, BossAnim::Idle);
    const uint16_t hp = uint16_t(uint32_t(bossRow(actor).hitPoints) << 4);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.setZSpare(0, 0);
    actor.velocity.setXY(0, 0);
    actor.spin.x = 0;
    actor.spin.y = 0x30;
    actor.collisionBox.pointAt(TableId::St3cGoldBossBox);
    actor.actorHit = 0;
    actor.platform = nullptr;
    const BoneBlock* home = boneBlockOf(actor, 4); // exe 0x5e1780(actor, 4)
    if (!home) notPorted(kStoryPort, "0x4f6f30: exe 0x5e1780(actor, 4) returned null (the exe uses address 0x20)");
    const uint32_t blockAt = home->at + 0x20;
    boss.block.pointAt(blockAt);
    st3cGoldBossGlobals().bossBlock = blockAt;
    BossBlock& b = *boss.block;
    b.clearBytes(); // exe 0x40dc40
    b.flags = kTauntPending;
    b.timer = 0x3c;
    animSetPose(actor, actor.anim, 0);      // exe 0x5e1c30
    hudAddBossGauge(actor, 0, 4, 0xffffff); // exe 0x415d30
    if (const uint32_t slot = allocActorE0()) { // exe 0x5e1440
        ControllerActor& spawner = controllerActorAt(slot);
        spawner.flags = kActorLive;
        spawner.type = kMinionSpawnerType;
        spawner.subtype = 0;
        spawner.variant = 0;
        spawner.spawnByte = 0;
        spawner.loopFlags = kActorStageClass;
    }
    if (const uint32_t slot = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        StaticActor& part = staticActorAt(slot);
        part.flags = kActorLive;
        part.type = kStatuePartType;
        part.subtype = 0;
        part.variant = 0;
        part.spawnByte = 0;
        part.loopFlags = kActorStageClass | kActorSweepGroup2;
        StatuePartParams(staticActorAt(slot)).statue = actor.at;
    }
    actor.enterState(uint8_t(BossState::PhaseOne));
}
void animate(AnimatedActor& actor) {
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, 0);                // exe 0x5e1c30
}
// the phases' common start: the player's distance and angle, whether he stands on something
void watchPlayer(const PlayerEnv& env, const AnimatedActor& actor, BossBlock& b) {
    b.distance = uint32_t(planarDistance(actor.pos, player().pos));
    b.playerAngle = uint16_t(angleOf(player().pos, actor.pos));
    if (ground(env, player()) < 0) setFlags(b, kPlayerGrounded);
    else clearFlags(b, kPlayerGrounded);
}
// its hit entries, or its boxes cleared while they are off
void hitsOrNone(const AnimatedActor& actor, BossBlock& b, bool off) {
    if (!off) {
        submitHits(actor, b, HitMode(b.hitMode)); // exe 0x4f9d10
        return;
    }
    for (size_t k = 0; k < 4; ++k) b.boxes[k].result = 0;
}
// exe 0x4f7050
void phaseOne(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    watchPlayer(env, actor, b);
    const int r = takeHits(actor, b);
    if (r == -1) {
        setFlags(b, kDying | kHitsOff);
        boss.status = kStatusDying;
        enterSub(actor, OneSub::Defeat);
    } else if (b.flags & kStaggered) {
        setFlags(b, kProvoked);
        enterSub(actor, OneSub::Stagger);
    } else if (r == 1) {
        setFlags(b, kProvoked);
    }
    switch (OneSub(actor.subState)) { // [0x8973a8]
    case OneSub::Idle: idle(env, actor, b); break;
    case OneSub::Turn: turnToPlayer(env, actor, b); break;
    case OneSub::Throw: throwPieces(env, actor, b); break;
    case OneSub::Swing: swing(env, actor, b); break;
    case OneSub::Taunt: taunt(env, actor, boss, b); break;
    case OneSub::Stagger: stagger(actor, b); break;
    case OneSub::Defeat: defeatOne(actor, boss, b); break;
    default: notPortedAt(kStoryPort, "0x4f7050 sub-state past [0x8973a8]", actor.subState);
    }
    hitsOrNone(actor, b, (b.flagsLow & kHitsOff) != 0);
    animate(actor);
}
// exe 0x4f7180
void phaseTwo(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss, BossBlock& b) {
    watchPlayer(env, actor, b);
    const int r = takeHits(actor, b);
    if (r == -1) {
        boss.status = kStatusDying;
        setFlags(b, kDying);
    } else if (r != 0) {
        setFlags(b, kProvoked);
    }
    switch (TwoSub(actor.subState)) { // [0x8973c4]
    case TwoSub::Start: startTwo(actor, b); break;
    case TwoSub::Leap: leap(env, actor, b); break;
    case TwoSub::Chase: chase(env, actor, boss, b); break;
    case TwoSub::Lunge: lunge(env, actor, b); break;
    case TwoSub::ToRing: toRing(env, actor, boss, b); break;
    case TwoSub::Circle: circle(env, actor, boss, b); break;
    case TwoSub::MoveTo: moveTo(actor, boss, b); break;
    case TwoSub::Attack1: attack1(actor, boss, b); break;
    case TwoSub::Attack2: attack2(env, actor, boss, b); break;
    case TwoSub::Attack3: attack3(env, actor, boss, b); break;
    case TwoSub::Face: face(actor, boss, b); break;
    case TwoSub::Releap: releap(env, actor, b); break;
    case TwoSub::Recover: recover(env, actor, boss, b); break;
    case TwoSub::Defeat: defeatTwo(actor, b); break;
    default: notPortedAt(kStoryPort, "0x4f7180 sub-state past [0x8973c4]", actor.subState);
    }
    hitsOrNone(actor, b, (b.flags & (kDying | kHitsOff)) != 0);
    if (!(b.flags & kPhaseTwoTimerDone)) {
        b.phaseTwoTimer -= 1u;
        if (b.phaseTwoTimer == 0) {
            setFlags(b, kPhaseTwoTimerDone);
            message(0x68);
        }
    }
    animate(actor);
}
void goldStatueBoss(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4f6e60
    BossFields& boss = classState<BossFields>(actor);
    st3cGoldBossGlobals().bossBlock = boss.block.address();
    BossBlock& b = *boss.block;
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    if (boss.statueSignals & kStatueDestroyed) {
        balanceAdd(-0x1194); // exe 0x5e51d0
        clearFlags(b, kTauntPending);
        if (!(boss.status & kStatusDying)) {
            b.phaseTwoTimer = 0x12c;
            boss.statueSignals = 0;
            actor.enterState(uint8_t(BossState::PhaseTwo));
        }
    } else if (boss.statueSignals & kStatueHalf) {
        boss.statueSignals = 0;
        message(0x66);
    }
    // exe 0x5e5a50(actor, 0): off screen / far (unless marked)
    if (!(actor.runFlags & kActorRunMarked)) {
        if (pointVisibleOrNear(actor.pos, 0) != 0) actor.runFlags |= kActorOffScreen;
        else actor.runFlags &= uint8_t(~kActorOffScreen);
    }
    switch (BossState(actor.state)) { // [0x897398]
    case BossState::Bind: bind(actor, boss); break;
    case BossState::PhaseOne: phaseOne(env, actor, boss, b); break;
    case BossState::PhaseTwo: phaseTwo(env, actor, boss, b); break;
    case BossState::End: // exe 0x4f72c0
        setStoryFlag(kFlagBossDone); // exe 0x510e10
        freeActor(actor);            // exe 0x5e1490
        break;
    default: notPortedAt(kStoryPort, "0x4f6e60 state past [0x897398]", actor.state);
    }
}

} // namespace

void goldStatueBossUpdate(const PlayerEnv& env, uint32_t actor) { goldStatueBoss(env, animatedActorAt(actor)); }

} // namespace od::port
