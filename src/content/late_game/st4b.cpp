// ST4B (the first visit's cutscene of script 0x59 and the boss fight that follows it): the stage classes. See
// content/late_game/st4b.hpp. Names are hypotheses from the observed behaviour.
#include "content/late_game/st4b.hpp"
#include "content/late_game/st4b_state.hpp"
#include "content/late_game/st4b_fx_state.hpp"

#include "content/saul_kada/breath_puff.hpp"

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
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/light_column.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/shockwave.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

using namespace od::port::st4b_state;

namespace od::port {

using namespace st4b_state;

namespace {

// the boss's move by rng & 0xf (a switch: the PC's byte table after the function's code, exe 0x4b9768; the PS1's jump
// table ST4BT 0x800e7058): 0..2 the dive, 3..7 the drop attack, 8..10 the volley, 11..15 the volley again
uint8_t bossMovePick(uint32_t r) { return r < 3 ? 0 : r < 8 ? 1 : r < 11 ? 2 : 3; }

void sound(uint16_t id) { soundPlayAtPos(id, kSoundAtListener); } // exe 0x5d5380(id, 0)
int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }
int16_t fold(int16_t v) { // the +-0x800 folds of the classes (16-bit)
    if (v >= 0x800) v = t16(v - 0x1000);
    if (v < -0x800) v = t16(v + 0x1000);
    return v;
}
// exe 0x5e1810: bone n's point as an s16 vector (a null result would crash the exe)
void bonePt(const AnimatedActor& actor, uint32_t bone, int16_t out[3]) {
    const ShortVec4& p = bonePoint(actor, bone, "st4b exe 0x5e1810");
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}
// exe 0x5e1970(actor, bone, v, out): the point v through the bone
void boneXform(const AnimatedActor& actor, uint32_t bone, int x, int y, int z, int16_t out[3]) {
    const int16_t v[3] = {int16_t(x), int16_t(y), int16_t(z)};
    bonePointOffset(actor, bone, v, out);
}
uint32_t stepTick() { return uint32_t(scriptState().stepTick); }
uint8_t scene() { return scriptState().stepSub; }

// ---- the cast of script 0x59 (types 0x16 / 2) ----------------------------------------------------------------------

enum class CastState : uint8_t { Bind = 0, Run = 1, Free = 2, First = 3, Second = 4, Third = 5, Fourth = 6 };

// the head of their ticks: the animation of the last tick, the previous pose, the tint, the contact bytes
void castHead(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
}
// their bind (exe 0x478b60 / 0x44b690; the variant's animation of `anims`): false when freed
bool castInit(AnimatedActor& actor, const uint8_t anims[3]) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return false;
    }
    actor.flags |= kActorDrawn;
    if (actor.variant <= 2) actor.anim = anims[actor.variant];
    actor.prevAnim = kNoAnimShown;
    animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = int16_t(0xff00);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.collisionBox = nullptr;
    return true;
}
// their tail: the animation (stepped while `stepping`), the shadow (exe 0x5f20f0: render output, not drawn), the
// texture tracks (exe 0x5e34f0)
void castTail(AnimatedActor& actor, KeyframeTracks& tracks, bool stepping) {
    if (actor.flags & kActorLive) {
        if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
        else if (stepping) animStepPose(actor);                             // exe 0x5e1e80
    }
    textureTracksTick(actor, tracks);
}
void walkForward(AnimatedActor& actor, int z) { moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, z); } // exe 0x5e36f0

// one face row: the timeline tick it fires at (0xffff: the end), the sequence and the key
struct FaceRow : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint16_t tick = 0; // +0x0
    uint8_t sequence = 0; // +0x2
    uint8_t key = 0; // +0x3
    template <class R> static FaceRow load(const R& r) {
        FaceRow v;
        v.tick = r.template read<uint16_t>(0x0);
        v.sequence = r.template read<uint8_t>(0x2);
        v.key = r.template read<uint8_t>(0x3);
        return v;
    }
};
// exe 0x4fb5d0(actor, t0, t1): the face rows of the tracks keyed to the cast timeline's tick: an exact match selects
// the track's sequence and advances its row
void faceRows(FaceRowCastFields& cast, uint32_t table0, uint32_t table1) {
    const FaceRow row[2] = {dataRecord<FaceRow>(table0 + uint32_t(cast.rows[0]) * 4, FaceRow::kBytes), dataRecord<FaceRow>(table1 + uint32_t(cast.rows[1]) * 4, FaceRow::kBytes)};
    for (uint32_t i = 0; i < 2; ++i) {
        const FaceRow& r = row[i];
        const uint16_t k = r.tick;
        if (k == 0xffff || uint32_t(k) != uint32_t(scriptState().part<CastTimelineTick>().stepTick)) continue;
        if (i) cast.tracks.second.select(r.sequence, r.key); // exe 0x5e33f0
        else cast.tracks.first.select(r.sequence, r.key);    // exe 0x5e33d0
        cast.rows[i] += 1u;
    }
}
void faceRowFirst(AnimatedActor& actor, FaceRowCastFields& cast) { // exe 0x478c60
    switch (actor.subState) {
    case 0:
        if (stepTick() == 0x94) {
            cast.stepping = 1;
            actor.subState = 1;
        }
        break;
    case 1:
        if (scene() != 1) return;
        if (stepTick() == 1) {
            actor.pos.x.whole = 0x220;
            actor.pos.z.whole = 0x80;
            actor.rotation.y = 0x400;
            actor.anim = 2;
            actor.subState = 2;
        }
        break;
    case 2:
        walkForward(actor, -0x50);
        if (stepTick() == 0x28) {
            actor.anim = 3;
            actor.subState = 3;
        }
        break;
    case 3:
        walkForward(actor, -0x28);
        if (stepTick() == 0x38) actor.subState = 4;
        break;
    case 4:
        switch (stepTick()) {
        case 0x1c0: actor.anim = 4; break;
        case 0x23d: actor.anim = 5; break;
        case 0x440: actor.anim = 6; break;
        default: break;
        }
        break;
    default: break;
    }
    if (scene() == 1) faceRows(cast, stateAddress(TableId::St4bFaceRowFirstRows), stateAddress(TableId::St4bFaceRowFirstRowsSecond));
}
void faceRowSecond(FaceRowCastFields& cast) { // exe 0x478d90
    if (stepTick() == 0x52) cast.stepping = 1;
    faceRows(cast, stateAddress(TableId::St4bFaceRowSecondRows), stateAddress(TableId::St4bFaceRowSecondRowsSecond));
}
void faceRowThird(AnimatedActor& actor, FaceRowCastFields& cast) { // exe 0x478dc0
    KeyframeTracks& t = cast.tracks;
    switch (actor.subState) {
    case 0:
        if (stepTick() == 0x14) {
            cast.stepping = 1;
            t.first.select(4, 0);
            actor.subState = 1;
        }
        return;
    case 1:
        if (scene() == 2 && stepTick() == 1) {
            actor.pos.x.whole = 0x200;
            actor.pos.z.whole = 0xe0;
            actor.rotation.y = 0x300;
            actor.subState = 2;
        }
        return;
    case 2:
        if (scene() != 3) return;
        switch (stepTick()) {
        case 1:
            actor.anim = 9;
            t.first.select(5, 0);
            t.second.select(4, 0);
            cast.stepping = 1;
            return;
        case 5: t.second.select(5, 0); return;
        case 0x24: t.second.select(6, 0); return;
        default: return;
        }
    default: return;
    }
}
void faceRowByVariant(AnimatedActor& actor, FaceRowCastFields& cast) { // exe 0x478c40: [0x890f20]
    switch (actor.variant) {
    case 0: faceRowFirst(actor, cast); return;
    case 1: faceRowSecond(cast); return;
    case 2: faceRowThird(actor, cast); return;
    default: notPortedAt(kStoryPort, "0x478c40 variant (table 0x890f20)", actor.variant);
    }
}
void faceRowCast(AnimatedActor& actor) { // exe 0x478a90
    FaceRowCastFields& cast = classState<FaceRowCastFields>(actor);
    castHead(actor);
    switch (CastState(actor.state)) { // [0x890f14]
    case CastState::Bind: {                 // exe 0x478b60
        static constexpr uint8_t kAnims[3] = {1, 7, 8};
        if (!castInit(actor, kAnims)) break;
        cast.word = 0;
        cast.stepping = 0;
        cast.clearRows();
        cast.tracks.init(stateAddress(TableId::St4bFaceRowFirstTracks), stateAddress(TableId::St4bFaceRowSecondTracks)); // exe 0x5e33a0
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        actor.enterStateBytes(uint8_t(CastState::Run));
        actor.restartStep();
        faceRowByVariant(actor, cast);
        break;
    }
    case CastState::Run: faceRowByVariant(actor, cast); break;
    case CastState::Free: freeActor(actor); break; // exe 0x468fa0
    case CastState::First: faceRowFirst(actor, cast); break;
    case CastState::Second: faceRowSecond(cast); break;
    case CastState::Third: faceRowThird(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x478a90 state (table 0x890f14)", actor.state);
    }
    castTail(actor, cast.tracks, cast.stepping != 0);
}

void walkInFirst(AnimatedActor& actor) { // exe 0x44b790
    switch (actor.subState) {
    case 0:
        if (stepTick() == 0x44) actor.subState = 1;
        return;
    case 1:
        walkForward(actor, -0x3e);
        if (stepTick() == 0x83) {
            actor.anim = 2;
            actor.subState = 2;
        }
        return;
    case 2:
        walkForward(actor, -0x3e);
        if (stepTick() == 0x8b) actor.subState = 3;
        return;
    case 3:
        if (scene() == 1 && stepTick() == 1) {
            actor.pos.x.whole = int16_t(0xffc0);
            actor.pos.z.whole = 0;
            actor.rotation.y = 0xc00;
            actor.anim = 1;
            actor.subState = 4;
        }
        return;
    case 4:
        walkForward(actor, -0x3e);
        if (stepTick() == 0x2f) {
            actor.anim = 2;
            actor.subState = 5;
        }
        return;
    case 5:
        walkForward(actor, -0x3e);
        if (stepTick() == 0x37) actor.subState = 6;
        return;
    case 6:
        if (stepTick() == 0x1ac) {
            actor.anim = 3;
            actor.subState = 7;
        }
        return;
    default: return;
    }
}
void walkInThird(AnimatedActor& actor, WalkInCastFields& cast) { // exe 0x44b8f0
    if (actor.subState == 0) {
        cast.stepping = 0;
        actor.subState = 1;
    } else if (actor.subState == 1 && stepTick() == 0x14) {
        cast.tracks.first.select(1, 0);
        cast.tracks.second.select(1, 0);
        cast.stepping = 1;
        actor.subState = 2;
    }
}
void walkInFourth(AnimatedActor& actor, WalkInCastFields& cast) { // exe 0x44ba00 (its own state: bind, run)
    switch (actor.state) {
    case 0:
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.collisionBox.pointAt(TableId::St39Script68CastBox);
        actor.clearClassBlock(); // exe 0x40dc40
        actor.enterState(1);
        actor.anim = 0;
        actor.prevAnim = kNoAnimShown;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.acceleration.z = 0;
        actor.acceleration.y = 0;
        actor.acceleration.x = 0;
        cast.animKey = 0;
        cast.tracks.init(stateAddress(TableId::St39Script68CastEyes), stateAddress(TableId::St39Script68CastMouth));
        textureTrackStop(actor, cast.tracks, FaceTrack::Eyes, 0);  // exe 0x5e3630
        textureTrackStop(actor, cast.tracks, FaceTrack::Mouth, 0); // exe 0x5e3660
        [[fallthrough]];
    case 1:
        if (scriptState().part<Script59Cue>().cue == 0x81) {
            actor.anim = 1;
            cast.animKey = 0;
        }
        return;
    default: return;
    }
}
void walkInByVariant(AnimatedActor& actor, WalkInCastFields& cast) { // exe 0x44b770: [0x88e65c]
    switch (actor.variant) {
    case 0: walkInFirst(actor); return;
    case 1: return; // exe 0x52ef00
    case 2: walkInThird(actor, cast); return;
    case 3: walkInFourth(actor, cast); return;
    default: notPortedAt(kStoryPort, "0x44b770 variant (table 0x88e65c)", actor.variant);
    }
}
void walkInCast(AnimatedActor& actor) { // exe 0x44b5c0
    WalkInCastFields& cast = classState<WalkInCastFields>(actor);
    castHead(actor);
    switch (CastState(actor.state)) { // [0x88e650]
    case CastState::Bind: {                 // exe 0x44b690
        static constexpr uint8_t kAnims[3] = {1, 3, 4};
        if (!castInit(actor, kAnims)) break;
        cast.word = 0;
        cast.stepping = 1;
        cast.tracks.init(stateAddress(TableId::St4bWalkInFirstTracks), stateAddress(TableId::St4bWalkInSecondTracks));
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        actor.enterStateBytes(uint8_t(CastState::Run));
        actor.restartStep();
        walkInByVariant(actor, cast);
        break;
    }
    case CastState::Run: walkInByVariant(actor, cast); break;
    case CastState::Free: freeActor(actor); break; // exe 0x468fa0
    case CastState::First: walkInFirst(actor); break;
    case CastState::Second: break; // exe 0x52ef00
    case CastState::Third: walkInThird(actor, cast); break;
    case CastState::Fourth: walkInFourth(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x44b5c0 state (table 0x88e650)", actor.state);
    }
    castTail(actor, cast.tracks, cast.stepping != 0);
}

// ---- the boss (exe 0x4b8d90, pool 0x20 type 0x3f sub 1) ------------------------------------------------------------

enum class BossState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class BossMove : uint8_t { Hover = 0, Bank = 1, Dive = 2, Fall = 3, Grounded = 4, Death = 5 };
enum class DiveStep : uint8_t { Level = 0, Cry = 1, Plunge = 2, Climb = 3, Return = 4 };
enum class FallStep : uint8_t { Fall = 0, Land = 1 };
enum class GroundStep : uint8_t { Watch = 0, Recoil = 1, Bite = 2, Shoot = 3, Rest = 4 };
enum class DeathStep : uint8_t { Cry = 0, Collapse = 1, Smoke = 2 };
enum class DropStep : uint8_t { Start = 0, Drops = 1, Fade = 2 };
enum class VolleyStep : uint8_t { Start = 0, Fire = 1, End = 2 };

// its flags
constexpr uint32_t kBossDead = 0x1, kBossHoldPose = 0x2, kBossNear = 0x4, kBossBankRight = 0x8, kBossCried = 0x10,
                   kBossKeepNear = 0x20, kBossEyesOut = 0x40, kBossEyesClosing = 0x80, kBossRolled = 0x100,
                   kBossMoveChosen = 0x200, kBossDropAttack = 0x400, kBossVolley = 0x800, kBossHalfLife = 0x1000,
                   kBossQuarterLife = 0x2000, kBossFlinch = 0x4000, kBossFalling = 0x8000, kBossGrounded = 0x10000,
                   kBossHitNow = 0x20000, kBossBiting = 0x40000, kBossVolleyAgain = 0x80000,
                   kBossLevelCry = 0x100000, kBossCryChosen = 0x200000, kTauntDive = 0x400000,
                   kTauntDrop = 0x800000, kTauntVolley = 0x1000000, kTauntsFirstUsed = 0x2000000,
                   kTauntOpened = 0x4000000;
constexpr uint32_t kTauntBank = 0x73c000; // s16 at stride 4
constexpr int8_t kTauntWindow = 4;
constexpr uint32_t kFlagBossDefeated = 0x681, kFlagHalfLifeSeen = 0x680;
constexpr uint32_t kBossTouch = kHitAttackThrown | kHitAttackSolid;     // its body's attack kinds
constexpr uint32_t kBossLimbTouch = kHitAttackHarmless | kBossTouch;    // its limbs'
constexpr uint32_t kBossEntry = hitGroup(kHitGroupBodies) | kHitNoLockOn;
constexpr uint8_t kEyeTrailType = 0x52, kVolleyShotType = 0x30;
constexpr uint32_t kEyeAOffset = 8, kEyeBOffset = 0x10; // the eye points in the work block
constexpr size_t kHeadEntry = 2, kLeftWingEntry = 5, kRightWingEntry = 6; // decoded frame entries it bends


// exe 0x4ba080(angle, target, step): the s16 angle moved by step toward the target (snapped within it), folded
void turn(int16_t& angle, int target, int step) {
    const int16_t tg = t16(target), st = t16(step);
    const int16_t v = angle;
    const int16_t d = fold(t16(v - tg));
    if (d >= st) angle = int16_t(uint16_t(v - st));
    else if (int(d) <= -int(st)) angle = int16_t(uint16_t(v + st));
    else angle = tg;
    if (angle >= 0x800) angle -= 0x1000u;
    if (angle < -0x800) angle += 0x1000u;
}
void turnYaw(AnimatedActor& actor, int t, int s) { turn(actor.rotation.y, t, s); }   // exe 0x4ba020
void turnPitch(AnimatedActor& actor, int t, int s) { turn(actor.rotation.x, t, s); } // exe 0x4ba040
void turnRoll(AnimatedActor& actor, int t, int s) { turn(actor.rotation.z, t, s); }  // exe 0x4ba060
int sideAngle(const BossFields& boss) { return (boss.flags & kBossBankRight) ? 0x400 : -0x400; }
int yawGap(const AnimatedActor& actor, const BossFields& boss) { // |fold(yaw - the target's)|
    const int16_t d = fold(t16(actor.rotation.y - boss.targetYaw));
    return d < 0 ? -d : d;
}

// exe 0x4bd030(boss, spawn byte, variant): a trail (the stage's spark emitter, game/spark_emitter) at the origin on the
// boss's bone `spawn byte`
void spawnTrail(AnimatedActor& actor, uint8_t spawnByte, uint8_t variant) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& trail = staticActorAt(slot);
    trail.pos.x.whole = 0;
    trail.variant = variant;
    trail.spawnByte = spawnByte;
    trail.pos.y.whole = 0;
    trail.pos.z.whole = 0;
    trail.flags = kActorLive;
    trail.type = kSparkEmitterStageType;
    trail.loopFlags |= kActorSweepGroup2;
    trail.setParamsWord(0xa0000);
    classState<SparkEmitterParams>(staticActorAt(slot)).parent = &actor;
}
// exe 0x4baaa0: a blast (global static type 7, variant 0) at the boss, size 0x200
void blastAtBoss(const AnimatedActor& actor) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& b = staticActorAt(slot);
    BlastParams params(actorHeaderAt(slot));
    b.loopFlags |= kActorSweepGroup2;
    b.flags = kActorLive;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = 0;
    b.pos.x.whole = actor.pos.x.whole;
    b.pos.y.whole = actor.pos.y.whole;
    b.pos.z.whole = actor.pos.z.whole;
    params.size = 0x200;
    params.attack = 0;
}
// exe 0x4baaf0: a small blast at a random point around the boss (x / z +-0x80, y -0x140 .. -0x41)
void smokeBlast(const AnimatedActor& actor) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    const uint32_t r1 = rngStep();
    const uint16_t x = uint16_t((r1 & 0xff) + uint16_t(actor.pos.x.whole) - 0x80);
    const uint32_t r2 = rngStep();
    const uint16_t y = uint16_t(((r2 >> 8) & 0xff) + uint16_t(actor.pos.y.whole) - 0x140);
    const uint32_t r3 = rngStep();
    const uint16_t z = uint16_t(((r3 >> 16) & 0xff) + uint16_t(actor.pos.z.whole) - 0x80);
    StaticActor& b = staticActorAt(slot);
    BlastParams params(actorHeaderAt(slot));
    b.pos.x.whole = int16_t(x);
    b.loopFlags |= kActorSweepGroup2;
    b.flags = kActorLive;
    b.type = typeByte(GlobalStaticType::Blast);
    b.variant = 0;
    b.pos.y.whole = int16_t(y);
    b.pos.z.whole = int16_t(z);
    params.size = 0x40;
    params.attack = 0;
}
// exe 0x4bad40: the two eye trails (stage static type 0x52), each on an eye point of the work block
void spawnEyeTrails(const BossFields& boss) {
    const auto& work = boss.work();
    const struct {
        const ShortVec4* point;
        uint32_t word; // its address in the work block (the exe's spawn parameter)
    } eyes[2] = {{&work.eyeA, boss.workBlock + kEyeAOffset}, {&work.eyeB, boss.workBlock + kEyeBOffset}};
    for (const auto& point : eyes) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!slot) continue;
        StaticActor& eye = staticActorAt(slot);
        eye.flags = kActorLive;
        eye.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        eye.type = kEyeTrailType;
        eye.variant = 0;
        eye.setParamsWord(point.word);
        classState<st4b_fx_state::EyeTrailFields>(eye).eye = point.point;
    }
}
// exe 0x4bac50(boss, k): at the work block's drop point k: a light column (variant 0x10), a blast (variant 0x20) and
// a shockwave (stage projectile type 3, variant 0xe)
void dropAt(const BossFields& boss, int k) {
    const ShortVec3& p = boss.work().drops[size_t(k)];
    if (const uint32_t slot = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        StaticActor& column = staticActorAt(slot);
        LightColumnParams params(staticActorAt(slot));
        column.flags = kActorLive;
        column.loopFlags |= kActorSweepGroup2;
        column.type = kLightColumnStageType;
        column.variant = 0x10;
        column.pos.x.whole = p.x;
        column.pos.y.whole = p.y;
        column.pos.z.whole = p.z;
        params.radius = 0x180;
        params.heightSteps = 0x60;
        params.damage = boss.row().shotAttack;
    }
    if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
        StaticActor& b = staticActorAt(slot);
        BlastParams params(actorHeaderAt(slot));
        b.flags = kActorLive;
        b.loopFlags |= kActorSweepGroup2;
        b.type = typeByte(GlobalStaticType::Blast);
        b.variant = 0x20;
        b.pos.x.whole = p.x;
        b.pos.y.whole = p.y;
        b.pos.z.whole = p.z;
        params.size = 0xc0;
        params.attack = boss.row().shotAttack;
    }
    if (const uint32_t slot = allocActor40(kActorStageClass)) { // exe 0x5e1280
        ProjectileActor& shock = projectileActorAt(slot);
        ShockwaveParams params(projectileActorAt(slot));
        shock.flags = kActorLive;
        shock.loopFlags |= kActorSweepGroup2;
        shock.type = kShockwaveStageType;
        shock.variant = 0xe;
        params.hitMode = 0;
        params.downward = 0;
        shock.pos.x.whole = p.x;
        shock.pos.y.whole = p.y;
        shock.pos.z.whole = p.z;
    }
}
// exe 0x4ba880: a breath puff (stage projectile type 5, variant 1) from bone 2 ((0, 0x20, -0x80) through it), aimed
// along bone 1 -> bone 2's yaw, pitch 0x100, speed -0x200
void spitPuff(const AnimatedActor& actor, const BossFields& boss) {
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    int16_t p1[3], p2[3], out[3];
    bonePt(actor, 1, p1);
    bonePt(actor, 2, p2);
    boneXform(actor, 2, 0, 0x20, -0x80, out);
    ProjectileActor& puff = projectileActorAt(slot);
    BreathPuffParams params(projectileActorAt(slot));
    puff.flags = kActorLive;
    puff.loopFlags |= kActorSweepGroup2;
    puff.type = kBreathPuffType;
    puff.variant = 1;
    puff.pos.x.whole = out[0];
    puff.pos.y.whole = out[1];
    puff.pos.z.whole = out[2];
    puff.rotation.x = 0x100;
    puff.rotation.y = int16_t(angleOf(p2, p1)); // exe 0x5e3c00(bone 2, bone 1)
    puff.rotation.z = 0;
    params.damage = boss.row().hitSize;
    params.frame = 1;
    params.life = 0x28;
    puff.velocity.z = int16_t(0xfe00);
    params.spin = 7;
    params.size = 0x20;
}
// The volley shot's spawn parameters: its attack and a word
struct VolleyShotParams {
    explicit VolleyShotParams(const ProjectileActor& actor) : VolleyShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit VolleyShotParams(ProjectileActor& actor) : attack(actor.params, 0), word(actor.params, 2) {}
    BytesField<uint16_t, 4> attack;
    BytesField<uint16_t, 4> word;
};
// exe 0x4ba950(boss, side): a volley shot (pool 0x40 type 0x30) from bone 4 / 5 (side 0 / 1), its yaw spread by the
// bend and a random 0..0x3f, speed 0x1400
void volleyShot(const AnimatedActor& actor, const BossFields& boss, uint32_t side) {
    const auto& work = boss.work();
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) return;
    const uint32_t r = rngStep();
    int16_t off = t16(0x60 - (work.bend >> 3) - int(r & 0x3f));
    int16_t out[3];
    if (side == 0) {
        boneXform(actor, 4, -0xc0, 0x18, -0xc0, out);
    } else {
        boneXform(actor, 5, 0xc0, 0x18, -0xc0, out);
        off = t16(-off);
    }
    ProjectileActor& shot = projectileActorAt(slot);
    VolleyShotParams params(projectileActorAt(slot));
    shot.flags = kActorLive;
    shot.loopFlags |= kActorSweepGroup2;
    shot.type = kVolleyShotType;
    shot.variant = 0;
    shot.pos.x.whole = out[0];
    shot.pos.y.whole = out[1];
    shot.pos.z.whole = out[2];
    const uint32_t r2 = rngStep();
    shot.rotation.x = int16_t(((r2 >> 16) & 0x1f) + uint16_t(boss.aimPitch) - 0x18);
    shot.rotation.z = 0;
    shot.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint16_t(off));
    shot.velocity.x = 0;
    shot.velocity.y = 0;
    shot.velocity.z = 0x1400;
    params.attack = boss.row().strongAttack;
    params.word = 0;
}
bool tauntWindowFree() { return windowStatus(uint8_t(kTauntWindow)) == 0; } // exe 0x557810(4)
void openTaunt(int16_t id) { windowOpen(kTauntWindow, kTauntBank, id); }   // exe 0x557390
// exe 0x4bada0: a taunt window: the first time message 1; then a random one of 0..3 not used yet (all used ->
// kTauntsFirstUsed), then of 4..7 (all used -> both sets cleared)
void taunt(BossFields& boss) {
    auto& work = boss.work();
    uint32_t i;
    if (!(boss.flags & kTauntOpened)) {
        boss.flags |= kTauntOpened;
        work.tauntsUsedLow |= 2u;
        i = 1;
    } else {
        i = rngStep() & 3;
        if (!(boss.flags & kTauntsFirstUsed)) {
            while (int32_t(work.tauntsUsed) & (1 << i)) i = (i + 1) & 3;
            work.tauntsUsed |= (1u << i);
            if ((work.tauntsUsedLow & 0xf) == 0xf) boss.flags |= kTauntsFirstUsed;
        } else {
            while (int32_t(work.tauntsUsed) & (1 << (i + 4))) i = (i + 1) & 3;
            i += 4;
            work.tauntsUsed |= (1u << i);
            if ((work.tauntsUsedLow & 0xf0) == 0xf0) {
                boss.flags &= ~kTauntsFirstUsed;
                work.tauntsUsed = 0;
            }
        }
    }
    openTaunt(dataScalar<int16_t>(stateAddress(TableId::St4bTauntMessages) + i * 4));
}
// exe 0x4ba100: the head's yaw follows the target by 0x10 (within +-0x280)
void headFollow(const AnimatedActor& actor, BossFields& boss) {
    const int16_t dx = boss.headYaw;
    const int16_t ax = fold(t16(actor.rotation.y - boss.targetYaw + dx));
    if (ax >= 0x10) {
        if (dx >= -0x280) boss.headYaw = int16_t(dx - 0x10);
    } else if (ax <= -0x10) {
        if (dx <= 0x280) boss.headYaw = int16_t(dx + 0x10);
    }
}
// exe 0x4ba160: farther than lim from the player -> moved onto that distance (the position interpolated with the
// player's)
void keepDistance(AnimatedActor& actor, BossFields& boss, int lim) {
    const int d = boss.targetDistance;
    if (d == 0) notPorted(kStoryPort, "0x4ba160: distance 0 (the exe divides by zero)");
    const int32_t k1 = int32_t(uint32_t(d - lim) << 8) / d;
    const int32_t k0 = int32_t(uint32_t(lim) << 8) / d;
    Fixed16* const own[3] = {&actor.pos.x, &actor.pos.y, &actor.pos.z};
    const Fixed16* const target[3] = {&player().pos.x, &player().pos.y, &player().pos.z};
    for (int i = 0; i < 3; ++i) {
        const int32_t p = int32_t(*own[i]) >> 8, q = int32_t(*target[i]) >> 8;
        *own[i] = int32_t(uint32_t(p) * uint32_t(k0) + uint32_t(q) * uint32_t(k1));
    }
    boss.targetDistance = int16_t(lim);
}
void distanceClamp(AnimatedActor& actor, BossFields& boss) {
    if (boss.targetDistance >= 0x3200) keepDistance(actor, boss, 0x3200);
    if ((boss.flagsLow & kBossKeepNear) && boss.targetDistance <= 0x3138) keepDistance(actor, boss, 0x3138);
}
// exe 0x4ba270: the eye points (bones 8 / 0xb at (-+0x100, 0, 0)) into the work block; kBossEyesClosing alone ->
// cleared and the eye timer 0x1e; else (kBossEyesOut alone -> kBossEyesClosing set) the eyes' flags cleared
void eyes(const AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    int16_t o[3];
    // exe 0x5e1810(actor, 8) / (actor, 0xb): the results are not used
    boneXform(actor, 8, -0x100, 0, 0, o);
    work.eyeA.x = o[0];
    work.eyeA.y = o[1];
    work.eyeA.z = o[2];
    boneXform(actor, 0xb, 0x100, 0, 0, o);
    work.eyeB.x = o[0];
    work.eyeB.y = o[1];
    work.eyeB.z = o[2];
    const uint32_t f = boss.flags, c = f & (kBossEyesOut | kBossEyesClosing);
    if (c == kBossEyesClosing) {
        boss.eyeTimer = 0x1e;
        boss.flags = f & ~kBossEyesClosing;
        return;
    }
    if (c == kBossEyesOut) boss.flags = f | kBossEyesClosing;
    work.eyeAClosed() = 0;
    work.eyeBClosed() = 0;
}
// exe 0x4ba330: the damage of its box (a hit of the player's: the damage, the flash; else the pending damage, 0x20 a
// tick) -> its hit points (x 10; dead -> kBossDead), half life -> kBossHalfLife + a blast, quarter -> kBossQuarterLife
// + sound 0x2b3; the damage taken adds up (past 0x80: kBossFlinch for one tick)
void takeDamage(AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    const int16_t hp0 = actor.hitPoints;
    const uint32_t h = work.hitBox.result;
    int n = 0;
    bool hitNow = false;
    if (h & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        n = int(h & kHitDamageMask);
        hitNow = n != 0;
    }
    if (hitNow) {
        boss.flags |= kBossHitNow;
        if (n > 0x20) n = 0x20;
        work.pendingDamage = 0;
    } else {
        boss.flags &= ~kBossHitNow;
        const int16_t p = work.pendingDamage;
        if (p > 0x20) {
            work.pendingDamage = int16_t(p - 0x20);
            n = 0x20;
        } else {
            n = p;
            work.pendingDamage = 0;
        }
    }
    const uint32_t f = boss.flags;
    const bool skip = ((f & kBossQuarterLife) && !(f & kBossGrounded)) || (f & (kBossHalfLife | kBossDead)) || n == 0;
    if (!skip) {
        if (hitPointsAdd(actor, -(n * 10)) != 0) { // exe 0x5e52f0
            boss.flags |= kBossDead;
        } else {
            const int16_t half = int16_t(actor.maxHitPoints >> 1);
            if (actor.hitPoints <= half && hp0 > half) {
                boss.flags |= kBossHalfLife;
                blastAtBoss(actor);
            }
            const int16_t q = int16_t(actor.maxHitPoints >> 2);
            if (actor.hitPoints <= q && hp0 > q) {
                sound(0x2b3);
                boss.flags |= kBossQuarterLife;
            }
        }
    }
    work.damageTaken = int16_t(uint16_t(uint16_t(work.damageTaken) + uint16_t(n)));
    if (work.damageTaken > 0x80) {
        boss.flags |= kBossFlinch;
        work.damageTaken = 0;
    } else {
        boss.flags &= ~kBossFlinch;
    }
}
// exe 0x4ba490: its hit entries (its box = the work block's, cleared, its previous x / z): bone points 0 / 6 / 7 / 9 /
// 0xa, the capsules of bones 2 / 8 / 0xb / 0xc / 0xd / 0xe / 0xf; bone 0xf's point again while biting
void hitEntries(const AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    const uint32_t owner = hitOwner(actor.slot);
    work.hitBox.result = 0;
    work.hitBox.previousX = actor.prevPos.x.whole;
    work.hitBox.previousZ = actor.prevPos.z.whole;
    const uint32_t attack = uint32_t(boss.row().attack);
    int16_t p[3], q[3];
    bonePt(actor, 0, p);
    hitListSubmit(p, work.hitBox, owner | hitGroup(kHitGroupBodies) | hitRadius(0x50), attack | kBossTouch);
    bonePt(actor, 2, p);
    bonePointAhead(actor, 2, 0xc0, q); // exe 0x5e18d0
    hitListSubmitCapsule(p, 0xc, q, 6, work.hitBox, owner | kBossEntry | hitCapsule(1),
                         uint32_t(boss.row().attack) | kBossLimbTouch);
    bonePt(actor, 6, p);
    hitListSubmit(p, work.hitBox, owner | kBossEntry | hitRadius(0x40), uint32_t(boss.row().attack) | kBossLimbTouch);
    bonePt(actor, 7, p);
    hitListSubmit(p, work.hitBox, owner | kBossEntry | hitRadius(0x40), uint32_t(boss.row().attack) | kBossLimbTouch);
    struct Cap {
        uint32_t bone;
        int16_t v[3];
        uint16_t ra, rb;
        uint32_t shape;
    };
    auto capsule = [&](const Cap& c) {
        int16_t pa[3], pb[3];
        bonePt(actor, c.bone, pa);
        boneXform(actor, c.bone, c.v[0], c.v[1], c.v[2], pb); // exe 0x5e1970
        hitListSubmitCapsule(pa, c.ra, pb, c.rb, work.hitBox, owner | c.shape,
                             uint32_t(boss.row().attack) | kBossLimbTouch);
    };
    capsule({8, {-0xa0, 0, 0}, 0x10, 0x10, kBossEntry});
    bonePt(actor, 9, p);
    hitListSubmit(p, work.hitBox, owner | kBossEntry | hitRadius(0x40), uint32_t(boss.row().attack) | kBossLimbTouch);
    bonePt(actor, 0xa, p);
    hitListSubmit(p, work.hitBox, owner | kBossEntry | hitRadius(0x40), uint32_t(boss.row().attack) | kBossLimbTouch);
    capsule({0xb, {0xa0, 0, 0}, 0x10, 0x10, kBossEntry});
    capsule({0xc, {0, 0, 0xc0}, 0x10, 0xe, kBossEntry | hitCapsule(1)});
    capsule({0xd, {0, 0, 0xc0}, 0xe, 0xc, kBossEntry | hitCapsule(1)});
    capsule({0xe, {0, 0, 0xc0}, 0xc, 0xa, kBossEntry | hitCapsule(1)});
    capsule({0xf, {0, 0, 0x180}, 0xa, 6, kBossEntry | hitCapsule(2)});
    if (boss.flags & kBossBiting) {
        bonePt(actor, 0xf, p);
        hitListSubmit(p, work.hitBox, owner | kBossEntry | hitRadius(0x180),
                      uint32_t(boss.row().bodyAttack) | kBossLimbTouch);
    }
}
// exe 0x4babc0 / 0x4bac00: every 10 ticks two trails (variant 0x16, spawn bytes 0 / 0xe), or one variant 0x19 (a
// random spawn byte 0..0xc)
void trailsLowLife(AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    work.timer -= 1u;
    if (work.timer != 0) return;
    spawnTrail(actor, 0, 0x16);
    spawnTrail(actor, 0xe, 0x16);
    work.timer = 0xa;
}
void trailsGrounded(AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    work.timer -= 1u;
    if (int16_t(work.timer) != 0) return;
    const uint32_t r = rngStep();
    spawnTrail(actor, uint8_t((r & 0xf) % 0xd), 0x19);
    work.timer = 0xa;
}

// exe 0x4b9e10 (kBossDropAttack): the drop attack -- sounds, the tint's red / green ramps, 5 points ahead along the
// target's yaw at tick 0x10, then every 4 ticks a drop at the next point
void dropAttack(AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    switch (DropStep(actor.step)) {
    case DropStep::Start:
        sound(0x2a8);
        sound(0x2af);
        boss.timer = 0x32;
        boss.counter = 0;
        ++actor.step;
        return;
    case DropStep::Drops:
        boss.timer -= 1u;
        if (boss.tintR < 0x1f) boss.tintR += 1u;
        if (boss.tintG > 8) boss.tintG -= 1u;
        if (boss.timer == 0x10) {
            for (int k = 0; k < 5; ++k) {
                ShortVec4& p = work.drops[size_t(k)];
                p.x = actor.pos.x.whole;
                p.y = int16_t(0xfd30);
                p.z = actor.pos.z.whole;
                moveAlongYaw16(p, boss.targetYaw, 0, 0, int32_t(uint32_t(-8 - k) << 13)); // exe 0x5e3750
            }
        }
        if (boss.timer <= 0x10 && (uint8_t(boss.timer) & 3) == 0) {
            sound(0x2a9);
            dropAt(boss, boss.counter);
            boss.counter += 1u;
            if (boss.timer == 0) ++actor.step;
        }
        return;
    case DropStep::Fade:
        if (boss.tintG < 0x10) boss.tintG += 1u;
        if (boss.tintR > 0x10) {
            boss.tintR -= 1u;
            return;
        }
        boss.flags &= ~kBossDropAttack;
        return;
    default: return;
    }
}
// exe 0x4b9f50 (kBossVolley): the shot volley -- the bends up, then 0x14 shots from alternating sides with sound
// 0x2aa, the bends back
void volley(AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    switch (VolleyStep(actor.step)) {
    case VolleyStep::Start:
        boss.timer = 0x3c;
        ++actor.step;
        return;
    case VolleyStep::Fire: {
        boss.timer -= 1u;
        if (boss.timer >= 0x34) {
            boss.neckBend += 0x40u;
            work.bend += 0x40u;
        }
        int16_t t = boss.timer;
        if (t > 0x20) return;
        if (t > 0xc) {
            if (!(t & 1)) sound(0x2aa);
            volleyShot(actor, boss, uint8_t(boss.timer) & 1);
        }
        t = boss.timer;
        if (t < 0x10) boss.neckBend -= 0x20u;
        if (t != 0) {
            work.bend -= 0x10u;
            return;
        }
        ++actor.step;
        return;
    }
    case VolleyStep::End: boss.flags &= ~kBossVolley; return;
    default: return;
    }
}
// the taunts of the first fight (window 4 free): the message once, then again by chance (rng & mask)
void tauntOnce(const AnimatedActor& actor, BossFields& boss, uint32_t bit, uint32_t mask, uint16_t id) {
    if (actor.spawnByte != 0 || !tauntWindowFree()) return;
    if (boss.flags & bit) {
        if (rngStep() & mask) return;
    }
    boss.flags |= bit;
    openTaunt(int16_t(id));
}
// exe 0x4b91c0: hovering, turning to the target; far: a random choice of the next move; near: kBossNear (rise and cry)
void hover(AnimatedActor& actor, BossFields& boss) {
    auto& work = boss.work();
    if (!(boss.flagsLow & kBossNear)) {
        turnRoll(actor, 0, 0x20);
        turnPitch(actor, boss.aimPitch, 0x80);
        if (boss.targetDistance > 0x1000) turnYaw(actor, boss.targetYaw, 8);
        if (actor.rotation.z == 0 && !(boss.flags & kBossLevelCry)) {
            boss.flags |= kBossLevelCry;
            sound(0x2a6);
        }
        const uint32_t f = boss.flags;
        if (!(f & kBossMoveChosen)) {
            if (f & kBossQuarterLife) {
                boss.flags = f | kBossFalling;
                actor.restartStep();
                actor.velocity.z = 0x800;
                actor.anim = 5;
                actor.subState = uint8_t(BossMove::Fall);
                return;
            }
            switch (bossMovePick(rngStep() & 0xf)) {
            case 0: // exe 0x4b92ad: the dive
                boss.flags &= ~kBossVolleyAgain;
                actor.restartStep();
                actor.subState = uint8_t(BossMove::Dive);
                return;
            case 1: // exe 0x4b92c9: the drop attack
                boss.flags = (boss.flags & ~kBossVolleyAgain) | kBossMoveChosen | kBossDropAttack;
                actor.restartStep();
                tauntOnce(actor, boss, kTauntDrop, 1, 0x10);
                break;
            case 2: // exe 0x4b9343: the volley
                boss.flags = (boss.flags & ~kBossVolleyAgain) | kBossMoveChosen | kBossVolley;
                actor.restartStep();
                tauntOnce(actor, boss, kTauntVolley, 1, 0x14);
                break;
            default: // exe 0x4b93ad: the volley, the second time in a row
                if (!(boss.flags & kBossVolleyAgain)) {
                    boss.flags |= kBossVolleyAgain | kBossMoveChosen;
                    break;
                }
                boss.flags = (boss.flags & ~kBossVolleyAgain) | kBossMoveChosen | kBossVolley;
                actor.restartStep();
                tauntOnce(actor, boss, kTauntVolley, 3, 0x14);
                break;
            }
        }
        if (boss.flags & kBossFlinch) {
            actor.anim = 5;
            actor.prevAnim = 0;
        }
        if (boss.targetDistance < 0x400) boss.flags = (boss.flags & ~(kBossLevelCry | kBossMoveChosen)) | kBossNear;
    } else { // exe 0x4b9478
        if (actor.rotation.x > -0x10) turnPitch(actor, -0x10, 0x28);
        else turnPitch(actor, -0x30, 1);
        if (!(boss.flags & kBossCried) && boss.targetDistance > 0x800) {
            boss.flags |= kBossCried;
            actor.anim = 1;
            actor.prevAnim = 0;
        }
        uint32_t f = boss.flags;
        if (!(f & (kBossLevelCry | kBossQuarterLife | kBossHalfLife)) && boss.targetDistance > 0x1000) {
            f |= kBossLevelCry;
            boss.flags = f;
            bool clear = true;
            if (!(f & kBossCryChosen)) {
                boss.flags = f | kBossCryChosen;
                uint16_t k = uint16_t(rngStep() & 3);
                if (work.lastCry == k) k = (k + 1) & 3;
                work.lastCry = k;
                switch (k) {
                case 0: sound(0x2ac); clear = (rngStep() & 8) != 0; break;
                case 1: sound(0x2ad); clear = (rngStep() & 8) != 0; break;
                case 2: sound(0x2ae); clear = (rngStep() & 8) != 0; break;
                default: break;
                }
            }
            if (clear) boss.flags &= ~kBossCryChosen;
        }
        const int16_t d = boss.targetDistance;
        if (d > 0x2000) {
            turnRoll(actor, sideAngle(boss), 0x40);
            boss.flags &= ~(kBossLevelCry | kBossRolled | kBossCried | kBossNear);
            const uint32_t r = rngStep();
            boss.pitchGoal = int16_t(0xfffffff0u - (r & 0x1f));
            actor.spin.x = int16_t((boss.flags & kBossBankRight) ? 0xffe8 : 0x18);
            actor.velocity.z = 0x1000;
            actor.subState = uint8_t(BossMove::Bank);
            if (actor.spawnByte == 0 && tauntWindowFree()) taunt(boss);
        } else if (d > 0x1a00) {
            if ((boss.flags & kBossHalfLife) && player().hitPoints >= 0) setStoryFlag(kFlagHalfLifeSeen);
            if (!(boss.flags & kBossRolled)) {
                boss.flags |= kBossRolled;
                if (rngStep() & 1) {
                    boss.flags |= kBossBankRight;
                    actor.anim = 3;
                } else {
                    actor.anim = 2;
                }
                actor.prevAnim = 0;
            } else {
                turnRoll(actor, sideAngle(boss), 0x40);
            }
        } else if (boss.flags & kBossFlinch) {
            actor.anim = 5;
            actor.prevAnim = 0;
        }
    }
    if (boss.flags & kBossDropAttack) dropAttack(actor, boss); // exe 0x4b9e10
    if (boss.flags & kBossVolley) volley(actor, boss);         // exe 0x4b9f50
}
// exe 0x4b9780: the banked turn (roll +-0x400, pitch to its goal) until facing the target; the eye trails once
void bank(AnimatedActor& actor, BossFields& boss) {
    const int t = sideAngle(boss);
    turnRoll(actor, t, 0x40);
    turnPitch(actor, boss.pitchGoal, 4);
    if (!(boss.flagsLow & (kBossEyesOut | kBossEyesClosing)) && actor.rotation.z == t16(t)) {
        spawnEyeTrails(boss);
        boss.flags |= kBossEyesOut;
    }
    if (!(boss.flags & kBossHoldPose) && actor.keyIndex == 0xe) boss.flags |= kBossHoldPose | kBossKeepNear;
    if (yawGap(actor, boss) < 0x40) {
        boss.flags &= ~(kBossHoldPose | kBossBankRight | kBossKeepNear | kBossEyesOut);
        actor.velocity.z = 0xc00;
        actor.spin.x = 0;
        actor.subState = uint8_t(BossMove::Hover);
    }
}
// exe 0x4b9850: the dive -- level out, cry, plunge (its rise / fall speeds), pull up, climb back
void dive(AnimatedActor& actor, BossFields& boss) {
    if (actor.step <= uint8_t(DiveStep::Climb)) turnYaw(actor, boss.targetYaw, 8);
    switch (DiveStep(actor.step)) {
    case DiveStep::Level:
        turnRoll(actor, 0, 0x20);
        if (actor.rotation.z != 0) {
            turnPitch(actor, boss.aimPitch, 0x80);
            return;
        }
        sound(0x2b0);
        tauntOnce(actor, boss, kTauntDive, 1, 0xa);
        actor.velocity.z = 0;
        actor.anim = 0xd;
        ++actor.step;
        return;
    case DiveStep::Cry:
        if (!actor.animEnded()) return;
        sound(0x2a7);
        actor.velocity.y = int16_t(0xfba0);
        actor.acceleration.y = 0x20;
        actor.anim = 4;
        ++actor.step;
        return;
    case DiveStep::Plunge:
        turnPitch(actor, 0xc0, 0x20);
        if (!actor.animEnded()) return;
        sound(0x2a6);
        actor.rotation.x += 0x1c0u;
        actor.anim = 0;
        actor.acceleration.y = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0xc00;
        ++actor.step;
        return;
    case DiveStep::Climb:
        if (boss.flags & kBossFlinch) {
            actor.anim = 5;
            actor.prevAnim = 0;
        }
        if (actor.pos.y.whole > -0x700) ++actor.step;
        return;
    case DiveStep::Return:
        if (boss.flags & kBossFlinch) {
            actor.anim = 5;
            actor.prevAnim = 0;
        }
        if (actor.rotation.x > 0) {
            turnYaw(actor, boss.targetYaw, 8);
            actor.rotation.x -= 0x40u;
        } else {
            actor.rotation.x = 0;
        }
        if (yawGap(actor, boss) >= 0x400) {
            actor.subState = uint8_t(BossMove::Hover);
            boss.flags |= kBossNear;
        }
        return;
    default: return;
    }
}
// exe 0x4b9a90: the fall at a quarter life -- down to y -0x2d0 (camera shake), then grounded
void fall(AnimatedActor& actor, BossFields& boss) {
    switch (FallStep(actor.step)) {
    case FallStep::Fall:
        turnYaw(actor, boss.targetYaw, 8);
        turnPitch(actor, boss.targetPitch, 0x40);
        turnRoll(actor, 0, 0x20);
        if (actor.animEnded()) actor.prevAnim = 0;
        if (actor.pos.y.whole >= -0x2d0) {
            cameraShakeAt(0x1000, 0x200, 0x100, 0x1000, actor.pos); // exe 0x40eab0
            boss.flags |= kBossGrounded;
            actor.anim = 6;
            actor.acceleration.z = int16_t(0xffa0);
            actor.rotation.x = 0;
            actor.pos.y.whole = int16_t(0xfd30);
            ++actor.step;
        }
        return;
    case FallStep::Land:
        if (actor.velocity.z <= 0) {
            actor.velocity.z = 0;
            actor.acceleration.z = 0;
        }
        if (actor.animEnded()) {
            boss.flags &= ~kBossFalling;
            actor.anim = 7;
            actor.subState = uint8_t(BossMove::Grounded);
            actor.restartStep();
        }
        return;
    default: return;
    }
}
// exe 0x4b9b80: grounded -- the head follows; a bite when near and facing, a puff (at the animation's frame 0xf)
// when farther, a recoil when hit (sound 0x2b2); dead -> the death
void grounded(AnimatedActor& actor, BossFields& boss) {
    headFollow(actor, boss);
    const int d = yawGap(actor, boss);
    auto hitRecoil = [&] {
        sound(0x2b2);
        actor.anim = 0xa;
        actor.step = uint8_t(GroundStep::Recoil);
    };
    auto bite = [&] {
        sound(0x2ab);
        sound(0x2b1);
        boss.flags |= kBossBiting;
        actor.anim = 8;
        actor.step = uint8_t(GroundStep::Bite);
    };
    switch (GroundStep(actor.step)) {
    case GroundStep::Watch: {
        const int16_t c = boss.targetDistance;
        if (c < 0x600 && d < 0x400) {
            actor.anim = 9;
            actor.step = uint8_t(GroundStep::Shoot);
        } else if (c < 0x4b0 && d >= 0x400) {
            bite();
        } else if (boss.flags & kBossHitNow) {
            hitRecoil();
        }
        break;
    }
    case GroundStep::Recoil:
        if (boss.targetDistance < 0x4b0 && d >= 0x400) {
            bite();
        } else if (actor.animEnded()) {
            actor.anim = 7;
            actor.step = uint8_t(GroundStep::Watch);
        }
        break;
    case GroundStep::Bite:
        if (actor.animEnded()) {
            boss.timer = 0x1e;
            boss.flags &= ~kBossBiting;
            actor.anim = 7;
            actor.step = uint8_t(GroundStep::Rest);
        }
        break;
    case GroundStep::Shoot:
        if (uint8_t(actor.animKey.frame) == 0xf) sound(0x2a9);
        if (actor.animKey.frame >= 0xf) spitPuff(actor, boss);
        if (actor.animEnded()) {
            boss.timer = 0x5a;
            actor.anim = 7;
            actor.step = uint8_t(GroundStep::Rest);
        }
        break;
    case GroundStep::Rest:
        if (boss.flags & kBossHitNow) {
            hitRecoil();
        } else {
            boss.timer -= 1u;
            if (uint16_t(boss.timer) == 0) actor.step = uint8_t(GroundStep::Watch);
        }
        break;
    default: break;
    }
    if (boss.flagsLow & kBossDead) {
        actor.subState = uint8_t(BossMove::Death);
        actor.step = 0;
    }
}
// exe 0x4b9d60: the death -- sound 0x2b3, animations 0xb / 0xc, 0x5a ticks of smoke blasts, story flag 0x681
void death(AnimatedActor& actor, BossFields& boss) {
    switch (DeathStep(actor.step)) {
    case DeathStep::Cry:
        sound(0x2b3);
        actor.anim = 0xb;
        ++actor.step;
        return;
    case DeathStep::Collapse:
        if (actor.animEnded()) {
            boss.timer = 0x5a;
            actor.anim = 0xc;
            ++actor.step;
        }
        return;
    case DeathStep::Smoke: {
        boss.timer -= 1u;
        const int16_t t = boss.timer;
        if (t < 0x20 ? (t & 3) == 0 : (t & 7) == 0) smokeBlast(actor);
        if (boss.timer == 0 && player().hitPoints >= 0) {
            setStoryFlag(kFlagBossDefeated); // exe 0x510e10
            ++actor.step;
        }
        return;
    }
    default: return;
    }
}
void bossMove(AnimatedActor& actor, BossFields& boss) { // [0x89484c]
    switch (BossMove(actor.subState)) {
    case BossMove::Hover: hover(actor, boss); return;
    case BossMove::Bank: bank(actor, boss); return;
    case BossMove::Dive: dive(actor, boss); return;
    case BossMove::Fall: fall(actor, boss); return;
    case BossMove::Grounded: grounded(actor, boss); return;
    case BossMove::Death: death(actor, boss); return;
    default: notPortedAt(kStoryPort, "0x4b8ec0 sub-state (table 0x89484c)", actor.subState);
    }
}
void bossBind(AnimatedActor& actor, BossFields& boss) { // exe 0x4b8db0
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                        // exe 0x5e1490
        return;
    }
    actor.flags |= kActorScenery;
    actor.clearMotion();
    allocBoneBlocks(actor, 1); // exe 0x5e16c0
    const uint32_t block = boneBlockAddress(actor, 4); // exe 0x5e1780(actor, 4)
    if (!block) notPorted(kStoryPort, "0x4b8db0: exe 0x5e1780(actor, 4) returned null (the exe writes through it)");
    boss.workBlock = block;
    boss.clearBlockAfterWorkBytes();
    auto& work = boss.work();
    work.clearBytes();
    actor.setParamsWord(0x101010);
    actor.velocity.z = 0xc00;
    boss.statsRow = statTableRow(actorStatTables().animated, actor.type);
    work.timer = 1;
    work.lastCry = 0xffff;
    const uint16_t hp = uint16_t(boss.row().hitPoints << 5);
    actor.maxHitPoints = int16_t(hp);
    if (actor.spawnByte == 0) {
        actor.hitPoints = int16_t(hp);
        boss.flags |= kBossCryChosen | kBossVolleyAgain | kBossMoveChosen;
    } else {
        actor.hitPoints = int16_t(int16_t(hp) >> 1);
        boss.flags |= kBossCryChosen | kBossVolleyAgain | kBossNear;
    }
    hudAddBossGauge(actor, 1, 4, 0x40c0e0); // exe 0x415d30
    actor.anim = 0;
    animSetPose(actor, 0, 0); // exe 0x5e1c30
    actor.enterStateBytes(uint8_t(BossState::Run));
    actor.restartStep();
}
void bossRun(AnimatedActor& actor, BossFields& boss) { // exe 0x4b8ec0
    auto& work = boss.work();
    int32_t target[3]; // the exe's stack points (16.16)
    if (!(boss.flags & kBossFalling)) {
        const Player& p = player();
        target[0] = p.pos.x;
        target[1] = int32_t(uint32_t(p.pos.y) + 0xff800000u);
        target[2] = p.pos.z;
    } else {
        target[0] = 0;
        target[1] = int32_t(0xfffffd30u);
        target[2] = 0;
    }
    const int32_t me[3] = {actor.pos.x, actor.pos.y, actor.pos.z};
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.tint = uint16_t(((uint32_t(int32_t(boss.tintB)) << 5 | uint32_t(int32_t(boss.tintG))) << 5) |
                          uint32_t(int32_t(boss.tintR)));
    int16_t from[3], to[3];
    for (int k = 0; k < 3; ++k) {
        from[k] = int16_t(uint32_t(target[k]) >> 16);
        to[k] = int16_t(uint32_t(me[k]) >> 16);
    }
    boss.targetDistance = int16_t(distance3(from, to));                           // exe 0x5e3a60
    boss.targetYaw = int16_t(angleOf(target[0], target[2], me[0], me[2]));       // exe 0x5e3bc0
    boss.targetPitch = int16_t(0x800 - pitchOf(from, to));                        // exe 0x5e3ca0
    target[1] = int32_t(uint32_t(target[1]) - 0x880000u);
    from[1] = int16_t(uint32_t(target[1]) >> 16);
    boss.aimPitch = int16_t(0x800 - pitchOf(from, to));
    if (boss.targetYaw >= 0x800) boss.targetYaw -= 0x1000u;
    if (actor.rotation.y >= 0x800) actor.rotation.y -= 0x1000u;
    if (actor.rotation.y < -0x800) actor.rotation.y += 0x1000u;
    bossMove(actor, boss);
    actor.velocity.y += uint16_t(actor.acceleration.y);
    actor.velocity.z += uint16_t(actor.acceleration.z);
    actor.rotation.y += uint16_t(actor.spin.x);
    moveAlongRotation(actor.pos, actor.rotation, 0, actor.velocity.y, -int(actor.velocity.z)); // exe 0x5e37b0
    distanceClamp(actor, boss);                                                                 // exe 0x4ba160
    if (actor.pos.y.whole >= -0x320 && !(boss.flags & (kBossFalling | kBossGrounded)))
        actor.pos.y.whole = int16_t(0xfce0);
    if (boss.flagsLow & (kBossEyesOut | kBossEyesClosing)) {
        eyes(actor, boss); // exe 0x4ba270
    } else if (boss.eyeTimer != 0) {
        boss.eyeTimer -= 1u;
        if (boss.eyeTimer == 0) {
            work.eyeAClosed() = 1;
            work.eyeBClosed() = 1;
        }
    }
    // kBossFalling / kBossGrounded: the shadow exe 0x5f20f0(actor, 0x100, 0x100, 0) -- not drawn by the port
    takeDamage(actor, boss); // exe 0x4ba330
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, 0);        // exe 0x5e1c50
    else if (boss.flagsLow & kBossHoldPose) animDecodeCurrent(actor);       // exe 0x5e1f30
    else animStep(actor);                                                    // exe 0x5e1ea0
    // the decoded frame bent: the head turned by its yaw, the wings by the neck's bend
    DecodedFrame& frame = decodedFrame();
    LaneVec4 head = frame.entries[kHeadEntry], left = frame.entries[kLeftWingEntry],
              right = frame.entries[kRightWingEntry];
    head.y = int16_t(uint16_t(head.y) + uint16_t(boss.headYaw));
    left.y = int16_t(uint16_t(left.y) - uint16_t(boss.neckBend));
    right.y = int16_t(uint16_t(right.y) + uint16_t(boss.neckBend));
    animPose(actor); // exe 0x5e2d90
    if (boss.flags & kBossQuarterLife) {
        if (!(boss.flags & kBossGrounded)) trailsLowLife(actor, boss);
        else trailsGrounded(actor, boss);
    }
    hitEntries(actor, boss); // exe 0x4ba490
}
void bossTick(AnimatedActor& actor) { // exe 0x4b8d90: [0x894840]
    BossFields& boss = classState<BossFields>(actor);
    switch (actor.state) {
    case 0: bossBind(actor, boss); return;
    case 1: bossRun(actor, boss); return;
    case 2: freeActor(actor); return; // exe 0x468fa0
    case 3: hover(actor, boss); return;
    case 4: bank(actor, boss); return;
    case 5: dive(actor, boss); return;
    case 6: fall(actor, boss); return;
    case 7: grounded(actor, boss); return;
    case 8: death(actor, boss); return;
    default: notPortedAt(kStoryPort, "0x4b8d90 state (table 0x894840)", actor.state);
    }
}

// ---- the statics (stage type 6) ------------------------------------------------------------------------------------

// exe 0x5a06e0 / 0x59e580: the rocking of the roll (as content/nino/ch2_flyer)
void rockStart(StaticActor& actor, PieceFields& piece) {
    actor.rotation.z = int16_t(actor.rotation.z >> 2);
    piece.rockAmplitude = 0x400;
    piece.rockSpeed = int16_t((uint16_t(actor.rotation.z) & 0xfff) < 0x800 ? 0x400 : 0xfc00);
}
void rockTick(StaticActor& actor, PieceFields& piece) {
    const int16_t amp = piece.rockAmplitude;
    const uint32_t r = uint16_t(actor.rotation.z) & 0xfff;
    actor.rotation.z = int16_t((piece.rockSpeed >> 4) + uint16_t(actor.rotation.z));
    if (r < 0x800) piece.rockSpeed = int16_t(piece.rockSpeed - 0x200);
    if (r > 0x800) piece.rockSpeed = int16_t(piece.rockSpeed + 0x200);
    if (amp < piece.rockSpeed) piece.rockSpeed = amp;
    if (piece.rockSpeed < -int(amp)) piece.rockSpeed = int16_t(-amp);
    if ((uint16_t(actor.prevRotation.z) ^ uint16_t(actor.rotation.z)) & 0x800) {
        const int16_t n = int16_t(amp - (amp >> 2));
        piece.rockAmplitude = n;
    }
}
// exe 0x5960b0: a blast (variant 0x21) at the actor, size 0x200, sound 0x8b
void pieceBlast(const StaticActor& actor) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return;
    StaticActor& b = staticActorAt(slot);
    BlastParams params(actorHeaderAt(slot));
    b.flags = kActorLive;
    b.type = typeByte(GlobalStaticType::Blast);
    b.loopFlags |= kActorSweepGroup2;
    b.variant = 0x21;
    b.pos.x.whole = actor.pos.x.whole;
    b.pos.y.whole = actor.pos.y.whole;
    b.pos.z.whole = actor.pos.z.whole;
    params.size = 0x200;
    params.attack = 0;
    sound(0x8b);
}
// exe 0x596110: a smoke trail (the stage's spark emitter, variant 0x1b) at the actor, following it
void pieceTrail(StaticActor& actor) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& trail = staticActorAt(slot);
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    trail.flags = kActorLive;
    trail.loopFlags |= kActorSweepGroup2;
    trail.type = kSparkEmitterStageType;
    trail.variant = 0x1b;
    trail.spawnByte = 0;
    trail.pos.x.whole = actor.pos.x.whole;
    trail.pos.y.whole = actor.pos.y.whole;
    trail.pos.z.whole = actor.pos.z.whole;
    params.driftX = 0;
    params.driftZ = 0;
    params.lifetime = 0x3c;
    params.parent = &actor;
}
bool pieceBind(StaticActor& actor) { // exe 0x5e0ba0, freed (exe 0x5e1540) when the bank has no model
    if (bindActorModel(actor, 0)) return true;
    freeActor(actor);
    return false;
}
// the pieces' hit entry (their box, cleared, the previous x / z)
void pieceHit(const StaticActor& actor, PieceFields& piece, uint32_t shape, uint32_t attack) {
    piece.hitBox.result = 0;
    piece.hitBox.previousX = actor.prevPos.x.whole;
    piece.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t p[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(p, piece.hitBox, hitOwner(actor.slot) | shape, attack);
}
constexpr uint32_t kPieceGroup = 5; // the pieces' hit-list group
// exe 0x595d80: state 1 a piece circling the arena; states 3 / 4 the script-driven falling piece
void circlingPiece(StaticActor& actor) {
    PieceFields& piece = classState<PieceFields>(actor);
    switch (actor.state) { // [0x8ce084]
    case 0:                // exe 0x595da0
        if (!pieceBind(actor)) return;
        actor.flags |= kActorDrawn;
        piece.circleTimer = 0;
        actor.scale.z = 0x100;
        actor.scale.y = 0x100;
        actor.scale.x = 0x100;
        actor.rotation.x = 0x100;
        actor.rotation.z = 0;
        actor.velocity.setXY(0, 0);
        actor.velocity.setZSpare(0, 0);
        actor.enterStateBytes(1);
        return;
    case 1: { // exe 0x595e00
        piece.circleTimer += 1u;
        if (piece.circleTimer == 0x80) {
            actor.pos.x.whole = 0x1200;
            actor.pos.y.whole = 0x600;
            actor.pos.z.whole = int16_t(0xdc00);
            actor.rotation.y = 0x800;
            piece.circleTimer = 0;
        }
        actor.rotation.y -= 8u;
        const int s = fixedSin(int(actor.rotation.y));
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0x12u);
        actor.pos.x.whole = int16_t(uint16_t(actor.pos.x.whole) - uint16_t((s << 6) >> 12));
        const int c = fixedCos(int(actor.rotation.y));
        actor.pos.z.whole = int16_t(uint16_t(actor.pos.z.whole) - uint16_t((c * 0x50) >> 12));
        return;
    }
    case 2:
    case 5: freeActor(actor); return; // exe 0x5afdd0 -> 0x5e1540
    case 3:                           // exe 0x595ea0
        if (!pieceBind(actor)) return;
        actor.clearMotion();
        actor.clearClassBlock();
        piece.travelYaw = actor.rotation.y;
        actor.velocity.z = 0x800;
        actor.enterState(1);
        return;
    case 4: { // exe 0x595f00: by the script step's sub and its tick
        actor.tint = kTintNeutral;
        actor.savePreviousPose();
        const uint8_t step = scriptState().steps->sub;
        const int32_t tick = scriptState().stepTick;
        if (step == 2) {
            if (tick != 0) {
                if (tick == 1) {
                    actor.pos.x.whole = 0;
                    actor.pos.y.whole = int16_t(0xf800);
                    actor.pos.z.whole = 0;
                    actor.velocity.z = 0x600;
                }
                if (tick == 0x1e) {
                    rockStart(actor, piece); // exe 0x5a06e0
                    pieceBlast(actor);
                    pieceTrail(actor);
                }
                if (tick > 0x1e) rockTick(actor, piece); // exe 0x59e580
            }
        } else if (step == 3) {
            if (tick != 0) {
                if (tick == 1) {
                    actor.pos.x.whole = 0x780;
                    actor.pos.y.whole = int16_t(0xf6a0);
                    actor.pos.z.whole = 0x2c0;
                    actor.rotation.y = 0x6a0;
                    actor.velocity.z = 0x380;
                    actor.velocity.y = int16_t(0xff40);
                    actor.acceleration.y = 6;
                    actor.acceleration.z = 0x10;
                    piece.travelYaw = 0x6a0;
                    pieceTrail(actor);
                }
                const uint16_t v = uint16_t(actor.acceleration.z);
                actor.rotation.x += 8u;
                actor.rotation.z -= v;
                actor.rotation.y -= 0xcu;
                actor.acceleration.z = int16_t(v + 2);
                if (!(uint8_t(tick) & 0xf)) pieceTrail(actor);
            }
        }
        actor.velocity.y += uint16_t(actor.acceleration.y);
        moveAlongYaw(actor.pos, piece.travelYaw, 0, actor.velocity.y, -int(actor.velocity.z)); // exe 0x5e36f0
        pieceHit(actor, piece, hitGroup(kPieceGroup) | kHitNoLockOn | hitRadius(0x80),
                 2u | kHitAttackHarmless | kHitAttackBump);
        return;
    }
    default: notPortedAt(kStoryPort, "0x595d80 state (table 0x8ce084)", actor.state);
    }
}
// exe 0x595420 (stage static type 6 sub 1): a spinning piece moving along its rotation, flashing when hit
void spinningPiece(StaticActor& actor) {
    PieceFields& piece = classState<PieceFields>(actor);
    switch (actor.state) { // [0x8ce014]
    case 0:                // exe 0x595440
        if (!pieceBind(actor)) return;
        actor.clearMotion();
        actor.clearClassBlock();
        actor.velocity.z = 0x600;
        actor.rotation.x = 0x80;
        actor.rotation.z = int16_t(0xff00);
        actor.state = 1;
        return;
    case 1: // exe 0x5954a0
        actor.tint = kTintNeutral;
        actor.savePreviousPose();
        actor.rotation.y += 0x10u;
        if (actor.rotation.y >= 0x800) actor.rotation.y -= 0x1000u;
        integrateVelocity(actor, 0, 0, -int(actor.velocity.z)); // exe 0x5e3690
        if (piece.hitBox.result & 0x7f0000) actor.tint = kTintFlash;
        pieceHit(actor, piece, hitGroup(kPieceGroup) | kHitNoLockOn | hitRadius(0x100),
                 2u | kHitAttackHarmless | kHitAttackSolid);
        return;
    case 2: freeActor(actor); return; // exe 0x5afdd0 -> 0x5e1540
    default: notPortedAt(kStoryPort, "0x595420 state (table 0x8ce014)", actor.state);
    }
}

} // namespace

void script59FaceRowCastUpdate(const PlayerEnv&, uint32_t actor) { faceRowCast(animatedActorAt(actor)); }
void script59WalkInCastUpdate(const PlayerEnv&, uint32_t actor) { walkInCast(animatedActorAt(actor)); }
void st4bFlyingBossUpdate(const PlayerEnv&, uint32_t actor) { bossTick(animatedActorAt(actor)); }
void st4bCirclingPieceUpdate(const PlayerEnv&, uint32_t actor) { circlingPiece(staticActorAt(actor)); }
void st4bSpinningPieceUpdate(const PlayerEnv&, uint32_t actor) { spinningPiece(staticActorAt(actor)); }

} // namespace od::port
