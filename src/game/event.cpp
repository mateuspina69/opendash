// The event runner (see event.hpp), transcribed from dash2.exe one function per exe function.
#include "engine/data_source.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"

#include <cstdio>

#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/camera_state.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"

namespace od::port {

namespace {

constexpr const char* kEventPort = "message port"; // the area of its port-gap errors (the message system's)

// The runners of the modes (exe table 0x8ae928, by EventMode: the named routines, engine/routines.csv)
const DataTable<RoutineTable<5>> eventRunners{TableId::EventRunners};

constexpr int kSpeakerTurnStep = 0x80;  // the speaker's yaw per tick toward its target
constexpr int16_t kStandTicks = 0x13;   // the stand before a chest's item get
constexpr uint8_t kItemGetFromWindow = 1; // the item get's state that opens its window (the stand hands over to it)
constexpr int8_t kNoWindow = -1;          // the event's window: the highest free one

// turn the speaker's yaw toward `target` by kSpeakerTurnStep a tick (the exe inlines it in 0x5436a0); true once it is
// there (snapped onto it within a step)
bool turnSpeakerYaw(ShortVec3* rotation, int16_t target) {
    const int16_t cur = rotation->y;
    int16_t d = int16_t((target - cur) & kAngleMask);
    if (d > kHalfTurn) d = int16_t(d - kFullTurn);
    if ((d < 0 ? -d : d) > kSpeakerTurnStep) {
        rotation->y = int16_t(cur + (d < 1 ? -kSpeakerTurnStep : kSpeakerTurnStep));
        return false;
    }
    rotation->y = target;
    return true;
}

// the event's window opened on its message of the stage's bank (a message id < 0: none)
void openEventWindow(EventState& ev) {
    if (ev.message >= 0) windowOpen(int8_t(ev.window), kMessageBank, ev.message);
}

// exe 0x5436a0: the talk event (TalkEventState)
void talkEvent(EventState& ev) {
    Player& p = player();
    const int8_t st = int8_t(ev.state);
    if (uint32_t(int32_t(st)) > uint32_t(TalkEventState::WaitClosed)) return;
    auto next = [&]() { // 0x543809 / 0x54381f: the turn states only when a speaker is turned
        ev.state += ev.turnedRotation ? 1u : 2u;
    };
    switch (TalkEventState(st)) {
    case TalkEventState::WaitPlayer:
        if (ev.flags & kEventInvulnerable) p.invulnerable = 1;
        if (p.sub() != PlayerSub::Idle) {
            if (p.airborne == kAirborneHanging) p.airborne = kAirborneFalling;
            return;
        }
        ev.state += 1u;
        [[fallthrough]];
    case TalkEventState::Open: {
        const bool keepTarget = p.carryFlags != 0;
        p.moveToSub(PlayerSub::Talk, TalkingStep::Enter);
        if (!keepTarget) p.interactionTarget.clear();
        openEventWindow(ev);
        if (ev.speakerPosition == nullptr) {
            ev.state += 2u;
            return;
        }
        cameraTalkStart(ev.speakerPosition);
        next();
        return;
    }
    case TalkEventState::TurnSpeaker:
        if (turnSpeakerYaw(ev.turnedRotation, ev.faceYaw) && cameraState().blendRequest == 0) ev.state += 1u;
        return;
    case TalkEventState::Release:
        messageSystem().waitBits &= ~(1u << (uint8_t(ev.window) & 0x1f));
        ev.state += 1u;
        [[fallthrough]];
    case TalkEventState::WaitTyping: {
        bool instant = false;
        if (p.sub() == PlayerSub::KnockBack || p.sub() == PlayerSub::KnockDown) {
            windowClose(int8_t(ev.window), WindowClose::Force);
            instant = true;
        }
        if (windowStatus(uint8_t(ev.window)) & kWindowStatusOpen) return;
        if (ev.speakerPosition == nullptr) {
            ev.state += 2u;
            return;
        }
        cameraTalkEnd(instant);
        next();
        return;
    }
    case TalkEventState::TurnBack:
        if (turnSpeakerYaw(ev.turnedRotation, ev.restoreYaw) && cameraState().blendRequest == 0) ev.state += 1u;
        return;
    case TalkEventState::WaitClosed:
        if (windowStatus(uint8_t(ev.window)) != 0) return;
        if ((ev.flags & kEventKeepsInput) == 0) p.inputMode = 0;
        if (ev.flags & kEventInvulnerable) p.invulnerable = 0;
        ev.active = 0;
        return;
    }
}

// exe 0x5438f0: the Pose / PoseThenItem modes -- the player's event pose (sub-state EventPose: animations
// PlayerAnim::EventPose / EventPoseHold) with an optional window; Pose ends with the pose, PoseThenItem hands over to
// the item get (its state 0)
void poseEvent(EventState& ev) {
    Player& p = player();
    if (ev.state == 0) {
        p.moveToSub(PlayerSub::EventPose, EventPoseStep::Enter);
        p.interactionTarget.clear();
        p.immunity = kImmuneHeld;
        openEventWindow(ev);
        ev.state += 1u;
    } else if (ev.state == 1 && p.sub() != PlayerSub::EventPose) {
        if (ev.eventMode() == EventMode::Pose) {
            ev.active = 0;
            return;
        }
        ev.switchTo(EventMode::ItemGet, 0);
        p.inputMode = kInputOff;
        p.immunity = kImmuneHeld;
    }
}

// exe 0x543970: the ItemGet mode -- the player's item-get pose (sub-state ItemGet: PlayerAnim::ItemGet / ItemGetHold),
// the jingle (unless kEventNoJingle), the window; it ends when the window is closed and the pose is over
void itemGetEvent(EventState& ev) {
    Player& p = player();
    switch (ev.state) {
    case 0:
        p.interactionTarget.clear();
        p.immunity = kImmuneHeld;
        if (p.sub() != PlayerSub::Idle && p.sub() != PlayerSub::Run) return;
        ev.state += 1u;
        [[fallthrough]];
    case kItemGetFromWindow:
        p.moveToSub(PlayerSub::ItemGet, EventPoseStep::Enter);
        if (!(ev.flags & kEventNoJingle)) soundPlayFlatForced(uint16_t(UiSound::ItemJingle));
        openEventWindow(ev);
        ev.state += 1u;
        return;
    case 2:
        if ((windowStatus(uint8_t(ev.window)) & kWindowStatusOpen) == 0) ev.state += 1u;
        return;
    case 3:
        if (windowStatus(uint8_t(ev.window)) == 0 && p.sub() != PlayerSub::ItemGet) ev.active = 0;
        return;
    default: return;
    }
}

// exe 0x543a40: the StandThenItem mode -- the player stands (sub-state Talk, the aim recentred) for kStandTicks, then
// the item get from its window (the treasure chests: exe 0x543b40(pos, 0, 0x40023, 0, message))
void standEvent(EventState& ev) {
    Player& p = player();
    if (ev.state == 0) {
        p.moveToSub(PlayerSub::Talk, TalkingStep::Enter);
        p.interactionTarget.clear();
        p.recentreAim = 1;
        p.immunity = kImmuneHeld;
        ev.countdown = kStandTicks;
        ev.state += 1u;
    } else if (ev.state == 1) {
        ev.countdown -= 1u;
        if (ev.countdown == 0) ev.switchTo(EventMode::ItemGet, kItemGetFromWindow);
    }
}

} // namespace

void eventTick() {
    EventState& ev = eventState();
    if (ev.active == 0) return;
    const uint8_t mode = ev.mode;
    const RoutineRef& h = eventRunners[mode];
    switch (h.id()) {
    case RoutineId::EventTalk: talkEvent(ev); return;
    case RoutineId::EventPose: poseEvent(ev); return;
    case RoutineId::EventItemGet: itemGetEvent(ev); return;
    case RoutineId::EventStandThenItem: standEvent(ev); return;
    default: break;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "event mode %u (handler %06x)", mode, h.pointerValue());
    notPorted(kEventPort, buf);
}

void eventStart(const Fixed16Vec3* pos, ShortVec3* rot, uint32_t param, uint16_t window, uint16_t message) {
    const GameMode mode = gameState().gameMode();
    if (int8_t(mode) < int8_t(GameMode::Field) || int8_t(mode) > int8_t(GameMode::Pause)) return;
    EventState& ev = eventState();
    Player& p = player();
    if (windowStatus(uint8_t(window)) != 0) windowClose(int8_t(ev.window), WindowClose::Force);
    ev.setModeWord(uint16_t(int32_t(param) >> 16));
    ev.active = 1;
    ev.flags = uint8_t(param);
    ev.window = int16_t(window);
    ev.message = int16_t(message);
    ev.countdown = 0;
    if (pos && ev.eventMode() == EventMode::Talk) {
        ev.speakerPosition = pos;
        if (param & kEventNoTurn) ev.turnedRotation = nullptr;
        else ev.turnedRotation = rot;
        const int16_t r = rot ? int16_t(uint16_t(uint16_t(rot->y) << 4)) : int16_t(0);
        ev.faceYaw = signedAngle(angleOf(p.pos, *ev.speakerPosition));
        cameraState().blendRequest = 0;
        ev.restoreYaw = int16_t(r >> 4);
        p.inputMode |= kInputOff;
        return;
    }
    ev.speakerPosition = nullptr;
    const uint16_t yaw = uint16_t(cameraState().viewAngles.y) & kAngleMask;
    ev.turnedRotation = nullptr;
    ev.restoreYaw = int16_t(yaw);
    ev.faceYaw = int16_t(yaw);
    p.inputMode |= kInputOff;
}

void eventStartAtActor(const Actor& actor, uint32_t param, uint16_t window, uint16_t message) {
    eventStart(&actor.pos, const_cast<ShortVec4*>(&actor.rotation), param, window, message);
}

void eventStartAt(uint32_t pos16, ShortVec3* rot, uint32_t param, uint16_t window, uint16_t message) {
    if (pos16 == 0) {
        eventStart(nullptr, nullptr, param, window, message);
        return;
    }
    const ShortVec3 pos = dataShortVec3(pos16);
    EventState& ev = eventState();
    eventStartPoint().point.x = int32_t(pos.x) << 16;
    eventStartPoint().point.y = int32_t(pos.y) << 16;
    eventStartPoint().point.z = int32_t(pos.z) << 16;
    eventStart(&eventStartPoint().point, rot, param, window, message);
}

void eventStartAtPoint(const RoomPlace& place, ShortVec3* rot, uint32_t param, uint16_t window, uint16_t message) {
    eventStartPoint().point.x = int32_t(place.x) << 16;
    eventStartPoint().point.y = int32_t(place.y) << 16;
    eventStartPoint().point.z = int32_t(place.z) << 16;
    eventStart(&eventStartPoint().point, rot, param, window, message);
}

void eventAbort() {
    EventState& ev = eventState();
    Player& p = player();
    ev.active = 0;
    if (ev.speakerPosition) cameraRestoreSaved();
    if (ev.turnedRotation) ev.turnedRotation->y = ev.restoreYaw;
    if (ev.flags & kEventInvulnerable) p.invulnerable = 0;
    if ((ev.flags & kEventKeepsInput) == 0) p.inputMode = 0;
}

} // namespace od::port
