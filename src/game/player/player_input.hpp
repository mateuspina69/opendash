// The pad: the pad pipeline of dash2.exe (exe 0x55cef0(pad, port) without the DirectInput read 0x409c20, replaced by
// the host's buttons), the player's copy of the pad (the input half of exe 0x574860: dead zones, the direction
// buttons turned into stick bytes, the button map) and the pad's vibration request. Validated by test_player_input
// against the ST09 player golden.
#pragma once

#include <cstdint>

#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct Player;

// The pad's buttons (PS1 bits, active high once read; the raw word is active low)
constexpr uint16_t kPadSelect = 0x0001;
constexpr uint16_t kPadWalk = 0x0002;     // not a pad button: the player's copy sets it for the walk modifier
constexpr uint16_t kPadStart = 0x0008;
constexpr uint16_t kPadUp = 0x0010;
constexpr uint16_t kPadRight = 0x0020;
constexpr uint16_t kPadDown = 0x0040;
constexpr uint16_t kPadLeft = 0x0080;
constexpr uint16_t kPadL2 = 0x0100;
constexpr uint16_t kPadR2 = 0x0200;
constexpr uint16_t kPadL1 = 0x0400;
constexpr uint16_t kPadR1 = 0x0800;
constexpr uint16_t kPadTriangle = 0x1000;
constexpr uint16_t kPadCircle = 0x2000;
constexpr uint16_t kPadCross = 0x4000;
constexpr uint16_t kPadSquare = 0x8000;
// the confirm buttons (circle / start): a choice taken, a row chosen, a message moved on
constexpr uint16_t kPadConfirm = kPadCircle | kPadStart;
constexpr uint8_t kPadTypeAnalog = 7;  // the pad type the pipeline reports (dead zones on the sticks)
constexpr uint8_t kPadTypeNone = 0x80; // the type's low byte >= this: no pad (the button map is not reloaded)
constexpr uint8_t kStickCentre = 0x80; // the raw analog axis at rest

// Pad 0 (0xa6bec0): the raw state the host writes and what the pipeline makes of it
struct PadState {
    uint16_t raw = 0;         // the raw buttons (active low)
    uint8_t analogX = 0;      // the raw analog axes (kStickCentre at rest)
    uint8_t analogY = 0;
    StateArray<uint8_t, 0x1e> driverBytes{}; // +0x06..+0x23: the rest of the console pad's buffer, which the PC driver
                                             // never fills (cleared at boot; only the rider walk past the static pool
                                             // reads them, game/carry)
    uint16_t type = 0;        // the pad type word (low byte: kPadTypeAnalog)
    uint16_t held = 0;
    uint16_t prevHeld = 0;
    uint16_t pressed = 0;
    uint16_t released = 0;
    uint16_t doubleTapped = 0; // 6 buttons tapped twice within 5 ticks
    uint16_t repeated = 0;     // 12 buttons auto-repeating (delay, rate)
    uint8_t repeatDelay = 0;
    uint8_t repeatRate = 0;
    int8_t stickX = 0;        // the stick (clamped to +-0x7e)
    int8_t stickY = 0;
    int8_t dpadX = 0;         // the d-pad as a stick (+-0x7f)
    int8_t dpadY = 0;
    StateArray<uint8_t, 6> tapState{}; // the double tap of each button: 0 none, 1 tapped once, 2 twice
    StateArray<uint8_t, 2> tapStateSpare{}; // (cleared with them; no reader)
    StateArray<uint8_t, 6> tapTicks{};
    StateArray<uint8_t, 2> tapTicksSpare{};
    StateArray<uint8_t, 12> repeatTicks{};

    // the type word's low byte (the pad's kind; the exe's byte store / read)
    uint8_t typeByte() const { return uint8_t(type); }
    void setTypeByte(uint8_t v) { type = uint16_t((type & 0xff00) | v); }
    // the stick / the d-pad bytes as words (x | y << 8: the demo's recorded words)
    void setStickWord(uint16_t v) {
        stickX = int8_t(uint8_t(v));
        stickY = int8_t(uint8_t(v >> 8));
    }
    void setDpadWord(uint16_t v) {
        dpadX = int8_t(uint8_t(v));
        dpadY = int8_t(uint8_t(v >> 8));
    }
    // exe 0x55d1f0: the double taps and the auto-repeat cleared (the exe's dword stores: +0x30, then +0x3c .. +0x57)
    void clearTapsAndRepeats() {
        doubleTapped = 0;
        repeated = 0;
        tapState.fill(0);
        tapStateSpare.fill(0);
        tapTicks.fill(0);
        tapTicksSpare.fill(0);
        repeatTicks.fill(0);
    }
};
inline PadState& pad0() {
    static PadState p;
    return p;
}
inline PadState& pad1() {
    static PadState p;
    return p;
}
inline void padSetRepeat(uint8_t delay, uint8_t rate) {
    pad1().repeatDelay = delay;
    pad0().repeatDelay = delay;
    pad1().repeatRate = rate;
    pad0().repeatRate = rate;
}
// exe 0x55d1d0: both pads' double taps and auto-repeats cleared
inline void padsClearTaps() {
    pad0().clearTapsAndRepeats();
    pad1().clearTapsAndRepeats();
}
constexpr uint8_t kPadRepeatDelay = 0xf; // the pads' default repeat: after 15 ticks, every tick
constexpr uint8_t kPadRepeatRate = 1;

// The replay pad (0xa6c3e8): the input a script feeds the player (the walk out of an exit; game/script_engine hands
// the player to it and clears it)
struct ReplayPad {
    uint16_t held = 0;
    uint16_t prevHeld = 0;
    uint16_t pressed = 0;
    uint16_t released = 0;
    uint16_t doubleTapped = 0;
    uint16_t repeated = 0;

    // the six words cleared, first to last
    void clear() { *this = ReplayPad{}; }
};
inline ReplayPad& replayPad() {
    static ReplayPad r;
    return r;
}

// OpenDash (default on; F1 "menu responsivo" / --menu-snappy 0 = the original): the sub-screens' cursors answer at
// once -- the d-pad repeats after 8 steps (the game's 15) every 3 (its 1) while a sub-screen is open (game mode 5),
// and the cursor lists take a move during the row change's animation (game/status_controls exe 0x5e83d0 waits for its 4 steps)
// The host's input capture flag (exe 0x40a3f0 returns it; set outside the ported code)
struct HostInput {
    uint32_t captured = 0;
};
inline HostInput& hostInput() {
    static HostInput h;
    return h;
}

// The pad vibration request
struct PadVibration {
    uint32_t requested = 0;
    uint8_t motorLow = 0;
    uint8_t motorHigh = 0;
    uint8_t strength = 0;
    uint8_t durationLow = 0;
    uint8_t durationHigh = 0;
};
inline PadVibration& padVibration() {
    static PadVibration v;
    return v;
}

void setMenuSnappy(bool on);
bool menuSnappy();

// exe 0x55cef0: the pad's raw state (the buttons held, active high, and the analog axes) run through the pipeline:
// held / pressed / released, the double taps, the auto-repeat, the sticks
void padUpdate(PadState& pad, uint16_t buttons, uint8_t stickX = kStickCentre, uint8_t stickY = kStickCentre);

// The input half of exe 0x574860: pad 0 (or the replay pad of a scripted walk) copied into the player's input fields,
// the analog dead zones, the direction buttons as stick bytes (exe 0x574cf0), the button map reloaded on a pad type
// change (exe 0x55cbe0); with the input off (or the pad-freezing script) everything cleared
void playerReadPad(Player& p);
// exe 0x55cbe0 -> 0x55cbb0: the default button map (exe 0x8c39e8) into the player's map (the per-pad-type index
// argument is pushed but unused); nothing without a pad
void loadDefaultButtonMap(Player& p);
// exe 0x55cbb0: the default button map copied into the player's map (the options' defaults call it without a pad check)
void copyDefaultButtonMap(Player& p);
// exe 0x40a3f0: the host's input is captured elsewhere (the global 0xa2d4c0): the jump and the hover are blocked
bool inputCaptured();

// exe 0x55cd10(a, b): the pad vibration request 0xa7ada0 (skipped with the vibration off or the front end's no-vibration
// mode; the rumble itself is output)
void padVibrate(uint32_t a, uint32_t b);
// exe 0x55cd80(a, b, near, far, pos): the same request scaled down by the distance of the camera's eye to the 16.16
// point (full at `near`, nothing past `far`)
void padVibrateAt(uint32_t a, uint32_t b, uint32_t nearDistance, uint32_t farDistance, const Fixed16Vec3& pos);
// ... at the point's whole parts (x / y / z)
void padVibrateAt(uint32_t a, uint32_t b, uint32_t nearDistance, uint32_t farDistance, const int16_t pos[3]);
// exe 0x55cc40 (the frame loop, 0x54a4cd, before the frame counter): the request's tick. Bytes 2 / 3 of `requested`
// mark the small / large motor done (a new request clears them). The small motor runs at motorLow for motorHigh
// ticks; the large one at strength, for durationLow ticks when that is set, and it loses durationHigh (signed) a
// tick, off once that would take it to 0 or below. Both off: the request ends. Then 0x409f90(small, large): the
// small motor's DirectInput effect started / stopped (on / off), the large one's magnitude large * 10000 / 255.
void padVibrationTick();
// what the last tick handed 0x409f90 (OpenDash sends it to the gamepad's motors)
struct PadMotors {
    uint8_t small = 0;
    uint8_t large = 0;
};
inline PadMotors& padMotors() {
    static PadMotors m;
    return m;
}

} // namespace od::port
