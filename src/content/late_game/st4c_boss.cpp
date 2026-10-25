// ST4C (Elysium, late game): the boss and the classes it spawns.
//
// The boss (exe 0x507280; pool 0x20 type 0x7d sub 0, ST4C table 0x90bdb0 -> [0x90bda4]; subs 1 / 2 = the cutscene
// casts 0x5086c0 / 0x508cd0, not spawned by it). Its class block: its hit box, two timers, its flags (shadow,
// hittable), the ground saved where it vanished, its vertical speed, the teleports left in the current combo, the hit
// point thresholds passed, the damage still to apply.
// Its tick: the previous pose, the tint bytes 0x10 / 0x42, the state [0x897ee0]: bind 0x507340 (y -0x60, hit points =
// the row's << 6, the boss gauge exe 0x415d30(actor, 1, 6, 0xffffff)), active 0x5073f0 (the move [0x897eec], then its
// hit entries: bone 0 and three offsets of bones 8 / 9 / 10 written into bone 0's point), defeat 0x5075a0 (animation
// 0xf, then story flag 0x681); then the animation (exe 0x5e1c30 / 0x5e1e80). The shadow (exe 0x5f20f0, drawn while
// flag kBossShadow is set) is render output and left out.
// Moves: rise 0x5075f0 (out of the floor by the animation's key index), teleport 0x5076a0 (a random point (+-0x400, or
// +-0x200 inside a combo), flash 0x1a, fade in 0x5082d0; then the hit-point threshold (hp < max * (3 - passed) / 4:
// the fog 0x26, a combo of 1 / 2 / 3 by the difficulty -> orbit shots), else 1 in 4 -> charges, near (rng) -> volley,
// else beam), vanish 0x507990 (flash, fade out 0x508360, 4 sparks 0x54, back to teleport), charges 0x507b20 (2 / 3 / 4
// times: the pair of charges 0x55, then 5 shots 0x27), beam 0x507cf0 (the beam 0x2d + 4 sparkles 0x57, rises, dashes
// forward until a wall, sinks), orbit shots 0x507ea0 (8 orbiting shots 0x25, leaps, slams: the shockwave, camera
// shake / pad vibration), volley 0x508100 (motes 0x1d, a volley of 5 bolts 0x2c). Every move but rise / vanish turns
// toward the player (0x5083f0, 0x40 or 0x80 per tick) and applies its damage (0x508460: at most 0x20 a tick, the rest
// later); dead -> defeat.
//
// The projectiles (their tick: the state returns live (the wall test exe 0x40f570: a wall -> exe 0x58d4e0 + push out +
// burst; else the mover queue exe 0x4140b0 unless variant & 0x80), burst or freed):
//  * 0x25 [0x8d16b4] 0x5cccd0 / 0x5ccdf0 / 0x5cce80 / 0x5ccfa0: orbits the boss (radius 0xe00 << 4) for 0x20 ticks,
//    then homes on the player's head (0x5cd0a0) for 0xd0 - variant * 4 ticks; shootable (hp 0x20, 0x5cd1e0); the
//    boss dying fades it.
//  * 0x27 [0x8d1784] 0x5ce740 / 0x5ce880: from the midpoint of the boss's hands (bones 3 / 4 / 6 / 7), fans out by
//    variant, homes for 0x40 ticks (0x5ceab0); the player's shield (exe 0x5737b0) stops it.
//  * 0x2c [0x8d17d4] 0x5cfbd0 / 0x5cfcd0 / 0x5cfd40: rises at bone 1 offset by variant for 0x1c ticks (pitched at the
//    player), then flies (capsule hit); impact: a light column (static type 0x11 variant 2) + a blast 0x22.
//  * 0x2d [0x8d17e0] 0x5d00a0 / 0x5d0170 / 0x5d0270: held at bone 8, a hit entry, two 0xf8 rings; while the boss's
//    attack runs.
// The statics (their bytes from +0x28): 0x54 [0x8d0b7c] 16 rising sparks; 0x55 [0x8d0b84] variant 0 a glow at the
// hands' midpoint (0x20 ticks of 0xf4 discs), then 4 ticks of a disc at that point; variant 1 32 rays (POLY_G4 OT
// packets around the projected point); 0x57 [0x8d0bf4] 8 sparkles circling bone 8. The effects (their bytes from
// +0x18): 0x1a [0x88d780] the teleport flash (0xc0 G4 pairs a tick, 4 RNG draws each), 0x1d [0x88d848] a rising mote.
// The controller 0x26 [0x88c020]: the fog colour / distance by its level (0..0x3f) while the boss's attack runs, then
// back down.
//
// Deviations: the OT packets (0x43a320, 0x5b9550) go through the draw_commands side channel (dc::otQuadG4, corners as
// camera-input points; the OTZ / OT links are not replicated, as game/global_fx). Names are hypotheses from the
// observed behaviour.
#include "content/late_game/st4c_boss.hpp"
#include "content/late_game/st4c_boss_state.hpp"

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
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/light_column.hpp"
#include "game/matrix.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/room_params.hpp"
#include "game/shockwave.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace st4c_boss_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
int16_t wholeOf(int32_t v) { return int16_t(uint32_t(v) >> 16); } // a 16.16 value's integer part
uint32_t grey15(uint32_t v) { return ((v << 5 | v) << 5) | v; }    // a grey 15-bit colour
uint8_t difficulty() { return gameState().difficulty; }
void sound(uint16_t id, const Actor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380

// the stat row of the boss's type (a null row would crash the exe)
AnimatedActorStats bossStats(const AnimatedActor& boss) {
    if (!hasAnimatedStats(boss.type)) notPortedAt(kStoryPort, "st4c_boss: null type row (the exe faults), type", boss.type);
    return animatedStats(boss.type);
}
// exe 0x5e1810: bone n's point (the exe has no null check)
ShortVec4& bone(const AnimatedActor& actor, uint32_t n) { return bonePoint(actor, n, "st4c_boss exe 0x5e1810"); }
// the tint word kept in a bone point's spare lane
BoneColour boneTint(const AnimatedActor& actor, uint32_t n) {
    return BoneColour(bone(actor, n).spare);
}
// the turn of an s16 angle toward a 12-bit angle by step (0x5083f0 / 0x5cd0a0): snapped onto it within snapBelow
void turnAt(int16_t& angle, int target, int step, int snapBelow) {
    const int d = angleDiff(uint32_t(target), uint32_t(int32_t(angle)));
    const int ad = d >= 0 ? d : -d;
    if (ad < snapBelow) angle = int16_t(target);
    else if (d >= 1) angle = int16_t(angle - step);
    else angle = int16_t(angle + step);
}
// a mover's after-move bytes: airborne, off any platform
void setAirborne(Actor& actor) {
    actor.airborne = kAirborneFalling;
    actor.onPlatform = 0;
    actor.platform = nullptr;
}
// the forward speed stored as the exe does: the z velocity's dword (its spare lane the sign)
void setForwardSpeed(Actor& actor, int32_t speed) {
    actor.velocity.setZSpare(int16_t(speed), int16_t(uint32_t(speed) >> 16));
}
// a blast (static global type 7, exe 0x5e1300) at the actor: its size 0xc0, its attack
void blast(const Actor& at, uint8_t variant, uint16_t attack) {
    const uint32_t slot = allocActor60(0);
    if (!slot) return;
    StaticActor& b = staticActorAt(slot);
    BlastParams params(actorHeaderAt(slot));
    b.loopFlags |= kActorSweepGroup2;
    b.flags = kActorLive;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = variant;
    b.pos = at.pos;
    params.size = 0xc0;
    params.attack = attack;
}

// ---- the screen-anchored OT polygons (as game/global_fx) ----------------------------------------------------------
struct Anchor {
    int16_t pt[3];
    CameraPoint c;
    float k;
};
Anchor anchorShifted(const int16_t p[3]) { // a point in game units, as exe 0x5f2dd0 shifts it
    const int sh = stage().coordinateShift & 0x1f;
    Anchor an{};
    for (int i = 0; i < 3; ++i) an.pt[i] = t16(int(p[i]) << sh);
    an.c = cameraPoint(an.pt);
    const int32_t focal = projection().focal;
    an.k = focal != 0 ? float(an.c.view[2]) / float(focal) : 0.0f;
    return an;
}
void g4Screen(uint32_t owner, const Anchor& an, const uint32_t c[4], const uint32_t xy[4]) { // DR_MODE abr 1 first
    int16_t v[4][3];
    float off[4][2];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) v[i][j] = an.pt[j];
        off[i][0] = float(t16(int16_t(uint16_t(xy[i])) - an.c.sx)) * an.k;
        off[i][1] = float(t16(int16_t(uint16_t(xy[i] >> 16)) - an.c.sy)) * an.k;
    }
    dc::otQuadG4(owner, c, 1, v, off);
}
void g4World(uint32_t owner, const uint32_t c[4], const int16_t v[4][3]) {
    static const float kNoOff[4][2] = {};
    dc::otQuadG4(owner, c, 1, v, kNoOff);
}

// ---- the boss (exe 0x507280) ---------------------------------------------------------------------------------------

enum class BossState : uint8_t { Bind = 0, Active = 1, Defeat = 2 };
enum class BossMove : uint8_t { Rise = 0, Teleport = 1, Vanish = 2, Charges = 3, Beam = 4, OrbitShots = 5, Volley = 6 };
enum class RiseStep : uint8_t { Start = 0, Rising = 1 };
enum class TeleportStep : uint8_t { Place = 0, Wait = 1, FadeIn = 2 };
enum class VanishStep : uint8_t { Start = 0, FadeOut = 1, Sparks = 2, Wait = 3 };
enum class ChargesStep : uint8_t { Start = 0, Charge = 1, Shots = 2, End = 3 };
enum class BeamStep : uint8_t { Start = 0, Rise = 1, Dash = 2, Sink = 3 };
enum class OrbitStep : uint8_t { Start = 0, Spawn = 1, Crouch = 2, Leap = 3, Fall = 4, Slam = 5, Pause = 6, Rise = 7, End = 8 };
enum class VolleyStep : uint8_t { Start = 0, Gather = 1, Bolts = 2, Release = 3, Recover = 4, End = 5 };
enum class BossAnim : uint8_t {
    Rise = 0, Charge = 1, Throw = 2, ChargeEnd = 3, BeamSink = 6, Beam = 7, OrbitShots = 8, Volley = 0xa,
    VolleyRelease = 0xb, VolleyRecover = 0xc, Vanish = 0xd, Appear = 0xe, Defeat = 0xf, Leap = 0x10, Slam = 0x11,
    SlamRise = 0x12
};

// its flags
constexpr uint8_t kBossShadow = 0x01, kBossHittable = 0x02;
constexpr uint32_t kFadeBones = 11;
constexpr uint32_t kFlagBossDefeated = 0x681;
constexpr uint32_t kDamagePerTick = 0x20;
constexpr uint16_t kHurtSound = 0x8d;
// the classes it spawns (the stage's tables)
constexpr uint8_t kOrbitShotType = 0x25, kHomingShotType = 0x27, kBoltType = 0x2c, kBeamType = 0x2d;
constexpr uint8_t kSparksType = 0x54, kChargeType = 0x55, kSparklesType = 0x57;
constexpr uint8_t kFlashType = 0x1a, kMoteType = 0x1d, kFogType = 0x26;

DataArray<int8_t> boneTints() {
    return DataArray<int8_t>(stateAddress(TableId::St4cBossBoneTints));
}
// the spawn word of the classes that follow the boss
struct BossChildParams : DataRecord {
    static constexpr uint32_t kBytes = 0x10;
    uint32_t spawnBoss = 0; // +0xc
    template <class R> static BossChildParams load(const R& r) {
        BossChildParams v;
        v.spawnBoss = r.template read<uint32_t>(0xc);
        return v;
    }
};
// the boss's attack runs while its step counter is set (its children end with it)
bool attackRuns(const AnimatedActor& boss) { return boss.stepCounter != 0; }

void playAnim(AnimatedActor& actor, BossAnim anim) {
    actor.anim = uint8_t(anim);
    actor.animKey.frame = 0;
}
// the animation's end key (its flags byte 0xff)
bool animDone(const AnimatedActor& actor) { return int8_t(actor.animKey.flags) == -1; }
void enterMove(AnimatedActor& actor, BossMove move) { actor.subState = uint8_t(move); }
// the next move inside a combo keeps the step counter (the step byte only), else both cleared
void stepsAfterMove(AnimatedActor& actor, const BossFields& boss) {
    if (boss.comboLeft != 0) actor.step = 0;
    else actor.restartStep();
}

void fadeStart(AnimatedActor& actor, BossFields& boss) { // exe 0x5082a0
    actor.flags |= kActorDrawn;
    boss.timer = 0x18;
    actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a00
}
// exe 0x5082d0: fade in -- bone k's tint = min(t[k] - timer, 0x10) grey when timer <= t[k], else black; true = done
bool fadeIn(AnimatedActor& actor, BossFields& boss) {
    const int timer = boss.timer;
    actor.tint = 0;
    for (uint32_t k = 0; k < kFadeBones; ++k) {
        const int threshold = boneTints()[k];
        BoneColour tint = boneTint(actor, k);
        if (timer <= threshold) {
            int level = threshold - timer;
            if (level >= 0x11) level = 0x10;
            tint = uint16_t(grey15(uint32_t(level)));
        } else {
            tint = 0;
        }
    }
    if (timer - 1 < 0) {
        actor.semiTransparency = semiTransparencyBits(0, 1); // exe 0x5e1a00
        return true;
    }
    boss.timer = int16_t(timer - 1);
    return false;
}
// exe 0x508360: fade out -- bone k's tint = max(timer - t[k] + 0x10, 0) grey when timer <= t[k], else neutral; true =
// done (hidden)
bool fadeOut(AnimatedActor& actor, BossFields& boss) {
    const int timer = boss.timer;
    actor.tint = 0; // (the exe's two byte stores)
    for (uint32_t k = 0; k < kFadeBones; ++k) {
        const int threshold = boneTints()[k];
        BoneColour tint = boneTint(actor, k);
        if (timer <= threshold) {
            int level = (timer - threshold) + 0x10;
            if (level < 0) level = 0;
            tint = uint16_t(grey15(uint32_t(level)));
        } else {
            tint = kTintNeutral;
        }
    }
    if (timer - 1 < 0) {
        actor.flags &= uint8_t(~kActorDrawn);
        return true;
    }
    boss.timer = int16_t(timer - 1);
    return false;
}
void facePlayer(AnimatedActor& actor, int step) { // exe 0x5083f0: the yaw toward the player
    turnAt(actor.rotation.y, angleOf(player().pos, actor.pos), step, step);
}
// exe 0x508460: the damage of the tick (at most 0x20, the rest queued); -1 dead, 1 hurt, 0 nothing
int takeDamage(AnimatedActor& actor, BossFields& boss) {
    uint32_t damage = boss.hitBox.result & kHitDamageMask;
    if ((boss.hitBox.result & kHitAttackByPlayer) && damage) {
        actor.tint = kTintFlash;
        sound(kHurtSound, actor);
        if (damage >= kDamagePerTick + 1) {
            boss.queuedDamage = uint16_t(damage - kDamagePerTick);
            damage = kDamagePerTick;
        }
        return hitPointsAdd(actor, -int(damage)) ? -1 : 1;
    }
    const uint32_t queued = boss.queuedDamage;
    if (!queued) return 0;
    if (queued >= kDamagePerTick + 1) {
        boss.queuedDamage = uint16_t(queued - kDamagePerTick);
        damage = kDamagePerTick;
    } else {
        damage = queued;
        boss.queuedDamage = 0;
    }
    return hitPointsAdd(actor, -int(damage)) ? -1 : 1;
}
// the common tail of the moves: dead -> defeat (its shadow on), else shadow and hittable
void moveTail(AnimatedActor& actor, BossFields& boss, bool takesDamage = true) {
    if (takesDamage && takeDamage(actor, boss) < 0) {
        actor.enterState(uint8_t(BossState::Defeat));
        boss.flags |= kBossShadow;
        return;
    }
    boss.flags |= kBossShadow | kBossHittable;
}
void notHittable(BossFields& boss) { boss.flags &= uint8_t(~(kBossShadow | kBossHittable)); }

void spawnFog(const AnimatedActor& actor) { // exe 0x508530: the fog controller (pool 0xe0 type 0x26) on the boss
    const uint32_t slot = allocActorE0();
    if (!slot) return;
    ControllerActor& fog = controllerActorAt(slot);
    fog.flags = kActorLive;
    fog.type = kFogType;
    fog.loopFlags |= kActorSweepGroup2;
    fog.variant = 0;
    fog.setParamsWord(actor.at);
}
// exe 0x508560: camera shake, pad vibration, the shockwave (its sequencer variant 3: a disc hit entry)
void slam(const AnimatedActor& actor) {
    cameraShakeAt(0x2000, 0x200, 0x100, 0x2000, actor.pos);   // exe 0x40eab0
    padVibrateAt(0x401, 0x10220e0, 0x100, 0x2000, actor.pos); // exe 0x55cd80
    const uint32_t slot = allocActor40(kActorStageClass);    // exe 0x5e1280
    if (!slot) return;
    ProjectileActor& shock = projectileActorAt(slot);
    ShockwaveParams params(projectileActorAt(slot));
    shock.loopFlags |= kActorSweepGroup2;
    shock.flags = kActorLive;
    shock.type = kShockwaveStageType;
    shock.variant = kShockwaveSequencer | 3;
    shock.pos.x = actor.pos.x;
    shock.pos.y = actor.pos.y;
    params.hitMode = kShockHitDisc;
    params.downward = 0;
    shock.pos.z = actor.pos.z;
}
void spawnMotes(const AnimatedActor& actor) { // exe 0x5085e0: four motes (pool 0xa0 type 0x1d) around the boss
    for (int k = 0; k < 4; ++k) {
        const uint32_t slot = allocActorA0(kActorStageClass);
        if (!slot) continue;
        EffectActor& mote = effectActorAt(slot);
        mote.flags = kActorLive;
        mote.type = kMoteType;
        mote.variant = 0;
        mote.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        mote.pos.x = int16_t((fixedCos(int(rngStep())) >> 5) + actor.pos.x.whole);
        mote.pos.y = int16_t(actor.pos.y.whole - int(rngStep() & 0x7f));
        mote.pos.z = int16_t((fixedSin(int(rngStep())) >> 5) + actor.pos.z.whole);
    }
}
// the teleport flash (pool 0xa0 type 0x1a) at the boss's x / z on the ground; false when none
bool spawnFlash(const PlayerEnv& env, const AnimatedActor& actor, uint16_t* groundOut) {
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!slot) return false;
    EffectActor& flash = effectActorAt(slot);
    flash.flags = kActorLive;
    flash.type = kFlashType;
    flash.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    flash.variant = 0;
    flash.pos.x = actor.pos.x.whole;
    const uint16_t g = uint16_t(actorGround(env, actor)); // exe 0x40f190
    flash.pos.y = int16_t(g);
    flash.pos.z = actor.pos.z.whole;
    if (groundOut) *groundOut = g;
    return true;
}
// a child of the boss (pool 0x40 / 0x60, the stage's class table): its type, variant, loop bits and the boss
void spawnChild(uint32_t slot, uint8_t type, uint8_t variant, uint8_t loopBits, const AnimatedActor& boss) {
    ActorHeader& child = actorHeaderAt(slot);
    child.flags = kActorLive;
    child.type = type;
    child.subtype = 0;
    child.variant = variant;
    child.loopFlags |= loopBits;
    child.setParamsWord(boss.at);
}
void restart(AnimatedActor& actor) { // the next move: vanish (difficulty >= 2) or rise, the steps cleared
    enterMove(actor, difficulty() >= 2 ? BossMove::Vanish : BossMove::Rise);
    actor.restartStep();
}

void bind(AnimatedActor& actor, BossFields& boss) { // exe 0x507340
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.pos.y = int32_t(0xffa00000u);
    actor.anim = uint8_t(BossAnim::Rise);
    actor.prevAnim = 0xff;
    actor.animKey.frame = 0;
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(uint32_t(animatedStats(actor.type).hitPoints) << 6) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St4cBossBox);
    boss.hitBox.previousX = actor.prevPos.x.whole;
    boss.hitBox.previousZ = actor.prevPos.z.whole;
    boss.hitBox.result = 0;
    boss.clearFlags();
    boss.comboLeft = 0;
    boss.thresholdsPassed = 0;
    boss.queuedDamage = 0;
    actor.enterStateKeepingStep(uint8_t(BossState::Active));
    actor.restartStep();
    hudAddBossGauge(actor, 1, 6, 0xffffff); // exe 0x415d30
}

void rise(AnimatedActor& actor, BossFields& boss) { // exe 0x5075f0
    switch (RiseStep(actor.step)) {
    case RiseStep::Start:
        playAnim(actor, BossAnim::Rise);
        actor.step = uint8_t(RiseStep::Rising);
        break;
    case RiseStep::Rising: {
        const int key = int8_t(actor.keyIndex);
        const uint32_t i = uint32_t((key * 0x1000) / 0x46) & 0xfff;
        actor.pos.y = int32_t((0xffffffc0u - uint32_t(int32_t(fixedCos(int(i))) >> 7)) << 16);
        if (key == 0) {
            enterMove(actor, BossMove::Vanish);
            stepsAfterMove(actor, boss);
        }
        break;
    }
    default: break;
    }
    moveTail(actor, boss);
}

void teleport(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) { // exe 0x5076a0
    const TeleportStep step = TeleportStep(actor.step);
    if (step == TeleportStep::Place) {
        if (boss.comboLeft != 0) {
            actor.pos.x = int32_t(((rngStep() & 0x3ff) - 0x200u) << 16);
            actor.pos.z = int32_t(((rngStep() & 0x3ff) - 0x200u) << 16);
        } else {
            actor.pos.x = int32_t(((rngStep() & 0x7ff) - 0x400u) << 16);
            actor.pos.z = int32_t(((rngStep() & 0x7ff) - 0x400u) << 16);
        }
        spawnFlash(env, actor, nullptr);
        sound(0x2b5, actor);
        boss.timer = 0x10;
        ++actor.step;
        notHittable(boss);
        return;
    }
    if (step == TeleportStep::Wait) {
        boss.timer -= 1u;
        if (boss.timer != 0) {
            notHittable(boss);
            return;
        }
        fadeStart(actor, boss);
        playAnim(actor, BossAnim::Appear);
        actor.pos.y = int32_t(0xfe740000u);
        actor.rotation.y = int16_t(angleOf(player().pos, actor.pos));
        boss.verticalSpeed = 0x180;
        ++actor.step;
    } else if (step != TeleportStep::FadeIn) {
        notHittable(boss);
        return;
    }
    if (!fadeIn(actor, boss)) {
        actor.pos.y += uint32_t(int32_t(boss.verticalSpeed) << 12);
        boss.verticalSpeed -= 0x10u;
        notHittable(boss);
        return;
    }
    auto pick = [&](BossMove next) {
        enterMove(actor, next);
        actor.restartStep();
        notHittable(boss);
        boss.queuedDamage = 0;
    };
    if (boss.comboLeft != 0) { // a combo goes on: the orbiting shots again
        enterMove(actor, BossMove::OrbitShots);
        actor.step = 0;
        notHittable(boss);
        boss.queuedDamage = 0;
        return;
    }
    if (int(actor.hitPoints) < (int(actor.maxHitPoints) * (3 - int(boss.thresholdsPassed))) >> 2) {
        spawnFog(actor);
        sound(0x2c5, actor);
        sound(0x2bf, actor);
        enterMove(actor, BossMove::OrbitShots);
        const uint8_t d = difficulty();
        boss.comboLeft = int8_t(d < 2 ? 1 : (d < 3 ? 2 : 3));
        boss.thresholdsPassed += 1u;
        actor.restartStep();
        notHittable(boss);
        boss.queuedDamage = 0;
        return;
    }
    if (!(rngStep() & 3)) {
        pick(BossMove::Charges);
        return;
    }
    const uint32_t r = rngStep();
    pick(uint32_t(planarDistance(actor.pos, player().pos)) < (r & 0x3ff) + 0x400 ? BossMove::Volley : BossMove::Beam);
}

void vanish(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) { // exe 0x507990
    switch (VanishStep(actor.step)) {
    case VanishStep::Start: {
        uint16_t g = 0;
        if (spawnFlash(env, actor, &g)) boss.savedGround = int16_t(g);
        else boss.savedGround = actor.pos.y.whole;
        fadeStart(actor, boss);
        sound(0x2b4, actor);
        playAnim(actor, BossAnim::Vanish);
        boss.verticalSpeed = 0;
        ++actor.step;
        break;
    }
    case VanishStep::FadeOut:
        if (!fadeOut(actor, boss)) {
            actor.pos.y += uint32_t(int32_t(boss.verticalSpeed) << 12);
            boss.verticalSpeed -= 0x10u;
        } else {
            boss.timer = 0x40;
            ++actor.step;
        }
        break;
    case VanishStep::Sparks:
        for (int k = 0; k < 4; ++k) {
            const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (!slot) continue;
            StaticActor& sparks = staticActorAt(slot);
            sparks.flags = kActorLive;
            sparks.type = kSparksType;
            sparks.subtype = 0;
            sparks.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
            sparks.variant = 0;
            sparks.pos.x = actor.pos.x;
            sparks.pos.y = int32_t(uint32_t((int(boss.savedGround) - int(rngStep() & 0x7f)) - 0x40) << 16);
            sparks.pos.z = actor.pos.z;
        }
        ++actor.step;
        [[fallthrough]];
    case VanishStep::Wait:
        boss.timer -= 1u;
        if (boss.timer == 0) {
            enterMove(actor, BossMove::Teleport);
            stepsAfterMove(actor, boss);
        }
        break;
    default: break;
    }
    notHittable(boss);
}

// exe 0x507b20: 2..4 x (the pair of charges 0x55, then 5 shots 0x27)
void charges(AnimatedActor& actor, BossFields& boss) {
    switch (ChargesStep(actor.step)) {
    case ChargesStep::Start: {
        const uint8_t d = difficulty();
        boss.timer = int16_t(d < 2 ? 2 : (d < 3 ? 3 : 4));
        playAnim(actor, BossAnim::Charge);
        sound(0x2c1, actor);
        ++actor.step;
        break;
    }
    case ChargesStep::Charge:
        if (!animDone(actor)) break;
        {
            const int16_t left = boss.timer;
            boss.timer = int16_t(left - 1);
            if (left != 0) {
                for (uint32_t k = 0; k < 2; ++k) {
                    const uint32_t slot = allocActor60(kActorStageClass);
                    if (!slot) continue;
                    spawnChild(slot, kChargeType, uint8_t(k), kActorSweepGroup2 | kActorLateLoop, actor);
                }
                sound(0x2b8, actor);
                boss.subTimer = 0x20;
                ++actor.step;
                ++actor.stepCounter;
            } else {
                playAnim(actor, BossAnim::ChargeEnd);
                actor.step += 2u;
            }
        }
        break;
    case ChargesStep::Shots:
        boss.subTimer -= 1u;
        if (boss.subTimer != 0) break;
        for (uint32_t k = 0; k < 5; ++k) {
            const uint32_t slot = allocActor40(kActorStageClass);
            if (!slot) continue;
            spawnChild(slot, kHomingShotType, uint8_t(k), kActorSweepGroup2, actor);
        }
        actor.anim = uint8_t(BossAnim::Throw);
        actor.prevAnim = 0xff;
        actor.animKey.frame = 0;
        sound(0x2b9, actor);
        actor.step -= 1u;
        break;
    case ChargesStep::End:
        if (animDone(actor)) restart(actor);
        break;
    default: break;
    }
    facePlayer(actor, 0x40);
    moveTail(actor, boss);
}

// exe 0x507cf0: the beam 0x2d + 4 sparkles 0x57, rise, dash to a wall, sink
void beam(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) {
    switch (BeamStep(actor.step)) {
    case BeamStep::Start: {
        const uint32_t slot = allocActor40(kActorStageClass);
        if (slot) {
            ActorHeader& shot = actorHeaderAt(slot);
            shot.flags = kActorLive;
            shot.type = kBeamType;
            shot.subtype = 0;
            shot.variant = 0;
            shot.loopFlags |= kActorSweepGroup2;
            shot.setParamsWord(actor.at);
        }
        for (int k = 0; k < 4; ++k) {
            const uint32_t s = allocActor60(kActorStageClass);
            if (!s) continue;
            spawnChild(s, kSparklesType, 0, kActorSweepGroup2, actor);
        }
        playAnim(actor, BossAnim::Beam);
        sound(0x2ba, actor);
        sound(0x2c2, actor);
        actor.velocity.y = 0x60;
        ++actor.step;
        ++actor.stepCounter;
        break;
    }
    case BeamStep::Rise:
        integrateVelocity(actor, 0, actor.velocity.y, 0); // exe 0x5e3690
        facePlayer(actor, 0x40);
        if (animDone(actor)) {
            sound(699, actor);
            actor.velocity.z = int16_t(0xf800);
            ++actor.step;
        }
        break;
    case BeamStep::Dash:
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
            actorPushOut(env, actor);                  // exe 0x410960
            playAnim(actor, BossAnim::BeamSink);
            actor.velocity.y = int16_t(0xff60);
            ++actor.step;
            actor.stepCounter = 0;
        }
        break;
    case BeamStep::Sink:
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (animDone(actor)) restart(actor);
        break;
    default: break;
    }
    moveTail(actor, boss);
}

void orbitShots(AnimatedActor& actor, BossFields& boss) { // exe 0x507ea0: 8 orbiting shots 0x25, leap, slam
    bool noDamage = false;
    switch (OrbitStep(actor.step)) {
    case OrbitStep::Start:
        playAnim(actor, BossAnim::OrbitShots);
        boss.timer = 0x18;
        ++actor.step;
        ++actor.stepCounter;
        break;
    case OrbitStep::Spawn:
        boss.timer -= 1u;
        if (boss.timer != 0) break;
        for (uint32_t k = 0; k < 8; ++k) {
            const uint32_t slot = allocActor40(kActorStageClass);
            if (!slot) continue;
            spawnChild(slot, kOrbitShotType, uint8_t(k), kActorSweepGroup2, actor);
        }
        sound(700, actor);
        ++actor.step;
        break;
    case OrbitStep::Crouch:
        if (!animDone(actor)) break;
        actor.velocity.y = int16_t(0xfe80);
        actor.anim = uint8_t(BossAnim::Leap);
        actor.animKey.frame = 0;
        sound(0x2c3, actor);
        ++actor.step;
        break;
    case OrbitStep::Leap:
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (animDone(actor)) {
            actor.velocity.y = 0x400;
            ++actor.step;
        }
        noDamage = true;
        break;
    case OrbitStep::Fall:
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (actor.pos.y >= 1) {
            actor.pos.y = 0;
            playAnim(actor, BossAnim::Slam);
            slam(actor);
            sound(0x2bd, actor);
            ++actor.step;
        }
        noDamage = true;
        break;
    case OrbitStep::Slam:
        if (animDone(actor)) {
            boss.timer = 8;
            ++actor.step;
        }
        noDamage = true;
        break;
    case OrbitStep::Pause:
        boss.timer -= 1u;
        if (boss.timer == 0) {
            actor.velocity.y = int16_t(0xff80);
            playAnim(actor, BossAnim::SlamRise);
            ++actor.step;
        }
        noDamage = true;
        break;
    case OrbitStep::Rise:
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (actor.pos.y < -0x600000) {
            actor.pos.y = int32_t(0xffa00000u);
            ++actor.step;
        }
        break;
    case OrbitStep::End:
        if (animDone(actor)) {
            enterMove(actor, difficulty() >= 2 ? BossMove::Vanish : BossMove::Rise);
            const uint8_t left = uint8_t(boss.comboLeft - 1);
            boss.comboLeft = int8_t(left);
            if (left != 0) actor.step = 0;
            else actor.restartStep();
        }
        break;
    default: break;
    }
    facePlayer(actor, 0x40);
    moveTail(actor, boss, !noDamage);
}

void volley(AnimatedActor& actor, BossFields& boss) { // exe 0x508100: motes, then 5 bolts 0x2c every 3 ticks
    const uint8_t step = actor.step;
    switch (VolleyStep(step)) {
    case VolleyStep::Start:
        playAnim(actor, BossAnim::Volley);
        actor.step = uint8_t(step + 1u);
        spawnMotes(actor);
        break;
    case VolleyStep::Gather:
        if (int8_t(actor.keyIndex) >= 0x19) {
            boss.timer = 3;
            boss.subTimer = 5;
            ++actor.step;
            ++actor.stepCounter;
        }
        spawnMotes(actor);
        break;
    case VolleyStep::Bolts:
        boss.timer -= 1u;
        if (boss.timer == 0) {
            boss.subTimer -= 1u;
            boss.timer = 3;
            if (boss.subTimer == 0) ++actor.step;
            const uint32_t slot = allocActor40(kActorStageClass);
            if (slot) {
                ActorHeader& bolt = actorHeaderAt(slot);
                bolt.flags = kActorLive;
                bolt.type = kBoltType;
                bolt.subtype = 0;
                bolt.variant = uint8_t(boss.subTimer);
                bolt.setParamsWord(actor.at);
                bolt.loopFlags |= kActorSweepGroup2;
            }
        }
        spawnMotes(actor);
        break;
    case VolleyStep::Release:
        if (animDone(actor)) {
            actor.prevAnim = 0xff;
            playAnim(actor, BossAnim::VolleyRelease);
            sound(0x2c6, actor);
            ++actor.step;
        }
        spawnMotes(actor);
        break;
    case VolleyStep::Recover:
        if (animDone(actor)) {
            actor.anim = uint8_t(BossAnim::VolleyRecover);
            actor.prevAnim = 0xff;
            actor.animKey.frame = 0;
            actor.step = uint8_t(step + 1u);
        }
        break;
    case VolleyStep::End:
        if (animDone(actor)) restart(actor);
        break;
    default: break;
    }
    facePlayer(actor, 0x80);
    moveTail(actor, boss);
}

// the boss's hit entries: bone 0, then bones 8 / 9 / 10 offset (each written into bone 0's point)
struct BodyEntry {
    uint32_t bone;
    int16_t x, y, z;
    uint32_t shape;
};
constexpr uint32_t kBodyGroup = hitGroup(kHitGroupBodies);
constexpr BodyEntry kBodyEntries[3] = {
    {8, 0, 0x68, 0, kBodyGroup | kHitNoLockOn | hitRadius(0x60)},
    {9, 0x68, 0xc0, 0, kBodyGroup | kHitNoLockOn | hitRadius(0x30)},
    {10, -0x68, 0xc0, 0, kBodyGroup | kHitNoLockOn | hitRadius(0x30)},
};

void active(const PlayerEnv& env, AnimatedActor& actor, BossFields& boss) { // exe 0x5073f0
    boss.clearFlags();
    switch (BossMove(actor.subState)) { // [0x897eec]
    case BossMove::Rise: rise(actor, boss); break;
    case BossMove::Teleport: teleport(env, actor, boss); break;
    case BossMove::Vanish: vanish(env, actor, boss); break;
    case BossMove::Charges: charges(actor, boss); break;
    case BossMove::Beam: beam(env, actor, boss); break;
    case BossMove::OrbitShots: orbitShots(actor, boss); break;
    case BossMove::Volley: volley(actor, boss); break;
    default: notPortedAt(kStoryPort, "0x5073f0 sub-state +9 (table 0x897eec)", actor.subState);
    }
    if (!(actor.flags & kActorDrawn)) return;
    boss.hitBox.previousX = actor.prevPos.x.whole;
    boss.hitBox.previousZ = actor.prevPos.z.whole;
    boss.hitBox.result = 0;
    if (!(boss.flags & kBossHittable)) return;
    const uint32_t touch = actor.hitPoints < 0 ? kHitAttackBump : kHitAttackSolid;
    const AnimatedActorStats row = bossStats(actor);
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(row.hitHeight);
    const uint32_t attack = uint32_t(row.attack) | touch;
    ShortVec4& point = bone(actor, 0);
    const int16_t p0[3] = {point.x, point.y, point.z};
    hitListSubmit(p0, boss.hitBox, owner | kBodyGroup | hitRadius(0x60), attack); // exe 0x5e4120
    for (const BodyEntry& e : kBodyEntries) { // exe 0x5e1970(actor, bone, v, bone 0's point)
        const int16_t v[3] = {e.x, e.y, e.z};
        int16_t out[3];
        bonePointOffset(actor, e.bone, v, out);
        point.x = out[0];
        point.y = out[1];
        point.z = out[2];
        const int16_t p[3] = {point.x, point.y, point.z};
        hitListSubmit(p, boss.hitBox, owner | e.shape, attack);
    }
}

void defeat(AnimatedActor& actor) { // exe 0x5075a0
    if (actor.subState == 0) {
        playAnim(actor, BossAnim::Defeat);
        actor.subState = 1;
    } else if (actor.subState == 1 && animDone(actor)) {
        setStoryFlag(kFlagBossDefeated);
        actor.subState += 1u;
    }
}

void bossTick(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x507280
    BossFields& boss = classState<BossFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.tint = kTintNeutral; // (the exe's two byte stores)
    actor.savePreviousContact();
    switch (BossState(actor.state)) { // [0x897ee0]
    case BossState::Bind: bind(actor, boss); break;
    case BossState::Active: active(env, actor, boss); break;
    case BossState::Defeat: defeat(actor); break;
    default: notPortedAt(kStoryPort, "0x507280 state (table 0x897ee0)", actor.state);
    }
    if (actor.flags & kActorLive) animateAsked(actor, actor.animKey.frame); // exe 0x5e1c30 / 0x5e1e80
}

// ---- the projectiles (pool 0x40) -----------------------------------------------------------------------------------

enum class ShotResult : int { Live = 0, Burst = 1, Freed = 2 };
constexpr uint8_t kVariantNoMoverQueue = 0x80;
constexpr uint32_t kEnemyShotGroup = 3; // the hit-list group of the boss's shots

// the common tick of 0x5ccc40 / 0x5ce6b0 / 0x5cfb40: live (a wall -> burst), burst or freed
template <class Fields, class State, class Burst>
void shotTick(const PlayerEnv& env, ProjectileActor& actor, State state, Burst burst) {
    Fields& shot = classState<Fields>(actor);
    const ShotResult r = state(actor, shot);
    if (r == ShotResult::Live) {
        const int32_t t = int32_t(actorTestMove(env, actor)); // exe 0x40f570
        if (t < 0) {
            queueBreakableWallHit(uint32_t(t), int(uint32_t(shot.attack) << 4)); // exe 0x58d4e0
            actorPushOut(env, actor);                                            // exe 0x410960
            burst(actor);
            return;
        }
        if (actor.variant & kVariantNoMoverQueue) return;
        actorCollisionSubmitMover(actor); // exe 0x4140b0
        return;
    }
    if (r == ShotResult::Burst) burst(actor);
    else if (r == ShotResult::Freed) freeActor(actor); // exe 0x5e1500
}

// -- 0x25: the orbiting shot. Its block: its life, attack, timer, travel rotation (pitch / yaw / roll), hit box, the
// boss
enum class OrbitShotState : uint8_t { Start = 0, Orbit = 1, Home = 2, Fade = 3 };
void orbitPos(ProjectileActor& actor, OrbitShotFields& shot) { // exe 0x5cd030: radius 0xe00 << 4, 0x100 higher
    const AnimatedActor& boss = *shot.boss;
    const uint32_t ang = uint16_t(shot.travel.y) & 0xfff;
    actor.pos.x = int32_t(uint32_t(fixedCos(int(ang)) * 0xe00) + uint32_t(boss.pos.x));
    actor.pos.y = int32_t(uint32_t(boss.pos.y) - 0x1000000u);
    actor.pos.z = int32_t(uint32_t(fixedSin(int(ang)) * 0xe00) + uint32_t(boss.pos.z));
}
// exe 0x5cd0a0: yaw / pitch toward a point around the player's head
void homeOrbit(ProjectileActor& actor, OrbitShotFields& shot, int step) {
    const Player& p = player();
    const uint32_t ang = uint16_t(shot.travel.y) & 0xfff;
    const int32_t t[3] = {int32_t(uint32_t(fixedCos(int(ang)) * 0x800) + uint32_t(p.pos.x)),
                          int32_t(uint32_t(p.pos.y) + uint32_t(actor.variant) * 0xfff00000u - 0x400000u),
                          int32_t(uint32_t(fixedSin(int(ang)) * 0x800) + uint32_t(p.pos.z))};
    const int yaw = angleOf(t[0], t[2], actor.pos.x, actor.pos.z);
    const int16_t from[3] = {wholeOf(t[0]), wholeOf(t[1]), wholeOf(t[2])};
    const int16_t to[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int pitch = 0x800 - pitchOf(from, to);
    turnAt(shot.travel.y, yaw, step, step);
    turnAt(shot.travel.x, pitch, step >> 1, step >> 1);
}
int orbitHurt(ProjectileActor& actor, const OrbitShotFields& shot) { // exe 0x5cd1e0
    const uint32_t damage = shot.hitBox.result & kHitDamageMask;
    if ((shot.hitBox.result & kHitAttackByPlayer) && damage) {
        actor.tint = kTintFlash;
        sound(kHurtSound, actor);
        return hitPointsAdd(actor, -int(damage)) ? -1 : 1;
    }
    return 0;
}
ShotResult orbitShot(ProjectileActor& actor, OrbitShotFields& shot) { // [0x8d16b4]
    switch (OrbitShotState(actor.state)) {
    case OrbitShotState::Start: { // exe 0x5cccd0
        if (!bindActorModel(actor, 0)) return ShotResult::Freed;
        actor.flags |= kActorDrawn;
        shot.boss = slotByIdentityWord<AnimatedActor>(shot.spawnBoss);
        shot.travel.x = 0;
        shot.travel.y = int16_t(uint32_t(actor.variant) << 9);
        shot.travel.z = 0;
        actor.rotation.setXY(0, 0);
        actor.rotation.setZSpare(0, 0);
        actor.velocity.setXY(0, 0);
        const uint8_t d = difficulty();
        setForwardSpeed(actor, d >= 3 ? -0x180 : (d >= 2 ? -0x140 : -0x100));
        orbitPos(actor, shot);
        actor.prevPos = actor.pos;
        moveAlongRotation(actor.prevPos, shot.travel, 0, 0, actor.velocity.z * -2); // exe 0x5e3810
        actor.hitPoints = 0x20;
        actor.maxHitPoints = 0x20;
        actor.collisionBox.pointAt(TableId::St4cBossOrbitShotBox);
        actor.actorHit = 0;
        shot.life = int16_t(0xd0 - int(actor.variant) * 4);
        shot.attack = bossStats(*shot.boss).strongAttack;
        shot.hitBox.result = 0;
        actor.tint = 0;
        actor.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a70
        sound(0xb1, actor);
        shot.timer = 0;
        ++actor.state;
        return ShotResult::Live;
    }
    case OrbitShotState::Orbit: // exe 0x5ccdf0
        shot.travel.y += 0x20u;
        actor.rotation.y -= 0x40u;
        orbitPos(actor, shot);
        shot.timer += 1u;
        if (shot.timer >= 0x20) {
            actor.semiTransparency = semiTransparencyBits(0, 0);
            actor.tint = kTintNeutral;
            shot.travel.y = int16_t(uint32_t(-int(shot.travel.y) - 0x400) & 0xfff);
            ++actor.state;
        } else {
            actor.tint = uint16_t(grey15(uint32_t(shot.timer >> 1)));
        }
        if (shot.boss->hitPoints < 0) actor.state = uint8_t(OrbitShotState::Fade);
        return ShotResult::Live;
    case OrbitShotState::Home: // exe 0x5cce80
        actor.rotation.y -= 0x40u;
        actor.prevPos = actor.pos;
        if (actor.actorHit != 0 || (shot.hitBox.result & kHitAttackBump)) return ShotResult::Burst;
        shot.life -= 1u;
        if (shot.life == 0) return ShotResult::Burst;
        homeOrbit(actor, shot, 0x10);
        moveAlongRotation(actor.pos, shot.travel, 0, 0, actor.velocity.z);
        actor.tint = kTintNeutral;
        if (orbitHurt(actor, shot) < 0) return ShotResult::Burst;
        shot.hitBox.result = 0;
        shot.hitBox.previousX = actor.prevPos.x.whole;
        shot.hitBox.previousZ = actor.prevPos.z.whole;
        {
            const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
            hitListSubmit(p, shot.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x40),
                          uint32_t(shot.attack) | kHitAttackSolid); // exe 0x5e4120
        }
        setAirborne(actor);
        if (shot.boss->hitPoints < 0) actor.state = uint8_t(OrbitShotState::Fade);
        return ShotResult::Live;
    case OrbitShotState::Fade: // exe 0x5ccfa0
        shot.timer -= 1u;
        if (shot.timer == 0) return ShotResult::Freed;
        actor.tint = uint16_t(grey15(uint32_t(shot.timer >> 1)));
        return ShotResult::Live;
    default: notPortedAt(kStoryPort, "0x5ccc40 state (table 0x8d16b4)", actor.state);
    }
}

// -- 0x27: the homing shot. Its block: its life, attack, travel rotation, hit box, the boss
enum class HomingShotState : uint8_t { Start = 0, Fly = 1 };
// the midpoint of the boss's hands (bones 3 / 4 / 6 / 7), doubled
void handsMid(const AnimatedActor& boss, int out[3]) {
    const ShortVec4& p3 = bone(boss, 3), p4 = bone(boss, 4), p6 = bone(boss, 6), p7 = bone(boss, 7);
    const int16_t a3[3] = {p3.x, p3.y, p3.z}, a4[3] = {p4.x, p4.y, p4.z};
    const int16_t a6[3] = {p6.x, p6.y, p6.z}, a7[3] = {p7.x, p7.y, p7.z};
    for (int k = 0; k < 3; ++k) out[k] = int(t16(a7[k] * 2 - a6[k])) + int(t16(a4[k] * 2 - a3[k]));
}
// the player's head (0x80 up) as a 16.16 point
void playerHead(int32_t t[3]) {
    const Player& p = player();
    t[0] = p.pos.x;
    t[1] = int32_t(uint32_t(p.pos.y) - 0x800000u);
    t[2] = p.pos.z;
}
int pitchFrom(const int32_t t[3], const Actor& actor) {
    const int16_t from[3] = {wholeOf(t[0]), wholeOf(t[1]), wholeOf(t[2])};
    const int16_t to[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    return pitchOf(from, to);
}
void homeShot(ProjectileActor& actor, ShortVec4& travel, int step) { // exe 0x5ceab0: toward the player's head
    int32_t t[3];
    playerHead(t);
    const int yaw = angleOf(t[0], t[2], actor.pos.x, actor.pos.z);
    const int pitch = 0x800 - pitchFrom(t, actor);
    const int as = step >= 0 ? step : -step;
    {
        const int d = angleDiff(uint32_t(yaw), uint32_t(int32_t(travel.y)));
        const int ad = d >= 0 ? d : -d;
        if (as <= ad) travel.y = int16_t(travel.y + (d >= 1 ? -step : step));
        else travel.y = int16_t(yaw);
    }
    const int d = angleDiff(uint32_t(pitch), uint32_t(int32_t(travel.x)));
    const int ad = d >= 0 ? d : -d;
    if (ad < (as >> 1)) travel.x = int16_t(pitch);
    else travel.x = int16_t(travel.x + (d >= 1 ? -(step >> 1) : (step >> 1)));
}
ShotResult homingShot(ProjectileActor& actor, HomingShotFields& shot) { // [0x8d1784]
    switch (HomingShotState(actor.state)) {
    case HomingShotState::Start: { // exe 0x5ce740
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return ShotResult::Freed;
        }
        actor.flags |= kActorDrawn;
        shot.boss = slotByIdentityWord<AnimatedActor>(shot.spawnBoss);
        int mid[3];
        handsMid(*shot.boss, mid);
        actor.pos.x = int32_t(uint32_t(mid[0]) * 0x8000u);
        actor.pos.y = int32_t(uint32_t(mid[1]) * 0x8000u);
        actor.pos.z = int32_t(uint32_t(mid[2]) * 0x8000u);
        int32_t t[3];
        playerHead(t);
        shot.travel.x = int16_t(0x800 - pitchFrom(t, actor));
        shot.travel.y = int16_t((int(actor.variant) - 2) * 0x200 + shot.boss->rotation.y);
        shot.travel.z = 0;
        actor.rotation.setXY(0, 0);
        actor.rotation.setZSpare(0, 0);
        actor.velocity.setXY(0, 0);
        setForwardSpeed(actor, -0x400);
        actor.prevPos = actor.pos;
        moveAlongRotation(actor.prevPos, shot.travel, 0, 0, actor.velocity.z * -2); // exe 0x5e3810
        actor.collisionBox.pointAt(TableId::St4cBossHomingShotBox);
        actor.actorHit = 0;
        shot.life = 0x40;
        shot.attack = bossStats(*shot.boss).strongAttack;
        shot.hitBox.result = 0;
        actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a70
        ++actor.state;
        return ShotResult::Live;
    }
    case HomingShotState::Fly: // exe 0x5ce880
        actor.prevPos = actor.pos;
        actor.rotation.x += 0x80u;
        actor.rotation.y += 0x80u;
        if (actor.actorHit != 0 || (shot.hitBox.result & kHitAttackBump)) return ShotResult::Burst;
        shot.life -= 1u;
        if (uint16_t(shot.life) == 0) return ShotResult::Burst;
        homeShot(actor, shot.travel, int(uint16_t(shot.life)) + 8);
        moveAlongRotation(actor.pos, shot.travel, 0, 0, actor.velocity.z);
        if (playerShieldBlocks(actor, 0x40)) return ShotResult::Freed; // exe 0x5737b0
        shot.hitBox.result = 0;
        shot.hitBox.previousX = actor.prevPos.x.whole;
        shot.hitBox.previousZ = actor.prevPos.z.whole;
        {
            const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
            hitListSubmit(p, shot.hitBox, hitOwner(actor.slot) | hitGroup(kEnemyShotGroup) | hitRadius(0x40),
                          uint32_t(shot.attack) | kHitAttackStrong); // exe 0x5e4120
        }
        setAirborne(actor);
        return ShotResult::Live;
    default: notPortedAt(kStoryPort, "0x5ce6b0 state (table 0x8d1784)", actor.state);
    }
}

// -- 0x2c: the bolt. Its block: its life, attack, timer, rise, travel rotation, hit box, the boss
enum class BoltState : uint8_t { Start = 0, Rise = 1, Fly = 2 };
constexpr uint8_t kBoltColumnVariant = 2, kBoltBlastVariant = 0x22;
// exe 0x5cff30: at the boss's bone 1, offset sideways by the variant, pitched at the player
void boltAim(ProjectileActor& actor, BoltFields& bolt) {
    const AnimatedActor& boss = *bolt.boss;
    const ShortVec4& bp = bone(boss, 1);
    const uint32_t yaw = uint16_t(boss.rotation.y) & 0xfff;
    const uint32_t side = ((uint32_t(actor.variant) - 2u) * 0x180u + yaw) & 0xfff;
    const int32_t pt[3] = {
        int32_t((uint32_t(fixedSin(int(yaw)) - fixedSin(int(side))) & 0xffff0u) * 0x1000u + uint32_t(int32_t(bp.x) << 16)),
        int32_t(uint32_t(int32_t(bp.y) - 0x80 + int32_t(bolt.rise)) << 16),
        int32_t((uint32_t(fixedCos(int(yaw)) - fixedCos(int(side))) & 0xffff0u) * 0x1000u + uint32_t(int32_t(bp.z) << 16))};
    const Player& p = player();
    const int16_t from[3] = {p.pos.x.whole, 0, p.pos.z.whole};
    const int16_t to[3] = {wholeOf(pt[0]), wholeOf(pt[1]), wholeOf(pt[2])};
    bolt.travel.x = int16_t(-pitchOf(from, to));
    bolt.travel.y = int16_t(yaw);
    bolt.travel.z = 0;
    actor.pos.x = pt[0];
    actor.pos.y = pt[1];
    actor.pos.z = pt[2];
    actor.rotation.x = int16_t(bolt.travel.x + 0x400);
    actor.rotation.z = 0;
    actor.rotation.y = bolt.travel.y;
}
// exe 0x5cfe80: a light column (variant 2) + a blast 0x22, sound 0x2be, freed
void boltBurst(ProjectileActor& actor) {
    BoltFields& bolt = classState<BoltFields>(actor);
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (slot) {
        StaticActor& column = staticActorAt(slot);
        LightColumnParams params(staticActorAt(slot));
        column.flags = kActorLive;
        column.type = kLightColumnStageType;
        column.variant = kBoltColumnVariant;
        column.loopFlags |= kActorSweepGroup2;
        column.pos.y = 0;
        column.pos.x = actor.pos.x;
        column.pos.z = actor.pos.z;
        params.radius = 0x100;
        params.heightSteps = 0x80;
        params.damage = uint8_t(bolt.attack >> 1);
    }
    const uint32_t blastSlot = allocActor60(0); // exe 0x5e1300
    if (blastSlot) {
        StaticActor& b = staticActorAt(blastSlot);
        BlastParams params(actorHeaderAt(blastSlot));
        b.flags = kActorLive;
        b.type = typeByte(GlobalStaticType::Blast);
        b.variant = kBoltBlastVariant;
        b.loopFlags |= kActorSweepGroup2;
        b.pos.y = 0;
        b.pos.x = actor.pos.x;
        b.pos.z = actor.pos.z;
        params.size = 0xc0;
        params.attack = uint16_t(bolt.attack >> 1);
    }
    sound(0x2be, actor);
    freeActor(actor);
}
ShotResult boltState(ProjectileActor& actor, BoltFields& bolt) { // [0x8d17d4]
    switch (BoltState(actor.state)) {
    case BoltState::Start: // exe 0x5cfbd0
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return ShotResult::Freed;
        }
        actor.flags |= kActorDrawn;
        bolt.boss = slotByIdentityWord<AnimatedActor>(bolt.spawnBoss);
        bolt.rise = 0;
        boltAim(actor, bolt);
        actor.velocity.setXY(0, 0);
        setForwardSpeed(actor, 0x1000);
        actor.prevPos = actor.pos;
        moveAlongRotation(actor.prevPos, bolt.travel, 0, 0, actor.velocity.z * -2); // exe 0x5e3810
        actor.collisionBox.pointAt(TableId::St4cBossBoltBox);
        actor.actorHit = 0;
        actor.scale.x = 0x180;
        actor.scale.y = 0x180;
        actor.scale.z = 0x180;
        actor.tint = 0;
        bolt.timer = 0x1c;
        bolt.life = 0x40;
        bolt.attack = bossStats(*bolt.boss).bodyAttack;
        bolt.hitBox.result = 0;
        actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a70
        sound(0x2b6, actor);
        ++actor.state;
        return ShotResult::Live;
    case BoltState::Rise: { // exe 0x5cfcd0
        bolt.rise -= 4u;
        boltAim(actor, bolt);
        bolt.timer -= 1u;
        if (bolt.timer != 0) {
            int level = 0x1c - bolt.timer;
            if (level >= 0x11) level = 0x10;
            actor.tint = uint16_t(grey15(uint32_t(level)));
            return ShotResult::Live;
        }
        sound(0x2b7, actor);
        ++actor.state;
        return ShotResult::Live;
    }
    case BoltState::Fly: { // exe 0x5cfd40
        actor.prevPos = actor.pos;
        if (actor.actorHit != 0 || (bolt.hitBox.result & kHitAttackBump)) return ShotResult::Burst;
        bolt.life -= 1u;
        if (bolt.life == 0) return ShotResult::Burst;
        moveAlongRotation(actor.pos, bolt.travel, 0, 0, actor.velocity.z);
        const int16_t back[3] = {actor.prevPos.x.whole, actor.prevPos.y.whole, actor.prevPos.z.whole};
        const int16_t front[3] = {t16(actor.pos.x.whole * 2 - back[0]), t16(actor.pos.y.whole * 2 - back[1]),
                                  t16(actor.pos.z.whole * 2 - back[2])};
        bolt.hitBox.result = 0;
        bolt.hitBox.previousX = actor.prevPos.x.whole;
        bolt.hitBox.previousZ = actor.prevPos.z.whole;
        hitListSubmitCapsule(front, 0xc, back, 0xc, bolt.hitBox,
                             hitOwner(actor.slot) | hitGroup(kEnemyShotGroup) | hitCapsule(2),
                             uint32_t(bolt.attack) | kHitAttackStrong); // exe 0x5e4190
        setAirborne(actor);
        return ShotResult::Live;
    }
    default: notPortedAt(kStoryPort, "0x5cfb40 state (table 0x8d17d4)", actor.state);
    }
}

// -- 0x2d: the beam at bone 8. Its block: its level (..0x10), a word, its hit box, the boss
enum class BeamShotState : uint8_t { Start = 0, Hold = 1, Fade = 2 };
constexpr uint8_t kBeamDrawFlag = 0x20; // +0 bit 5, set with the drawn bit (its reader is not traced)
void beamAtBone(ProjectileActor& actor, BeamShotFields& beam) { // exe 0x5d0310: the boss's bone 8 into the position
    const ShortVec4& bp = bone(*beam.boss, 8);
    actor.pos.x = int32_t(uint32_t(int32_t(bp.x)) << 16);
    actor.pos.y = int32_t(uint32_t(int32_t(bp.y)) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(bp.z)) << 16);
}
void beamDraw(const ProjectileActor& actor, const BeamShotFields& beam) { // exe 0x5d0350: two 0xf8 ring records
    const uint32_t v3 = uint32_t(int32_t(beam.level)) << 3;
    const uint32_t colour = v3 | ((uint32_t(int32_t(beam.level)) << 11 | v3) << 8) | 0xf8000000u;
    const uint32_t x = uint16_t(actor.pos.x.whole), z = uint16_t(actor.pos.z.whole);
    const uint32_t f = (gameState().frame & 0x3fu) << 16;
    dc::emit({f | 0x200100fu, colour, x, 0x22000000u, z | 0x1020000u, 0x1200180u, 0x7c9120e0u, 0x2f3fffu}, actor.at);
    dc::emit({f | 0x200100eu, colour, x, 0x22000000u, z | 0x1020000u, 0xc00120u, 0x7c9120e0u, 0x2f3fffu}, actor.at);
}
void beamShot(ProjectileActor& actor) { // exe 0x5d0060: [0x8d17e0], then the tint = grey(level >> 1)
    BeamShotFields& beam = classState<BeamShotFields>(actor);
    switch (BeamShotState(actor.state)) {
    case BeamShotState::Start: // exe 0x5d00a0
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            break;
        }
        actor.flags |= kActorDrawn | kBeamDrawFlag;
        beam.boss = slotByIdentityWord<AnimatedActor>(beam.spawnBoss);
        beamAtBone(actor, beam);
        actor.prevPos = actor.pos;
        actor.rotation.x = 0;
        actor.rotation.y = beam.boss->rotation.y;
        actor.rotation.z = 0;
        actor.velocity.setXY(0, 0);
        actor.velocity.setZSpare(0, 0);
        actor.scale.x = 0x400;
        actor.scale.y = 0x400;
        actor.scale.z = 0x400;
        actor.actorHit = 0;
        beam.hitBox.result = 0;
        beam.level = 0;
        beam.word = 0;
        actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a70
        ++actor.state;
        break;
    case BeamShotState::Hold: { // exe 0x5d0170
        const AnimatedActor& boss = *beam.boss;
        actor.prevPos = actor.pos;
        beamAtBone(actor, beam);
        actor.rotation.z += 0x80u;
        actor.rotation.y = boss.rotation.y;
        beam.hitBox.result = 0;
        beam.hitBox.previousX = actor.prevPos.x.whole;
        beam.hitBox.previousZ = actor.prevPos.z.whole;
        const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        hitListSubmit(p, beam.hitBox, hitOwner(actor.slot) | hitGroup(kEnemyShotGroup) | hitRadius(0x180),
                      uint32_t(bossStats(boss).shotAttack) | kHitAttackStrong); // exe 0x5e4120
        setAirborne(actor);
        if (beam.level <= 0xf) beam.level += 1u;
        beamDraw(actor, beam);
        if (!attackRuns(boss)) ++actor.state;
        break;
    }
    case BeamShotState::Fade: { // exe 0x5d0270
        beam.level -= 1u;
        if (beam.level == 0) {
            freeActor(actor);
            break;
        }
        actor.prevPos = actor.pos;
        beamAtBone(actor, beam);
        actor.rotation.y = beam.boss->rotation.y;
        actor.rotation.z += 0x80u;
        setAirborne(actor);
        beamDraw(actor, beam);
        break;
    }
    default: notPortedAt(kStoryPort, "0x5d0060 state (table 0x8d17e0)", actor.state);
    }
    actor.tint = uint16_t(grey15(uint32_t(beam.level >> 1)));
}

// ---- the statics (pool 0x60) ---------------------------------------------------------------------------------------

// -- 0x54: 16 rising sparks. Its bytes: the dead sparks, the live count, the sparks {x, y, z, angle}, their sizes and
// lives
enum class SparksState : uint8_t { Start = 0, Run = 1 };
void sparksDraw(const StaticActor& actor, SparksFields& f) { // exe 0x5b8eb0
    if (f.live <= 0) return;
    for (int i = 0; i < f.live; ++i) {
        Spark& e = f.sparks[size_t(i)];
        const uint8_t life = f.lives[size_t(i)];
        if (!life) continue;
        const uint8_t left = uint8_t(life - 1);
        f.lives[size_t(i)] = left;
        if (!left) {
            f.dead += 1u;
            continue;
        }
        e.y -= left;
        const uint32_t ang = (uint16_t(e.angle) + 0x40u) & 0xfff;
        e.angle = int16_t(ang);
        const int r = (0x20 - int(left)) * 8;
        const uint32_t x = uint16_t(t16((fixedCos(int(ang)) * r >> 12) + e.x));
        const uint32_t z = uint16_t(t16((fixedSin(int(ang)) * r >> 12) + e.z));
        const uint32_t xy = uint32_t(uint16_t(e.y)) << 16 | x;
        const uint32_t grey = (uint32_t(left) | uint32_t(left) << 8 | uint32_t(left) << 16) << 1;
        const uint32_t sz = f.sizes[size_t(i)];
        dc::emit({0x4000u, grey | 0xf0000000u, xy, 0x22000000u, z | 0x30000u, sz << 0x12}, actor.at);
        dc::emit({0x4000u, (grey | 0xf8000000u) << 1, xy, 0x22000000u, z | 0x30000u, sz << 0x10}, actor.at);
    }
}
void sparks(StaticActor& actor) { // exe 0x5b8cf0: [0x8d0b7c]
    SparksFields& f = classState<SparksFields>(actor);
    switch (SparksState(actor.state)) {
    case SparksState::Start: // exe 0x5b8d10
        f.dead = 0;
        f.live = 1;
        for (uint32_t i = 0; i < uint32_t(kSparkCount); ++i) {
            Spark& e = f.sparks[i];
            e.x = int16_t(int(rngStep() & 0x1f) + actor.pos.x.whole - 0x10);
            e.y = int16_t(actor.pos.y.whole - int(rngStep() & 0xff));
            e.z = int16_t(int(rngStep() & 0x1f) + actor.pos.z.whole - 0x10);
            e.angle = int16_t(rngStep() & 0xfff);
            f.sizes[i] = uint8_t((rngStep() & 3) + 2);
            f.lives[i] = uint8_t((rngStep() & 0xf) + 0x18);
        }
        sparksDraw(actor, f);
        ++actor.state;
        return;
    case SparksState::Run: // exe 0x5b8e70
        f.live += 1u;
        if (f.live >= 0x11) {
            f.live = 0x10;
            if (f.dead == 0x10) freeActor(actor);
        }
        sparksDraw(actor, f);
        return;
    default: notPortedAt(kStoryPort, "0x5b8cf0 state (table 0x8d0b7c)", actor.state);
    }
}

// -- 0x55: the charge between the boss's hands. Its bytes: a timer, a word, 32 angles, lengths and s8 sizes / spins,
// the boss
enum class ChargeState : uint8_t { Start = 0, Glow = 1, Disc = 2 };
void chargeMid(const ChargeFields& f, int16_t out[3]) { // exe 0x5b9830: the midpoint of the boss's hands
    int mid[3];
    handsMid(*f.boss, mid);
    for (int k = 0; k < 3; ++k) out[k] = t16(mid[k] >> 1);
}
// exe 0x5b92f0: 32 screen discs 0xf4 around the projected midpoint + two cores
void glowDraw(const StaticActor& actor, const ChargeFields& f) {
    int16_t pt[3];
    chargeMid(f, pt);
    int16_t sxy[2];
    if (projectOnScreen(pt, sxy) < 0) return; // exe 0x5f2dd0
    int16_t depth = 0;
    const int width = projectedWidth(pt, 0x100, &depth); // exe 0x5f2f20
    if (!width) return;
    int16_t dz = t16(depth - 8);
    if (dz <= 0) dz = 1;
    const uint32_t d = uint16_t(dz);
    const uint32_t bright = uint32_t(int(f.timer) * -8 + 0x100);
    const uint32_t quarter = bright >> 2, half = bright >> 1;
    const uint32_t cHalf = ((half | 0xfffff400u) << 8 | half) << 8 | half;
    const uint32_t cQuarter = ((quarter | 0xfffff400u) << 8 | quarter) << 8 | quarter;
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        const uint32_t ang = f.angles[i] & 0xfff;
        const int r = int32_t(uint32_t(f.lengths[i]) * uint32_t(f.timer + 4) * uint32_t(width)) >> 0xd;
        const uint32_t xy = uint32_t(uint16_t(t16((fixedSin(int(ang)) * r >> 12) + sxy[1]))) << 16 |
                            uint16_t(t16((fixedCos(int(ang)) * r >> 12) + sxy[0]));
        const int size = f.spins[i];
        dc::emit({0x4000u, cHalf, xy, 0x22000000u, d | 0x30000u, uint32_t((size * width) >> 10) << 16}, actor.at);
        dc::emit({0x4000u, cQuarter, xy, 0x22000000u, d | 0x30000u, uint32_t((size * width) >> 8) << 16}, actor.at);
    }
    const uint32_t cxy = uint32_t(uint16_t(sxy[1])) << 16 | uint16_t(sxy[0]);
    dc::emit({0x4000u, (bright | 0xfff40000u) << 8 | bright, cxy, 0x22000000u, d | 0x20000u,
              uint32_t((width * 3) >> 2) << 16},
             actor.at);
    dc::emit({0x4000u, ((bright | 0xfffff400u) << 8 | bright) << 8 | bright, cxy, 0x22000000u, d | 0x20000u,
              uint32_t(width >> 1) << 16},
             actor.at);
}
// exe 0x5b9550: 32 additive rays (POLY_G4: two corners at the projected midpoint, two at +- a spread at the ray's
// length)
void raysDraw(const StaticActor& actor, ChargeFields& f) {
    int16_t pt[3];
    chargeMid(f, pt);
    int16_t sxy[2];
    if (projectOnScreen(pt, sxy) < 0) return;
    int16_t depth = 0;
    const int width = projectedWidth(pt, 0x100, &depth);
    if (!width) return;
    const Anchor an = anchorShifted(pt);
    const uint32_t bright = uint32_t(int(f.timer) * -8 + 0x100) >> 2;
    const uint32_t col = (((bright & 0xffff) | 0x3a00u) << 8 | bright) << 8 | bright;
    const uint32_t cxy = uint32_t(uint16_t(sxy[1])) << 16 | uint16_t(sxy[0]);
    for (uint32_t i = 0; i < kChargeRays; ++i) {
        f.angles[i] += uint32_t(int(f.spins[i]) * 2);
        const uint32_t ang = f.angles[i];
        const int length = f.lengths[i];
        const int spread = ((0xa0 - length) * 3) >> 2;
        const int r = int32_t(uint32_t(0x28 - int(f.timer)) * uint32_t(length) * uint32_t(width)) >> 0xc;
        const uint32_t a0 = ((ang & 0xfff) - uint32_t(spread)) & 0xfff, a1 = ((ang & 0xfff) + uint32_t(spread)) & 0xfff;
        auto at = [&](uint32_t g) {
            return uint32_t(uint16_t(t16((fixedSin(int(g)) * r >> 12) + sxy[1]))) << 16 |
                   uint16_t(t16((fixedCos(int(g)) * r >> 12) + sxy[0]));
        };
        const uint32_t xy[4] = {cxy, cxy, at(a0), at(a1)};
        const uint32_t c[4] = {col, col, 0, 0};
        g4Screen(actor.at, an, c, xy);
    }
}
void discDraw(const StaticActor& actor, const ChargeFields& f) { // exe 0x5b9770: two discs 0xf0 at the actor
    const uint32_t xy = uint32_t(uint16_t(actor.pos.y.whole)) << 16 | uint16_t(actor.pos.x.whole);
    const uint32_t bright = uint32_t(int(f.timer) * 0x20 - 1);
    const uint32_t z = uint16_t(actor.pos.z.whole);
    dc::emit({0x4000u, ((bright | 0xfffff000u) << 8 | bright) << 8 | bright, xy, 0x22000000u, z | 0x20000u, 0xe00000u},
             actor.at);
    dc::emit({0x4000u, (bright | 0xfff00000u) << 8 | bright, xy, 0x22000000u, z | 0x20000u, 0x1400000u}, actor.at);
}
void chargeDraw(const StaticActor& actor, ChargeFields& f) {
    if (actor.variant == 0) glowDraw(actor, f);
    else raysDraw(actor, f);
}
void charge(StaticActor& actor) { // exe 0x5b9010: [0x8d0b84]
    ChargeFields& f = classState<ChargeFields>(actor);
    switch (ChargeState(actor.state)) {
    case ChargeState::Start: // exe 0x5b9030
        f.boss = slotByIdentityWord<AnimatedActor>(f.spawnBoss);
        f.timer = 0x20;
        f.word = 1;
        for (uint32_t i = 0; i < kChargeRays; ++i) {
            f.angles[i] = uint16_t(((rngStep() & 0x7f) + (i << 7)) & 0xfff);
            if (actor.variant == 0) {
                f.lengths[i] = uint8_t((rngStep() & 0x7f) + 0x40);
                f.spins[i] = int8_t((rngStep() & 0x3f) + 0x10);
            } else {
                f.lengths[i] = uint8_t((rngStep() & 0x7f) + 0x20);
                f.spins[i] = int8_t((rngStep() & 0x7f) - 0x40);
            }
        }
        chargeDraw(actor, f);
        ++actor.state;
        return;
    case ChargeState::Glow: // exe 0x5b91f0
        if (!attackRuns(*f.boss)) {
            freeActor(actor);
            return;
        }
        f.timer -= 1u;
        if (f.timer != 0) {
            chargeDraw(actor, f);
            return;
        }
        if (actor.variant != 0) {
            freeActor(actor);
            return;
        }
        {
            int16_t pt[3];
            chargeMid(f, pt);
            actor.pos.x = int32_t(uint32_t(int32_t(pt[0])) << 16);
            actor.pos.y = int32_t(uint32_t(int32_t(pt[1])) << 16);
            actor.pos.z = int32_t(uint32_t(int32_t(pt[2])) << 16);
        }
        f.timer = 4;
        discDraw(actor, f);
        ++actor.state;
        return;
    case ChargeState::Disc: // exe 0x5b92b0
        f.timer -= 1u;
        if (f.timer != 0 && attackRuns(*f.boss)) discDraw(actor, f);
        else freeActor(actor);
        return;
    default: notPortedAt(kStoryPort, "0x5b9010 state (table 0x8d0b84)", actor.state);
    }
}

// -- 0x57: the sparkles circling bone 8. Its bytes: a level (..0x10), the count (..8), the next slot, the sparkles
// {phase, angle, size}, the boss
enum class SparklesState : uint8_t { Start = 0, Run = 1, Fade = 2 };
void sparkleSlot(SparklesFields& f, uint32_t mask) { // the next slot of 8: phase rng & mask, angle by slot, size
    const int k = f.next;
    Sparkle& e = f.sparkles[size_t(k)];
    e.phase = int16_t(rngStep() & mask);
    e.angle = int16_t(((rngStep() & 0x3ff) + (uint32_t(k) << 10)) & 0xfff);
    e.size = int16_t((rngStep() & 0x3f) + 4);
    f.next = int16_t(uint8_t(k + 1) & 7);
}
// exe 0x5ba430: around the boss's bone 8, turned by (0x400, its yaw, 0)
void sparklesDraw(const StaticActor& actor, SparklesFields& f) {
    const AnimatedActor& boss = *f.boss;
    const ShortVec4& bp = bone(boss, 8);
    const int16_t px = bp.x, py = bp.y, pz = bp.z;
    const Matrix3 m = rotationZYX(0x400, boss.rotation.y, 0); // exe 0x40c070
    for (int k = 0; k < f.count; ++k) {
        Sparkle& e = f.sparkles[size_t(k)];
        const uint32_t phase = (uint16_t(e.phase) + 0x80u) & 0x3ff;
        e.phase = int16_t(phase);
        const uint32_t g = uint16_t(e.angle) & 0xfff;
        const int s = fixedSin(int(phase));
        const int16_t in[3] = {int16_t(((fixedCos(int(g)) * 0x180 >> 12) * s) >> 12),
                               int16_t((fixedCos(int(phase)) * -0x180) >> 12),
                               int16_t(((fixedSin(int(g)) * 0x180 >> 12) * s) >> 12)};
        int16_t out[3];
        transformShortVector(m, in, out); // exe 0x40bb30
        const uint32_t xy = uint32_t(uint16_t(t16(out[1] + py))) << 16 | uint16_t(t16(out[0] + px));
        const uint32_t z = uint16_t(t16(out[2] + pz));
        const uint32_t c = uint32_t(((0x400 - int(e.phase)) * int(f.level)) >> 6);
        const uint32_t sz = uint32_t(int32_t(e.size));
        dc::emit({0x4000u, ((c | 0xfffff000u) << 8 | c) << 8 | c, xy, 0x22000000u, z | 0x30000u, sz << 16}, actor.at);
        dc::emit({0x4000u, c | 0xf0000000u, xy, 0x22000000u, z | 0x30000u, sz << 17}, actor.at);
    }
}
void sparkles(StaticActor& actor) { // exe 0x5ba240: [0x8d0bf4]
    SparklesFields& f = classState<SparklesFields>(actor);
    switch (SparklesState(actor.state)) {
    case SparklesState::Start: // exe 0x5ba260
        f.boss = slotByIdentityWord<AnimatedActor>(f.spawnBoss);
        f.level = 0;
        f.count = 0;
        f.next = 0;
        ++actor.state;
        return;
    case SparklesState::Run: // exe 0x5ba290
        if (f.level <= 0xf) f.level += 1u;
        if (f.count <= 7) f.count += 1u;
        sparkleSlot(f, 0x7f);
        sparklesDraw(actor, f);
        if (!attackRuns(*f.boss)) ++actor.state;
        return;
    case SparklesState::Fade: // exe 0x5ba360
        f.level -= 1u;
        if (f.level == 0) {
            freeActor(actor);
            return;
        }
        if (f.count <= 7) f.count += 1u;
        sparkleSlot(f, 0x3f);
        sparklesDraw(actor, f);
        return;
    default: notPortedAt(kStoryPort, "0x5ba240 state (table 0x8d0bf4)", actor.state);
    }
}

// ---- the effects (pool 0xa0) ---------------------------------------------------------------------------------------

enum class FlashState : uint8_t { Start = 0, Run = 1 };
enum class MoteState : uint8_t { Start = 0, Rise = 1 };
// the flash's timer; the mote's timer and radius
struct FlashFields {
    explicit FlashFields(const EffectActor& actor) : FlashFields(const_cast<EffectActor&>(actor)) {}
    explicit FlashFields(EffectActor& actor) : timer(actor.trail[0]) {}
    int16_t& timer;
};
struct MoteFields {
    explicit MoteFields(const EffectActor& actor) : MoteFields(const_cast<EffectActor&>(actor)) {}
    explicit MoteFields(EffectActor& actor) : timer(actor.trail[0]), radius(actor.trail[1]) {}
    int16_t& timer;
    int16_t& radius;
};
// exe 0x43a320: 0xc0 random spokes around the effect, each two additive POLY_G4 (rings of radius 0x80 at y 0, 0xa0 at
// y = -0x180 - rng, 0xc0 at twice that; the radii >> (0xc - shift)), coloured [0x88d760 + (rng & 7) * 4] & the timer
void flashDraw(const EffectActor& actor, const FlashFields& f) {
    const DataArray<uint32_t> colours(stateAddress(TableId::St4cBossFlashColours));
    const int sh = (0xc - int(stage().coordinateShift)) & 0x1f;
    const uint32_t t = uint32_t(int32_t(f.timer));
    const uint32_t mask = t | ((t << 8 | t) << 8);
    const int16_t org[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    for (int n = 0; n < 0xc0; ++n) {
        const uint32_t r0 = rngStep(), r1 = rngStep(), r2 = rngStep();
        const uint32_t angA = r0 & 0xfff;
        const uint32_t colour = colours[r2 & 7] & mask;
        const int16_t y1 = t16(0xfe80 - int(rngStep() & 0x7f));
        const uint32_t angB = ((r1 & 0x7f) + 0x20 + angA) & 0xfff;
        auto pt = [&](uint32_t g, int radius, int16_t y, int16_t out[3]) {
            out[0] = t16(org[0] + t16((fixedCos(int(g)) * radius) >> sh));
            out[1] = t16(org[1] + y);
            out[2] = t16(org[2] + t16((fixedSin(int(g)) * radius) >> sh));
        };
        int16_t p0a[3], p0b[3], p1a[3], p1b[3], p2a[3], p2b[3];
        pt(angA, 0x80, 0, p0a);
        pt(angB, 0x80, 0, p0b);
        pt(angA, 0xa0, y1, p1a);
        pt(angB, 0xa0, y1, p1b);
        const int16_t y2 = t16(int(y1) * 2);
        pt(angA, 0xc0, y2, p2a);
        pt(angB, 0xc0, y2, p2b);
        const uint32_t col = (colour | 0x1d000000u) * 2u;
        const uint32_t c1[4] = {col, col, colour, colour};
        const int16_t q1[4][3] = {{p0a[0], p0a[1], p0a[2]}, {p0b[0], p0b[1], p0b[2]},
                                  {p1a[0], p1a[1], p1a[2]}, {p1b[0], p1b[1], p1b[2]}};
        g4World(actor.at, c1, q1);
        const uint32_t c2[4] = {colour | 0x3a000000u, colour | 0x3a000000u, 0, 0};
        const int16_t q2[4][3] = {{p1a[0], p1a[1], p1a[2]}, {p1b[0], p1b[1], p1b[2]},
                                  {p2a[0], p2a[1], p2a[2]}, {p2b[0], p2b[1], p2b[2]}};
        g4World(actor.at, c2, q2);
    }
}
void flashEffect(EffectActor& actor) { // exe 0x43a2a0: [0x88d780]
    FlashFields f(actor);
    switch (FlashState(actor.state)) {
    case FlashState::Start: // exe 0x43a2c0
        f.timer = 0x28;
        flashDraw(actor, f);
        ++actor.state;
        return;
    case FlashState::Run: // exe 0x43a2f0
        f.timer -= 1u;
        if (f.timer != 0) flashDraw(actor, f);
        else freeActor(actor);
        return;
    default: notPortedAt(kStoryPort, "0x43a2a0 state (table 0x88d780)", actor.state);
    }
}
void moteDraw(const EffectActor& actor, const MoteFields& f) { // exe 0x43ad10: two discs 0xf0
    const uint32_t t = uint32_t(int32_t(f.timer));
    const uint32_t xy = actor.positionWord(0), z = uint16_t(actor.pos.z) | 0x30000u;
    const uint32_t r = uint32_t(int32_t(f.radius));
    dc::emit({0x60004000u, t * 0x100e0eu | 0xf0000000u, xy, 0x22000000u, z, r << 16}, actor.at);
    dc::emit({0x60004000u, t * 0xc0000u | 0xf0000000u, xy, 0x22000000u, z, r << 0x12}, actor.at);
}
void mote(EffectActor& actor) { // exe 0x43ac70: [0x88d848]
    MoteFields f(actor);
    switch (MoteState(actor.state)) {
    case MoteState::Start: // exe 0x43ac90
        f.timer = 0xc;
        f.radius = int16_t((rngStep() & 0x1f) + 8);
        moteDraw(actor, f);
        ++actor.state;
        return;
    case MoteState::Rise: // exe 0x43ace0
        f.timer -= 1u;
        if (f.timer != 0) {
            actor.pos.y -= 0x40u;
            moteDraw(actor, f);
        } else {
            freeActor(actor);
        }
        return;
    default: notPortedAt(kStoryPort, "0x43ac70 state (table 0x88d848)", actor.state);
    }
}

// ---- the fog controller (pool 0xe0) --------------------------------------------------------------------------------

enum class FogState : uint8_t { Start = 0, Darken = 1, Clear = 2 };
// the fog colour (exe 0x40ba90) / distance (exe 0x40ba60) and the player's burden by the level
void fogLevel(const FogFields& f) {
    const int v = f.level;
    setFogColour(uint8_t(v * 2), uint8_t(v), uint8_t(v));
    setFogDistance((v + 0x50) * 0x10);
    player().burden = int8_t(uint8_t(uint16_t(f.level) >> 1));
}
void fog(ControllerActor& actor) { // exe 0x427640: [0x88c020]
    FogFields& f = classState<FogFields>(actor);
    switch (FogState(actor.state)) {
    case FogState::Start: // exe 0x427660
        f.level = 0;
        f.setBossWord(f.spawnBoss);
        ++actor.state;
        return;
    case FogState::Darken: // exe 0x427680
        if (!attackRuns(f.boss())) {
            ++actor.state;
            return;
        }
        if (f.level <= 0x3e) f.level += 1u;
        fogLevel(f);
        return;
    case FogState::Clear: // exe 0x4276e0
        f.level -= 1u;
        if (f.level != 0) fogLevel(f);
        else freeActor(actor);
        return;
    default: notPortedAt(kStoryPort, "0x427640 state (table 0x88c020)", actor.state);
    }
}

} // namespace

void elysiumBossUpdate(const PlayerEnv& env, uint32_t actor) { bossTick(env, animatedActorAt(actor)); }

void elysiumOrbitShotUpdate(const PlayerEnv& env, uint32_t actor) {
    shotTick<OrbitShotFields>(env, projectileActorAt(actor), orbitShot, [](ProjectileActor& shot) { // exe 0x5ccfe0
        blast(shot, 0, 8);
        freeActor(shot);
    });
}

void elysiumHomingShotUpdate(const PlayerEnv& env, uint32_t actor) {
    shotTick<HomingShotFields>(env, projectileActorAt(actor), homingShot, [](ProjectileActor& shot) { // exe 0x5ce9a0
        blast(shot, 1, 8);
        freeActor(shot);
    });
}

void elysiumBoltUpdate(const PlayerEnv& env, uint32_t actor) {
    shotTick<BoltFields>(env, projectileActorAt(actor), boltState, boltBurst);
}

void elysiumBeamUpdate(const PlayerEnv&, uint32_t actor) { beamShot(projectileActorAt(actor)); }

void elysiumSparksUpdate(const PlayerEnv&, uint32_t actor) { sparks(staticActorAt(actor)); }

void elysiumChargeUpdate(const PlayerEnv&, uint32_t actor) { charge(staticActorAt(actor)); }

void elysiumSparklesUpdate(const PlayerEnv&, uint32_t actor) { sparkles(staticActorAt(actor)); }

void elysiumFlashUpdate(const PlayerEnv&, uint32_t actor) { flashEffect(effectActorAt(actor)); }

void elysiumMoteUpdate(const PlayerEnv&, uint32_t actor) { mote(effectActorAt(actor)); }

void elysiumFogUpdate(const PlayerEnv&, uint32_t actor) { fog(controllerActorAt(actor)); }

} // namespace od::port
