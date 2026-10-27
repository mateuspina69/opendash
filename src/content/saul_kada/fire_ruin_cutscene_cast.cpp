// ST27 / ST28 (the fire ruin, story 13): the cutscene cast of pool 0x20 type 0x48 (hypothesis names). Each starts by
// binding its model (state 0), runs (state 1) and is freed (state 2); the script's step sub-id and tick time its turns
// and walks. The holder and the depths' variant 2 carry a prop of the stage's static type 0x40
// (content/saul_kada/st28_cast_props) at their bone 3; the dropper lets one go and rises out of the ground.
#include "content/saul_kada/fire_ruin_cutscene_cast.hpp"
#include "content/saul_kada/fire_ruin_cutscene_cast_state.hpp"

#include "content/saul_kada/st28_cast_props.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/collision.hpp"
#include "game/footstep_dust.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/spark_emitter_state.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

using namespace fire_ruin_cutscene_cast_state;

enum class CastState : uint8_t { Bind = 0, Run = 1, Free = 2 };
constexpr uint8_t kNoAnim = 0xff;
constexpr uint32_t kCarryBone = 3;

// the script's step sub-id and its tick
uint8_t stepSub() { return scriptState().stepSub; }
int32_t stepTick() { return scriptState().stepTick; }
bool tickIn(int32_t lo, int32_t hi) { return stepTick() > lo && stepTick() < hi; } // lo < tick < hi (signed)
void turn(AnimatedActor& actor, int d) { actor.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + uint32_t(d))); }

// the point {0, 0, -0x20} of bone 3 as the prop's 16.16 position
void propAtBone(const AnimatedActor& actor, StaticActor& prop) {
    static constexpr int16_t kOffset[3] = {0, 0, -0x20};
    int16_t point[3];
    bonePointOffset(actor, kCarryBone, kOffset, point); // exe 0x5e1970
    prop.pos.x = int32_t(uint32_t(int32_t(point[0])) << 16);
    prop.pos.y = int32_t(uint32_t(int32_t(point[1])) << 16);
    prop.pos.z = int32_t(uint32_t(int32_t(point[2])) << 16);
}
// a prop (the stage's static type 0x40, its sub) at bone 3 (inline in exe 0x4d0c30 / 0x4d1100, exe 0x5b20e0 for sub
// 2); 0 when the pool is full
uint32_t spawnProp(const AnimatedActor& actor, uint8_t subtype) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return 0;
    StaticActor& prop = staticActorAt(slot);
    prop.flags = kActorLive;
    prop.type = kCastPropType;
    prop.subtype = subtype;
    prop.variant = 0;
    prop.spawnByte = 0;
    propAtBone(actor, prop);
    prop.rotation.x = 0;
    prop.rotation.y = 0;
    prop.rotation.z = 0;
    prop.params[0] = 0; // its spawn parameter (the props do not read it)
    return slot;
}

// the head the cast shares: the previous animation and pose, the tint neutral
void castHead(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
}
// the tail the cast shares: the animation while it is live (`gated`: the depths' cast steps it only while it runs).
// The shadow (exe 0x5f20f0(actor, 0x80, 0xc0, 0) when drawn) is render output.
void castTail(AnimatedActor& actor, const TickCastFields* gate) {
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0);       // exe 0x5e1c30
    else if (!gate || gate->animRuns != 0) animStepPose(actor);                 // exe 0x5e1e80
}
// exe 0x5e0ba0: its model (variant 0); none -> freed (exe 0x5e1490)
bool bound(AnimatedActor& actor) {
    if (bindActorModel(actor, 0)) return true;
    freeActor(actor);
    return false;
}
void enterRun(AnimatedActor& actor) {
    actor.subState = 0;
    actor.restartStep();
    actor.state = uint8_t(CastState::Run);
}
// the contact and the collision box cleared at the bind
void clearContact(AnimatedActor& actor) {
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.collisionBox = nullptr;
}

// ---- exe 0x4d0b80 (ST28 type 0x48 sub 1): states [0x8952a8] ---------------------------------------------------------
void propHolder(AnimatedActor& actor) {
    HolderFields& walker = classState<HolderFields>(actor);
    castHead(actor);
    switch (CastState(actor.state)) {
    case CastState::Bind: // exe 0x4d0c30
        if (!bound(actor)) return;
        actor.flags |= kActorDrawn;
        actor.anim = 0;
        actor.prevAnim = kNoAnim;
        actor.velocity.setXY(0, 0);
        actor.velocity.setZSpare(0, 0);
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        clearContact(actor);
        walker.holding = 0;
        if (const uint32_t prop = spawnProp(actor, kHeldPropSubtype)) {
            walker.heldProp = &staticActorAt(prop);
            walker.holding = 1;
        }
        enterRun(actor);
        break;
    case CastState::Run: // exe 0x4d0d20: the prop follows bone 3
        if (walker.holding != 0) propAtBone(actor, *walker.heldProp);
        break;
    case CastState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4d0b80 state", actor.state);
    }
    castTail(actor, nullptr);
}

// ---- exe 0x4d0d90 (ST27 type 0x48 sub 2): states [0x8952b4], variants [0x8952c0] -------------------------------------
enum class EntranceVariant : uint8_t { TurnsA = 0, Walker = 1, Still = 2, TurnsB = 3 };
// exe 0x4d0ed0
void turnsA(AnimatedActor& actor) {
    switch (stepSub()) {
    case 2:
        if (tickIn(5, 0xf)) turn(actor, 0x40);
        break;
    case 4:
        if (tickIn(0x3c, 0x46)) turn(actor, 0x80);
        break;
    case 10:
        if (tickIn(0x28a, 0x2a2)) turn(actor, -0x40);
        break;
    default: break;
    }
}
// exe 0x4d0f40 (by the step sub-id, [0x4d0fcc])
void walkingCast(AnimatedActor& actor) {
    switch (stepSub()) {
    case 2: // exe 0x4d0f58
        if (tickIn(0x118, 0x122)) turn(actor, -0x40);
        break;
    case 4: // exe 0x4d0f73
        if (tickIn(0xa, 0x1e)) turn(actor, -0x40);
        break;
    case 6: // exe 0x4d0f8a: placed at the step's first tick, then walks
        if (stepTick() == 1) {
            actor.pos.x.whole = int16_t(0xfc00);
            actor.pos.z.whole = int16_t(0xfd00);
            actor.rotation.y = 0x800;
            actor.velocity.z = int16_t(0xff00);
        }
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        break;
    case 7: actor.state = uint8_t(CastState::Free); break; // exe 0x4d0fc1: freed next tick
    default: break;
    }
}
// exe 0x4d0ff0
void turnsB(AnimatedActor& actor) {
    switch (stepSub()) {
    case 0:
        if (tickIn(0x19, 0x32)) turn(actor, 0x40);
        break;
    case 1:
        if (actor.subState == 0 && stepTick() == 1) {
            actor.rotation.y = 0x800;
            actor.subState = 1;
        }
        break;
    default: break;
    }
}
void entranceCast(AnimatedActor& actor) {
    TickCastFields& walker = classState<TickCastFields>(actor);
    castHead(actor);
    switch (CastState(actor.state)) {
    case CastState::Bind: // exe 0x4d0e40
        if (!bound(actor)) return;
        actor.prevAnim = kNoAnim;
        actor.velocity.y = int16_t(0xff60);
        actor.flags |= kActorDrawn;
        actor.anim = 0;
        actor.velocity.x = 0;
        actor.velocity.z = 0;
        clearContact(actor);
        walker.ticks = 0;
        enterRun(actor);
        break;
    case CastState::Run: // exe 0x4d0eb0
        switch (EntranceVariant(actor.variant)) {
        case EntranceVariant::TurnsA: turnsA(actor); break;
        case EntranceVariant::Walker: walkingCast(actor); break;
        case EntranceVariant::Still: break; // 0x52ef00
        case EntranceVariant::TurnsB: turnsB(actor); break;
        default: notPortedAt(kStoryPort, "0x4d0d90 variant", actor.variant);
        }
        break;
    case CastState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4d0d90 state", actor.state);
    }
    castTail(actor, nullptr);
}

// ---- exe 0x4d1040 (ST28 type 0x48 sub 2): states [0x8952d0], variants [0x8952dc] -------------------------------------
enum class DepthsVariant : uint8_t { Jumper = 0, Strider = 1, Thrower = 2 };
enum class JumperPhase : uint8_t { Walk = 0, Run = 1, Climb = 2, Done = 3 };
enum class ThrowerPhase : uint8_t { Watch = 0, Wait = 1, Lift = 2, Turn = 3, Walk = 4 };
constexpr uint8_t kAnimThrowerStart = 0x19, kAnimThrowerLift = 0xf;

uint32_t countUp(TickCastFields& walker) { // ++ticks
    const uint32_t n = walker.ticks + 1;
    walker.ticks = n;
    return n;
}
// exe 0x4d1260
void jumper(AnimatedActor& actor, TickCastFields& walker) {
    switch (JumperPhase(actor.subState)) {
    case JumperPhase::Walk:
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (stepSub() == 4) {
            actor.velocity.z = int16_t(0xfd00);
            if (stepTick() == 0xa) actor.subState = uint8_t(JumperPhase::Run);
        }
        break;
    case JumperPhase::Run: {
        integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
        const uint32_t n = countUp(walker);
        if (n < 0x5a) {
            if (n > 0x2c) turn(actor, -0x1c);
            else if (n > 7) turn(actor, 0x10);
        }
        if (stepSub() == 5 && stepTick() == 1) {
            actor.pos.x.whole = 0x200;
            actor.pos.y.whole = int16_t(0xfa80);
            actor.pos.z.whole = 0xa00;
            actor.rotation.y = 0;
            actor.subState = uint8_t(JumperPhase::Climb);
            walker.ticks = 0;
        }
        break;
    }
    case JumperPhase::Climb:
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0xcu);
        if (countUp(walker) > 0x1c) actor.subState = uint8_t(JumperPhase::Done);
        break;
    default: break;
    }
}
// exe 0x4d1350
void strider(AnimatedActor& actor, TickCastFields& walker) {
    if (actor.subState != 0) return;
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    const uint32_t n = countUp(walker);
    if (n < 0x52 && n > 0x32) turn(actor, 0x10);
    if (walker.ticks > 0x78) ++actor.subState;
}
// exe 0x4d13a0 (by its phase, [0x4d1490])
void thrower(AnimatedActor& actor, TickCastFields& walker) {
    switch (ThrowerPhase(actor.subState)) {
    case ThrowerPhase::Watch: // exe 0x4d13c0: the animation stops at tick 0x53
        if (stepTick() == 0x53) {
            walker.animRuns = 0;
            ++actor.subState;
        }
        break;
    case ThrowerPhase::Wait: // exe 0x4d13df
        if (stepSub() == 1 && stepTick() == 0xa) {
            walker.animRuns = 1;
            actor.anim = 0;
            ++actor.subState;
        }
        break;
    case ThrowerPhase::Lift: // exe 0x4d140f
        if (stepSub() == 4) {
            actor.anim = kAnimThrowerLift;
            ++actor.subState;
        }
        break;
    case ThrowerPhase::Turn: // exe 0x4d1427
        turn(actor, 0x40);
        if (countUp(walker) > 0x1f) {
            walker.ticks = 0;
            ++actor.subState;
        }
        break;
    case ThrowerPhase::Walk: // exe 0x4d1454
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (countUp(walker) > 0x3c) {
            walker.ticks = 0;
            ++actor.subState;
        }
        break;
    default: break;
    }
}
void depthsCast(AnimatedActor& actor) {
    TickCastFields& walker = classState<TickCastFields>(actor);
    castHead(actor);
    switch (CastState(actor.state)) {
    case CastState::Bind: { // exe 0x4d1100
        if (!bound(actor)) return;
        const DepthsVariant variant = DepthsVariant(actor.variant);
        actor.flags |= kActorDrawn;
        actor.anim = variant == DepthsVariant::Jumper || variant == DepthsVariant::Strider ? 0 : kAnimThrowerStart;
        actor.prevAnim = kNoAnim;
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(0xff60);
        actor.velocity.z = int16_t(variant == DepthsVariant::Jumper    ? 0xff00
                                   : variant == DepthsVariant::Strider ? 0xfe00
                                                                       : 0xfc00);
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        clearContact(actor);
        walker.ticks = 0;
        walker.animRuns = 1;
        if (DepthsVariant(actor.variant) == DepthsVariant::Thrower)
            if (const uint32_t prop = spawnProp(actor, kThrownPropSubtype)) classState<ThrownPropParams>(staticActorAt(prop)).carrier = &actor;
        enterRun(actor);
        break;
    }
    case CastState::Run: // exe 0x4d1240
        switch (DepthsVariant(actor.variant)) {
        case DepthsVariant::Jumper: jumper(actor, walker); break;
        case DepthsVariant::Strider: strider(actor, walker); break;
        case DepthsVariant::Thrower: thrower(actor, walker); break;
        default: notPortedAt(kStoryPort, "0x4d1040 variant", actor.variant);
        }
        break;
    case CastState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4d1040 state", actor.state);
    }
    castTail(actor, &walker);
}

// ---- exe 0x4d14b0 (ST28 type 0x48 sub 3): states [0x8952f4], phases [0x4d16e8] ---------------------------------------
enum class DropperPhase : uint8_t { Start = 0, Drop = 1, Rise = 2, Risen = 3 };
constexpr uint8_t kAnimDropper = 0xf;
const DataTable<ScalarTable<uint8_t, 3>> sparkBones{TableId::FireRuinCutsceneCastSparkBones}; // 3, 4, 7
constexpr uint8_t kSparkVariant = 8;
constexpr int16_t kRiseGravity = 0x40;
constexpr uint8_t kRiseDustVariant = 1;

// exe 0x4d15cf: the prop dropped (exe 0x5b20e0), three spark emitters at its bones; its fall stopped, its gravity set
void dropProp(AnimatedActor& actor) {
    if (const uint32_t prop = spawnProp(actor, kDroppedPropSubtype)) classState<DroppedPropParams>(staticActorAt(prop)).carrier = &actor;
    for (size_t i = 0; i < sparkBones.size(); ++i) {
        static constexpr int16_t kZero[3] = {0, 0, 0};
        int16_t point[3];
        bonePointOffset(actor, sparkBones[i], kZero, point); // exe 0x5e1970
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (!slot) continue;
        StaticActor& spark = staticActorAt(slot);
        spark.flags = kActorLive;
        spark.type = kSparkEmitterStageType;
        spark.variant = kSparkVariant;
        spark.loopFlags |= kActorSweepGroup2;
        spark.pos.x.whole = point[0];
        spark.pos.y.whole = point[1];
        spark.pos.z.whole = point[2];
        spark_emitter_state::SparkEmitterFields& params = classState<spark_emitter_state::SparkEmitterFields>(staticActorAt(slot));
        params.driftX = 0;
        params.driftZ = 0;
        params.lifetime = 0;
    }
    actor.velocity.y = 0;
    ++actor.subState;
    actor.acceleration.y = kRiseGravity;
}
void propDropper(const PlayerEnv& env, AnimatedActor& actor) {
    DropperFields& walker = classState<DropperFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    switch (CastState(actor.state)) {
    case CastState::Bind: // exe 0x4d1540
        if (!bound(actor)) return;
        actor.anim = kAnimDropper;
        actor.flags |= kActorDrawn;
        actor.prevAnim = kNoAnim;
        actor.velocity.setXY(0, 0);
        actor.velocity.setZSpare(0, 0);
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.collisionBox.pointAt(TableId::FireRuinCutsceneCastDropperBox);
        actor.enterStateBytes(uint8_t(CastState::Run));
        break;
    case CastState::Run: // exe 0x4d15a0
        switch (DropperPhase(actor.subState)) {
        case DropperPhase::Start: actor.subState = uint8_t(DropperPhase::Drop); break; // exe 0x4d15c4
        case DropperPhase::Drop: dropProp(actor); break;
        case DropperPhase::Rise: // exe 0x4d166d: once let go, rises 6 a tick until pushed out; then the ground height
            if (PropCarrierParams(actor).letGo == 0) break;
            actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 6u);
            if (actorPushOut(env, actor) & kWallOnY) { // exe 0x410960
                ++actor.subState;
                walker.groundY = int16_t(uint16_t(uint32_t(actorGround(env, actor)))); // exe 0x40f190
            }
            break;
        case DropperPhase::Risen: // exe 0x4d16a5: dust at (x, ground, z) every 8th frame
            if ((gameState().frame & 7) == 0) {
                const int16_t point[3] = {actor.pos.x.whole, walker.groundY, actor.pos.z.whole};
                footstepDustAt(actor, kRiseDustVariant, point); // exe 0x5ad170
            }
            actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 6u);
            break;
        default: break;
        }
        break;
    case CastState::Free: freeActor(actor); return; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4d14b0 state", actor.state);
    }
    // its animation whatever its flags; the shadow (exe 0x5f20f0(actor, 0x60, 0xc0, 0)) is render output
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30
    else animStepPose(actor);                                            // exe 0x5e1e80
}

} // namespace

void propHolderUpdate(const PlayerEnv&, uint32_t actor) { propHolder(animatedActorAt(actor)); }
void entranceCastUpdate(const PlayerEnv&, uint32_t actor) { entranceCast(animatedActorAt(actor)); }
void depthsCastUpdate(const PlayerEnv&, uint32_t actor) { depthsCast(animatedActorAt(actor)); }
void propDropperUpdate(const PlayerEnv& env, uint32_t actor) { propDropper(env, animatedActorAt(actor)); }

} // namespace od::port
