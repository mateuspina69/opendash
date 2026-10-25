// The PC's input bindings (see input_bindings.hpp): their defaults and the keys' names.
#include "game/loaded_data.hpp"
#include "game/input_bindings.hpp"
#include "engine/data_source.hpp"

namespace od::port {

namespace {

// OpenDash's default keys: {DirectInput code, PS1 bit} of the exe's nine actions (exe table 0x91b8a8); the exe's own
// table 0x847830 has W A S D, Q L1, E R1, space circle, ctrl triangle, shift cross
constexpr uint16_t kKeyDefaults[9][2] = {{0x11, 0x10},   {0x1f, 0x40},  {0x1e, 0x80},
                                         {0x20, 0x20},   {0x12, 0x2000}, {0x10, 0x400},
                                         {0x13, 0x800},  {0x17, 0x1000}, {0x39, 0x4000}};
// OpenDash's default pad and mouse: {device button, PS1 bit} -- the pad's 4 square, 2 cross, 3 circle, 5 triangle,
// 7 L1, 8 R1, 6 L2, 1 R2, 9 start, 10 select; the mouse's 1 square, 2 R2 (the exe's choice too, tables 0x8478f0 /
// 0x847918)
constexpr uint16_t kPadDefaults[kPadBindings][2] = {{4, 0x8000}, {2, 0x4000}, {3, 0x2000}, {5, 0x1000}, {7, 0x400},
                                                    {8, 0x800},  {6, 0x100},  {1, 0x200},  {9, 0x8},    {10, 0x1}};
constexpr uint16_t kMouseDefaults[kMouseBindings][2] = {{1, 0x8000}, {2, 0x200}};
// the key names (exe table 0x8475c8: {u32 DirectInput code, char* name}, a 0 code ends it)
struct KeyNameEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint32_t code = 0; // +0x0
    uint32_t name = 0; // +0x4
    template <class R> static KeyNameEntry load(const R& r) {
        KeyNameEntry v;
        v.code = r.template read<uint32_t>(0x0);
        v.name = r.template read<uint32_t>(0x4);
        return v;
    }
};
constexpr size_t kMaxKeyName = 0x80;

// the address of the key's name (0: not nameable)
uint32_t keyNameAddress(uint32_t key) {
    for (uint32_t p = stateAddress(TableId::InputBindingsKeyNames);; p += 8) {
        const KeyNameEntry e = dataRecord<KeyNameEntry>(p, KeyNameEntry::kBytes);
        if (e.code == 0) return 0;
        if (e.code == key) return e.name;
    }
}

} // namespace

void inputConfigDefaults() {
    InputBindings& b = inputBindings();
    for (size_t i = 0; i < kKeyBindings; ++i)
        b.keys.setWord(i, i < 9 ? uint32_t(kKeyDefaults[i][0]) | uint32_t(kKeyDefaults[i][1]) << 16 : 0);
    for (size_t i = 0; i < kPadBindings; ++i) b.pad.setWord(i, uint32_t(kPadDefaults[i][0]) | uint32_t(kPadDefaults[i][1]) << 16);
    for (size_t i = 0; i < kMouseBindings; ++i)
        b.mouse.setWord(i, uint32_t(kMouseDefaults[i][0]) | uint32_t(kMouseDefaults[i][1]) << 16);
}

std::string keyName(uint32_t key) {
    std::string s;
    const uint32_t a = keyNameAddress(key);
    if (a == 0) return s;
    return dataString(a, kMaxKeyName);
}

bool keyNameable(uint32_t key) { return keyNameAddress(key) != 0; }

} // namespace od::port
