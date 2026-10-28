// The pad (see player_input.hpp).
#include "game/player/player_input.hpp"

#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/camera_state.hpp"
#include "game/distances.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/script_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

constexpr int kStickDeadZone = 0x38;  // an axis below this is 0 (with the other axis below kStickFullTilt)
constexpr int kStickFullTilt = 0x68;  // the walk modifier: an axis tilted kStickDeadZone..kStickFullTilt
constexpr int8_t kDigitalStick = 0x7f; // a direction button as a stick byte (+-)
constexpr int kAxisClamp = 0x7e;
constexpr uint8_t kTapWindow = 5;     // ticks between the two taps of a double tap
constexpr uint8_t kSnappyRepeatDelay = 8; // OpenDash's menu repeat (setMenuSnappy)
constexpr uint8_t kSnappyRepeatRate = 3;
constexpr uint16_t kPadHorizontal = kPadLeft | kPadRight;
constexpr uint16_t kPadVertical = kPadUp | kPadDown;

constexpr uint32_t kVibrationForce = 0xff000000u; // b's top byte: replace a stronger request too

// exe 0x8c39e8: the default button map (16 masks, PlayerButton order)
const DataTable<ScalarTable<uint16_t, 16>> defaultButtonMap{TableId::PlayerInputDefaultButtonMap};

// (char)(raw - 0x80), |v| > 0x7e -> +-0x7e
int8_t clampAxis(uint8_t raw) {
    int8_t c = int8_t(uint8_t(raw - kStickCentre));
    const int a = c < 0 ? -c : c;
    if (kAxisClamp < a) c = int8_t(uint8_t(((c < 1 ? 1u : 0u) - 1u & 0xfcu) + 0x82u));
    return c;
}

// a direction pair of the held buttons as a stick byte: one held -> +-kDigitalStick (neg: the negative one), else 0
int8_t dpadAxis(uint16_t held, uint16_t both, uint16_t neg) {
    if ((held & both) == 0 || (held & both) == both) return 0;
    return (held & neg) ? int8_t(-kDigitalStick) : kDigitalStick;
}

// exe 0x55d020: the double tap of `bit` (slot k)
void doubleTap(PadState& pad, uint16_t bit, uint32_t k) {
    if (!(bit & pad.doubleTapped) && (pad.pressed & bit) == bit) {
        pad.tapTicks[k] = 0;
        if (pad.tapState[k] == 0) {
            pad.tapState[k] = 1;
            return;
        }
        pad.tapState[k] = 2;
        pad.doubleTapped |= bit;
        return;
    }
    const uint8_t c = pad.tapState[k];
    if (c == 0) return;
    bool clear = c == 2;
    if (!clear) {
        const uint8_t t = pad.tapTicks[k];
        pad.tapTicks[k] = uint8_t(t + 1);
        clear = kTapWindow < t;
    }
    if (clear) {
        pad.tapState[k] = 0;
        pad.doubleTapped &= uint16_t(~bit);
    }
}

bool gMenuSnappy = true;

// exe 0x55d140: the auto-repeat of `bit` (slot k)
void repeat(PadState& pad, uint16_t bit, uint32_t k) {
    GameState& game = gameState();
    // a sub-screen is open
    const bool snappy = gMenuSnappy && game.gameMode() == GameMode::StatusMenu;
    if (bit & pad.held) {
        const uint8_t delay = snappy ? kSnappyRepeatDelay : uint8_t(pad.repeatDelay);
        uint8_t n = pad.repeatTicks[k];
        if (n < delay) pad.repeatTicks[k] = uint8_t(n + 1);
        if ((bit & pad.pressed) || delay <= pad.repeatTicks[k]) {
            pad.repeated |= bit;
            n = pad.repeatTicks[k];
            if (n < delay) return;
            pad.repeatTicks[k] = uint8_t(n - (snappy ? kSnappyRepeatRate : uint8_t(pad.repeatRate)));
            return;
        }
    } else {
        pad.repeatTicks[k] = 0;
    }
    pad.repeated &= uint16_t(~bit);
}

// exe 0x574cf0: a direction pair of the held buttons (mask; neg its negative one) as the stick byte `axis`
void directionToStick(const Player& p, int8_t& axis, uint16_t mask, uint16_t neg) {
    const uint16_t v = p.held & mask;
    if (v != 0 && v != mask) axis = (v & neg) ? int8_t(-kDigitalStick) : kDigitalStick;
}

} // namespace

// OpenDash's snappy menu repeat on / off and its state
void setMenuSnappy(bool on) { gMenuSnappy = on; }
bool menuSnappy() { return gMenuSnappy; }

void padUpdate(PadState& pad, uint16_t buttons, uint8_t stickX, uint8_t stickY) {
    // exe 0x409c20's part: the raw state (active low) and the analog axes
    pad.raw = uint16_t(~buttons);
    pad.analogX = stickX;
    pad.analogY = stickY;
    // exe 0x55cef0
    const uint16_t held = uint16_t(~pad.raw);
    pad.prevHeld = pad.held;
    pad.type = kPadTypeAnalog;
    pad.held = held;
    pad.stickX = clampAxis(pad.analogX);
    pad.stickY = clampAxis(pad.analogY);
    pad.dpadX = dpadAxis(held, kPadHorizontal, kPadLeft);
    pad.dpadY = dpadAxis(held, kPadVertical, kPadUp);
    pad.pressed = uint16_t(~pad.prevHeld & held);
    pad.released = uint16_t(pad.raw & pad.prevHeld);
    static constexpr uint16_t kTap[6] = {kPadUp, kPadDown, kPadLeft, kPadRight, kPadL1, kPadR1};
    for (uint32_t k = 0; k < 6; ++k) doubleTap(pad, kTap[k], k);
    static constexpr uint16_t kRepeat[12] = {kPadUp, kPadDown, kPadLeft, kPadRight, kPadL1,     kPadR1,
                                             kPadL2, kPadR2,   kPadTriangle, kPadCross, kPadSquare, kPadCircle};
    for (uint32_t k = 0; k < 12; ++k) repeat(pad, kRepeat[k], k);
}

void copyDefaultButtonMap(Player& p) {
    for (size_t i = 0; i < p.buttonMap.size(); ++i) p.buttonMap[i] = defaultButtonMap[i];
}

void loadDefaultButtonMap(Player& p) {
    if ((pad0().type & 0xff) >= kPadTypeNone) return;
    copyDefaultButtonMap(p);
}

bool inputCaptured() { return hostInput().captured != 0; }

void playerReadPad(Player& p) {
    const PadState& pad = pad0();
    const uint8_t mode = p.inputMode;
    if ((mode & kInputOff) || scriptState().flags == kScriptEnded) {
        p.held = 0;
        p.pressed = 0;
        p.released = 0;
        p.doubleTapped = 0;
        p.repeated = 0;
        p.turnAxis = 0;
        p.lookAxis = 0;
        p.moveX = 0;
        p.moveY = 0;
        return;
    }
    uint16_t type, held;
    if (!(mode & kInputReplay)) {
        type = pad.type;
        if (uint8_t(p.padType) != uint8_t(type)) {
            loadDefaultButtonMap(p);
            type = pad.type;
        }
        p.pressed = pad.pressed;
        p.released = pad.released;
        held = pad.held;
        p.doubleTapped = pad.doubleTapped;
        p.repeated = pad.repeated;
        p.padType = type;
        p.held = held;
        p.turnAxis = pad.stickX;
        p.lookAxis = pad.stickY;
        p.moveX = pad.dpadX;
        p.moveY = pad.dpadY;
        type &= 0xff;
    } else { // the replay pad: scripted input (the walk out of an exit)
        const ReplayPad& replay = replayPad();
        p.pressed = replay.pressed;
        p.released = replay.released;
        held = replay.held;
        p.held = held;
        p.doubleTapped = replay.doubleTapped;
        p.repeated = replay.repeated;
        p.turnAxis = 0;
        p.lookAxis = 0;
        p.moveX = 0;
        p.moveY = 0;
        type = 0;
    }
    // the special weapon mode 2 (one weapon on both buttons): the fire button acts as the special one
    const uint16_t special = p.button(PlayerButton::Special);
    const uint16_t fire = p.button(PlayerButton::Fire);
    if (p.weaponMode == kWeaponModeSpecial && p.specialAllowed != 0) {
        if (held & fire) {
            held = uint16_t((held & ~fire) | special);
            p.held = held;
        }
        uint16_t pressedNow = p.pressed;
        if (fire & pressedNow) {
            pressedNow = uint16_t((pressedNow & ~fire) | special);
            p.pressed = pressedNow;
        }
    }
    uint8_t moveX;
    if (type == kPadTypeAnalog) { // the analog pad: dead zones on the sticks, the walk bit dropped
        held &= uint16_t(~kPadWalk);
        p.held = held;
        const int turn = std::abs(p.turnAxis), look = std::abs(p.lookAxis), x = std::abs(p.moveX), y = std::abs(p.moveY);
        moveX = uint8_t(p.moveX);
        if (turn < kStickDeadZone && look < kStickFullTilt) p.turnAxis = 0;
        if (look < kStickDeadZone && turn < kStickFullTilt) p.lookAxis = 0;
        if (x < kStickDeadZone && y < kStickFullTilt) {
            moveX = 0;
            p.moveX = 0;
        }
        if (y < kStickDeadZone && x < kStickFullTilt) p.moveY = 0;
    } else {
        moveX = uint8_t(p.moveX);
    }
    { // the lock-on: the aim / lock buttons pressed end the kept aim; a trigger with its auto-lock option holds the lock
        const uint16_t aim = p.button(PlayerButton::Aim), lock = p.button(PlayerButton::Lock);
        const uint16_t pressedNow = p.pressed;
        if ((aim | lock) & pressedNow) p.clearLockRequestBits(kLockKeepAim);
        uint16_t h = p.held;
        const GameOptions& options = gameOptions();
        if (((h & fire) && options.autoLockBuster != 0) || ((h & special) && options.autoLockSpecial != 0)) {
            h |= lock;
            p.held = h;
            if (uint16_t((special | fire) & pressedNow) && !(p.lockRequest & (kLockSpecial | kLockBuster)))
                p.pressed |= lock;
        }
    }
    // the direction buttons as sticks (the turn stick from left / right when they are the d-pad's)
    uint16_t left = p.button(PlayerButton::Left);
    if (left == kPadLeft) {
        if (std::abs(int8_t(moveX)) < kStickDeadZone) {
            directionToStick(p, p.moveX, kPadHorizontal, kPadLeft);
            left = p.button(PlayerButton::Left);
            moveX = uint8_t(p.moveX);
        }
    } else {
        if (left == kPadL2) p.held &= uint16_t(~(p.button(PlayerButton::Right) | left));
        if (p.sub() != PlayerSub::Look) {
            if (int8_t(moveX) != 0) {
                const uint16_t sides = p.button(PlayerButton::Right) | left;
                p.turnAxis = int8_t(moveX);
                p.moveX = 0;
                directionToStick(p, p.moveX, sides, left);
            } else {
                if (std::abs(p.turnAxis) < kStickDeadZone) {
                    directionToStick(p, p.turnAxis, kPadHorizontal, kPadLeft);
                    left = p.button(PlayerButton::Left);
                }
                directionToStick(p, p.moveX, uint16_t(p.button(PlayerButton::Right) | left), left);
            }
            left = p.button(PlayerButton::Left);
            moveX = uint8_t(p.moveX);
        }
    }
    uint8_t moveY = uint8_t(p.moveY);
    if (std::abs(int8_t(moveY)) < kStickDeadZone) {
        directionToStick(p, p.moveY, kPadVertical, kPadUp);
        left = p.button(PlayerButton::Left);
        moveX = uint8_t(p.moveX);
        moveY = uint8_t(p.moveY);
    }
    if (p.sub() == PlayerSub::Look) { // looking: the move stick aims
        p.turnAxis = int8_t(moveX);
        p.lookAxis = int8_t(moveY);
    } else {
        uint8_t turn;
        if (left != kPadLeft && p.lockedActor != nullptr) { // locked on: the turn stick strafes
            bool none = false;
            if (moveX == 0) {
                moveX = uint8_t(p.turnAxis);
                p.moveX = int8_t(moveX);
                if (moveX == 0) none = true;
            }
            if (!none) {
                if (int8_t(moveX) < 0)
                    p.held |= left;
                else
                    p.held |= p.button(PlayerButton::Right);
            }
            turn = 0;
            p.turnAxis = 0;
        } else {
            turn = uint8_t(p.turnAxis);
        }
        if (left == kPadLeft && std::abs(int8_t(turn)) < kStickDeadZone) {
            const uint16_t turnLeft = p.button(PlayerButton::TurnLeft);
            directionToStick(p, p.turnAxis, uint16_t(p.button(PlayerButton::TurnRight) | turnLeft), turnLeft);
            moveX = uint8_t(p.moveX);
        }
    }
    // the walk modifier: the use button held, or the stick half tilted, or circle
    bool walk = (p.button(PlayerButton::Use) & p.held) != 0;
    if (!walk) {
        const int x = int8_t(moveX), y = p.moveY;
        const int v = std::abs(x) > std::abs(y) ? std::abs(x) : std::abs(y);
        walk = v >= kStickDeadZone && v < kStickFullTilt;
    }
    if (walk) p.setHeldBits(kPadWalk);
    if (p.held & kPadCircle) p.setHeldBits(kPadWalk);
}

void padVibrate(uint32_t a, uint32_t b) {
    if (gameOptions().vibrationOff != 0 || gameState().frontEnd == kFrontEndDemo) return;
    PadVibration& v = padVibration();
    v.requested = 1;
    if ((b & 0xffff) >= v.strength || (b & kVibrationForce) != 0) {
        v.strength = uint8_t(b & 0xff);
        v.motorLow = uint8_t(a & 0xff);
        v.motorHigh = uint8_t((a >> 8) & 0xff);
        v.durationLow = uint8_t((b >> 8) & 0xff);
        v.durationHigh = uint8_t((b >> 16) & 0xff);
    }
}

void padVibrateAt(uint32_t a, uint32_t b, uint32_t nearDistance, uint32_t farDistance, const Fixed16Vec3& pos) {
    const int16_t whole[3] = {pos.x.whole, pos.y.whole, pos.z.whole};
    padVibrateAt(a, b, nearDistance, farDistance, whole);
}

void padVibrateAt(uint32_t a, uint32_t b, uint32_t nearDistance, uint32_t farDistance, const int16_t pos[3]) {
    if (gameOptions().vibrationOff != 0 || gameState().frontEnd == kFrontEndDemo) return;
    PadVibration& v = padVibration();
    v.requested = 1;
    const CameraState& camera = cameraState();
    const int dx = camera.eye.x - int(pos[0]), dy = camera.eye.y - int(pos[1]), dz = camera.eye.z - int(pos[2]);
    const uint32_t d = uint32_t(isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dy * dy) + uint32_t(dx * dx))));
    if (d > farDistance) return;
    const uint32_t k = farDistance - d, span = farDistance - nearDistance;
    const uint32_t strength = ((b & 0xffff) * k) / span + 1;
    if (v.strength <= strength || (b & kVibrationForce)) {
        v.strength = uint8_t(strength);
        v.motorLow = uint8_t(((a & 0xffff) * k) / span + 1);
        v.motorHigh = uint8_t((((a >> 8) & 0xff) * k) / span + 1);
        v.durationLow = uint8_t((((b >> 8) & 0xff) * k) / span + 1);
        v.durationHigh = uint8_t((((b >> 16) & 0xff) * k) / span + 1);
    }
}

void padVibrationTick() {
    PadVibration& v = padVibration();
    if ((v.requested & 0xff) == 0) return;
    const auto setFlag = [&](int byte) { v.requested |= 1u << (byte * 8); };
    if (((v.requested >> 16) & 0xff) == 0 && --v.motorHigh == 0) {
        v.motorLow = 0;
        setFlag(2);
    }
    if (((v.requested >> 24) & 0xff) == 0) {
        if (v.durationLow != 0 && --v.durationLow == 0) {
            v.strength = 0;
            setFlag(3);
        } else if (int(v.strength) <= int(int8_t(v.durationHigh))) {
            v.strength = 0;
            setFlag(3);
        } else {
            v.strength = uint8_t(v.strength - v.durationHigh);
        }
    }
    if (v.strength == 0 && v.motorLow == 0) v.requested = 0;
    padMotors() = {v.motorLow, v.strength};
}

} // namespace od::port
