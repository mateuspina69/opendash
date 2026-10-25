// The giant seen through the window (ST26 sub 5, story 13; pool 0x20 type 0x45 sub 2, exe 0x4cb980; hypothesis
// names). The ST26 enemy spawner places it at its side's point once the player enters that side's box; its yaw picks
// the side (yaw 0x400: side 1, walking towards x -0xc8e; else side 0, towards x +0x128e).
// States [0x8950b4]: 0 bind 0x4cba40, 1 active 0x4cbb70 (its sub-states [0x8950c0]: 0 the walk 0x4cc270, 1 the strike
// 0x4cc610; with the hit list: 11 spheres and 8 capsules on its bones), 2 freed 0x468fa0, 3 / 4 the walk / the strike
// alone (no hit list).
//  - the walk (its steps [0x4cc5e4]): z -0xb2 a tick while it stays in its side's box; once the player is in that box
//    too (or was seen there), it walks on to its animation's frame 0x3b (side 0) / 0xc (side 1), then animation 0x40 /
//    0x43, a shake, the strike. Seen before: it clamps at the end x and turns (yaw +-0x10 a tick to 0x400 / 0xc00, the
//    side flipped). A key's event bit 1: a pause of 4 ticks; bits 4 / 8: the reaction animations of the table 0x895130
//    with a push z -0x630. Every footstep key (event 0x20) shakes the camera.
//  - the strike (its steps [0x4cc960]): animation 0x41 / 0x44 (through the wall): frames 0x10..0x23 yaw -+0x33, 0x24
//    yaw 0x800, frame 0x34 breaks the wall (its cell's segment variant 1, 16 pieces of debris, flag 0x62 / 0x63, sound
//    0x222) once; 60 ticks later animation 0x42 / 0x45 (frame 0x2d: a turn from the table 0x89508e; frames
//    0x21..0x42 yaw +-0x20, 0x43 yaw 0xc00 / 0x400), then animation 1 with a push z -0x7a6 and the walk off to the end
//    x (then the walk's turn).
#include "content/saul_kada/window_giant.hpp"
#include "content/saul_kada/window_giant_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/wall_debris.hpp"

namespace od::port {

using namespace window_giant_state;

namespace {

enum class GiantState : uint8_t { Bind = 0, Active = 1, Free = 2, Walk = 3, Strike = 4 };
enum class GiantSub : uint8_t { Walk = 0, Strike = 1 };
enum class WalkStep : uint8_t {
    Walk = 0,     // towards the end while in the box
    Pause = 1,    // a few ticks still
    Approach = 2, // the player seen: on to the strike's frame
    Turn = 3,     // turning back
    React = 4,    // a reaction animation asked
    Reacting = 5, // ... played to its end
    TurnWait = 6, // the animation ended -> the turn
    TurnDone = 7, // ... -> the walk
    ToStrike = 8,
};
enum class StrikeStep : uint8_t { Swing = 0, Hold = 1, Return = 2, WalkOff = 3 };
enum class GiantSide : uint8_t { Right = 0, Left = 1 };

constexpr uint8_t kGiantHold = 0x01;    // the animation's frame held (decoded, not stepped)
constexpr uint8_t kGiantWallA = 0x08;   // side 0's wall struck
constexpr uint8_t kGiantWallB = 0x10;   // side 1's wall struck
constexpr uint8_t kGiantTurning = 0x20; // at the end x: the turn next
// the event bits of its animation keys
constexpr uint8_t kEventPause = 0x01, kEventReactA = 0x04, kEventReactB = 0x08, kEventFootstep = 0x20;

constexpr uint8_t kAnimWalk = 1;
constexpr uint8_t kAnimRaise[2] = {0x40, 0x43}, kAnimStrike[2] = {0x41, 0x44}, kAnimReturn[2] = {0x42, 0x45};
constexpr uint8_t kStrikeFrame[2] = {0x3b, 0xc};
constexpr uint8_t kNoAnim = 0xff;
constexpr int16_t kEndX[2] = {0x128e, -0xc8e};
constexpr int16_t kWalkSpeed = -0xb2, kReactPush = -0x630, kReturnPush = -0x7a6;
constexpr int16_t kSideYaw = 0x400; // its yaw on side 1 (side 0: 0xc00)
const DataTable<ScalarTable<uint8_t, 2>> reactionAnims{TableId::WindowGiantReactionAnims};
const DataTable<ScalarTable<uint16_t, 0x80>> returnTurns{TableId::WindowGiantReturnTurns}; // by frame
constexpr uint32_t kWallColumn[2] = {0x44, 0x3c}, kWallRow = 0x40;
constexpr uint32_t kWallFlag[2] = {0x62, 0x63};
constexpr uint16_t kWallSound = 0x222, kHurtSound = 0x8d;
// the debris pieces' offsets from the wall's point (s16 x / y)
struct DebrisOffset : DataRecord {
    int16_t x = 0;
    int16_t y = 0;
    static DebrisOffset load(const TableReader& r) {
        DebrisOffset rec;
        rec.x = r.s16(0);
        rec.y = r.s16(2);
        return rec;
    }
};
const DataTable<RecordTable<DebrisOffset, 16, 4>> debrisOffsets{TableId::WindowGiantDebrisOffsets};
constexpr uint8_t kDebrisSparkVariant = 0xa;
constexpr int16_t kGiantScale = 0x480;
// the parts hidden at the bind
constexpr size_t kHiddenParts[7] = {0, 12, 13, 14, 15, 16, 17};

uint32_t sideIndex(const GiantFields& giant) { return giant.side == uint8_t(GiantSide::Right) ? 0 : 1; }
void shake(const AnimatedActor& actor) { cameraShakeAt(0x2000, 0x400, 0x100, 0x1000, actor.pos); } // exe 0x40eab0

// exe 0x4cca10: the wall of its side falls apart: 16 pieces of the segment's debris at the wall's point less their
// offsets, each with a spark emitter (variant 0xa) for one tick
void wallDebris(const GiantFields& giant) {
    const uint32_t side = sideIndex(giant);
    const GridCell cell(gridCellAtIndex(kWallColumn[side], kWallRow));
    const SegmentEntry segment = segmentEntry(cell.word);
    const SegmentPoolEntry pool = segmentPoolEntry(segment.poolIndex);
    const BreakPoint point = segmentBreakPoint(segment, pool); // exe 0x58cff0
    for (size_t k = 0; k < 16; ++k) {
        const DebrisOffset offset = debrisOffsets[k];
        const uint32_t debrisSlot = allocActor60(0); // exe 0x5e1300
        if (!debrisSlot) continue;
        placeWallDebris(debrisSlot, pool, int16_t(uint16_t(point.x - offset.x)), int16_t(uint16_t(point.y - offset.y)),
                        point.z);
        const StaticActor& debris = staticActorAt(debrisSlot);
        const uint32_t sparkSlot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!sparkSlot) continue;
        StaticActor& spark = staticActorAt(sparkSlot);
        spark.flags = kActorLive;
        spark.loopFlags |= kActorSweepGroup2;
        spark.type = kSparkEmitterStageType;
        spark.variant = kDebrisSparkVariant;
        spark.pos.x.whole = debris.pos.x.whole;
        spark.pos.y.whole = debris.pos.y.whole;
        spark.pos.z.whole = debris.pos.z.whole;
        SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(sparkSlot));
        params.driftX = 0;
        params.driftZ = 0;
        params.lifetime = 1;
    }
}

// exe 0x4cc970(actor, k, entry): a reaction key (event 4 / 8) -> its animation [0x895130 + k * 2 + 0 / 1] at the
// entry; false when the key has neither
bool reactionAnim(AnimatedActor& actor, GiantFields& giant, uint32_t k, uint8_t entry) {
    if (actor.animKey.events & kEventReactA) {
        actor.anim = reactionAnims[k * 2];
        giant.animEntry = entry;
        return true;
    }
    if (actor.animKey.events & kEventReactB) {
        actor.anim = reactionAnims[k * 2 + 1];
        giant.animEntry = entry;
        return true;
    }
    return false;
}
// exe 0x4cc9d0(actor, anim, z, entry): an animation (kNoAnim: kept) at the entry, then a push z (exe 0x5e3690)
void animPush(AnimatedActor& actor, GiantFields& giant, uint8_t anim, int16_t z, uint8_t entry) {
    if (anim != kNoAnim) {
        actor.anim = anim;
        giant.animEntry = entry;
    }
    integrateVelocity(actor, 0, 0, z);
}
// the end x of its side reached: clamped there; true when it is
bool clampAtEnd(AnimatedActor& actor, const GiantFields& giant) {
    const uint32_t side = sideIndex(giant);
    if (side == 0 ? actor.pos.x.whole < kEndX[0] : actor.pos.x.whole > kEndX[1]) return false;
    actor.pos.x.whole = kEndX[side];
    return true;
}

// exe 0x4cba40: the model, the hit points of its row, scale 0x480, a few parts hidden, its side from its yaw
void bind(AnimatedActor& actor, GiantFields& giant) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, &variant)
        freeActor(actor);                        // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn | kActorScenery;
    actor.anim = kAnimWalk;
    giant.animEntry = 0;
    actor.prevAnim = kNoAnim;
    const uint16_t hp = uint16_t(uint32_t(animatedStats(actor.type).hitPoints) << 2);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox = nullptr;
    actor.scale.x = kGiantScale;
    actor.scale.y = kGiantScale;
    actor.scale.z = kGiantScale;
    actor.parts[kHiddenParts[0]].flags |= kPartHidden;
    actor.interactionBox = nullptr;
    actor.parts[kHiddenParts[1]].flags |= kPartHidden;
    actor.interaction = 0;
    actor.parts[kHiddenParts[2]].flags |= kPartHidden;
    actor.velocity.setXY(0, 0);
    actor.parts[kHiddenParts[3]].flags |= kPartHidden;
    actor.velocity.setZSpare(0, 0);
    actor.parts[kHiddenParts[4]].flags |= kPartHidden;
    actor.rotation.z = 0;
    actor.parts[kHiddenParts[5]].flags |= kPartHidden;
    actor.rotation.x = 0;
    actor.parts[kHiddenParts[6]].flags |= kPartHidden;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    giant.hitBox.result = 0;
    giant.flags = 0;
    giant.seen = 0;
    actor.state = uint8_t(GiantState::Active);
    actor.subState = uint8_t(GiantSub::Walk);
    actor.step = 0;
    giant.side = uint8_t(actor.rotation.y == kSideYaw ? GiantSide::Left : GiantSide::Right);
}

// exe 0x4cc270: the walk
void walk(AnimatedActor& actor, GiantFields& giant) {
    const uint32_t side = sideIndex(giant);
    switch (WalkStep(actor.step)) {
    case WalkStep::Walk:
    case WalkStep::Approach: // exe 0x4cc28b
        if (actor.animKey.events & kEventPause) {
            giant.timer = 4;
            giant.flags |= kGiantHold;
            giant.resumeStep = actor.step;
            actor.step = uint8_t(WalkStep::Pause);
            break;
        }
        integrateVelocity(actor, 0, 0, kWalkSpeed);
        if (WalkStep(actor.step) == WalkStep::Walk) {
            const CellBox box(stateAddress(TableId::WindowGiantSideBoxes) + uint32_t(giant.side) * 4);
            if (!pointInCellBox(actor.pos, box)) {
                giant.seen = 0;
            } else if (giant.seen != 0 || pointInCellBox(player().pos, box)) {
                actor.step = uint8_t(WalkStep::Approach);
            } else {
                giant.seen = 1;
            }
        } else if (giant.seen == 0) {
            if (actor.keyIndex == kStrikeFrame[side]) {
                actor.anim = kAnimRaise[side];
                giant.animEntry = 0;
                actor.subState = uint8_t(GiantSub::Strike);
                actor.step = uint8_t(StrikeStep::Swing);
                shake(actor);
            }
        } else {
            if (clampAtEnd(actor, giant)) giant.flags |= kGiantTurning;
            if (giant.flags & kGiantTurning) actor.step = uint8_t(WalkStep::TurnWait);
        }
        break;
    case WalkStep::Pause: // exe 0x4cc451
        if (giant.timer == 0) {
            actor.step = giant.resumeStep;
            giant.resumeStep -= 1u;
            giant.flags &= uint8_t(~kGiantHold);
        } else {
            giant.timer -= 1u;
        }
        break;
    case WalkStep::Turn: // exe 0x4cc494
        if (giant.side == uint8_t(GiantSide::Right)) {
            const uint16_t yaw = uint16_t((uint16_t(actor.rotation.y) + 0x10) & 0xfff);
            actor.rotation.y = int16_t(yaw);
            if (yaw == 0x400) {
                giant.side = uint8_t(GiantSide::Left);
                actor.step = uint8_t(WalkStep::TurnDone);
            }
        } else {
            const uint16_t yaw = uint16_t((uint16_t(actor.rotation.y) - 0x10) & 0xfff);
            actor.rotation.y = int16_t(yaw);
            if (yaw == 0xc00) {
                giant.side = uint8_t(GiantSide::Right);
                actor.step = uint8_t(WalkStep::TurnDone);
            }
        }
        break;
    case WalkStep::React: // exe 0x4cc4e8
        if (reactionAnim(actor, giant, 0, 0)) {
            animPush(actor, giant, kNoAnim, kReactPush, 0);
            actor.step = uint8_t(WalkStep::Reacting);
            giant.reactReturn = uint8_t(WalkStep::TurnDone);
            giant.reactAnim = actor.anim;
            giant.reactPush = 0;
            giant.reactEntry = 0;
        }
        break;
    case WalkStep::Reacting: // exe 0x4cc52d
        if (actor.animEnded()) {
            actor.step = giant.reactReturn;
            if (giant.reactPush != 0) {
                animPush(actor, giant, giant.reactAnim, int16_t(uint16_t(uint32_t(-int32_t(giant.reactPush.get())) << 4)),
                         giant.reactEntry);
            } else if (int32_t(int8_t(giant.reactAnim)) != int32_t(actor.anim)) {
                actor.anim = giant.reactAnim;
                giant.animEntry = giant.reactEntry;
            }
        }
        break;
    case WalkStep::TurnWait:
    case WalkStep::TurnDone: // exe 0x4cc586
        if (actor.animEnded()) {
            giant.flags &= uint8_t(~kGiantTurning);
            actor.step = uint8_t(WalkStep(actor.step) != WalkStep::TurnWait ? WalkStep::Walk : WalkStep::Turn);
        }
        break;
    case WalkStep::ToStrike: // exe 0x4cc5b0
        actor.subState = uint8_t(GiantSub::Strike);
        actor.step = uint8_t(StrikeStep::Swing);
        break;
    default: break;
    }
    if (actor.animKey.events & kEventFootstep) shake(actor); // exe 0x4cc5b8
}

// a held frame's timer: the frame held `ticks` once, counted down, then released
void holdFrame(GiantFields& giant, uint16_t ticks) {
    const uint8_t flags = giant.flags;
    if (!(flags & kGiantHold)) {
        giant.timer = ticks;
        giant.flags = uint8_t(flags | kGiantHold);
    } else if (giant.timer != 0) {
        giant.timer -= 1u;
    } else {
        giant.flags = uint8_t(flags & ~kGiantHold);
    }
}

// exe 0x4cc610: the strike through the wall
void strike(AnimatedActor& actor, GiantFields& giant) {
    const uint32_t side = sideIndex(giant);
    switch (StrikeStep(actor.step)) {
    case StrikeStep::Swing: { // exe 0x4cc630
        if (actor.animEnded()) {
            actor.anim = kAnimStrike[side];
            giant.animEntry = 0;
            ++actor.step;
            giant.timer = 0x3c;
            return;
        }
        if (actor.keyIndex == 0xf) holdFrame(giant, 8);
        const int8_t frame = int8_t(actor.keyIndex);
        if (frame >= 0x10 && frame < 0x24) {
            actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + (side == 0 ? uint16_t(-0x33) : uint16_t(0x33)));
            return;
        }
        if (frame == 0x24) {
            actor.rotation.y = 0x800;
            return;
        }
        if (frame != 0x34) return;
        const uint8_t struck = side != 0 ? kGiantWallB : kGiantWallA;
        if (giant.flags & struck) return;
        const uint32_t flag = kWallFlag[side];
        if (!storyFlag(flag)) {                                      // exe 0x510e50
            switchGridCellVariant(kWallColumn[side], kWallRow, 1); // exe 0x58d5f0(cell, 1)
            wallDebris(giant);                                       // exe 0x4cca10
            setStoryFlag(flag);                                      // exe 0x510e10
            soundPlayAtPos(kWallSound, actor.pos);
        }
        giant.flags |= struck;
        return;
    }
    case StrikeStep::Hold: // exe 0x4cc7ed
        if (giant.timer != 0) {
            giant.timer -= 1u;
            return;
        }
        actor.anim = kAnimReturn[side];
        giant.animEntry = 0;
        ++actor.step;
        return;
    case StrikeStep::Return: { // exe 0x4cc83a
        if (actor.animEnded()) {
            ++actor.step;
            animPush(actor, giant, kAnimWalk, kReturnPush, 0);
            actor.anim = kAnimWalk;
            if (side == 0) {
                actor.rotation.y = 0xc00;
                giant.animEntry = 0x3d;
            } else {
                actor.rotation.y = kSideYaw;
                giant.animEntry = 0xe;
            }
            return;
        }
        const int8_t frame = int8_t(actor.keyIndex);
        if (frame == 0x2d) {
            const uint8_t flags = giant.flags;
            if (!(flags & kGiantHold)) {
                giant.timer = 5;
                giant.flags = uint8_t(flags | kGiantHold);
                actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + returnTurns[size_t(int8_t(actor.keyIndex))]);
            } else if (giant.timer != 0) {
                giant.timer -= 1u;
            } else {
                giant.flags = uint8_t(flags & ~kGiantHold);
            }
            return;
        }
        if (frame >= 0x21 && frame < 0x43) {
            actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + (side == 0 ? uint16_t(0x20) : uint16_t(-0x20)));
            return;
        }
        if (frame == 0x43) actor.rotation.y = side == 0 ? 0xc00 : kSideYaw;
        return;
    }
    case StrikeStep::WalkOff: // exe 0x4cc913
        integrateVelocity(actor, 0, 0, kWalkSpeed);
        if (clampAtEnd(actor, giant)) {
            actor.subState = uint8_t(GiantSub::Walk);
            actor.step = uint8_t(WalkStep::TurnWait);
        }
        giant.flags &= uint8_t(~kGiantHold);
        return;
    default: return;
    }
}

// its hit list: spheres on its bones (bone, offset, shape: group 2, never locked on, the radius / 4) ...
struct Sphere {
    uint8_t bone;
    int16_t v[3];
    uint32_t shape;
};
constexpr Sphere kSpheres[11] = {
    {0, {0, 0, 0}, 0x48004b},        {1, {0, -0x30, 0}, 0x400025},    {2, {0, -0x60, 0}, 0x480057},
    {2, {0, -0x90, 0}, 0x480057},    {3, {0, 0x10, -0x40}, 0x480032}, {8, {0, 0x70, 0}, 0x480032},
    {0xb, {0, 0x70, 0}, 0x480032},   {0xe, {0, 0x40, 0}, 0x480025},   {0xe, {0, 0x40, -0x60}, 0x480025},
    {0x11, {0, 0x40, 0}, 0x480025}, {0x11, {0, 0x40, -0x60}, 0x480025},
};
// ... and capsules between bone pairs (radius 0x14)
constexpr uint8_t kCapsules[8][2] = {{6, 7}, {7, 8}, {9, 0xa}, {0xa, 0xb}, {0xc, 0xd}, {0xd, 0xe}, {0xf, 0x10}, {0x10, 0x11}};
constexpr uint16_t kCapsuleRadius = 0x14;

// exe 0x4cbb70: the active state: hurt (a flash and a sound), its sub-state, its hit list
void active(AnimatedActor& actor, GiantFields& giant) {
    const AnimatedActorStats stats = animatedStats(actor.type);
    if (giant.hitBox.result & kHitAttackByPlayer) {
        actor.tint = kTintFlash;
        soundPlayAtPos(kHurtSound, actor.pos);
    }
    switch (GiantSub(actor.subState)) { // [0x8950c0]
    case GiantSub::Walk: walk(actor, giant); break;
    case GiantSub::Strike: strike(actor, giant); break;
    default: notPortedAt(kStoryPort, "0x4cbb70 sub-state (table 0x8950c0)", actor.subState);
    }
    const uint32_t attack = uint32_t(stats.attack) | kHitAttackSolid;
    giant.hitBox.previousX = actor.prevPos.x.whole;
    giant.hitBox.previousZ = actor.prevPos.z.whole;
    giant.hitBox.result = 0;
    const uint32_t owner = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight);
    int16_t out[3];
    for (const Sphere& s : kSpheres) {
        bonePointOffset(actor, s.bone, s.v, out);                     // exe 0x5e1970
        hitListSubmit(out, giant.hitBox, owner | s.shape, attack); // exe 0x5e4120
    }
    // the capsules: both ends' bone points computed per capsule (exe 0x5e1970 has no side effect beyond its output)
    static constexpr int16_t kZero[3] = {0, 0, 0};
    const uint32_t capsuleOwner = owner | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(1);
    for (const auto& c : kCapsules) {
        int16_t p0[3], p1[3];
        bonePointOffset(actor, c[0], kZero, p0);
        bonePointOffset(actor, c[1], kZero, p1);
        hitListSubmitCapsule(p0, kCapsuleRadius, p1, kCapsuleRadius, giant.hitBox, capsuleOwner, attack); // exe 0x5e4190
    }
}

void windowGiant(AnimatedActor& actor) { // exe 0x4cb980
    GiantFields& giant = classState<GiantFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (GiantState(actor.state)) { // [0x8950b4]
    case GiantState::Bind: bind(actor, giant); break;
    case GiantState::Active: active(actor, giant); break;
    case GiantState::Free: freeActor(actor); break; // 0x468fa0
    case GiantState::Walk: walk(actor, giant); break;
    case GiantState::Strike: strike(actor, giant); break;
    default: notPortedAt(kStoryPort, "0x4cb980 state (table 0x8950b4)", actor.state); return;
    }
    if (actor.anim == actor.prevAnim) {
        if (giant.flags & kGiantHold) animDecodeCurrent(actor); // exe 0x5e1f30
        else animStep(actor);                                  // exe 0x5e1ea0
    } else {
        animSet(actor, actor.anim, giant.animEntry); // exe 0x5e1c50
    }
    animPose(actor); // exe 0x5e2d90
}

} // namespace

void windowGiantUpdate(const PlayerEnv&, uint32_t actor) { windowGiant(animatedActorAt(actor)); }

} // namespace od::port
