// The town's type-1 statics of ST08 / ST09 (see town_props.hpp), moved here from game/actors.
#include "content/yosyonke/town_props.hpp"
#include "content/yosyonke/town_props_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/carry.hpp"
#include "game/collision.hpp"
#include "game/event_state.hpp"
#include "game/hit_list.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/rewards.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/spawn.hpp"
#include "game/actors.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace town_props_state;

namespace {

constexpr const char* kTownPropsPort = "town props"; // the area of its port-gap errors

constexpr uint32_t kPropTalkEventParam = 0x12; // the talk event's mode / flags (exe 0x543b40)

// exe 0x58e060 / 0x58ed30: the talk of a prop -- the player's talk starts the event of `message` (sub-state 1), the
// event's end goes back (0); a stand-on solid every tick
void talkProp(StaticActor& actor, uint16_t message) {
    if (actor.subState == 0) {
        if (player().talksTo(actor) && eventState().active == 0) {
            eventStartAt(0, nullptr, kPropTalkEventParam, 0, message);
            actor.subState = 1;
        }
    } else if (actor.subState == 1 && eventState().active == 0) {
        actor.subState = 0;
    }
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
}

// ---- exe 0x58dff0: the ST08 static type 1 (states 0x8cd9e4) ---------------------------------------------------------
enum class St08PropState : uint8_t {
    Init = 0,  // exe 0x58e010: bind; variant 4: the boxes and the talk
    Main = 1,  // exe 0x58e060: variant 4 talks
    Inert = 2,     // exe 0x52ef00 (ret): nothing runs
    InertCopy = 3, // the table's second entry for the same empty routine
};
constexpr uint8_t kSt08TalkingVariant = 4;
constexpr uint16_t kSt08PropMessage = 0x5f;
const DataTable<CollisionBoxRecord> st08PropCollisionBox{TableId::TownPropsSt08PropCollisionBox};
const DataTable<InteractionBoxRecord> st08PropTalkBox{TableId::TownPropsSt08PropTalkBox};

// exe 0x58dff0's body: the state (variant 4 talks)
void st08Prop(StaticActor& actor) {
    switch (St08PropState(actor.state)) {
    case St08PropState::Init:
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        if (actor.variant == kSt08TalkingVariant) {
            actor.collisionBox.pointAt(st08PropCollisionBox.stateAddress());
            actor.interactionBox.pointAt(st08PropTalkBox.stateAddress());
            actor.interaction = kInteractTalk;
            actor.airborne = 0;
            actor.onPlatform = 0;
            actor.platform = nullptr;
            actor.lastMover = nullptr;
        }
        actor.state = uint8_t(St08PropState::Main);
        return;
    case St08PropState::Main:
        if (actor.variant == kSt08TalkingVariant) talkProp(actor, kSt08PropMessage);
        return;
    case St08PropState::Inert:
    case St08PropState::InertCopy: return;
    default: notPortedAt(kTownPropsPort, "ST08 static type 1 state", actor.state);
    }
}

constexpr uint16_t kPropKicked = 0x1, kPropFlying = 0x2, kPropLifted = 0x4;

enum class St09PropState : uint8_t {
    Init = 0,    // exe 0x58e100
    Main = 1,    // exe 0x58e1e0: by variant
    Free = 2,    // exe 0x5afdd0 (exe 0x5e1540)
    Rebind = 3,  // exe 0x58eeb0: bind again, then state 4
    Turning = 4, // exe 0x58eee0: its counter; by variant: a solid, rolling, rocking
};
enum class St09TurningVariant : uint8_t { Solid = 0, Rolling = 1, Rocking = 2 };
constexpr int16_t kRollStep = 0x10;
enum class St09PropVariant : uint8_t {
    Breakable = 0, // exe 0x58e240
    Kickable = 1,  // exe 0x58e340: kicked 0x58e360 / lifted 0x58e600
    Solid = 4,     // exe 0x58e6a0: a solid (+ draw primitives: render output)
    Talking = 5,   // exe 0x58ed30
};
constexpr uint8_t kSt09SceneryFirst = 2, kSt09SceneryLast = 4; // the late-loop variants (scenery)
constexpr uint16_t kSt09PropMessage = 0x13;
constexpr uint16_t kSoundBreak = 0x285, kSoundBump = 0x286;
constexpr int kBalanceLoss = -8;              // the balance a broken / bumped prop costs (exe 0x5e51d0)
constexpr uint32_t kHitKick = 0x80000;        // the hit-box result bit of a kick
constexpr uint16_t kDebrisTicks = 0x10, kDebrisHide = 8;
constexpr uint16_t kKickTicks = 10;
constexpr int16_t kKickUp = 0x20, kKickSpeed = 0x400, kKickBrake = -8, kFallAccel = -0x20;
constexpr int kGroundSnap = 0x18;

const DataTable<CollisionBoxRecord> st09KickableCollisionBox{TableId::TownPropsSt09KickableCollisionBox};
const DataTable<InteractionBoxRecord> st09KickableTalkBox{TableId::TownPropsSt09KickableTalkBox};
const DataTable<CollisionBoxRecord> st09TalkingCollisionBox{TableId::TownPropsSt09TalkingCollisionBox};
const DataTable<InteractionBoxRecord> st09TalkingTalkBox{TableId::TownPropsSt09TalkingTalkBox};


// exe 0x58eda0: a debris spark of the breaking prop (effect pool global type 0 variant 3), +-0x20 around it
void st09Debris(const StaticActor& actor) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& spark = effectActorAt(address);
    spark.flags = kActorLive;
    spark.type = 0;
    spark.loopFlags |= kActorLateLoop | kActorSweepGroup1;
    spark.variant = 3;
    uint32_t r = rngStep();
    spark.pos.x = int16_t(int32_t(actor.pos.x.whole) - int32_t(r & 0x3f) + 0x20);
    r = rngStep();
    spark.pos.y = int16_t(int32_t(actor.pos.y.whole) - int32_t((r >> 8) & 0x3f) - 0x40);
    r = rngStep();
    spark.params[0] = 0xc0; // the spark's size word +0xc = 0xc0
    spark.params[1] = 0;
    spark.pos.z = int16_t(int32_t(actor.pos.z.whole) - int32_t((r >> 16) & 0x3f) + 0x20);
}
// exe 0x58ee60: what the broken prop leaves (static global type 1 variant 1, stage-class flag), 0x10 up, same yaw
void st09Remains(const StaticActor& actor) {
    const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!address) return;
    StaticActor& remains = staticActorAt(address);
    remains.flags = kActorLive | kActorDrawn;
    remains.loopFlags |= kActorSweepGroup2;
    remains.type = 1;
    remains.variant = 1;
    remains.pos.x.whole = actor.pos.x.whole;
    remains.pos.y.whole = int16_t(int32_t(actor.pos.y.whole) - 0x10);
    remains.pos.z.whole = actor.pos.z.whole;
    remains.rotation.y = actor.rotation.y;
}
// exe 0x58e360: the kicked block. Every bump while flying costs balance with the bump sound.
void st09Kicked(const PlayerEnv& env, StaticActor& actor, St09PropFields& prop) {
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    const int angle = angleOf(actor.pos, player().pos); // exe 0x5e3bc0
    prop.playerAngle = int16_t(angle);
    if (int16_t(angle) >= kHalfTurn) prop.playerAngle = int16_t(angle - 0x1000);
    if (actor.subState == 0) {
        if (prop.flags & kPropKicked) {
            soundPlayAtPos(kSoundBump, actor.pos);
            balanceAdd(kBalanceLoss);
            prop.flags |= kPropFlying;
            prop.timer = kKickTicks;
            prop.travelYaw = player().rotation.y;
            actor.velocity.y = kKickUp;
            actor.velocity.z = kKickSpeed;
            if (cameraState().lookPitch > 0) actor.velocity.y = int16_t(kKickUp - int32_t(cameraState().lookPitch));
            actor.acceleration.z = kKickBrake;
            actor.subState = 1;
        }
    } else if (actor.subState == 1) {
        --prop.timer;
        if (prop.timer == 0) {
            prop.flags &= uint16_t(~kPropKicked);
            actor.subState = 0;
        }
    }
    actor.velocity.y += uint16_t(actor.acceleration.y);
    actor.velocity.z += uint16_t(actor.acceleration.z);
    if (actor.velocity.z < 0) {
        actor.velocity.z = 0;
        actor.acceleration.z = 0;
        if (prop.flags & kPropFlying) prop.flags &= uint16_t(~kPropFlying);
    }
    // exe 0x5e36f0
    moveAlongYaw(actor.pos, prop.travelYaw, 0, -int(actor.velocity.y), -int(actor.velocity.z));
    if (int32_t(actorTestMove(env, actor)) < 0) {             // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor);         // exe 0x410960
        if (r & kWallOnSides) {
            soundPlayAtPos(kSoundBump, actor.pos);
            balanceAdd(kBalanceLoss);
            int16_t pitch = 0, yaw = prop.travelYaw;
            reflectRotationOffWalls(pitch, yaw, r); // exe 0x410740 on the triple {0, travel yaw, 0}
            prop.travelYaw = yaw;
        }
        if (r & kWallOnYPlus) actor.velocity.y = int16_t(uint16_t(0u - uint16_t(actor.velocity.y)));
    }
    const int g = actorGround(env, actor); // exe 0x40f190
    if (actor.pos.y.whole >= g - kGroundSnap) {
        actor.velocity.y = 0;
        actor.acceleration.y = 0;
    } else {
        actor.acceleration.y = kFallAccel;
    }
    if (actor.actorHit != 0u) {
        if (prop.flags & kPropFlying) {
            soundPlayAtPos(kSoundBump, actor.pos);
            balanceAdd(kBalanceLoss);
        }
        if (actor.actorHit & kSolidAboveOrBelow) actor.actorHit &= 0xf0ffu;
        int16_t pitch = 0, yaw = prop.travelYaw;
        reflectOffActorHit(pitch, yaw, actor.actorHit); // exe 0x4108b0 on the triple {0, travel yaw, 0}
        prop.travelYaw = yaw;
    } else {
        actorCollisionSubmitMover(actor); // exe 0x4140b0
    }
    actor.actorHit = 0;
    if (prop.hitBox.result & kHitKick) prop.flags |= kPropKicked;
    if (playerLiftTargetCheck(actor, 0)) prop.flags |= kPropLifted; // exe 0x573a60
    prop.hitBox.result = 0;
    prop.hitBox.previousX = actor.prevPos.x.whole;
    prop.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    hitListSubmit(pos, static_cast<HitBoxHead&>(prop.hitBox),
                  hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x30),
                  kHitAttackSolid); // exe 0x5e4120
}
// exe 0x58e600: lifted -- at the player's hands (0x10 up) until released, then thrown along the player's yaw (any
// release: no drop branch here) and the kicked step the same tick
void st09Lifted(const PlayerEnv& env, StaticActor& actor, St09PropFields& prop) {
    carryAtHands(actor, 0, -0x10, 0); // exe 0x573ab0
    if (carryReleased() == CarryRelease::Held) return; // exe 0x573950
    int32_t v[3];
    playerAimVector(0, 0x100, 0x300, v); // exe 0x5e3e50
    prop.travelYaw = actor.rotation.y;
    actor.velocity.x = 0;
    actor.velocity.y = int16_t(v[1]);
    actor.velocity.z = int16_t(v[2]);
    actor.acceleration.y = kFallAccel;
    actor.acceleration.z = kKickBrake;
    carryDropFromPlayer(actor); // exe 0x573990
    prop.flags = uint16_t((prop.flags & ~kPropLifted) | kPropFlying);
    st09Kicked(env, actor, prop);
}

// exe 0x58e0e0's body: the state by variant (breakable / kicked / scenery / solid)
void st09Prop(const PlayerEnv& env, StaticActor& actor) {
    St09PropFields& prop = classState<St09PropFields>(actor);
    switch (St09PropState(actor.state)) {
    case St09PropState::Init: {
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        actor.clearMotion();
        actor.clearClassBlock();
        const uint8_t v = actor.variant;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        if (v == uint8_t(St09PropVariant::Kickable)) {
            actor.collisionBox.pointAt(st09KickableCollisionBox.stateAddress());
            actor.interactionBox.pointAt(st09KickableTalkBox.stateAddress());
            actor.interaction = kInteractLift;
            ++actor.state;
            actor.subState = 0;
            return;
        }
        if (v >= kSt09SceneryFirst && v <= kSt09SceneryLast) {
            actor.loopFlags |= kActorLateLoop;
            actor.flags |= kActorScenery;
            ++actor.state;
            actor.subState = 0;
            return;
        }
        if (v == uint8_t(St09PropVariant::Talking)) {
            actor.collisionBox.pointAt(st09TalkingCollisionBox.stateAddress());
            actor.interactionBox.pointAt(st09TalkingTalkBox.stateAddress());
            actor.interaction = kInteractTalk;
            actor.airborne = 0;
            actor.onPlatform = 0;
            actor.platform = nullptr;
            actor.lastMover = nullptr;
        }
        actor.subState = 0;
        ++actor.state;
        return;
    }
    case St09PropState::Main:
        switch (St09PropVariant(actor.variant)) {
        case St09PropVariant::Breakable:
            if (actor.subState == 0) {
                actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
                if (prop.hitBox.result & kHitKick) {
                    soundPlayAtPos(kSoundBreak, actor.pos);
                    balanceAdd(kBalanceLoss);
                    prop.timer = kDebrisTicks;
                    ++actor.subState;
                }
                prop.hitBox.result = 0;
                prop.hitBox.previousX = actor.prevPos.x.whole;
                prop.hitBox.previousZ = actor.prevPos.z.whole;
                const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole - 0x20)),
                                        actor.pos.z.whole};
                hitListSubmit(pos, static_cast<HitBoxHead&>(prop.hitBox),
                              hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0x40),
                              kHitAttackHarmless | kHitAttackBump);
            } else if (actor.subState == 1) {
                st09Debris(actor);
                if (int16_t(prop.timer) == kDebrisHide) actor.flags &= uint8_t(~kActorDrawn);
                --prop.timer;
                if (prop.timer != 0) return;
                st09Remains(actor);
                ++actor.state;
            }
            return;
        case St09PropVariant::Kickable:
            if (!(prop.flags & kPropLifted)) st09Kicked(env, actor, prop);
            else st09Lifted(env, actor, prop);
            return;
        case St09PropVariant::Solid:
            actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
            return;
        case St09PropVariant::Talking: talkProp(actor, kSt09PropMessage); return;
        default: return;
        }
    case St09PropState::Free: freeActor(actor); return;
    case St09PropState::Rebind:
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        ++actor.state;
        return;
    case St09PropState::Turning: { // its first parameter counts the ticks
        const uint8_t t = uint8_t(actor.params[0] + 1u);
        actor.params[0] = t;
        switch (St09TurningVariant(actor.variant)) {
        case St09TurningVariant::Solid: actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); return;
        case St09TurningVariant::Rolling:
            actor.rotation.z = int16_t((uint32_t(uint16_t(actor.rotation.z)) - kRollStep) & kAngleMask);
            return;
        case St09TurningVariant::Rocking: // the pitch from the sine of the counter (0x80 steps a turn)
            actor.rotation.x = int16_t(fixedSin(int(uint32_t(t & 0x7fu) << 5)) >> 7);
            return;
        default: return;
        }
    }
    default: notPortedAt(kTownPropsPort, "ST09 static type 1 state (table 0x8cdb6c has 5)", actor.state);
    }
}

} // namespace

void st08PropUpdate(const PlayerEnv&, uint32_t address) { st08Prop(staticActorAt(address)); }

void st09PropUpdate(const PlayerEnv& env, uint32_t address) { st09Prop(env, staticActorAt(address)); }

} // namespace od::port
