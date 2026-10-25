// OpenDash (modern controls, not the exe): while the buster fires, Megaman faces where the camera looks.
//
// Modern steering turns the body toward the move direction and runs "forward", so firing while turning the camera
// swung the camera round to Megaman's face. While the buster is held (the game's fire mask +0x12e; not a special
// weapon in use, arm +0x13c & 0x40; not locked on, not skating), the body yaw +0x2a turns toward the camera's forward
// (orbitYaw - pi, see orbitBehind) by up to kTurn per tick, and the stick becomes the body-relative d-pad (8 ways:
// up = ahead, sideways = strafe, down = back off), as the original plays while firing.
#include "app/play_session.hpp"

#include <algorithm>
#include <cmath>
#include "game/player/player_state.hpp"

namespace od::app {

namespace {
constexpr int kTurn = 0x200; // per tick (a quarter turn in two ticks)
}

bool PlayerView::fireFacing(uint16_t& ps1, float lx, float ly) {
    od::port::Player& pl = od::port::player();
    // not the kick (buster type +0x18d == 1: towns / weapon mode 2): it cancels the walk as in the exe
    if (skating() || lockedOn() || pl.playerState() != od::port::PlayerState::Playing ||
        !(ps1 & pl.button(od::port::PlayerButton::Fire)) || (pl.armFlags & od::port::kArmSpecial) ||
        pl.activeBuster == od::port::kTownBuster)
        return false;
    const uint8_t sub = pl.subState;
    if (sub == 0 || sub == 1 || sub == 2 || sub == 5 || sub == 6) { // on the ground, moving or standing
        const int face = int(std::lround((orbitYaw - 3.14159265f) * 4096.0f / 6.2831853f)) & 0xfff;
        const int body = pl.rotation.y;
        const int diff = ((face - body + 0x800) & 0xfff) - 0x800;
        pl.rotation.y = int16_t((body + std::clamp(diff, -kTurn, kTurn)) & 0xfff);

    }
    if (lx * lx + ly * ly < 0.04f) return true;
    const int stickAngle = int(std::lround(std::atan2(lx, -ly) * 4096.0f / 6.2831853f)); // 0 up, +0x400 right
    const int o = ((stickAngle + 0x100) & 0xfff) >> 9; // 0 ahead, 2 / 6 the sides, 4 behind
    static constexpr uint16_t kDir8[8] = {0x10, 0x10 | 0x20, 0x20, 0x20 | 0x40, 0x40, 0x40 | 0x80, 0x80, 0x80 | 0x10};
    ps1 = uint16_t(ps1 | kDir8[o]);
    return true;
}

} // namespace od::app
