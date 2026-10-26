// The PC's input bindings: the binding tables the key configuration edits and OpenDash's input reads (the keys, the
// pad buttons and the mouse buttons bound to the PS1 buttons the game reads: {u16 the device's code, u16 the PS1
// button bit}), their defaults (the input set-up exe 0x409a0c at boot: OpenDash's keys, the exe's pad and mouse), the
// raw-devices flag of a binding being captured, the keys' names, and the lookup the messages and the sub-screen use to
// name a button by the code bound to it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "game/state_array.hpp"

namespace od::port {

// One binding (4 bytes): the device's code (a pad button 1..10, a key's DirectInput code, a mouse button 1..2) and the
// PS1 button bit it gives
struct InputBinding {
    uint16_t code = 0;
    uint16_t ps1Bit = 0;
};
template <size_t N> struct BindingTable {
    StateArray<InputBinding, N> bindings{};

    static constexpr size_t size() { return N; }
    // exe 0x40a1a0 / 0x40a260 / 0x40a320: the code bound to the PS1 bit (the first match; 0: none)
    uint16_t codeOf(uint32_t bit) const {
        for (size_t i = 0; i < N; ++i)
            if (bindings[i].ps1Bit == bit) return bindings[i].code;
        return 0;
    }
    // exe 0x40a1d0 / 0x40a290 / 0x40a350: the first binding of the PS1 bit gets `code`
    void bind(uint32_t bit, uint16_t code) {
        for (size_t i = 0; i < N; ++i)
            if (bindings[i].ps1Bit == bit) {
                bindings[i].code = code;
                return;
            }
    }
    // the bindings as dwords (code | bit << 16: the copies of the tables, the defaults, the key configuration's keep /
    // restore)
    uint32_t word(size_t i) const { return uint32_t(bindings[i].code) | uint32_t(bindings[i].ps1Bit) << 16; }
    void setWord(size_t i, uint32_t v) {
        bindings[i].code = uint16_t(v);
        bindings[i].ps1Bit = uint16_t(v >> 16);
    }
};
constexpr size_t kKeyBindings = 16, kPadBindings = 10, kMouseBindings = 2;
struct InputBindings {
    BindingTable<kMouseBindings> mouse;
    BindingTable<kKeyBindings> keys;
    BindingTable<kPadBindings> pad;
    uint32_t rawDevices = 0; // exe 0x40a3e0: the devices are read raw (no bindings) while a binding is captured
};
inline InputBindings& inputBindings() {
    static InputBindings s;
    return s;
}

// exe 0x40a260(bit): the pad button bound to the PS1 button `bit` (0: none)
inline uint16_t padButtonKey(uint16_t bit) { return inputBindings().pad.codeOf(bit); }

// exe 0x40a210 / 0x40a2d0 / 0x40a390 (the input set-up exe 0x409a0c at boot): the default bindings (the tables mapped
// when the host has not) -- the keys OpenDash's own (its controls: W A S D, E circle (use / talk), R R1, Q L1, I
// triangle, space cross; the actions are the exe's nine), the pad and the mouse the exe's (exe tables 0x8478f0 /
// 0x847918)
void inputConfigDefaults();

// exe 0x40a050(key): the key's name (OpenDash's list of the DirectInput codes); "" = not nameable
std::string keyName(uint32_t key);
// the key can be named (a key the configuration accepts)
bool keyNameable(uint32_t key);

} // namespace od::port
