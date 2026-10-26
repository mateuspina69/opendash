// The PC's input bindings (see input_bindings.hpp): their defaults and the keys' names.
#include "game/loaded_data.hpp"
#include "game/input_bindings.hpp"

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
// the nameable keys: {DirectInput code, name} (what the key configuration shows; OpenDash's own list, the same keys
// and names as the PC exe's)
struct KeyName {
    uint32_t code;
    const char* name;
};
constexpr KeyName kKeyNames[] = {
    {0x02, "1"},   {0x03, "2"},   {0x04, "3"},   {0x05, "4"},   {0x06, "5"},   {0x07, "6"},    {0x08, "7"},   {0x09, "8"},
    {0x0a, "9"},   {0x0b, "0"},   {0x0c, "-"},   {0x0d, "="},   {0x0e, "BS"},  {0x0f, "TAB"},  {0x10, "Q"},   {0x11, "W"},
    {0x12, "E"},   {0x13, "R"},   {0x14, "T"},   {0x15, "Y"},   {0x16, "U"},   {0x17, "I"},    {0x18, "O"},   {0x19, "P"},
    {0x1a, "["},   {0x1b, "]"},   {0x1e, "A"},   {0x1f, "S"},   {0x20, "D"},   {0x21, "F"},    {0x22, "G"},   {0x23, "H"},
    {0x24, "J"},   {0x25, "K"},   {0x26, "L"},   {0x27, ";"},   {0x28, "'"},   {0x29, "`"},    {0x2b, "\\"},  {0x2c, "Z"},
    {0x2d, "X"},   {0x2e, "C"},   {0x2f, "V"},   {0x30, "B"},   {0x31, "N"},   {0x32, "M"},    {0x33, ","},   {0x34, "."},
    {0x35, "/"},   {0x90, "^"},   {0x7d, "~"},   {0x91, "@"},   {0x92, ":"},   {0x1d, "L-C"},  {0x2a, "L-S"}, {0x9d, "R-C"},
    {0x36, "R-S"}, {0x39, "SPC"}, {0xd2, "INS"}, {0xd3, "DEL"}, {0xc7, "HOME"}, {0xcf, "END"}, {0xc9, "PGUP"}, {0xd1, "PGDN"}};

// the key's name (nullptr: not nameable)
const char* keyNameOf(uint32_t key) {
    for (const KeyName& k : kKeyNames)
        if (k.code == key) return k.name;
    return nullptr;
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
    const char* n = keyNameOf(key);
    return n ? n : "";
}

bool keyNameable(uint32_t key) { return keyNameOf(key) != nullptr; }

} // namespace od::port
