// The gold-statue boss's shots (ST3C pool 0x40 type 0x1f, exe 0x5cb200; hypothesis names). Per sub-type state tables
// [0x8d1630 + sub * 4].
//  sub 0, attack 3's homing shot (the exe keeps its block (+0xbc) in the global 0xa5a938): 0 start 0x5cb250 (its
//  model, speed -0x680, a trail), 1 run 0x5cb2f0 -- once launched a hit or a blocked move ends it -- its sub-state
//  [0x8d1624] 0x5cb3b0: rising 0xf ticks, then homing on the player (its turn / pitch steps) for up to 0x3c ticks
//  (from 0x14 on, within 0x400 of the player it stops homing), then 0xb4 ticks straight, then the end; its hit entry
//  0x5cba80 every tick; 2 end 0x5cb340 (a blast, freed).
//  sub 1, phase one's thrown pieces (the boss spawns them with itself and their side; the exe keeps the block in the
//  global 0xa5a934): 0 start 0x5cb560 (hidden, the electric ring), 1 run 0x5cb610 by sub-state [0x8d1628]: 0 0x5cb6c0
//  held at the boss's bone 2 / 3 (the ring growing to 0xa0) until the boss releases it, then aimed at the player; 1
//  0x5cb8e0 homing 0x60 ticks, rolling with its turn, shrinking from 0x80, the end after 0x90; then 0x5cbb90 moves the
//  ring onto it; a hit or a blocked move ends it, else its hit entry 0x5cbb10; 2 end 0x5cb690 (the ring and itself
//  freed).
// Deviation: exe 0x5cb3b0's yaw at the launch is taken from an uninitialised local point (stack garbage; (0, 0, 0)
// here).
#include "content/saul_kada/st3c_boss_shot.hpp"
#include "content/saul_kada/st3c_boss_shot_state.hpp"

#include "content/saul_kada/st3c_gold_piece.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/missile_trail.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st3c_boss_shot_state;

namespace {

enum class ShotState : uint8_t { Start = 0, Run = 1, End = 2 };
enum class HomingStep : uint8_t { Start = 0, Rise = 1, Home = 2, Straight = 3 };
enum class PieceSub : uint8_t { Held = 0, Homing = 1 };
// the exe's globals holding the running shot's / piece's block
// the class block's offset in the projectile slot: the globals keep the block's exe address (a value; never read through)
constexpr uint32_t kClassBlockWord = 0xbc;
constexpr int16_t kHomingSpeed = int16_t(0xf980);
constexpr uint8_t kTrailVariant = 2;
constexpr uint16_t kRingFull = 0xa0;

void moveForward(ProjectileActor& actor, int z) { moveAlongRotation(actor.pos, actor.rotation, 0, 0, z); } // exe 0x5e37b0
// the player's position 0x80 higher (the exe's stack copy)
void playerAim(int32_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x;
    out[1] = int32_t(uint32_t(p.pos.y) + 0xff800000u);
    out[2] = p.pos.z;
}
void positionValues(const ProjectileActor& actor, int32_t out[3]) {
    out[0] = actor.pos.x;
    out[1] = actor.pos.y;
    out[2] = actor.pos.z;
}
// exe 0x5cbc00(actor, angle, step): its pitch turned toward the angle by step (snapped within)
void pitchTo(ProjectileActor& actor, int angle, int step) {
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(angle)); // exe 0x5e3d80
    const int t = step < 0 ? -step : step;
    if (uint32_t(t + d) <= uint32_t(t + t)) {
        if (step > 0) actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + uint32_t(d)));
        else actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) - uint32_t(d)));
        return;
    }
    if (d > t) actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + uint32_t(step)));
    if (d < -t) actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) - uint32_t(step)));
}
// the homing of exe 0x5cb3b0 / 0x5cb8e0: its yaw toward the aim (its turn step), its pitch toward it (its pitch step)
void homeOn(ProjectileActor& actor, const int32_t aim[3]) {
    turnYawTo(actor.rotation.y, uint32_t(angleOf(aim[0], aim[2], actor.pos.x, actor.pos.z)), actor.spin.y); // 0x5e3df0
    int32_t at[3];
    positionValues(actor, at);
    pitchTo(actor, pitchOf(at, aim) + 0x800, actor.spin.x); // exe 0x5e3ca0 / 0x5cbc00
}
// a hit entry at its position (through the projection scratch's point)
void submitAt(const ProjectileActor& actor, HitBoxHead& box, uint32_t shape, uint32_t attack) {
    LaneVec4& point = projectionScratch().point;
    point.x = actor.pos.x.whole;
    point.y = actor.pos.y.whole;
    point.z = actor.pos.z.whole;
    const int16_t at[3] = {point.x, point.y, point.z};
    hitListSubmit(at, box, shape, attack); // exe 0x5e4120
}

// ---- sub 0: the homing shot ----------------------------------------------------------------------------------------
void homingRun(const PlayerEnv& env, ProjectileActor& actor, HomingFields& shot);
// exe 0x5cb250: its model, its speed, its spins, a trail
void homingStart(const PlayerEnv& env, ProjectileActor& actor, HomingFields& shot) {
    bossShotGlobals().homingBlock = slotIdentityWord(actor) + kClassBlockWord;
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1500
        return;
    }
    actor.flags |= kActorDrawn;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = kHomingSpeed;
    actor.rotation.x = int16_t(0xfc00);
    actor.rotation.y = 0;
    actor.rotation.z = 0;
    actor.spin.setXY(0x54, 0x54);
    actor.collisionBox = nullptr;
    shot.clearBytes(); // exe 0x40dc40
    if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300: the trail (the global static type 6, variant 2)
        StaticActor& trail = staticActorAt(slot);
        trail.flags = kActorLive;
        trail.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
        trail.type = typeByte(GlobalStaticType::MissileTrail);
        trail.variant = kTrailVariant;
        classState<MissileTrailFields>(staticActorAt(slot)).parent = actor.at;
    }
    actor.enterState(uint8_t(ShotState::Run));
    homingRun(env, actor, shot);
}
// exe 0x5cb3b0 (its steps [0x5cb54c])
void homingFlight(ProjectileActor& actor, HomingFields& shot) {
    actor.rotation.z = int16_t(uint16_t(uint16_t(actor.rotation.z) + 0xf3u));
    switch (HomingStep(actor.step)) {
    case HomingStep::Start:
        shot.timer = 0xf;
        ++actor.step;
        return;
    case HomingStep::Rise: {
        moveForward(actor, actor.velocity.z);
        shot.timer -= 1u;
        if (shot.timer != 0) return;
        shot.timer = 0;
        shot.launched = 1;
        // the yaw from an uninitialised local point of the exe (stack garbage): (0, 0, 0) here
        actor.rotation.y = int16_t(angleOf(0, 0, actor.pos.x, actor.pos.z));
        ++actor.step;
        return;
    }
    case HomingStep::Home: {
        int32_t aim[3];
        playerAim(aim);
        homeOn(actor, aim);
        moveForward(actor, actor.velocity.z);
        shot.timer += 1u;
        const uint16_t ticks = shot.timer;
        if (ticks == 0x3c) {
            shot.timer = 0xb4;
            ++actor.step;
            return;
        }
        if (ticks < 0x14) return;
        if (uint32_t(distance3(player().pos, actor.pos)) >= 0x400u) return; // exe 0x5e3a10
        shot.timer = 0xb4;
        ++actor.step;
        return;
    }
    case HomingStep::Straight:
        moveForward(actor, actor.velocity.z);
        shot.timer -= 1u;
        if (shot.timer == 0) actor.enterState(uint8_t(ShotState::End));
        return;
    default: return;
    }
}
// exe 0x5cba80: its hit entry (its x / z as the box's previous ones)
void homingHit(const ProjectileActor& actor, HomingFields& shot) {
    bossShotGlobals().homingBlock = slotIdentityWord(actor) + kClassBlockWord;
    shot.hitBox.previousX = actor.pos.x.whole;
    shot.hitBox.previousZ = actor.pos.z.whole;
    shot.hitBox.result = 0;
    submitAt(actor, shot.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupHeavyThrown) | hitRadius(0x20),
             uint32_t(int32_t(BossShotParams(actor).attack)) | kHitAttackStrong | kHitAttackThrown |
                 kHitAttackSolid);
}
// exe 0x5cb2f0
void homingRun(const PlayerEnv& env, ProjectileActor& actor, HomingFields& shot) {
    bossShotGlobals().homingBlock = slotIdentityWord(actor) + kClassBlockWord;
    if (shot.launched != 0 && (shot.hitBox.result != 0 || actorTestMove(env, actor) != 0)) { // exe 0x40f570
        actor.enterState(uint8_t(ShotState::End));
        return;
    }
    switch (actor.subState) {
    case 0: homingFlight(actor, shot); break;
    // [0x8d1624] + 1 / + 2 are the pieces' sub-states (a shared table); nothing sets them for the homing shot
    default: notPortedAt(kStoryPort, "0x5cb2f0 sub-state (table 0x8d1624)", actor.subState);
    }
    homingHit(actor, shot);
}
// exe 0x5cb340: a blast (variant 1, its blast attack), freed
void homingEnd(ProjectileActor& actor, HomingFields& shot) {
    bossShotGlobals().homingBlock = slotIdentityWord(actor) + kClassBlockWord;
    if (const uint32_t slot = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(slot);
        BlastParams params(actorHeaderAt(slot));
        blast.subtype = 0;
        blast.spawnByte = 0;
        blast.flags = kActorLive;
        blast.loopFlags |= kActorSweepGroup2;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 1;
        blast.pos.x = actor.pos.x;
        blast.pos.y = actor.pos.y;
        params.size = 0x100;
        blast.pos.z = actor.pos.z;
        params.attack = uint16_t(int32_t(BossShotParams(actor).blastAttack.get()));
    }
    freeActor(actor); // exe 0x5e1500
}

// ---- sub 1: the thrown pieces ------------------------------------------------------------------------------------
// exe 0x5cb6c0: held at the boss's bone (the ring growing) until released, then aimed at the player
void pieceHeld(ProjectileActor& actor, PieceFields& piece) {
    const BossShotParams params(actor);
    if (params.released != 0) { // released by the boss
        int32_t aim[3];
        playerAim(aim);
        int angle;
        if (params.side != 0) {
            actor.rotation.x = int16_t(0xfe00);
            piece.ringAngles.x = int16_t(0xfe00);
            angle = angleOf(aim[0], aim[2], actor.pos.x, actor.pos.z) - 0x400;
        } else {
            actor.rotation.x = 0x200;
            piece.ringAngles.x = 0x200;
            angle = angleOf(aim[0], aim[2], actor.pos.x, actor.pos.z) + 0x400;
        }
        actor.rotation.y = int16_t(angle);
        piece.ringAngles.z = 0;
        int32_t at[3];
        positionValues(actor, at);
        actor.rotation.x = int16_t(pitchOf(at, aim) + 0x800); // exe 0x5e3ca0
        storeRotationYXZ(piece.ringAngles.x, piece.ringAngles.y, piece.ringAngles.z, actor.rootMatrix,
                         piece.rootPad); // exe 0x40bfd0
        piece.ringSize = kRingFull;
        actor.subState = uint8_t(PieceSub::Homing);
        actor.restartStep();
        return;
    }
    if (piece.ringSize < kRingFull) {
        piece.ringSize += 3u;
        if (piece.ringSize > kRingFull) piece.ringSize = kRingFull;
    }
    const AnimatedActor& boss = *slotByIdentityWord<AnimatedActor>(piece.boss);
    const uint32_t bone = params.side != 0 ? 2 : 3;
    const ShortVec4* point = bonePointOrNull(boss, bone);              // exe 0x5e1810
    const BoneBlock* block = boneBlockOf(boss, int(bone / kBonesPerBlock)); // exe 0x5e17d0
    if (!block) notPortedAt(kStoryPort, "0x5cb6c0: exe 0x5e17d0 returned null (the exe copies from 0), bone", bone);
    actor.copyRootWordsFrom(*block, bone % kBonesPerBlock);
    ProjectionScratch& scratch = projectionScratch();
    scratch.point.x = int16_t(bone == 2 ? 0xff60 : 0xa0);
    scratch.point.y = 0;
    scratch.point.z = 0;
    scratch.transformed[2] = 0x8000;
    scratch.transformed[1] = 0x8000;
    scratch.transformed[0] = 0x8000;
    const int32_t halves[3] = {scratch.transformed[0], scratch.transformed[1], scratch.transformed[2]};
    actor.rootMatrix = scaleColumns(actor.rootMatrix, halves); // exe 0x40c0e0
    const int16_t offset[3] = {scratch.point.x, scratch.point.y, scratch.point.z};
    int16_t turned[3];
    transformShortVector(actor.rootMatrix, offset, turned); // exe 0x40bb30
    scratch.rotatedPoint.x = turned[0];
    scratch.rotatedPoint.y = turned[1];
    scratch.rotatedPoint.z = turned[2];
    if (!point) notPortedAt(kStoryPort, "0x5cb6c0: exe 0x5e1810 returned null (the exe faults), bone", bone);
    const ShortVec4& at = *point;
    actor.pos.x = int32_t(uint32_t(int32_t(at.x) + scratch.rotatedPoint.x) << 16);
    actor.pos.y = int32_t(uint32_t(int32_t(at.y) + scratch.rotatedPoint.y) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(at.z) + scratch.rotatedPoint.z) << 16);
}
// exe 0x5cb8e0: homing 0x60 ticks, rolling with its turn, the ring shrinking from 0x80, the end after 0x90
void pieceHoming(ProjectileActor& actor, PieceFields& piece) {
    int32_t aim[3];
    playerAim(aim);
    switch (actor.step) {
    case 0:
        piece.timer = 0;
        actor.spin.x = 0x20;
        actor.spin.y = 0x20;
        actor.velocity.z = int16_t(0xfa00);
        ++actor.step;
        [[fallthrough]];
    case 1: break;
    default: return;
    }
    piece.timer += 1u;
    if (piece.timer < 0x60) homeOn(actor, aim);
    piece.ringAngles.x = actor.rotation.x;
    const int d = angleDiff(uint32_t(int32_t(actor.prevRotation.y)), uint32_t(int32_t(actor.rotation.y))); // 0x5e3d80
    if (d > 0) {
        piece.ringAngles.z = int16_t(uint16_t(uint16_t(piece.ringAngles.z) + 0x10u));
    } else if (d < 0) {
        piece.ringAngles.z = int16_t(uint16_t(uint16_t(piece.ringAngles.z) - 0x10u));
    } else {
        const int16_t roll = piece.ringAngles.z;
        if (roll > 0) piece.ringAngles.z = int16_t(roll - 0x40);
        else if (roll < 0) piece.ringAngles.z = int16_t(roll + 0x40);
    }
    if (piece.ringAngles.z > 0x100) piece.ringAngles.z = 0x100;
    else if (piece.ringAngles.z < -0x100) piece.ringAngles.z = int16_t(0xff00);
    storeRotationYXZ(piece.ringAngles.x, piece.ringAngles.y, piece.ringAngles.z, actor.rootMatrix,
                     piece.rootPad); // exe 0x40bfd0
    moveForward(actor, actor.velocity.z); // exe 0x5e37b0
    const uint16_t ticks = piece.timer;
    if (ticks <= 0x80) return;
    if (ticks > 0x90) {
        piece.ringSize = 0;
        actor.enterState(uint8_t(ShotState::End));
        return;
    }
    piece.ringSize += 0xfff6u;
}
// exe 0x5cbb90: the ring onto it (its position, its rotation from the ring's angles, its matrix, its size)
void ringFollow(const ProjectileActor& actor, const PieceFields& piece) {
    const uint32_t ringSlot = piece.ring;
    if (!ringSlot) return;
    StaticActor& ring = staticActorAt(ringSlot);
    ring.pos = actor.pos;
    ring.rotation = piece.ringAngles;
    ring.setRootWords(piece.rootWords.data());
    ElectricRingParams(staticActorAt(ringSlot)).size = int16_t(piece.ringSize);
}
// exe 0x5cbb10: its hit entry
void pieceHit(const ProjectileActor& actor, PieceFields& piece) {
    piece.hitBox.previousX = actor.prevPos.x.whole;
    piece.hitBox.previousZ = actor.prevPos.z.whole;
    piece.hitBox.result = 0;
    submitAt(actor, piece.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupHeavyThrown) | hitRadius(0x64),
             uint32_t(int32_t(BossShotParams(actor).attack)) | kHitAttackHarmless | kHitAttackStrong |
                 kHitAttackThrown | kHitAttackSolid);
}
// exe 0x5cb610
void pieceRun(const PlayerEnv& env, ProjectileActor& actor, PieceFields& piece) {
    bossShotGlobals().pieceBlock = slotIdentityWord(actor) + kClassBlockWord;
    switch (PieceSub(actor.subState)) {
    case PieceSub::Held: pieceHeld(actor, piece); break;
    case PieceSub::Homing: pieceHoming(actor, piece); break;
    default: notPortedAt(kStoryPort, "0x5cb610 sub-state (table 0x8d1628)", actor.subState);
    }
    ringFollow(actor, piece);
    if (piece.hitBox.result != 0) {
        actor.enterState(uint8_t(ShotState::End));
        return;
    }
    if (actorTestMove(env, actor) != 0) { // exe 0x40f570
        actor.enterState(uint8_t(ShotState::End));
        return;
    }
    if (ShotState(actor.state) != ShotState::End) pieceHit(actor, piece);
    else piece.hitBox.result = 0;
}
// exe 0x5cb560: hidden, its electric ring
void pieceStart(const PlayerEnv& env, ProjectileActor& actor, PieceFields& piece) {
    bossShotGlobals().pieceBlock = slotIdentityWord(actor) + kClassBlockWord;
    actor.setDrawn(false);
    actor.velocity.setZSpare(0, 0);
    actor.velocity.setXY(0, 0);
    actor.collisionBox = nullptr;
    piece.clearBytes(); // exe 0x40dc40
    const uint32_t ringSlot = allocActor60(kActorStageClass); // exe 0x5e12f0
    piece.ring = ringSlot;
    if (ringSlot) {
        StaticActor& ring = staticActorAt(ringSlot);
        ring.flags = kActorLive;
        ring.type = kGoldPieceType;
        ring.subtype = kElectricRingSubtype;
        ring.variant = 0;
        ring.spawnByte = 0;
        ring.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    }
    actor.enterState(uint8_t(ShotState::Run));
    pieceRun(env, actor, piece);
}
// exe 0x5cb690: the ring and itself freed
void pieceEnd(ProjectileActor& actor, PieceFields& piece) {
    bossShotGlobals().pieceBlock = slotIdentityWord(actor) + kClassBlockWord;
    if (const uint32_t ringSlot = piece.ring) freeActor(staticActorAt(ringSlot)); // exe 0x5e1540
    freeActor(actor);                                                           // exe 0x5e1500
}

void bossShot(const PlayerEnv& env, ProjectileActor& actor) { // exe 0x5cb200
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    const uint32_t sub = actor.subtype, state = actor.state;
    if (sub == kHomingShotSubtype) {
        HomingFields& shot = classState<HomingFields>(actor);
        switch (ShotState(state)) {
        case ShotState::Start: homingStart(env, actor, shot); return;
        case ShotState::Run: homingRun(env, actor, shot); return;
        case ShotState::End: homingEnd(actor, shot); return;
        default: break;
        }
    } else if (sub == kThrownPieceSubtype) {
        PieceFields& piece = classState<PieceFields>(actor);
        switch (ShotState(state)) {
        case ShotState::Start: pieceStart(env, actor, piece); return;
        case ShotState::Run: pieceRun(env, actor, piece); return;
        case ShotState::End: pieceEnd(actor, piece); return;
        default: break;
        }
    }
    notPortedAt(kStoryPort, "0x5cb200 sub << 8 | state (table 0x8d1630)", sub << 8 | state);
}

} // namespace

void bossShotUpdate(const PlayerEnv& env, uint32_t actor) { bossShot(env, projectileActorAt(actor)); }

} // namespace od::port
