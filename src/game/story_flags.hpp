// The story's event flags: the bit array 0xa73cc0 (0x120 bytes; game/game_init loads / clears it) that the scripts, the
// rooms and the classes test (flag n = bit n & 7 of byte n >> 3), its groups, the flags of the systems (the menus, the
// transitions, the doors, the scripts' solids) and the second flag block the save keeps. The flags of the story itself
// are named by the areas that own them (content/<area>).
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/state_array.hpp"

namespace od::port {

constexpr size_t kStoryFlagBytes = 0x120;
// four bytes of a flag array as one little-endian dword (the save copies the arrays as dwords)
template <size_t N> uint32_t flagBytesWord(const StateArray<uint8_t, N>& b, size_t i) {
    return uint32_t(b[i * 4]) | uint32_t(b[i * 4 + 1]) << 8 | uint32_t(b[i * 4 + 2]) << 16 | uint32_t(b[i * 4 + 3]) << 24;
}
template <size_t N> void setFlagBytesWord(StateArray<uint8_t, N>& b, size_t i, uint32_t v) {
    for (size_t k = 0; k < 4; ++k) b[i * 4 + k] = uint8_t(v >> (8 * k));
}

struct StoryFlags {
    StateArray<uint8_t, kStoryFlagBytes> bytes{};

    // the flags as the save copies them (dwords)
    uint32_t word(size_t i) const { return flagBytesWord(bytes, i); }
    void setWord(size_t i, uint32_t v) { setFlagBytesWord(bytes, i, v); }
};
inline StoryFlags& storyFlags() {
    static StoryFlags s;
    return s;
}

// The byte of event flag n: the exe's flag routines index the story flags unchecked, so the flags past them
// (n >> 3 >= kStoryFlagBytes) are the bytes of the extra flag block that follows them (storyFlagByte)
uint8_t& storyFlagByte(uint32_t n);

// exe 0x510e50: event flag n is set
inline bool storyFlag(uint32_t n) { return ((1u << (n & 7)) & storyFlagByte(n)) != 0; }
// exe 0x510e10 / 0x510e30: event flag n set / cleared (a byte read-modify-write)
inline void setStoryFlag(uint32_t n) { storyFlagByte(n) |= uint8_t(1u << (n & 7)); }
inline void clearStoryFlag(uint32_t n) { storyFlagByte(n) &= uint8_t(~(1u << (n & 7))); }

// ---- the systems' flags ------------------------------------------------------------------------------------------
constexpr uint32_t kFlagStoryMenuOpen = 0x245;  // a story menu holds the game (the save menus): no soft reset, no
                                                // status sub-screen; a load clears it
constexpr uint32_t kFlagGameOverRescue = 0x246; // a game over warps back instead (the licence exam: game/title's
                                                // rescue)
                                                // (else to ST0B)
constexpr uint32_t kFlagSaveMenuAsked = 0x6fa;  // the talker's message chose "save" (fb 26 06 fa): its save menu runs
                                                // (game/save_menus clears it on the close)
constexpr uint32_t kFlagTransitionArmed = 0x700;    // a transition is armed: the room scripts start script 0 on it
constexpr uint32_t kFlagTransitionArriving = 0x701; // the transition's second run, in the room entered
constexpr uint32_t kFlagDoorYes = 0x702;            // the door event's "yes" (game/door_actor)
constexpr uint32_t kFlagScriptSolids = 0x703;       // the script-passable volumes stay solid (game/collision)
constexpr uint32_t kFlagLocked = 0x710;             // + a door's lock / an exit's id: locked
constexpr uint32_t kFlagTransitionHeld = 0x730;     // + an exit's id: armed but not asked (a script plays it)
constexpr uint32_t kLockFlags = 0x20;               // the lock flags kFlagLocked .. kFlagLocked + 0x1f
// the lock flags kFlagLocked + 0 .. count - 1 cleared, in order (the room helpers unlock the room's doors)
inline void clearLockFlags(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) clearStoryFlag(kFlagLocked + i);
}

// ---- the story's flags that more than one area reads -----------------------------------------------------------
constexpr uint32_t kFlagSt3eStory2Event = 0x5a0; // ST3E's story-2 event done (content/forbidden_island's script
                                                 // 0x64; the Flutter's door, content/yosyonke, reads it too)

// The flag groups exe 0x512810 clears
constexpr uint32_t kFlagGroupAll = 0;       // every flag
constexpr uint32_t kFlagGroupStory = 0x580; // 0x580..0x67f: cleared when the story advances (the arrival placement)
constexpr uint32_t kFlagGroupStage = 0x680; // 0x680..0x6ff: cleared by a stage load
constexpr uint32_t kFlagGroupRoom = 0x700;  // 0x710..0x74f (the doors' locks): cleared by a stage load
// exe 0x512810(group): the group's flags cleared (the byte ranges one byte at a time; the room group flag by flag)
inline void clearStoryFlagGroup(uint32_t group) {
    StoryFlags& f = storyFlags();
    auto clearBytes = [&](uint32_t firstFlag, uint32_t lastFlag) {
        for (uint32_t i = firstFlag >> 3; i <= lastFlag >> 3; ++i) f.bytes[i] = 0;
    };
    if (group == kFlagGroupStage) clearBytes(0x680, 0x6ff);
    else if (group == kFlagGroupAll) clearBytes(0, 0x8ff);
    else if (group == kFlagGroupStory) clearBytes(0x580, 0x67f);
    else if (group == kFlagGroupRoom)
        for (uint32_t n = 0x710; n < 0x750; ++n) clearStoryFlag(n);
}

// The second flag block (0xa73de0, 0x600 bytes): the map's explored cells (a bit per cell, each area's from its own
// byte: game/minimap marks them, the status sub-screen's map page shows them), kept in the save, cleared by a new game
constexpr size_t kExtraFlagBytes = 0x600;
struct ExtraFlags {
    StateArray<uint8_t, kExtraFlagBytes> bytes{};

    // the bytes cleared (a new game)
    void clearBytes() { bytes.fill(0); }
    // the block as the save copies it (dwords)
    uint32_t word(size_t i) const { return flagBytesWord(bytes, i); }
    void setWord(size_t i, uint32_t v) { setFlagBytesWord(bytes, i, v); }
};
inline ExtraFlags& extraFlags() {
    static ExtraFlags s;
    return s;
}

inline uint8_t& storyFlagByte(uint32_t n) {
    const size_t i = n >> 3;
    if (i < kStoryFlagBytes) return storyFlags().bytes[i];
    return extraFlags().bytes[i - kStoryFlagBytes];
}

} // namespace od::port
