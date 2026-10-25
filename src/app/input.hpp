// The host's input: the logical PS1-style buttons from the keyboard / gamepad (through the game's key
// configuration tables when the game state has them), the left stick, and the bindings to / from opendash.ini.
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

#include "game/input_bindings.hpp"


namespace od::app {

// Logical PS1-style buttons; physical mapping (keyboard / gamepad) lives here only.
enum Button : uint32_t {
    kUp = 1u << 0, kDown = 1u << 1, kLeft = 1u << 2, kRight = 1u << 3,
    kCross = 1u << 4, kSquare = 1u << 5, kCircle = 1u << 6, kTriangle = 1u << 7,
    kL1 = 1u << 8, kR1 = 1u << 9, kStart = 1u << 10, kL2 = 1u << 11, kR2 = 1u << 12, kSelect = 1u << 13,
};

struct Input {
    uint32_t held = 0;
    uint32_t pressed = 0;            // edge since the previous sim tick
    uint8_t stickX = 0x80, stickY = 0x80; // left analog stick as the PS1 pad reports it (0x80 = centre)
};

// Left stick -> PS1 analog bytes (0 = left / up, 0xff = right / down). The game applies its own dead zone
// (0x38, walk below 0x68: FUN_00574860), so the raw value is passed through.
void pollStick(SDL_Gamepad* pad, Input& in);

// The bindings of the options page's key configuration (game/key_config, the options page 6), in the exe's tables: keys 0xa2d4cc (16 x
// {DirectInput code, PS1 bit}), pad 0xa2d7c0 (10 x {button 1..10, PS1 bit}), mouse 0xa2d4c4 (2 x {button 1..2, PS1
// bit}); 0xa2d814 set (a binding being captured): the devices drive no PS1 bit.
// The nameable keys (exe table 0x8475c8) by DirectInput code -> SDL scancode (the Japanese-layout ^ @ : have none)
inline constexpr std::pair<uint16_t, SDL_Scancode> kDikKeys[] = {
    {0x02, SDL_SCANCODE_1}, {0x03, SDL_SCANCODE_2}, {0x04, SDL_SCANCODE_3}, {0x05, SDL_SCANCODE_4},
    {0x06, SDL_SCANCODE_5}, {0x07, SDL_SCANCODE_6}, {0x08, SDL_SCANCODE_7}, {0x09, SDL_SCANCODE_8},
    {0x0a, SDL_SCANCODE_9}, {0x0b, SDL_SCANCODE_0}, {0x0c, SDL_SCANCODE_MINUS}, {0x0d, SDL_SCANCODE_EQUALS},
    {0x0e, SDL_SCANCODE_BACKSPACE}, {0x0f, SDL_SCANCODE_TAB}, {0x10, SDL_SCANCODE_Q}, {0x11, SDL_SCANCODE_W},
    {0x12, SDL_SCANCODE_E}, {0x13, SDL_SCANCODE_R}, {0x14, SDL_SCANCODE_T}, {0x15, SDL_SCANCODE_Y},
    {0x16, SDL_SCANCODE_U}, {0x17, SDL_SCANCODE_I}, {0x18, SDL_SCANCODE_O}, {0x19, SDL_SCANCODE_P},
    {0x1a, SDL_SCANCODE_LEFTBRACKET}, {0x1b, SDL_SCANCODE_RIGHTBRACKET}, {0x1d, SDL_SCANCODE_LCTRL},
    {0x1e, SDL_SCANCODE_A}, {0x1f, SDL_SCANCODE_S}, {0x20, SDL_SCANCODE_D}, {0x21, SDL_SCANCODE_F},
    {0x22, SDL_SCANCODE_G}, {0x23, SDL_SCANCODE_H}, {0x24, SDL_SCANCODE_J}, {0x25, SDL_SCANCODE_K},
    {0x26, SDL_SCANCODE_L}, {0x27, SDL_SCANCODE_SEMICOLON}, {0x28, SDL_SCANCODE_APOSTROPHE},
    {0x29, SDL_SCANCODE_GRAVE}, {0x2a, SDL_SCANCODE_LSHIFT}, {0x2b, SDL_SCANCODE_BACKSLASH},
    {0x2c, SDL_SCANCODE_Z}, {0x2d, SDL_SCANCODE_X}, {0x2e, SDL_SCANCODE_C}, {0x2f, SDL_SCANCODE_V},
    {0x30, SDL_SCANCODE_B}, {0x31, SDL_SCANCODE_N}, {0x32, SDL_SCANCODE_M}, {0x33, SDL_SCANCODE_COMMA},
    {0x34, SDL_SCANCODE_PERIOD}, {0x35, SDL_SCANCODE_SLASH}, {0x36, SDL_SCANCODE_RSHIFT},
    {0x39, SDL_SCANCODE_SPACE}, {0x7d, SDL_SCANCODE_INTERNATIONAL3}, {0x9d, SDL_SCANCODE_RCTRL},
    {0xc7, SDL_SCANCODE_HOME}, {0xc9, SDL_SCANCODE_PAGEUP}, {0xcf, SDL_SCANCODE_END},
    {0xd1, SDL_SCANCODE_PAGEDOWN}, {0xd2, SDL_SCANCODE_INSERT}, {0xd3, SDL_SCANCODE_DELETE},
};
SDL_Scancode dikScancode(uint16_t dik);

// OpenDash's pad buttons 1..10 (the numbers the key configuration shows): the exe's default table 0x8478f0 then maps
// them as OpenDash always did (south cross, east circle, west square, north triangle, shoulders L1 / R1, triggers
// L2 / R2, start, back select)
bool padButtonDown(SDL_Gamepad* pad, int n);

uint32_t logicalOfPs1(uint32_t ps1);

// Buttons. With the bindings (play / the title: gameBindings, the tables of the game state): the key configuration's
// keys, pad buttons and mouse buttons (left / right only while play mode holds the mouse captured: fireHeld /
// aimHeld), plus OpenDash's fixed keys -- arrows (the d-pad), J square, K circle, Z L2, C R2, Enter start, Backspace
// select -- and the pad's d-pad. Without (the stage viewer): the fixed keys, Space cross, I triangle, Q L1, E R1
// (WASD / Q / E fly the free camera there) and the exe's default pad table.
uint32_t pollButtons(SDL_Gamepad* pad, bool gameBindings, bool aimHeld, bool fireHeld);

// the binding tables to / from opendash.ini (code:bit hex pairs)
template <size_t N> std::string bindingsText(const od::port::BindingTable<N>& table) {
    std::string s;
    char e[24];
    for (size_t i = 0; i < N; ++i) {
        const od::port::InputBinding& b = table.bindings[i];
        std::snprintf(e, sizeof e, "%s%x:%x", i ? "," : "", unsigned(b.code), unsigned(b.ps1Bit));
        s += e;
    }
    return s;
}

template <size_t N> void bindingsLoad(od::port::BindingTable<N>& table, const std::string& text) {
    if (text.empty()) return;
    std::stringstream ss(text);
    size_t i = 0;
    for (std::string e; std::getline(ss, e, ',') && i < N; ++i) {
        const size_t c = e.find(':');
        if (c == std::string::npos) return;
        od::port::InputBinding& b = table.bindings[i];
        b.code = uint16_t(std::stoul(e.substr(0, c), nullptr, 16));
        b.ps1Bit = uint16_t(std::stoul(e.substr(c + 1), nullptr, 16));
    }
}

} // namespace od::app
