// The event system's state (game/event runs it): the event request 0xa5f280 that a talk / door / chest starts (exe
// 0x543bb0), its modes, flags and the talk event's states, and the speaker point the start routines take by address.
#pragma once

#include <cstdint>

#include "game/vectors.hpp"
#include "game/state_array.hpp"

namespace od::port {

constexpr uint8_t kNoSpeakerAnimation = 0xff; // +0xb: no animation asked for (consumed)
constexpr uint8_t kSpeakerAnimationCount = 0x3f; // +0xb below this: an animation the speaker should play
// +1: the event's flags (the low byte of the start's param)
constexpr uint8_t kEventInvulnerable = 0x1; // the player is invulnerable while it runs
constexpr uint8_t kEventWaitsWindow = 0x2; // the player's pose waits for the event's window to close
constexpr uint8_t kEventKeepsInput = 0x8;  // the player's input mode stays as the event set it
constexpr uint8_t kEventNoTurn = 0x10;     // the speaker is not turned toward the player
constexpr uint8_t kEventNoJingle = 0x20;   // the item get plays without its jingle

// +2: what the event does (the runners' table 0x8ae928); the start's param is mode << 16 | flags
enum class EventMode : uint8_t {
    Talk = 0,          // the message event (exe 0x5436a0): the talk camera, the speaker turned, the window
    Pose = 1,          // the player's event pose with an optional window (exe 0x5438f0)
    ItemGet = 2,       // the item-get pose, its jingle and window (exe 0x543970)
    PoseThenItem = 3,  // the event pose, then the item get
    StandThenItem = 4, // the player stands a moment, then the item get from its window (exe 0x543a40: the chests)
};
// +3 of the talk event
enum class TalkEventState : uint8_t {
    WaitPlayer = 0,  // until the player is free (a hanging player let go)
    Open = 1,        // the player talks, the window opens, the talk camera starts
    TurnSpeaker = 2, // the speaker turns toward the player, the camera blends
    Release = 3,     // the window's wait bit cleared (fb 2b goes on)
    WaitTyping = 4,  // until the window is done; the camera back
    TurnBack = 5,    // the speaker turns back
    WaitClosed = 6,  // until the window is closed: the event ends
};

// The block 0xa5f280: +0 active, +1 flags, +2 mode, +3 state, +4 s16 window, +6 s16 message id, +8 a countdown,
// +0xa / +0xb the speaker bytes the message's window hands over (exe 0x559740: window +0x25 / +0x26), +0xc / +0x10 the
// speaker's position and turned rotation pointers, +0x14 / +0x16 the turned speaker's yaw and the yaw it turns to
struct EventState {
    uint8_t active = 0;            // an event runs (talk, door, chest ...)
    uint8_t flags = 0;             // kEvent*
    uint8_t mode = 0;              // EventMode
    uint8_t state = 0;             // the mode's state (TalkEventState ...)
    int16_t window = 0;            // the event's message window
    int16_t message = 0;           // its message id (< 0: no window)
    int16_t countdown = 0;         // the stand's ticks (StandThenItem)
    uint8_t newLine = 0;           // the window started a line (the talking player nods; consumed)
    uint8_t speakerAnimation = 0;  // the animation the message asks the speaking actor to play
    const Fixed16Vec3* speakerPosition = nullptr; // the speaker's 16.16 position (an actor's / the start point; null:
                                                  // none)
    ShortVec3* turnedRotation = nullptr;          // the turned speaker's rotation (y: its yaw; null: none)
    int16_t restoreYaw = 0;        // the turned speaker's yaw before the talk
    int16_t faceYaw = 0;           // the yaw it turns to (toward the player)

    EventMode eventMode() const { return EventMode(mode); }
    // the mode m at state s (the exe's word store at +2)
    void switchTo(EventMode m, uint8_t s) {
        mode = uint8_t(m);
        state = s;
    }
    // the start's mode word (param >> 16: the mode and the state byte above it)
    void setModeWord(uint16_t w) {
        mode = uint8_t(w);
        state = uint8_t(w >> 8);
    }

    // exe 0x543660: no event (the head -- active / flags / mode / state, the window and the message id -- and the
    // speaker's position and rotation pointers cleared)
    void reset() {
        active = 0;
        flags = 0;
        mode = 0;
        state = 0;
        window = 0;
        message = 0;
        speakerPosition = nullptr;
        turnedRotation = nullptr;
    }
};
inline EventState& eventState() {
    static EventState s;
    return s;
}

// The 16.16 point a start routine passes as the speaker position (its address goes into EventState::speakerPosition)
struct EventStartPoint {
    Fixed16Vec3 point;
};
inline EventStartPoint& eventStartPoint() {
    static EventStartPoint s;
    return s;
}

} // namespace od::port
