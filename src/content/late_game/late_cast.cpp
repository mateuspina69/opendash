// Late-game cutscene cast of ST4B (script 0x59, exe 0x533b20) and ST54 (script 0x6f, exe 0x53c3f0). See
// content/late_game/late_cast.hpp. Names are hypotheses from the observed behaviour.
//
// Every class here: [state table + state * 4](actor, the script block); state 0 binds the model (none: freed, exe
// 0x5e1490), state 1 runs the variant / action routine, state 2 frees (exe 0x468fa0). The exe tables continue
// straight into the variant / action tables, so states 3.. are those routines directly.
//
// ST4B (script 0x59; the camera sequence 0x8aa6e0 spawns them with op 0x40; the scene is the step's sub, the cast
// timeline its tick). Tails: the animation (live: the animation changed -> exe 0x5e1c30, else exe 0x5e1e80 while the
// class's stepping byte is set), the shadow (exe 0x5f20f0: render output, not drawn by the port), the texture tracks
// (exe 0x5e34f0). Face rows exe 0x4fb5d0(actor, t0, t1): {u16 tick (0xffff end), u8 seq, u8 key}, an exact match
// with the cast timeline's tick selects track 0 / 1.
//   0x45c570 (states 0x88f5e8, variants 0x88f5f4 = 0x45c720 / 0x45c780)   0x4660d0 (states 0x88fe34: run 0x4daf20)
//   0x49ba80 (states 0x893740: run 0x49bc10 by the scene, rows 0x49bdf0)  0x4bc280 (states 0x8949d0, variants
//   0x8949dc = 0x4bc460 / 0x4bc750)   0x4dadc0 (states 0x89575c, variants 0x895768 = 0x52ef00 / 0x4daf20)
//   0x4efe50 (states 0x896844: run 0x4effd0)   0x4f43f0 (states 0x896ff8, variants 0x897004 = 0x4f45a0 / 0x4f4600)
//   0x4fb3e0 (states 0x897a08, variants 0x897a14 = 0x4fb590 / 0x52ef00)
//   0x43c110 (pool 0xa0, [0x88d878 + variant * 4]): the table of 0x43bff0 from its entry 2 -- content/yosyonke/st0a_fx
//   (stageEffectFromVariant2Update).
//
// ST54 (script 0x6f; camera 0x8ade10, steps 0x8ae078): the steps write the cast's sub-state (the action) and step (its
// phase); every action routine runs [action table + action * 4](actor, the block). Rows exe 0x479090(actor, t0, t1) as
// 0x4fb5d0 with the row indices of the class. Tails (no live gate in the exe): the animation changed -> exe 0x5e1c50
// (entry 0, or the class's entry for 0x4ea6e0), else exe 0x5e1ea0 (or exe 0x5e1f30 for 0x45d4f0 while it holds the
// frame); the pose exe 0x5e2d90; the shadow exe 0x5f20f0 (render output); the texture tracks exe 0x5e34f0.
//   0x44bac0 (states 0x88e84c, actions 0x88e858)   0x45d4f0 (0x88f938 / 0x88f944)   0x4661f0 (0x88fe80 / 0x88fe8c)
//   0x478ea0 (0x890f8c / 0x890f98)   0x47e0a0 (0x891304 / 0x891310)   0x4ea6e0 (0x896544 / 0x896550)
//   0x4f6b70 (0x897380 / 0x89738c: actions 0..2 ported, the ones script 0x6f writes)
//
// Deviation (ST54 tails): the exe runs the animation / pose / spark tail even on the tick the actor was freed (state 2,
// or a failed model bind in state 0); the port skips the tail once the live bit is clear (the freed actor's bone
// blocks are released; nothing reads them before they are reallocated and rewritten).
#include "content/late_game/late_cast.hpp"
#include "content/late_game/late_cast_state.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/skid_sparks.hpp"
#include "game/spark_emitter.hpp"
#include "game/spark_emitter_state.hpp"
#include "game/state_array.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace late_cast_state;

namespace {

int32_t tick() { return scriptState().stepTick; }
uint8_t scene() { return scriptState().stepSub; }
uint32_t timelineTick() { return uint32_t(scriptState().part<CastTimelineTick>().stepTick); }
int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

enum class CastState : uint8_t { Bind = 0, Run = 1, Free = 2 };
constexpr uint8_t kFirstDirect = 3; // the first state that is a variant / action routine itself

// one face / texture row: the timeline tick it fires at (0xffff: the end), the sequence and the key
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
// exe 0x4fb5d0 / 0x479090 / 0x49bdf0 (actor, t0, t1): the two row tables of the tracks: a row whose tick equals the
// cast timeline's selects its sequence on track 0 / 1 and its index advances
void rows(KeyframeTracks& tracks, StateArray<uint8_t, 2>& index, uint32_t t0, uint32_t t1) {
    const FaceRow row[2] = {dataRecord<FaceRow>(t0 + uint32_t(index[0]) * 4, FaceRow::kBytes), dataRecord<FaceRow>(t1 + uint32_t(index[1]) * 4, FaceRow::kBytes)};
    for (uint32_t i = 0; i < 2; ++i) {
        const FaceRow& r = row[i];
        const uint16_t k = r.tick;
        if (k == 0xffff || uint32_t(k) != timelineTick()) continue;
        if (i) tracks.second.select(r.sequence, r.key); // exe 0x5e33f0
        else tracks.first.select(r.sequence, r.key);    // exe 0x5e33d0
        index[i] += 1u;
    }
}

// ---- ST4B common ---------------------------------------------------------------------------------------------------

void faceRows(St4bCastFields& cast, uint32_t t0, uint32_t t1) { rows(cast.tracks, cast.rows, t0, t1); } // 0x4fb5d0
void sceneRows(St4bCastFields& cast, uint32_t t0, uint32_t t1) {                                       // 0x49bdf0
    if (timelineTick() == 0) cast.clearRows();
    rows(cast.tracks, cast.rows, t0, t1);
}
// the copies at the top of every ST4B / ST54 class (the tint neutral)
void castHead(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
}
void animTail(AnimatedActor& actor, bool stepping) { // the ST4B animation tail (live only)
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else if (stepping) animStepPose(actor);                             // exe 0x5e1e80
}
// the common part of the ST4B binds (after the animation and exe 0x5e1c30): the velocities (z `vz`), rotation x / z,
// the contact bytes, the platform, the actor hit, the collision box
void clearBind(AnimatedActor& actor, int16_t vz) {
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = vz;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.collisionBox = nullptr;
}
// the bind of 0x45c630 / 0x49bb50 / 0x4f44b0 / 0x4fb4b0: the model (by the spawn byte when `bySpawnByte`, else variant
// 0), drawn, the animation, the clears, the word / stepping / rows cleared, the tracks on sequence 0 / 0, state 1
bool bindTalker(AnimatedActor& actor, St4bCastFields& cast, bool bySpawnByte, uint8_t anim, int16_t vz, uint32_t t0,
                uint32_t t1) {
    if (!bindActorModel(actor, bySpawnByte ? actor.spawnByte : 0)) { // exe 0x5e0bc0 / 0x5e0ba0
        freeActor(actor);                                            // exe 0x5e1490
        return false;
    }
    actor.flags |= kActorDrawn;
    actor.anim = anim;
    actor.prevAnim = kNoAnimShown;
    animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    clearBind(actor, vz);
    cast.word = 0;
    cast.stepping = 0;
    cast.rows[0] = 0;
    cast.rows[1] = 0;
    cast.tracks.init(t0, t1); // exe 0x5e33a0
    cast.tracks.first.select(0, 0);
    cast.tracks.second.select(0, 0);
    actor.enterStateBytes(uint8_t(CastState::Run));
    actor.clearStepBytes();
    return true;
}
// the bind of 0x466170 / 0x4dae60: no tracks; the word / stepping cleared, state 1 (the exe does not run state 1)
bool bindIdle(AnimatedActor& actor, IdleCastFields& cast, uint8_t anim) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return false;
    }
    actor.flags |= kActorDrawn;
    actor.anim = anim;
    actor.prevAnim = kNoAnimShown;
    animSetPose(actor, anim, 0); // exe 0x5e1c30
    clearBind(actor, 0);
    cast.word = 0;
    cast.stepping = 0;
    actor.subState = 0;
    actor.clearStepBytes();
    actor.state = uint8_t(CastState::Run);
    return true;
}

// ---- the cued talker A (exe 0x45c570, ST4B type 9) -----------------------------------------------------------------
void talkerAFirst(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x45c720 (variant 0)
    if (tick() == 0x11d) {
        cast.stepping = 1;
        actor.subState = 1;
    } else if (tick() == 800) {
        actor.anim = 2;
    }
    faceRows(cast, stateAddress(TableId::LateCastTalkerAFirstRows), stateAddress(TableId::LateCastTalkerAFirstRowsSecond));
}
void talkerASecond(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x45c780 (variant 1)
    switch (tick()) {
    case 0xd8: cast.stepping = 1; break;
    case 0x1bf: actor.anim = 4; break;
    case 0x2cb: actor.anim = 5; break;
    case 0x41d: actor.anim = 6; break;
    case 0x53b: actor.anim = 7; break;
    default: break;
    }
    faceRows(cast, stateAddress(TableId::LateCastTalkerASecondRows), stateAddress(TableId::LateCastTalkerASecondRowsSecond));
}
void talkerARun(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x45c700: [0x88f5f4]
    switch (actor.variant) {
    case 0: talkerAFirst(actor, cast); return;
    case 1: talkerASecond(actor, cast); return;
    default: notPortedAt(kStoryPort, "0x45c570 variant (table 0x88f5f4)", actor.variant);
    }
}
void cuedTalkerA(AnimatedActor& actor) {
    St4bCastFields& cast = classState<St4bCastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x88f5e8]
    case 0: {              // exe 0x45c630
        uint8_t anim = actor.anim;
        if (actor.variant == 0) anim = 1;
        else if (actor.variant == 1) anim = 3;
        if (!bindTalker(actor, cast, true, anim, 0, stateAddress(TableId::LateCastCuedTalkerAFirstTracks), stateAddress(TableId::LateCastCuedTalkerASecondTracks))) break;
        talkerARun(actor, cast);
        break;
    }
    case 1: talkerARun(actor, cast); break;
    case 2: freeActor(actor); break; // exe 0x468fa0
    case 3: talkerAFirst(actor, cast); break;
    case 4: talkerASecond(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x45c570 state (table 0x88f5e8)", actor.state);
    }
    animTail(actor, cast.stepping != 0);
    textureTracksTick(actor, cast.tracks); // exe 0x5e34f0
}

// ---- the late steppers (exe 0x4660d0, ST4B type 0xb; exe 0x4dadc0, ST4B type 0x50) ---------------------------------
void stepFromLateTick(IdleCastFields& cast) { // exe 0x4daf20: from tick 0x53b the animation steps
    if (tick() == 0x53b) cast.stepping = 1;
}
void lateStepper(AnimatedActor& actor) {
    IdleCastFields& cast = classState<IdleCastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x88fe34]
    case 0: bindIdle(actor, cast, 1); break; // exe 0x466170
    case 1: stepFromLateTick(cast); break;
    case 2: freeActor(actor); break;
    default: notPortedAt(kStoryPort, "0x4660d0 state (table 0x88fe34)", actor.state);
    }
    animTail(actor, cast.stepping != 0);
}
void idleOrStepperRun(const AnimatedActor& actor, IdleCastFields& cast) { // exe 0x4daf00: [0x895768]
    switch (actor.variant) {
    case 0: return; // exe 0x52ef00
    case 1: stepFromLateTick(cast); return;
    default: notPortedAt(kStoryPort, "0x4dadc0 variant (table 0x895768)", actor.variant);
    }
}
void idleOrStepper(AnimatedActor& actor) {
    IdleCastFields& cast = classState<IdleCastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x89575c]
    case 0: bindIdle(actor, cast, actor.variant != 0 ? 2 : 1); break; // exe 0x4dae60
    case 1: idleOrStepperRun(actor, cast); break;
    case 2: freeActor(actor); break;
    case 3: break; // exe 0x52ef00
    case 4: stepFromLateTick(cast); break;
    default: notPortedAt(kStoryPort, "0x4dadc0 state (table 0x89575c)", actor.state);
    }
    animTail(actor, cast.stepping != 0);
}

// ---- the scene talker (exe 0x49ba80, ST4B type 0x2f) ---------------------------------------------------------------
void sceneTalkerRun(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x49bc10: by the scene (3 and > 8: nothing)
    const int32_t t = tick();
    switch (scene()) {
    case 0:
        if (t == 0x85) cast.stepping = 1;
        sceneRows(cast, stateAddress(TableId::LateCastSceneTalkerScene0First), stateAddress(TableId::LateCastSceneTalkerScene0Second));
        return;
    case 1:
        if (t == 0xeb) actor.anim = 2;
        sceneRows(cast, stateAddress(TableId::LateCastSceneTalkerScene1First), stateAddress(TableId::LateCastSceneTalkerScene1Second));
        return;
    case 2:
        if (t == 0x314) actor.anim = 3;
        sceneRows(cast, stateAddress(TableId::LateCastSceneTalkerScene2First), stateAddress(TableId::LateCastSceneTalkerScene2Second));
        return;
    case 4:
        if (t == 10) actor.anim = 4;
        else if (t == 0x22e) actor.anim = 5;
        else if (t == 0x248) actor.anim = 6;
        sceneRows(cast, stateAddress(TableId::LateCastSceneTalkerScene4First), stateAddress(TableId::LateCastSceneTalkerScene4Second));
        return;
    case 5:
        if (t == 10) actor.anim = 7;
        return;
    case 6:
        if (t == 10) actor.anim = 8;
        else if (t == 0xfd) actor.anim = 9;
        sceneRows(cast, stateAddress(TableId::LateCastSceneTalkerScene6First), stateAddress(TableId::LateCastSceneTalkerScene6Second));
        return;
    case 7:
        if (t == 5) actor.anim = 10;
        sceneRows(cast, stateAddress(TableId::LateCastSceneTalkerScene7First), stateAddress(TableId::LateCastSceneTalkerScene7Second));
        return;
    case 8:
        if (t == 0x33d) actor.anim = 0xb;
        sceneRows(cast, stateAddress(TableId::LateCastSceneTalkerScene8First), stateAddress(TableId::LateCastSceneTalkerScene8Second));
        return;
    default: return;
    }
}
void sceneTalker(AnimatedActor& actor) {
    St4bCastFields& cast = classState<St4bCastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x893740]
    case 0:                // exe 0x49bb50
        if (!bindTalker(actor, cast, false, 1, int16_t(0xff00), stateAddress(TableId::LateCastSceneTalkerFirstTracks), stateAddress(TableId::LateCastSceneTalkerSecondTracks))) break;
        sceneTalkerRun(actor, cast);
        break;
    case 1: sceneTalkerRun(actor, cast); break;
    case 2: freeActor(actor); break;
    default: notPortedAt(kStoryPort, "0x49ba80 state (table 0x893740)", actor.state);
    }
    animTail(actor, cast.stepping != 0);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- the flyer (exe 0x4bc280, ST4B type 0x3f sub 0) ----------------------------------------------------------------
enum class FlightStep : uint8_t { Waypoints = 0, Coast = 1, Drift = 2 };
constexpr uint16_t kFlyerDimTint = 0x2108;
// One waypoint row (0x10 bytes): its s16 point and the rotation's two words
struct Waypoint : DataRecord {
    static constexpr uint32_t kBytes = 0x10;
    ShortVec3 point; // +0x0
    uint32_t rotationXY = 0; // +0x8
    uint32_t rotationZ = 0; // +0xc
    template <class R> static Waypoint load(const R& r) {
        Waypoint v;
        v.point = {r.template read<int16_t>(0x0), r.template read<int16_t>(0x2), r.template read<int16_t>(0x4)};
        v.rotationXY = r.template read<uint32_t>(0x8);
        v.rotationZ = r.template read<uint32_t>(0xc);
        return v;
    }
};
constexpr uint32_t kWaypointsPerVariant = 6;
void toWaypoint(AnimatedActor& actor, uint32_t n) { // the row (variant * 6 + n) of the waypoints
    const Waypoint w = dataRecord<Waypoint>(stateAddress(TableId::LateCastWaypoints) + (uint32_t(actor.variant) * kWaypointsPerVariant + n) * 0x10, Waypoint::kBytes);
    actor.pos.x.whole = w.point.x;
    actor.pos.y.whole = w.point.y;
    actor.pos.z.whole = w.point.z;
    actor.rotation.setWords(w.rotationXY, w.rotationZ);
}
// exe 0x4bc980(actor, bone): a smoke trail (the stage's spark emitter, variant 0x1d) at the origin on the bone
void spawnTrail(AnimatedActor& actor, uint8_t bone) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& trail = staticActorAt(slot);
    trail.pos.x.whole = 0;
    trail.spawnByte = bone;
    trail.pos.y.whole = 0;
    trail.pos.z.whole = 0;
    trail.setParamsWord(0);
    trail.flags = kActorLive;
    trail.type = kSparkEmitterStageType;
    trail.variant = 0x1d;
    trail.loopFlags |= kActorSweepGroup2;
    classState<spark_emitter_state::SparkEmitterFields>(staticActorAt(slot)).parent = &actor;
}
// the flight (scene 1 of variant 0 / every tick of variant 1): along the waypoints (a new row every 0x29 ticks of the
// counter; the 5th row -> coasting), forward 0x2000 a tick, a puff (a blast, variant 0x10) while the counter is 4 or
// 5; coasting two more ticks forward, then the landing animation `land` (variant 1: yaw 0xc00), rotation x / z 0;
// nine ticks of x / y drift by `drift`
void flight(AnimatedActor& actor, FlyerFields& flyer, uint8_t land, int drift, bool yaw) {
    switch (FlightStep(actor.subState)) {
    case FlightStep::Waypoints: {
        const uint32_t c = flyer.counter + 1;
        flyer.counter = c;
        if (c > 0x28) {
            const uint32_t n = flyer.row + 1;
            flyer.row = n;
            if (n == 5) actor.subState += 1u;
            toWaypoint(actor, flyer.row);
            flyer.counter = 0;
        }
        moveAlongRotation(actor.pos, actor.rotation, 0, 0, -0x2000); // exe 0x5e37b0
        const uint32_t k = flyer.counter;
        if (k <= 3 || k >= 6) return;
        const uint32_t slot = allocActor60(0); // exe 0x5e1300
        if (!slot) return;
        StaticActor& puff = staticActorAt(slot);
        BlastParams params(actorHeaderAt(slot));
        puff.flags = kActorLive;
        puff.type = typeByte(GlobalStaticType::Blast);
        puff.variant = 0x10;
        puff.pos.x.whole = int16_t((rngStep() & 0x1ff) + uint16_t(actor.pos.x.whole));
        puff.pos.y.whole = int16_t((rngStep() & 0x7f) + uint16_t(actor.pos.y.whole));
        puff.pos.z.whole = int16_t((rngStep() & 0x1ff) + uint16_t(actor.pos.z.whole));
        const uint32_t r = rngStep();
        params.attack = 0;
        params.size = uint16_t(((r & 3) + 8) << 4);
        return;
    }
    case FlightStep::Coast: {
        moveAlongRotation(actor.pos, actor.rotation, 0, 0, -0x2000); // exe 0x5e37b0
        const uint32_t c = flyer.counter + 1;
        flyer.counter = c;
        if (c <= 1) return;
        actor.anim = land;
        actor.rotation.x = 0;
        actor.rotation.z = 0;
        if (yaw) actor.rotation.y = 0xc00;
        flyer.counter = 0;
        actor.subState += 1u;
        return;
    }
    case FlightStep::Drift: {
        actor.pos.x.whole = int16_t(uint16_t(actor.pos.x.whole) + drift);
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + drift);
        const uint32_t c = flyer.counter + 1;
        flyer.counter = c;
        if (c >= 9) {
            actor.subState += 1u;
            flyer.counter = 0;
        }
        return;
    }
    default: return;
    }
}
// exe 0x4bc460 (variant 0): the flight in scene 1; scene 5: rises with five smoke trails, dimmed
void flyerFirst(AnimatedActor& actor, FlyerFields& flyer) {
    if (scene() == 1) {
        flight(actor, flyer, 0x11, -8, false);
        return;
    }
    if (scene() != 5) return;
    if (actor.step == 0) {
        if (tick() != 1) return;
        actor.anim = 0x13;
        spawnTrail(actor, 0);
        spawnTrail(actor, 4);
        spawnTrail(actor, 7);
        spawnTrail(actor, 9);
        spawnTrail(actor, 0xd);
        flyer.dimmed = 1;
        actor.tint = kFlyerDimTint;
        actor.step = 1;
        return;
    }
    if (actor.step != 1) return;
    moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, 0x400); // exe 0x5e36f0
    actor.velocity.y += 0x40u;
    const uint32_t c = flyer.counter + 1;
    flyer.counter = c;
    if (c >= 0x3d) actor.step = 2;
}
void flyerSecond(AnimatedActor& actor, FlyerFields& flyer) { flight(actor, flyer, 0x12, 8, true); } // exe 0x4bc750
void flyerRun(AnimatedActor& actor, FlyerFields& flyer) { // exe 0x4bc440: [0x8949dc]
    switch (actor.variant) {
    case 0: flyerFirst(actor, flyer); return;
    case 1: flyerSecond(actor, flyer); return;
    default: notPortedAt(kStoryPort, "0x4bc280 variant (table 0x8949dc; 2.. belong to the subs 2 / 3)", actor.variant);
    }
}
void flyer(AnimatedActor& actor) {
    FlyerFields& flyer = classState<FlyerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    flyer.heldKey = actor.animKey.frame;
    actor.tint = flyer.dimmed != 0 ? kFlyerDimTint : kTintNeutral;
    switch (actor.state) { // [0x8949d0]
    case 0:                // exe 0x4bc360
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
            freeActor(actor);
            break;
        }
        actor.flags |= kActorDrawn;
        actor.anim = 0;
        actor.prevAnim = kNoAnimShown;
        animSetPose(actor, 0, 0); // exe 0x5e1c30
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        toWaypoint(actor, 0);
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        actor.collisionBox = nullptr;
        flyer.dimmed = 0;
        flyer.counter = 0;
        flyer.row = 0;
        flyer.stepping = 1;
        flyer.heldKey = 0;
        actor.enterStateBytes(uint8_t(CastState::Run));
        actor.clearStepBytes();
        flyerRun(actor, flyer);
        break;
    case 1: flyerRun(actor, flyer); break;
    case 2: freeActor(actor); break;
    case 3: flyerFirst(actor, flyer); break;
    case 4: flyerSecond(actor, flyer); break;
    default: notPortedAt(kStoryPort, "0x4bc280 state (table 0x8949d0; 5.. are the sub-2 / sub-3 classes')", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else if (flyer.stepping & 1) animStepPose(actor);                   // exe 0x5e1e80
    else animSetPose(actor, actor.anim, flyer.heldKey);                 // exe 0x5e1c30(actor, anim, the held key)
}

void sceneWalkerRun(AnimatedActor& actor, SceneWalkerFields& cast) { // exe 0x4effd0
    switch (actor.subState) {
    case 0:
        if (scene() == 0 && tick() == 0x85) {
            cast.stepping = 1;
            actor.subState = 1;
        }
        return;
    case 1:
        if (scene() == 1 && tick() == 0xeb) {
            actor.anim = 2;
            actor.subState = 2;
        }
        return;
    case 2:
        if (scene() == 2 && tick() == 0x314) {
            actor.anim = 3;
            actor.subState = 3;
        }
        return;
    default: return;
    }
}
void sceneWalker(AnimatedActor& actor) {
    SceneWalkerFields& cast = classState<SceneWalkerFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x896844]
    case 0:                // exe 0x4eff20 (state 1 not run)
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            break;
        }
        actor.flags |= kActorDrawn;
        actor.anim = 1;
        actor.prevAnim = kNoAnimShown;
        animSetPose(actor, 1, 0);
        clearBind(actor, int16_t(0xff00));
        cast.word = 0;
        cast.stepping = 0;
        cast.tracks.init(stateAddress(TableId::LateCastSceneWalkerFirstTracks), stateAddress(TableId::LateCastSceneWalkerSecondTracks));
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        actor.subState = 0;
        actor.clearStepBytes();
        actor.state = uint8_t(CastState::Run);
        break;
    case 1: sceneWalkerRun(actor, cast); break;
    case 2: freeActor(actor); break;
    default: notPortedAt(kStoryPort, "0x4efe50 state (table 0x896844)", actor.state);
    }
    animTail(actor, cast.stepping != 0);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- the cued talker B (exe 0x4f43f0, ST4B type 0x72) --------------------------------------------------------------
void talkerBFirst(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x4f45a0 (variant 0)
    if (tick() == 10) {
        cast.stepping = 1;
        actor.subState = 1;
    } else if (tick() == 0x1b5) {
        actor.anim = 2;
    }
    faceRows(cast, stateAddress(TableId::LateCastTalkerBFirstRows), stateAddress(TableId::LateCastTalkerBFirstRowsSecond));
}
void talkerBSecond(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x4f4600 (variant 1)
    switch (tick()) {
    case 0x104: cast.stepping = 1; break;
    case 0x1bf: actor.anim = 4; break;
    case 0x1ee: actor.anim = 5; break;
    case 0x3b3: actor.anim = 6; break;
    case 0x3ef: actor.anim = 7; break;
    case 0x435: actor.anim = 8; break;
    case 0x4d7: actor.anim = 9; break;
    case 0x53b: actor.anim = 10; break;
    default: break;
    }
    faceRows(cast, stateAddress(TableId::LateCastTalkerBSecondRows), stateAddress(TableId::LateCastTalkerBSecondRowsSecond));
}
void talkerBRun(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x4f4580: [0x897004]
    switch (actor.variant) {
    case 0: talkerBFirst(actor, cast); return;
    case 1: talkerBSecond(actor, cast); return;
    default: notPortedAt(kStoryPort, "0x4f43f0 variant (table 0x897004)", actor.variant);
    }
}
void cuedTalkerB(AnimatedActor& actor) {
    St4bCastFields& cast = classState<St4bCastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x896ff8]
    case 0:                // exe 0x4f44b0
        if (!bindTalker(actor, cast, true, actor.variant != 0 ? 3 : 1, 0, stateAddress(TableId::LateCastCuedTalkerBFirstTracks), stateAddress(TableId::LateCastCuedTalkerBSecondTracks))) break;
        talkerBRun(actor, cast);
        break;
    case 1: talkerBRun(actor, cast); break;
    case 2: freeActor(actor); break;
    case 3: talkerBFirst(actor, cast); break;
    case 4: talkerBSecond(actor, cast); break;
    default: notPortedAt(kStoryPort, "0x4f43f0 state (table 0x896ff8)", actor.state);
    }
    animTail(actor, cast.stepping != 0);
    textureTracksTick(actor, cast.tracks);
}

// ---- the late talker (exe 0x4fb3e0, ST4B type 0x75) ----------------------------------------------------------------
void lateTalkerFirst(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x4fb590 (variant 0)
    if (actor.subState == 0 && tick() == 0x294) {
        cast.stepping = 1;
        actor.subState = 1;
    }
    faceRows(cast, stateAddress(TableId::LateCastLateTalkerRows), stateAddress(TableId::LateCastLateTalkerRowsSecond));
}
void lateTalkerRun(AnimatedActor& actor, St4bCastFields& cast) { // exe 0x4fb570: [0x897a14]
    switch (actor.variant) {
    case 0: lateTalkerFirst(actor, cast); return;
    case 1: return; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x4fb3e0 variant (table 0x897a14)", actor.variant);
    }
}
void lateTalker(AnimatedActor& actor) {
    St4bCastFields& cast = classState<St4bCastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x897a08]
    case 0:                // exe 0x4fb4b0
        if (!bindTalker(actor, cast, false, 1, int16_t(0xff00), stateAddress(TableId::LateCastLateTalkerFirstTracks), stateAddress(TableId::LateCastLateTalkerSecondTracks))) break;
        lateTalkerRun(actor, cast);
        break;
    case 1: lateTalkerRun(actor, cast); break;
    case 2: freeActor(actor); break;
    case 3: lateTalkerFirst(actor, cast); break;
    case 4: break; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x4fb3e0 state (table 0x897a08)", actor.state);
    }
    animTail(actor, cast.stepping != 0);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- ST54 common ---------------------------------------------------------------------------------------------------

void actionRows(St54CastFields& cast, uint32_t t0, uint32_t t1) { rows(cast.tracks, cast.rows, t0, t1); } // 0x479090
// the action of a state past the routines' own (the action table follows the states)
uint8_t directAction(const AnimatedActor& actor) { return uint8_t(actor.state - kFirstDirect); }
// the common part of the ST54 binds: model variant 0 (none: freed), drawn as scenery, animation 0 (none shown), the
// collision / interaction pointers and byte, the velocities, rotation x / z, the contact bytes, the platform, the
// actor hit cleared
bool bindSt54(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return false;
    }
    actor.flags |= kActorDrawn | kActorScenery;
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.collisionBox = nullptr;
    actor.clearInteractionBytes();
    actor.velocity.clearBytes();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    return true;
}
// the ST54 tails: false when the actor is gone (see the deviation at the top)
bool animTail54(AnimatedActor& actor, int entry, bool decodeOnly) {
    if (!(actor.flags & kActorLive)) return false;
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, entry); // exe 0x5e1c50
    else if (decodeOnly) animDecodeCurrent(actor);                       // exe 0x5e1f30
    else animStep(actor);                                                // exe 0x5e1ea0
    animPose(actor);                                                     // exe 0x5e2d90
    return true;
}
// every 8th frame: a skid spark (global effect type 3, variant 0x20) at the bone's point (bone, v)
void boneSpark(const AnimatedActor& actor, uint32_t bone, int16_t x, int16_t y, int16_t z) {
    if (gameState().frame & 7) return;
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& spark = effectActorAt(slot);
    spark.flags = kActorLive;
    spark.type = typeByte(GlobalEffectType::SkidSparks);
    spark.subtype = 0;
    spark.variant = 0x20;
    spark.spawnByte = 0;
    const int16_t v[3] = {x, y, z};
    int16_t o[3];
    bonePointOffset(actor, bone, v, o); // exe 0x5e1970
    spark.pos.x = o[0];
    spark.pos.y = o[1];
    spark.pos.z = o[2];
    classState<SkidSparkFields>(effectActorAt(slot)).yaw = 0;
}
// an action's first tick: its animation, its phase 1, the tracks' sequences, the rows' indices 0
bool startAction(AnimatedActor& actor, St54CastFields& cast, uint8_t anim, uint8_t s0, uint8_t s1) {
    if (actor.step != 0) return false;
    actor.anim = anim;
    actor.step = 1;
    cast.tracks.first.select(s0, 0);
    cast.tracks.second.select(s1, 0);
    cast.rows[0] = 0;
    cast.rows[1] = 0;
    return true;
}

// ---- the action talker (exe 0x44bac0, ST54 type 2) -----------------------------------------------------------------
void actionTalkerAct(AnimatedActor& actor, St54CastFields& cast, uint8_t n) { // [0x88e858]
    switch (n) {
    case 0: return; // exe 0x52ef00
    case 1:         // exe 0x44bc40
        if (actor.step != 0) return;
        actor.anim = 1;
        actor.step = 1;
        return;
    case 2: startAction(actor, cast, 2, 0, 0); actionRows(cast, stateAddress(TableId::LateCastActionTalkerAct2First), stateAddress(TableId::LateCastActionTalkerAct2Second)); return; // exe 0x44bc60
    case 3: startAction(actor, cast, 3, 1, 0); actionRows(cast, stateAddress(TableId::LateCastActionTalkerAct3First), stateAddress(TableId::LateCastActionTalkerAct3Second)); return; // exe 0x44bcc0
    case 4: startAction(actor, cast, 4, 3, 1); actionRows(cast, stateAddress(TableId::LateCastActionTalkerAct4First), stateAddress(TableId::LateCastActionTalkerAct4Second)); return; // exe 0x44bd20
    case 5: startAction(actor, cast, 5, 8, 1); actionRows(cast, stateAddress(TableId::LateCastActionTalkerAct5First), stateAddress(TableId::LateCastActionTalkerAct5Second)); return; // exe 0x44bd80
    case 6: startAction(actor, cast, 6, 4, 2); actionRows(cast, stateAddress(TableId::LateCastActionTalkerAct6First), stateAddress(TableId::LateCastActionTalkerAct6Second)); return; // exe 0x44bde0
    case 7: startAction(actor, cast, 7, 4, 2); actionRows(cast, stateAddress(TableId::LateCastActionTalkerAct7First), stateAddress(TableId::LateCastActionTalkerAct7Second)); return; // exe 0x44be40
    default: notPortedAt(kStoryPort, "0x44bac0 action (table 0x88e858)", n);
    }
}
void actionTalker(AnimatedActor& actor) {
    St54CastFields& cast = classState<St54CastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x88e84c]
    case 0:                // exe 0x44bb80
        if (!bindSt54(actor)) break;
        cast.tracks.init(stateAddress(TableId::LateCastActionTalkerFirstTracks), stateAddress(TableId::LateCastActionTalkerSecondTracks));
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        actor.enterStateBytes(uint8_t(CastState::Run));
        actionTalkerAct(actor, cast, actor.subState);
        break;
    case 1: actionTalkerAct(actor, cast, actor.subState); break; // exe 0x44bc20
    case 2: freeActor(actor); break;
    default: actionTalkerAct(actor, cast, directAction(actor)); break;
    }
    if (!animTail54(actor, 0, false)) return;
    // exe 0x5f20f0(actor, 0x30, 0xc0, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- the holding talker (exe 0x45d4f0, ST54 type 9) ----------------------------------------------------------------
constexpr uint8_t kHoldFrame = 0x01; // the decoded frame is held (exe 0x5e1f30)
constexpr size_t kHolderFacePart = 4, kHolderOtherPart = 17;
void holdingTalkerAct(AnimatedActor& actor, HoldingTalkerFields& cast, uint8_t n) { // [0x88f944]
    switch (n) {
    case 0: return; // exe 0x52ef00
    case 1:         // exe 0x45d690: started with phase 1 by the script; the hold of the decoded frame
        if (actor.step == 1) {
            actor.anim = 1;
            actor.step = 2;
            cast.rows[0] = 0;
            cast.rows[1] = 0;
            cast.tracks.first.select(0, 0);
            cast.tracks.second.select(0, 0);
        } else if (actor.step == 2) {
            if (scene() == 3 && tick() > 0xff) cast.hold &= uint8_t(~kHoldFrame);
            else if (actor.keyIndex == 0x2b) cast.hold |= kHoldFrame;
        }
        actionRows(cast, stateAddress(TableId::LateCastHoldingTalkerAct1First), stateAddress(TableId::LateCastHoldingTalkerAct1Second));
        return;
    case 2: // exe 0x45d720
        if (!startAction(actor, cast, 2, 4, 2) && actor.step == 1 && actor.keyIndex == 0xa) {
            actor.parts[kHolderFacePart].flags &= uint8_t(~kPartHidden);
            actor.parts[kHolderOtherPart].flags |= kPartHidden;
        }
        actionRows(cast, stateAddress(TableId::LateCastHoldingTalkerAct2First), stateAddress(TableId::LateCastHoldingTalkerAct2Second));
        return;
    case 3: startAction(actor, cast, 3, 1, 1); actionRows(cast, stateAddress(TableId::LateCastHoldingTalkerAct3First), stateAddress(TableId::LateCastHoldingTalkerAct3Second)); return; // exe 0x45d7b0
    case 4: startAction(actor, cast, 4, 2, 1); actionRows(cast, stateAddress(TableId::LateCastHoldingTalkerAct4First), stateAddress(TableId::LateCastHoldingTalkerAct4Second)); return; // exe 0x45d810
    case 5:                                                                                  // exe 0x45d870 (no rows)
        if (actor.step != 0) return;
        actor.anim = 5;
        actor.step = 1;
        cast.tracks.first.select(3, 0);
        cast.tracks.second.select(2, 0);
        return;
    case 6: startAction(actor, cast, 6, 3, 2); actionRows(cast, stateAddress(TableId::LateCastHoldingTalkerAct6First), stateAddress(TableId::LateCastHoldingTalkerAct6Second)); return; // exe 0x45d8b0
    case 7: startAction(actor, cast, 7, 3, 2); actionRows(cast, stateAddress(TableId::LateCastHoldingTalkerAct7First), stateAddress(TableId::LateCastHoldingTalkerAct7Second)); return; // exe 0x45d910
    default: notPortedAt(kStoryPort, "0x45d4f0 action (table 0x88f944)", n);
    }
}
void holdingTalker(AnimatedActor& actor) {
    HoldingTalkerFields& cast = classState<HoldingTalkerFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x88f938]
    case 0:                // exe 0x45d5c0
        if (!bindSt54(actor)) break;
        cast.rows[0] = 0;
        cast.rows[1] = 0;
        cast.tracks.init(stateAddress(TableId::LateCastHoldingTalkerFirstTracks), stateAddress(TableId::LateCastHoldingTalkerSecondTracks));
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        actor.enterStateBytes(uint8_t(CastState::Run));
        cast.hold = 0;
        holdingTalkerAct(actor, cast, actor.subState);
        break;
    case 1: holdingTalkerAct(actor, cast, actor.subState); break; // exe 0x45d670
    case 2: freeActor(actor); break;
    default: holdingTalkerAct(actor, cast, directAction(actor)); break;
    }
    if (!animTail54(actor, 0, (cast.hold & kHoldFrame) != 0)) return;
    // exe 0x5f20f0(actor, 0x30, 0xc0, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- the pacer (exe 0x4661f0, ST54 type 0xb: its action starts as its first parameter byte) ------------------------
constexpr size_t kPacerPartA = 8, kPacerPartB = 7, kPacerPartC = 2;
// exe 0x4664c0(actor, t): one row table on track 0
void pacerRows(PacerFields& cast, uint32_t t0) {
    const FaceRow r = dataRecord<FaceRow>(t0 + uint32_t(cast.row) * 4, FaceRow::kBytes);
    const uint16_t k = r.tick;
    if (k == 0xffff || uint32_t(k) != timelineTick()) return;
    cast.tracks.first.select(r.sequence, r.key); // exe 0x5e33d0
    cast.row += 1u;
}
void pacerAct(AnimatedActor& actor, PacerFields& cast, uint8_t n) { // [0x88fe8c]
    switch (n) {
    case 0: // exe 0x466370: walks back and forth (turning 0x800 every 0x3c ticks)
        if (actor.step == 0) {
            actor.anim = 3;
            actor.step = 1;
            cast.turnTimer = 0x3c;
            return;
        }
        if (actor.step != 1) return;
        if (cast.turnTimer == 0) {
            actor.rotation.y += 0x800u;
            cast.turnTimer = 0x3c;
        } else {
            cast.turnTimer -= 1u;
        }
        integrateVelocity(actor, 0, 0, -0x80); // exe 0x5e3690
        return;
    case 1: // exe 0x4663d0: phase 2 (from the script) walks
        if (actor.step == 0) {
            actor.anim = 2;
            actor.step = 1;
            actor.parts[kPacerPartA].flags |= kPartHidden;
            return;
        }
        if (actor.step == 2) integrateVelocity(actor, 0, 0, -0x80);
        return;
    case 2: // exe 0x466420
        if (actor.step == 0) {
            actor.anim = 4;
            actor.step = 1;
            cast.tracks.first.select(0, 0);
            cast.row = 0;
            actor.parts[kPacerPartA].flags |= kPartHidden;
            actor.parts[kPacerPartB].flags &= uint8_t(~kPartHidden);
            actor.parts[kPacerPartC].flags &= uint8_t(~kPartHidden);
        } else if (actor.step == 2) {
            integrateVelocity(actor, 0, 0, -0x30);
        }
        pacerRows(cast, stateAddress(TableId::LateCastPacerRows));
        return;
    default: notPortedAt(kStoryPort, "0x4661f0 action (table 0x88fe8c)", n);
    }
}
void pacer(AnimatedActor& actor) {
    PacerFields& cast = classState<PacerFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x88fe80]
    case 0: {              // exe 0x4662b0
        const uint8_t first = cast.firstAction;
        if (!bindSt54(actor)) break;
        actor.state = uint8_t(CastState::Run);
        actor.subState = first;
        actor.clearStepBytes();
        cast.row = 0;
        cast.tracks.init(stateAddress(TableId::LateCastPacerTracks), 0);
        cast.tracks.first.select(0, 0);
        pacerAct(actor, cast, actor.subState);
        break;
    }
    case 1: pacerAct(actor, cast, actor.subState); break; // exe 0x466350
    case 2: freeActor(actor); break;
    default: pacerAct(actor, cast, directAction(actor)); break;
    }
    if (!animTail54(actor, 0, false)) return;
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- the row talker (exe 0x478ea0, ST54 type 0x16) -----------------------------------------------------------------
void rowTalkerAct(AnimatedActor& actor, St54CastFields& cast, uint8_t n) { // [0x890f98]
    switch (n) {
    case 0: return;                                         // exe 0x52ef00
    case 1: actionRows(cast, stateAddress(TableId::LateCastRowTalkerAct1First), stateAddress(TableId::LateCastRowTalkerAct1Second)); return;  // exe 0x479030
    case 2:                                                 // exe 0x479050
        if (actor.step != 0) return;
        actor.anim = 1;
        actor.step = 1;
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        return;
    default: notPortedAt(kStoryPort, "0x478ea0 action (table 0x890f98)", n);
    }
}
void rowTalker(AnimatedActor& actor) {
    St54CastFields& cast = classState<St54CastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x890f8c]
    case 0:                // exe 0x478f60
        if (!bindSt54(actor)) break;
        actor.enterStateBytes(uint8_t(CastState::Run));
        actor.step = 0;
        cast.rows[0] = 0;
        cast.rows[1] = 0;
        cast.tracks.init(stateAddress(TableId::LateCastRowTalkerFirstTracks), stateAddress(TableId::LateCastRowTalkerSecondTracks));
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        rowTalkerAct(actor, cast, actor.subState);
        break;
    case 1: rowTalkerAct(actor, cast, actor.subState); break; // exe 0x479010
    case 2: freeActor(actor); break;
    default: rowTalkerAct(actor, cast, directAction(actor)); break;
    }
    if (!animTail54(actor, 0, false)) return;
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- the mouth talker (exe 0x47e0a0, ST54 type 0x1a) ---------------------------------------------------------------
enum class MouthMode : uint8_t { Still = 0, Talk = 1, Close = 2 };
constexpr size_t kMouthClosedPart = 1, kMouthOpenPartA = 10, kMouthOpenPartB = 11;
// the talk rows: their tick and the mouth's mode
struct TalkRow : DataRecord {
    static constexpr uint32_t kBytes = 0x3;
    uint16_t tick = 0; // +0x0
    uint8_t mode = 0; // +0x2
    template <class R> static TalkRow load(const R& r) {
        TalkRow v;
        v.tick = r.template read<uint16_t>(0x0);
        v.mode = r.template read<uint8_t>(0x2);
        return v;
    }
};
void mouthClosed(AnimatedActor& actor) { // the closed mouth shown, the open ones hidden
    actor.parts[kMouthClosedPart].flags &= uint8_t(~kPartHidden);
    actor.parts[kMouthOpenPartA].flags |= kPartHidden;
    actor.parts[kMouthOpenPartB].flags |= kPartHidden;
}
// exe 0x47e3e0(actor, t): the talk rows: a match sets the mouth's mode
void talkRows(MouthTalkerFields& cast, uint32_t t0) {
    const TalkRow r = dataRecord<TalkRow>(t0 + uint32_t(cast.row) * 4, TalkRow::kBytes);
    const uint16_t k = r.tick;
    if (k == 0xffff || uint32_t(k) != timelineTick()) return;
    cast.mode = r.mode;
    cast.row += 1u;
}
// exe 0x47e320: talking -> the mouth flips every 4 ticks of the flap counter; closing -> closed, both cleared
void mouthTick(AnimatedActor& actor, MouthTalkerFields& cast) {
    if (MouthMode(cast.mode) == MouthMode::Talk) {
        const uint8_t c = cast.flaps;
        if (!(c & 4)) {
            mouthClosed(actor);
        } else {
            actor.parts[kMouthClosedPart].flags |= kPartHidden;
            actor.parts[kMouthOpenPartA].flags |= kPartHidden;
            actor.parts[kMouthOpenPartB].flags &= uint8_t(~kPartHidden);
        }
        cast.flaps = uint8_t(c + 1u);
        return;
    }
    if (MouthMode(cast.mode) != MouthMode::Close) return;
    mouthClosed(actor);
    cast.mode = 0;
    cast.flaps = 0;
}
void mouthTalkerAct(AnimatedActor& actor, MouthTalkerFields& cast, uint8_t n) { // [0x891310]
    switch (n) {
    case 0: mouthClosed(actor); return; // exe 0x47e260
    case 1:                             // exe 0x47e2a0
        if (actor.step == 0) {
            actor.step = 1;
            actor.anim = 1;
            mouthClosed(actor);
        } else if (actor.step == 1 && actor.animEnded()) {
            actor.step = 2;
        }
        talkRows(cast, stateAddress(TableId::LateCastMouthTalkerRows));
        mouthTick(actor, cast);
        return;
    default: notPortedAt(kStoryPort, "0x47e0a0 action (table 0x891310)", n);
    }
}
void mouthTalker(AnimatedActor& actor) {
    MouthTalkerFields& cast = classState<MouthTalkerFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x891304]
    case 0:                // exe 0x47e1d0 (the action is not run)
        if (!bindSt54(actor)) break;
        cast.row = 0;
        actor.enterStateBytes(uint8_t(CastState::Run));
        break;
    case 1: mouthTalkerAct(actor, cast, actor.subState); break; // exe 0x47e240
    case 2: freeActor(actor); break;
    default: mouthTalkerAct(actor, cast, directAction(actor)); break;
    }
    if (!animTail54(actor, 0, false)) return;
    boneSpark(actor, 5, -0x10, 0x34, -0x14);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) while drawn: the shadow (render output)
}

// ---- the circler (exe 0x4ea6e0, ST54 type 0x63) --------------------------------------------------------------------
enum class WalkStep : uint8_t { Start = 0, Wait = 1, Turn = 2, ToGoal = 3, Arrive = 4, Done = 5, Rows = 6 };
// exe 0x4eada0(actor, t): the rows on track 0 (one table)
void circlerRows(CirclerFields& cast) {
    const FaceRow r = dataRecord<FaceRow>(stateAddress(TableId::LateCastCirclerRows) + uint32_t(int32_t(cast.row)) * 4, FaceRow::kBytes);
    const uint16_t k = r.tick;
    if (k == 0xffff || uint32_t(k) != timelineTick()) return;
    cast.tracks.first.select(r.sequence, r.key); // exe 0x5e33d0
    cast.row += 1u;
}
// face the goal (exe 0x5e3c00 + 0x800, exe 0x5e3df0 step 0x20) and walk 0x10 a tick
void toGoal(AnimatedActor& actor) {
    const int16_t from[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const ShortVec3 goalPoint = dataShortVec3(stateAddress(TableId::LateCastCirclerGoal));
    const int16_t goal[3] = {goalPoint.x, goalPoint.y, goalPoint.z};
    const int16_t ang = t16(angleOf(from, goal)); // exe 0x5e3c00
    actor.rotation.y += 0x800u;
    turnYawTo(actor.rotation.y, uint32_t(int32_t(ang)), 0x20);
    actor.rotation.y -= 0x800u;
    moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, -0x10); // exe 0x5e36f0
}
void circlerAct(AnimatedActor& actor, CirclerFields& cast, uint8_t n) { // [0x896550]
    switch (n) {
    case 0: { // exe 0x4ea890: circles the centre at the radius, the angle +-8 a tick by the countdown
        if (actor.step == 0) {
            actor.anim = 1;
            cast.animEntry = 0;
            return;
        }
        if (actor.step != 1) return;
        const int16_t v = cast.countdown;
        if (v >= 0x80) {
            cast.countdown = int16_t(v - 1);
            cast.angle = uint16_t((cast.angle + 8u) & 0xfff);
        } else if (v != 0) {
            cast.countdown = int16_t(v - 1);
            if (t16(v - 1) == 0) cast.countdown = 0x100;
            cast.angle = uint16_t((cast.angle - 8u) & 0xfff);
        }
        const int32_t r = cast.radius;
        const int16_t angle = int16_t(cast.angle.get());
        const int16_t x = t16((int32_t(uint32_t(fixedSin(int(angle))) * uint32_t(r)) >> 12) + cast.centreX);
        actor.pos.x.whole = x;
        const int16_t z = t16((int32_t(uint32_t(fixedCos(int(angle))) * uint32_t(r)) >> 12) + cast.centreZ);
        actor.pos.z.whole = z;
        actor.pos.x = int32_t(uint32_t(int32_t(x)) << 16);
        actor.pos.z = int32_t(uint32_t(int32_t(z)) << 16);
        return;
    }
    case 1: // exe 0x4ea970
    case 3: // exe 0x4ea9d0
    case 4: // exe 0x4ea9f0
        if (actor.step != 0) return;
        actor.anim = n == 1 ? 2 : n == 3 ? 4 : 5;
        cast.animEntry = 0;
        ++actor.step;
        return;
    case 2: // exe 0x4ea990
        if (actor.step == 0) {
            actor.anim = 2;
            actor.prevAnim = kNoAnimShown;
            return;
        }
        if (actor.step != 1) return;
        actor.anim = 3;
        cast.animEntry = 0;
        ++actor.step;
        return;
    case 5: // exe 0x4eaa10 (jump table 0x4ead78; phase 5 and > 6: rows only)
        switch (WalkStep(actor.step)) {
        case WalkStep::Start:
            actor.anim = 6;
            cast.animEntry = 0;
            ++actor.step;
            cast.timer = 0x3c;
            break;
        case WalkStep::Wait:
            if (!actor.animEnded()) break;
            if (cast.timer == 0) {
                actor.anim = 7;
                cast.animEntry = 0;
                ++actor.step;
            } else {
                cast.timer -= 1u;
            }
            break;
        case WalkStep::Turn:
            if (actor.animEnded()) {
                actor.anim = 8;
                cast.animEntry = 0xf;
                ++actor.step;
                toGoal(actor);
            } else if (int8_t(actor.keyIndex) >= 0x25) {
                toGoal(actor);
            }
            break;
        case WalkStep::ToGoal: {
            toGoal(actor);
            const ShortVec3 goal = dataShortVec3(stateAddress(TableId::LateCastCirclerGoal));
            const int dx = t16(goal.x - actor.pos.x.whole), dz = t16(goal.z - actor.pos.z.whole);
            if (isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz))) < 8) ++actor.step; // exe 0x5e3b40
            break;
        }
        case WalkStep::Arrive:
            if (actor.animEnded()) {
                actor.anim = 9;
                cast.animEntry = 0;
            } else {
                moveAlongYaw(actor.pos, actor.rotation.y, 0, 0, -0x10);
            }
            break;
        case WalkStep::Rows:
            actor.anim = 10;
            cast.animEntry = 0;
            ++actor.step;
            cast.tracks.first.select(0, 0);
            cast.row = 0;
            break;
        default: break;
        }
        circlerRows(cast);
        return;
    default: notPortedAt(kStoryPort, "0x4ea6e0 action (table 0x896550)", n);
    }
}
void circler(AnimatedActor& actor) {
    CirclerFields& cast = classState<CirclerFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x896544]
    case 0:                // exe 0x4ea7b0
        if (!bindSt54(actor)) break;
        actor.enterStateBytes(uint8_t(CastState::Run));
        cast.tracks.init(stateAddress(TableId::LateCastCirclerTracks), 0);
        cast.tracks.first.select(0, 0);
        cast.radius = 0x58;
        cast.angleLow = 0;
        cast.angleHigh = 8;
        cast.countdown = 0xc0;
        cast.centreXWord = 0;
        cast.centreZ = int16_t(0xfd88);
        circlerAct(actor, cast, actor.subState);
        break;
    case 1: circlerAct(actor, cast, actor.subState); break; // exe 0x4ea870
    case 2: freeActor(actor); break;
    default: circlerAct(actor, cast, directAction(actor)); break;
    }
    if (!animTail54(actor, cast.animEntry, false)) return;
    // exe 0x5f20f0(actor, 0x30, 0xc0, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

// ---- the spark talker (exe 0x4f6b70, ST54 type 0x72) ---------------------------------------------------------------
void sparkTalkerAct(AnimatedActor& actor, St54CastFields& cast, uint8_t n) { // [0x89738c]
    switch (n) {
    case 0: // exe 0x4f6d70: phase 1 (the script) -> animation 1
        if (actor.step != 1) return;
        actor.anim = 1;
        actor.step = 2;
        return;
    case 1: // exe 0x4f6d90
        if (actor.step == 0) {
            actor.anim = 2;
            actor.step = 1;
            cast.rows[0] = 0;
            cast.rows[1] = 0;
        }
        actionRows(cast, stateAddress(TableId::LateCastSparkTalkerActFirst), stateAddress(TableId::LateCastSparkTalkerAct1Second));
        return;
    case 2: // exe 0x4f6dd0
        if (!startAction(actor, cast, 3, 0, 2) && actor.step == 1) {
            if (actor.animEnded()) actor.step = 2;
            else if (actor.keyIndex == 0x4a) cast.tracks.second.select(0, 0);
        }
        actionRows(cast, stateAddress(TableId::LateCastSparkTalkerActFirst), stateAddress(TableId::LateCastSparkTalkerAct2Second));
        return;
    default: notPortedAt(kStoryPort, "0x4f6b70 action (table 0x89738c; 3.. not ported, script 0x6f writes 0..2)", n);
    }
}
void sparkTalker(AnimatedActor& actor) {
    St54CastFields& cast = classState<St54CastFields>(actor);
    castHead(actor);
    switch (actor.state) { // [0x897380]
    case 0:                // exe 0x4f6cb0
        if (!bindSt54(actor)) break;
        cast.tracks.init(stateAddress(TableId::LateCastSparkTalkerFirstTracks), stateAddress(TableId::LateCastSparkTalkerSecondTracks));
        cast.tracks.first.select(0, 0);
        cast.tracks.second.select(0, 0);
        actor.enterStateBytes(uint8_t(CastState::Run));
        sparkTalkerAct(actor, cast, actor.subState);
        break;
    case 1: sparkTalkerAct(actor, cast, actor.subState); break; // exe 0x4f6d50
    case 2: freeActor(actor); break;
    default: sparkTalkerAct(actor, cast, directAction(actor)); break;
    }
    if (!animTail54(actor, 0, false)) return;
    boneSpark(actor, 8, -0x10, 0x10, -0x10);
    // exe 0x5f20f0(actor, 0x30, 0x160, 0) while drawn: the shadow (render output)
    textureTracksTick(actor, cast.tracks);
}

} // namespace

void script59CuedTalkerAUpdate(const PlayerEnv&, uint32_t actor) { cuedTalkerA(animatedActorAt(actor)); }
void script59LateStepperUpdate(const PlayerEnv&, uint32_t actor) { lateStepper(animatedActorAt(actor)); }
void script59SceneTalkerUpdate(const PlayerEnv&, uint32_t actor) { sceneTalker(animatedActorAt(actor)); }
void script59FlyerUpdate(const PlayerEnv&, uint32_t actor) { flyer(animatedActorAt(actor)); }
void script59IdleOrStepperUpdate(const PlayerEnv&, uint32_t actor) { idleOrStepper(animatedActorAt(actor)); }
void script59SceneWalkerUpdate(const PlayerEnv&, uint32_t actor) { sceneWalker(animatedActorAt(actor)); }
void script59CuedTalkerBUpdate(const PlayerEnv&, uint32_t actor) { cuedTalkerB(animatedActorAt(actor)); }
void script59LateTalkerUpdate(const PlayerEnv&, uint32_t actor) { lateTalker(animatedActorAt(actor)); }
void script6fActionTalkerUpdate(const PlayerEnv&, uint32_t actor) { actionTalker(animatedActorAt(actor)); }
void script6fHoldingTalkerUpdate(const PlayerEnv&, uint32_t actor) { holdingTalker(animatedActorAt(actor)); }
void script6fPacerUpdate(const PlayerEnv&, uint32_t actor) { pacer(animatedActorAt(actor)); }
void script6fRowTalkerUpdate(const PlayerEnv&, uint32_t actor) { rowTalker(animatedActorAt(actor)); }
void script6fMouthTalkerUpdate(const PlayerEnv&, uint32_t actor) { mouthTalker(animatedActorAt(actor)); }
void script6fCirclerUpdate(const PlayerEnv&, uint32_t actor) { circler(animatedActorAt(actor)); }
void script6fSparkTalkerUpdate(const PlayerEnv&, uint32_t actor) { sparkTalker(animatedActorAt(actor)); }

} // namespace od::port

// kHandlersTranscribed: 0x45c570 0x45c630 0x45c700 0x45c720 0x45c780 0x4660d0 0x466170 0x4daf20 0x49ba80 0x49bb50
// kHandlersTranscribed: 0x49bc10 0x49bdf0 0x4bc280 0x4bc360 0x4bc440 0x4bc460 0x4bc750 0x4bc980 0x4dadc0 0x4dae60
// kHandlersTranscribed: 0x4daf00 0x4efe50 0x4eff20 0x4effd0 0x4f43f0 0x4f44b0 0x4f4580 0x4f45a0 0x4f4600 0x4fb3e0
// kHandlersTranscribed: 0x4fb4b0 0x4fb570 0x4fb590 0x4fb5d0 0x43c110 0x44bac0 0x44bb80 0x44bc20 0x44bc40 0x44bc60
// kHandlersTranscribed: 0x44bcc0 0x44bd20 0x44bd80 0x44bde0 0x44be40 0x479090 0x45d4f0 0x45d5c0 0x45d670 0x45d690
// kHandlersTranscribed: 0x45d720 0x45d7b0 0x45d810 0x45d870 0x45d8b0 0x45d910 0x4661f0 0x4662b0 0x466350 0x466370
// kHandlersTranscribed: 0x4663d0 0x466420 0x4664c0 0x478ea0 0x478f60 0x479010 0x479030 0x479050 0x47e0a0 0x47e1d0
// kHandlersTranscribed: 0x47e240 0x47e260 0x47e2a0 0x47e320 0x47e3e0 0x4ea6e0 0x4ea7b0 0x4ea870 0x4ea890 0x4ea970
// kHandlersTranscribed: 0x4ea990 0x4ea9d0 0x4ea9f0 0x4eaa10 0x4eada0 0x4f6b70 0x4f6cb0 0x4f6d50 0x4f6d70 0x4f6d90
// kHandlersTranscribed: 0x4f6dd0
