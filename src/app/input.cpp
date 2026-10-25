#include "app/input.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <utility>

namespace od::app {

void pollStick(SDL_Gamepad* pad, Input& in) {
    in.stickX = in.stickY = 0x80;
    if (!pad) return;
    auto conv = [](Sint16 v) { return uint8_t(std::clamp((int(v) >> 8) + 0x80, 0, 0xff)); };
    in.stickX = conv(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
    in.stickY = conv(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
}

SDL_Scancode dikScancode(uint16_t dik) {
    for (const auto& [d, sc] : kDikKeys)
        if (d == dik) return sc;
    return SDL_SCANCODE_UNKNOWN;
}

bool padButtonDown(SDL_Gamepad* pad, int n) {
    if (!pad) return false;
    switch (n) {
    case 1: return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
    case 2: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
    case 3: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST);
    case 4: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST);
    case 5: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH);
    case 6: return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
    case 7: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    case 8: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    case 9: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
    case 10: return SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK);
    default: return false;
    }
}

uint32_t logicalOfPs1(uint32_t ps1) {
    static constexpr std::pair<uint16_t, uint32_t> kMap[] = {
        {0x10, kUp},       {0x20, kRight},      {0x40, kDown}, {0x80, kLeft}, {0x4000, kCross}, {0x8000, kSquare},
        {0x2000, kCircle}, {0x1000, kTriangle}, {0x400, kL1},  {0x800, kR1},  {0x8, kStart},    {0x100, kL2},
        {0x200, kR2},      {0x1, kSelect}};
    uint32_t b = 0;
    for (auto [bit, l] : kMap)
        if (ps1 & bit) b |= l;
    return b;
}

uint32_t pollButtons(SDL_Gamepad* pad, bool gameBindings, bool aimHeld, bool fireHeld) {
    const bool* k = SDL_GetKeyboardState(nullptr);
    uint32_t b = 0;
    if (k[SDL_SCANCODE_UP]) b |= kUp;
    if (k[SDL_SCANCODE_DOWN]) b |= kDown;
    if (k[SDL_SCANCODE_LEFT]) b |= kLeft;
    if (k[SDL_SCANCODE_RIGHT]) b |= kRight;
    if (k[SDL_SCANCODE_J]) b |= kSquare;
    if (k[SDL_SCANCODE_K]) b |= kCircle;
    if (k[SDL_SCANCODE_Z]) b |= kL2;
    if (k[SDL_SCANCODE_C]) b |= kR2;
    if (k[SDL_SCANCODE_RETURN]) b |= kStart;
    if (k[SDL_SCANCODE_BACKSPACE]) b |= kSelect;
    od::port::InputBindings& bindings = od::port::inputBindings();
    const bool tables = gameBindings;
    if (!tables) {
        if (k[SDL_SCANCODE_SPACE]) b |= kCross;
        if (k[SDL_SCANCODE_I]) b |= kTriangle;
        if (k[SDL_SCANCODE_Q]) b |= kL1;
        if (k[SDL_SCANCODE_E]) b |= kR1;
        static constexpr uint32_t kDef[11] = {0, kR2, kCross, kCircle, kSquare, kTriangle, kL2, kL1, kR1, kStart, kSelect};
        for (int n = 1; n <= 10; ++n)
            if (padButtonDown(pad, n)) b |= kDef[n];
    } else if (bindings.rawDevices == 0) {
        uint32_t ps1 = 0;
        for (size_t i = 0; i < bindings.keys.size(); ++i) {
            const uint16_t code = bindings.keys.bindings[i].code;
            const SDL_Scancode sc = code ? dikScancode(code) : SDL_SCANCODE_UNKNOWN;
            if (sc != SDL_SCANCODE_UNKNOWN && k[sc]) ps1 |= bindings.keys.bindings[i].ps1Bit;
        }
        for (size_t i = 0; i < bindings.pad.size(); ++i)
            if (padButtonDown(pad, bindings.pad.bindings[i].code)) ps1 |= bindings.pad.bindings[i].ps1Bit;
        for (size_t i = 0; i < bindings.mouse.size(); ++i) {
            const uint16_t btn = bindings.mouse.bindings[i].code;
            if ((btn == 1 && fireHeld) || (btn == 2 && aimHeld)) ps1 |= bindings.mouse.bindings[i].ps1Bit;
        }

        b |= logicalOfPs1(ps1);
    }
    if (pad) { // the d-pad is not configurable (the exe reads the pad's POV / axes as the d-pad)
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= kUp;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= kDown;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= kLeft;
        if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= kRight;
    }
    // the right stick orbits OpenDash's free camera (PlayerView::orbit), it is not a PS1 input
    return b;
}

} // namespace od::app
