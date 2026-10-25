// Calbania's and Pokte's rooms (ST1F..ST25, ST30..ST32) in the stage / room setup and the room scripts: their room
// helpers, the parts of their room functions beyond the template, ST32's stage case tail and their room-script
// handlers (game/stage_setup and game/room_scripts dispatch them through the content registry), and the state of
// Glyde's base's room scripts (ST31).
#pragma once

#include <cstdint>

#include "game/content_registry.hpp"
#include "game/state_array.hpp"

namespace od::content {

// The room scripts' state of Glyde's base: ST30's escape (sub 2: the timer, the one carried out and the gate's hint
// latch, the rescue script's latch; the place of the last gate static of subs 0..2) and ST31's rooms (its room helper
// clears sub 0's tick count, its two box latches and sub 1's script latch; sub 0's walk grid, boxes and walkers, sub
// 1's Birdbots)
struct GlydeBaseRoomState {
    uint32_t escapeTicks = 0; // 0xa5a6c8: the escape's ticks left (the escape timer shows them)
    int16_t gateStaticX = 0; // 0xa5a6cc: the place of the last gate static (sub 2's script 0x22)
    int16_t gateStaticZ = 0; // 0xa5a6ce
    uint8_t rescueLatch = 0; // 0xa5a6d0: script 0x25 started
    uint32_t carriedActor = 0; // 0xa5a6d4: the one the player carries out (pool 0x20 type 0x71)
    uint8_t gateHintLatch = 0; // 0xa5a6d8: the hint at the gate without the carried one shown
    port::StateArray<uint32_t, 3> cellBirdbots{}; // 0xa5a6dc: ST31 sub 1's Birdbots
    uint32_t ticks = 0; // 0xa5a6e8
    port::StateArray<uint32_t, 3> walkers{}; // 0xa5a6ec: ST31 sub 0's walkers
    uint8_t boxLatchA = 0; // 0xa5a6f8
    uint8_t boxLatchB = 0; // 0xa5a6f9
    uint8_t scriptLatch = 0; // 0xa5a6fa
    port::StateArray<uint8_t, 0x31> walkGrid{}; // 0xa7ad40: ST31 sub 0's 7 x 7 walk grid (the walkers' cells)
    port::StateArray<uint32_t, 4> boxes{}; // 0xa7ad80: ST31 sub 0's boxes
};
inline GlydeBaseRoomState& glydeBaseRoomState() {
    static GlydeBaseRoomState s;
    return s;
}

// the islands' room helpers / room-script handlers in the chains, their room functions' parts and stage cases' tails
void registerCalbaniaRooms(od::port::ContentRegistry& r);

} // namespace od::content
