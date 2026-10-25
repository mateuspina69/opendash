// The statue of the pirate base's script 0x5d (ST3C sub 2, story 12: pool 0x20 type 0x73 variant 1, exe 0x4fa800) and
// its hit part (static ST3C type 0x50, exe 0x5b7890); hypothesis names.
//
// The statue binds on the ground with its hit part (static type 0x50, its parameter = the statue); then follows the
// script's step sub-id: 1 -> ready, 5 -> turned round and pushed back (-0x2800 z), 6 / 7 / 8 the next poses; its pose
// held (the current key decoded each tick) while it stands.
// The hit part rides the statue's bone 0 (that bone's matrix, its columns scaled by 0x1000 << (3 - shift), as its root;
// its position = the bone's point + the root * (exe 0x8d0b18), << (0x10 - shift)) and takes the hits (sound 0x8d, the
// flash) for the statue: the statue's part state kPartHitNow while hit, kPartHalf once below half its hit points;
// destroyed -> the statue's part state kPartBroken | kPartHitNow, flag 0x68, then a blast 0x80 above it along its
// rotation and freed. Its hit entry sits 0x80 above it along its rotation in its root's frame; the statue's guard bit
// shields it.
#include "content/saul_kada/st3c_statue.hpp"
#include "content/saul_kada/st3c_statue_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"

using namespace od::port::st3c_statue_state;

namespace od::port {

using namespace st3c_statue_state;

namespace {

// The statue's parameters the hit part reads and writes: its guard (kStatueShielded) and the part's state (kPart*)
struct StatueParams {
    explicit StatueParams(ActorHeader& actor) : guard(actor.params[2]), partState(actor.params[3]) {}
    uint8_t& guard;     // +0xe
    uint8_t& partState; // +0xf
};
constexpr uint8_t kStatueGuardUp = 0x01;   // its hit entry is the lock-on's while clear
constexpr uint8_t kStatueShielded = 0x02;  // the part takes no hits (and has no hit entry)
constexpr uint8_t kPartBroken = 0x01;
constexpr uint8_t kPartHitNow = 0x02;
constexpr uint8_t kPartHalf = 0x04;
constexpr uint8_t kStatuePartType = 0x50;

// ---- exe 0x4fa800: the statue -------------------------------------------------------------------------------------
enum class StatueState : uint8_t { Bind = 0, Cues = 1 };
// [0x8978e4]
enum class StatueCue : uint8_t { WaitFirst = 0, Ready = 1, PushedBack = 2, Pose6 = 3, Pose7 = 4, Idle = 5 };
constexpr int kPushBack = -0x2800;

void nextCue(AnimatedActor& actor, StatueCue cue) {
    actor.subState = uint8_t(cue);
    actor.restartStep();
}
// exe 0x4fa8b0
void bindStatue(const PlayerEnv& env, AnimatedActor& actor, StatueFields& statue) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.collisionBox.pointAt(TableId::St3cStatueBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    statue.holdPose = 1;
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    if (const uint32_t slot = allocActor60(kActorStageClass)) { // exe 0x5e12f0: its hit part
        StaticActor& part = staticActorAt(slot);
        part.flags = kActorLive;
        part.loopFlags |= kActorSweepGroup2;
        part.type = kStatuePartType;
        part.subtype = 0;
        part.variant = 0;
        part.spawnByte = 0;
        part.setParamsWord(actor.at);
    }
    actor.enterState(uint8_t(StatueState::Cues));
}
// exe 0x4fa960 [0x8978e4] on the script's step sub-id
void followCues(AnimatedActor& actor, StatueFields& statue) {
    const uint8_t cue = scriptState().stepSub;
    switch (StatueCue(actor.subState)) {
    case StatueCue::WaitFirst: // exe 0x4f4900
        if (cue == 1) nextCue(actor, StatueCue::Ready);
        break;
    case StatueCue::Ready: // 0x4fa980
        if (cue == 5) {
            actor.rotation.y += 0x800u;
            integrateVelocity(actor, 0, 0, kPushBack); // exe 0x5e3690
            statue.holdPose = 0;
            nextCue(actor, StatueCue::PushedBack);
        }
        break;
    case StatueCue::PushedBack: // 0x4fa9c0
        if (cue == 6) {
            statue.holdPose = 1;
            nextCue(actor, StatueCue::Pose6);
        }
        break;
    case StatueCue::Pose6: // 0x4fa9e0
        if (cue == 7) {
            statue.holdPose = 0;
            nextCue(actor, StatueCue::Pose7);
        }
        break;
    case StatueCue::Pose7: // 0x4faa00
        if (cue == 8) nextCue(actor, StatueCue::Pose7);
        break;
    case StatueCue::Idle: break; // 0x52ef00
    default: notPortedAt(kStoryPort, "0x4fa960 sub-state (table 0x8978e4)", actor.subState);
    }
}
void scriptStatue(const PlayerEnv& env, AnimatedActor& actor) {
    StatueFields& statue = classState<StatueFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (StatueState(actor.state)) {
    case StatueState::Bind: bindStatue(env, actor, statue); break;
    case StatueState::Cues: followCues(actor, statue); break;
    default: notPortedAt(kStoryPort, "0x4fa800 state (table 0x8978dc)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        if (statue.holdPose) animDecodeCurrent(actor); // exe 0x5e1f30
        else animStepPose(actor);                      // exe 0x5e1e80
    } else {
        animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    }
}

// ---- exe 0x5b7890: the statue's hit part --------------------------------------------------------------------------
// States [0x8d0af8]
enum class PartState : uint8_t { Bind = 0, Ride = 1, Burst = 2 };
constexpr uint8_t kPartFlagBroken = 0x01;
constexpr uint8_t kPartFlagInvulnerable = 0x02;
const DataTable<ShortPoint> rootOffset{TableId::St3cStatueRootOffset}; // the part's point in its root's frame (exe 0x8d0b18)
constexpr uint16_t kHurtSound = 0x8d;
constexpr uint32_t kFlagStatueBroken = 0x68;
constexpr uint16_t kBlastSize = 0x100;
constexpr uint8_t kBlastVariant = 1;
constexpr int16_t kEntryAbove = -0x80;
// the hits it takes: a strong one, a thrown one, the player's, a bump
constexpr uint32_t kPartHits = kHitAttackStrong | kHitAttackThrown | kHitAttackByPlayer | kHitAttackBump;
// its hit entry: a body (radius 0x80), not hurting, a solid
constexpr uint32_t kPartShape = hitGroup(kHitGroupBodies) | hitRadius(0x80);
constexpr uint32_t kPartAttack = kHitAttackHarmless | kHitAttackThrown | kHitAttackSolid;

// the point 0x80 above the part along its rotation (in the projections' scratch: the transform = the root * R(the
// rotation), the point turned by it); its offset from the part
LaneVec3 aboveOffset(const StaticActor& actor) {
    ProjectionScratch& scratch = projectionScratch();
    scratch.point.x = actor.rotation.x;
    scratch.point.y = actor.rotation.y;
    scratch.point.z = actor.rotation.z;
    const Matrix3 r = rotationYXZ(scratch.point.x, scratch.point.y, scratch.point.z); // exe 0x40bfd0
    scratch.transform.matrix = mulMatrix3x3(actor.rootMatrix, r);
    scratch.point.x = 0;
    scratch.point.y = kEntryAbove;
    scratch.point.z = 0;
    const int16_t up[3] = {scratch.point.x, scratch.point.y, scratch.point.z};
    int16_t turned[3];
    transformShortVector(scratch.transform.matrix.get(), up, turned); // exe 0x40bb30
    scratch.rotatedPoint.x = turned[0];
    scratch.rotatedPoint.y = turned[1];
    scratch.rotatedPoint.z = turned[2];
    return scratch.rotatedPoint;
}
// exe 0x5b7c40: a hit on the part (one of kPartHits, with damage) -> the flash, sound 0x8d, the damage unless
// invulnerable; -1 = destroyed, 1 = hit, 0 = none
int takeHit(StaticActor& actor, PartFields& part) {
    const uint32_t hit = part.hitBox.result;
    const uint32_t damage = hit & kHitDamageMask;
    if (!(hit & kPartHits) || damage == 0) return 0;
    actor.tint = kTintFlash;
    soundPlayAtPos(kHurtSound, actor.pos); // exe 0x5d5380
    if (!(part.flags & kPartFlagInvulnerable) && hitPointsAdd(actor, -int(damage)) != 0) return -1; // exe 0x5e52f0
    return 1;
}
// exe 0x5b78f0
void bindPart(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                         // exe 0x5e1540
        return;
    }
    actor.flags |= kActorDrawn | kActorPoseOnRoot;
    const uint16_t hp = hasStaticStats(actor.type)
                            ? uint16_t(staticStats(actor.type, actor.spawnByte).hitPoints << 4)
                            : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.clearWords();
    actor.acceleration.clearWords();
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.rotation.y = 0x800;
    actor.rotation.x = 0xa0;
    actor.airborne = 1;
    actor.collisionBox.pointAt(TableId::St3cStatuePartBox);
    actor.clearClassBlock();
    actor.enterState(uint8_t(PartState::Ride));
}
// exe 0x5b79a0
void ride(StaticActor& actor, PartFields& part) {
    const uint32_t statueAt = StatuePartParams(actor).statue;
    StatueParams statue(actorHeaderAt(statueAt));
    const int r = takeHit(actor, part);
    if (r == -1) {
        if (!(statue.guard & kStatueShielded)) {
            part.flags |= kPartFlagBroken;
            statue.partState |= kPartBroken | kPartHitNow;
            actor.enterState(uint8_t(PartState::Burst));
            actor.setDrawn(false);
            setStoryFlag(kFlagStatueBroken);
        }
        return;
    }
    if (r == 1) {
        statue.partState |= kPartHitNow;
        if (int16_t(actor.maxHitPoints >> 1) > actor.hitPoints && !(part.flagsWord & kPartFlagHalf)) {
            part.flagsWord |= kPartFlagHalf;
            statue.partState |= kPartHalf;
        }
    } else {
        statue.partState &= uint8_t(~kPartHitNow);
    }
    const AnimatedActor& statueActor = animatedActorAt(statueAt);
    const BoneBlock* block = boneBlockOf(statueActor, 0); // exe 0x5e17d0
    if (!block) {
        notPortedAt(kStoryPort, "FUN_005e17d0 returned null (the exe copies from 0)", statueAt);
        return;
    }
    actor.copyRootWordsFrom(*block, 0);
    const uint8_t shift = stage().coordinateShift;
    const int32_t scale = int32_t(0x1000u << ((3u - uint32_t(shift)) & 0x1f));
    ProjectionScratch& scratch = projectionScratch();
    scratch.transformed[0] = scale;
    scratch.transformed[1] = scale;
    scratch.transformed[2] = scale;
    const int32_t columns[3] = {scale, scale, scale};
    actor.rootMatrix = scaleColumns(actor.rootMatrix, columns); // exe 0x40c0e0
    const int16_t offset[3] = {rootOffset->x, rootOffset->y, rootOffset->z};
    int16_t turned[3];
    transformShortVector(actor.rootMatrix, offset, turned); // exe 0x40bb30
    scratch.point.x = turned[0];
    scratch.point.y = turned[1];
    scratch.point.z = turned[2];
    const ShortVec4& bone = bonePoint(statueActor, 0, kStoryPort); // exe 0x5e1810 (none: the port gap)
    const uint32_t up = (0x10u - uint32_t(stage().coordinateShift)) & 0x1f;
    actor.pos.x = int32_t(uint32_t(int32_t(bone.x) + scratch.point.x) << up);
    actor.pos.y = int32_t(uint32_t(int32_t(bone.y) + scratch.point.y) << up);
    actor.pos.z = int32_t(uint32_t(int32_t(bone.z) + scratch.point.z) << up);
    if ((part.flags & kPartFlagBroken) || (statue.guard & kStatueShielded)) {
        part.hitBox.result = 0;
        return;
    }
    // 0x5b7cb0: the hit-list entry
    part.hitBox.previousX = actor.prevPos.x.whole;
    part.hitBox.previousZ = actor.prevPos.z.whole;
    part.hitBox.result = 0;
    const LaneVec3 above = aboveOffset(actor);
    scratch.point.x = int16_t(uint16_t(actor.pos.x.whole) + uint16_t(above.x));
    scratch.point.y = int16_t(uint16_t(actor.pos.y.whole) + uint16_t(above.y));
    scratch.point.z = int16_t(uint16_t(actor.pos.z.whole) + uint16_t(above.z));
    const uint32_t lockOn = (uint32_t(~statue.guard) & kStatueGuardUp) ? kHitNoLockOn : 0u;
    const int16_t pos[3] = {scratch.point.x, scratch.point.y, scratch.point.z};
    hitListSubmit(pos, part.hitBox, hitOwner(actor.slot) | lockOn | kPartShape,
                  uint32_t(staticStats(actor.type, actor.spawnByte).damage) | kPartAttack); // exe 0x5e4120
}
// exe 0x5b7b60: the blast 0x80 above it, then freed
void burst(StaticActor& actor) {
    const LaneVec3 above = aboveOffset(actor);
    if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(slot);
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.subtype = 0;
        blast.variant = kBlastVariant;
        blast.spawnByte = 0;
        blast.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(int32_t(above.x)));
        blast.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(int32_t(above.y)));
        BlastParams(actorHeaderAt(slot)).size = kBlastSize;
        blast.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(int32_t(above.z)));
    }
    freeActor(actor); // exe 0x5e1540
}
void statuePart(StaticActor& actor) {
    PartFields& part = classState<PartFields>(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    partLink().hitBox = slotFieldWord(actor, kStaticClassBlockOffset); // its hit box's exe address
    actor.tint = kTintNeutral;
    switch (PartState(actor.state)) {
    case PartState::Bind: bindPart(actor); return;
    case PartState::Ride: ride(actor, part); return;
    case PartState::Burst: burst(actor); return;
    default: notPortedAt(kStoryPort, "0x5b7890 state (table 0x8d0af8)", actor.state);
    }
}

} // namespace

void scriptStatueUpdate(const PlayerEnv& env, uint32_t actor) { scriptStatue(env, animatedActorAt(actor)); }
void statuePartUpdate(const PlayerEnv&, uint32_t actor) { statuePart(staticActorAt(actor)); }

} // namespace od::port
