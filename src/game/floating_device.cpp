// The floating device class 0x4f2c40 (see floating_device.hpp). States [0x896c44]: 0 init 0x4f2d30, 1 0x4f2dd0 (sub-
// states [0x896c50]: 0 0x4f2df0 wait for the examine, 1 0x4f2ea0 the talk), 2 0x468fa0 (freed).
#include "game/actor_state.hpp"
#include "game/floating_device.hpp"
#include "game/floating_device_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/fixed_point.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace floating_device_state;

namespace {

constexpr const char* kDevicePort = "floating device port"; // the area of its port-gap errors


enum class DeviceState : uint8_t {
    Init = 0,  // exe 0x4f2d30
    Main = 1,  // exe 0x4f2dd0
    Freed = 2, // exe 0x468fa0
};
enum class DeviceSub : uint8_t {
    Wait = 0, // exe 0x4f2df0
    Talk = 1, // exe 0x4f2ea0
};
enum class DeviceTalkStep : uint8_t { Start = 0, Running = 1, Over = 2 };

const DataTable<CollisionBoxRecord> deviceCollisionBox{TableId::FloatingDeviceCollisionBox};
const DataTable<InteractionBoxRecord> deviceTalkBox{TableId::FloatingDeviceTalkBox};
constexpr uint32_t kDeviceBone = 1; // the bone drawn semi-transparent
constexpr int16_t kBobBase = 8;     // the bob's centre below the base height
// the examine's events: the one that leads out (mode 4: the player stands, then the item get / choice) and the talk
constexpr uint32_t kLeadOutEvent = uint32_t(EventMode::StandThenItem) << 16 | kEventWaitsWindow;
constexpr uint32_t kTalkEvent = uint32_t(EventMode::Talk) << 16 | kEventWaitsWindow | kEventNoTurn;

void deviceInit(AnimatedActor& actor, FloatingDeviceFields& f) { // exe 0x4f2d30
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    actor.clearClassBlock();
    actor.collisionBox.pointAt(deviceCollisionBox.stateAddress());
    actor.interaction = kInteractTalk;
    actor.interactionBox.pointAt(deviceTalkBox.stateAddress());
    actor.anim = 0;
    actor.prevAnim = 0xff;
    actor.airborne = 0;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.spin.setXY(0, 0);
    actor.spin.setZSpare(0, 0);
    setBoneSemiTransparency(actor, kDeviceBone, 1, 0);
    f.bobPhase = 0;
    f.baseHeight = actor.pos.y.whole;
    actor.enterState(uint8_t(DeviceState::Main));
}

void deviceWait(AnimatedActor& actor, FloatingDeviceFields& f) { // exe 0x4f2df0: the examine
    if (f.talking != 0) return;
    if (!player().talksTo(actor) || eventState().active != 0) return;
    if (f.leadsOut != 0) {
        // exe 0x543b40(&{x, y, z}, 0, mode 4, 0, message): the s16 point as 16.16 in the event's start point
        EventState& ev = eventState();
        eventStartPoint().point.x = wholeToFixed16(actor.pos.x.whole);
        eventStartPoint().point.y = wholeToFixed16(actor.pos.y.whole);
        eventStartPoint().point.z = wholeToFixed16(actor.pos.z.whole);
        eventStart(&eventStartPoint().point, nullptr, kLeadOutEvent, 0, f.message);
        actor.state = uint8_t(DeviceState::Freed);
        actor.subState = 0;
        actor.enterStep(0);
        return;
    }
    eventStart(&actor.pos, &actor.rotation, kTalkEvent, 0, f.message); // exe 0x543af0
    f.talking = 1;
    actor.subState = uint8_t(DeviceSub::Talk);
    actor.enterStep(0);
}

void deviceTalk(AnimatedActor& actor, FloatingDeviceFields& f) { // exe 0x4f2ea0: the event over -> back to the wait
    switch (DeviceTalkStep(actor.step)) {
    case DeviceTalkStep::Start: actor.enterStep(uint8_t(DeviceTalkStep::Running)); [[fallthrough]];
    case DeviceTalkStep::Running:
        if (eventState().active == 0) actor.enterStep(uint8_t(DeviceTalkStep::Over));
        return;
    case DeviceTalkStep::Over:
        f.talking = 0;
        actor.subState = uint8_t(DeviceSub::Wait);
        actor.enterStep(0);
        return;
    default: return;
    }
}

void device(AnimatedActor& actor) { // exe 0x4f2c40
    FloatingDeviceFields& f = classState<FloatingDeviceFields>(actor);
    actor.savePreviousPose();
    switch (DeviceState(actor.state)) {
    case DeviceState::Init: deviceInit(actor, f); break;
    case DeviceState::Main: // exe 0x4f2dd0
        switch (DeviceSub(actor.subState)) {
        case DeviceSub::Wait: deviceWait(actor, f); break;
        case DeviceSub::Talk: deviceTalk(actor, f); break;
        default: notPortedAt(kDevicePort, "0x4f2c40 sub-state", actor.subState);
        }
        break;
    case DeviceState::Freed: freeActor(actor); break; // exe 0x468fa0
    default: notPortedAt(kDevicePort, "0x4f2c40 state", actor.state);
    }
    const int16_t scale = int16_t(uint16_t(uint32_t(f.scaleSteps) << 5));
    actor.scale.z = scale;
    actor.scale.y = scale;
    actor.scale.x = scale;
    actor.rotation.y = int16_t((uint32_t(f.spin) + uint16_t(actor.rotation.y)) & kAngleMask);
    const uint32_t phase = uint8_t(uint8_t(f.bobPhase) + 1) & 0x3fu;
    f.bobPhase = uint16_t(phase);
    actor.pos.y.whole = int16_t(uint32_t(int32_t(cos64(phase)) >> 8) + uint16_t(f.baseHeight) - uint32_t(kBobBase));
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0);
    else animStepPose(actor);
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050(actor, 1, 0xffff, 0)
}

} // namespace

void floatingDeviceUpdate(const PlayerEnv&, uint32_t actor) { device(animatedActorAt(actor)); }

} // namespace od::port
