// The transition request (game/transition runs it, game/exit_transitions plays its script): the block 0xa6d2e0 that the
// exits, the doors and the scripts fill to ask for a room / stage change, the room's exit table entries it is filled
// from, and the transition kinds.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "game/state_array.hpp"
#include "game/loaded_data.hpp"
#include "game/patched_data.hpp"
#include "game/stage_state.hpp"

namespace od::port {

// +6: the transition script 0 plays (the armed exit's trigger type - 2; the stage's kind handlers dispatch it)
enum class TransitionKind : uint8_t {
    HingedDoor = 0,    // exit type 2: one hinged door leaf, a walk through
    DoubleDoor = 1,    // exit type 3: two hinged leaves
    SlidingPair = 2,   // exit type 4: two sliding leaves
    Frozen = 4,        // exit type 6: the player frozen until the load
    Passage = 6,       // exit type 8: a sliding door / passage, walked out and (in the room entered) walked in
    Pit = 8,           // exit type 10: a fixed camera above the trigger
    Ladder = 9,        // exit type 11: a ladder / hole climbed
    PushedOut = 10,    // exit type 12: the player pushed forward
    Teleporter = 11,   // exit type 13: a teleporter pad (ST40)
    WalkOff = 13,      // exit type 15: walked off the trigger
    DoorActor = 14,    // the door actor's "yes" (game/door_actor)
};

// A point and a facing of a room (s16 x / y / z, s16 yaw): an exit's trigger centre, its destination
struct RoomPlace : DataRecord {
    static constexpr uint32_t kBytes = 8;
    int16_t x = 0, y = 0, z = 0, yaw = 0;
    template <class R> static RoomPlace load(const R& r, size_t o = 0) {
        return {{}, r.s16(o), r.s16(o + 2), r.s16(o + 4), r.s16(o + 6)};
    }
    void save(std::span<uint8_t> out, size_t o = 0) const {
        storeLe(out, o, x);
        storeLe(out, o + 2, y);
        storeLe(out, o + 4, z);
        storeLe(out, o + 6, yaw);
    }
};

// An entry of a room's exit table (0x18 bytes, exe data; an id of kExitEnd ends the table). The entries a stage setup
// redirects are patched records (game/patched_data): the readers of the table read them through exitRecordAt.
struct ExitEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x18;
    uint8_t id = 0;
    uint8_t byte1 = 0;         // the door's model variant / the teleporter pad's side
    uint8_t byte2 = 0;         // the transition camera's view (low nibble), the leaves' offsets (high)
    uint8_t byte3 = 0;         // (no reader is traced; the entry's copies take it)
    int16_t lockedMessage = 0; // the event a locked door starts (< 0: none)
    uint8_t destStage = 0;
    uint8_t destRoom = 0;
    RoomPlace trigger;         // +8: the trigger volume's centre and facing
    RoomPlace destination;     // +0x10

    template <class R> static ExitEntry load(const R& r) {
        return {{}, r.u8(0), r.u8(1), r.u8(2), r.u8(3), r.s16(4), r.u8(6), r.u8(7), RoomPlace::load(r, 8),
                RoomPlace::load(r, 0x10)};
    }
    void save(std::span<uint8_t> out) const {
        out[0] = id;
        out[1] = byte1;
        out[2] = byte2;
        out[3] = byte3;
        storeLe(out, 4, lockedMessage);
        out[6] = destStage;
        out[7] = destRoom;
        trigger.save(out, 8);
        destination.save(out, 0x10);
    }
};
constexpr uint8_t kExitEnd = 0xff;
constexpr uint32_t kExitEntrySize = 0x18;
// the exit entry at a pointer of an exit table: its patched copy when a stage setup redirects it, else the data's
inline ExitEntry exitRecordAt(uint32_t pointer) { return recordAt<ExitEntry>(pointer); }
// the room's exit entry i
inline ExitEntry exitEntry(uint32_t i) { return exitRecordAt(stage().exitTable + i * kExitEntrySize); }

// The fades a request asks for (+0x18 / +0x19: the fade in after the load, the fade out before it; kNoFade: none -- a
// fade out of kNoFade switches the sub-room in place)
constexpr uint8_t kNoFade = 0xff;

struct TransitionRequest {
    uint8_t state = 0;        // kTransition* below
    uint8_t keepPosition = 0; // non-zero: the arrival placement leaves the player where it is
    uint8_t exitId = 0;       // the exit's id byte and its second byte
    uint8_t exitByte1 = 0;
    uint8_t destStage = 0;    // the destination stage / sub-room
    uint8_t destRoom = 0;
    uint8_t kind = 0;         // TransitionKind: what script 0 plays
    uint8_t exitByte2 = 0;    // the exit's third byte
    RoomPlace trigger;        // the trigger's centre (a warp: the move's x / z offsets)
    RoomPlace destination;    // where the player arrives
    uint8_t fadeIn = 0;       // the fade after the load (kNoFade: none)
    uint8_t fadeOut = 0;      // the fade before it (kNoFade: the sub-room switched in place)
    StateArray<uint8_t, 2> spare{}; // the request's last two bytes (cleared with it; no reader is traced)

    // the request cleared (a host's quiet game state: no transition pending)
    void clearBytes() { *this = TransitionRequest{}; }
};

constexpr uint8_t kTransitionNone = 0;
constexpr uint8_t kTransitionAsked = 1;
constexpr uint8_t kTransitionAccepted = 2;   // the field mode took it: the script plays the walk-out
constexpr uint8_t kTransitionScriptDone = 3; // the script is done: the room / stage switches now
constexpr uint8_t kTransitionWarp = 0xff;    // an in-stage warp (a relative move, no load)

inline TransitionRequest& transitionRequest() {
    static TransitionRequest s;
    return s;
}

// The request a script makes (the stores the scripts inline): `state` (kTransitionAccepted: a load / an arrival in
// another room, kTransitionWarp: an in-stage warp) to stage / room, the player at (x, y, z) facing yaw, the fades in /
// out (kNoFade: none); the arrival's position kept off and the exit's bytes cleared
inline void requestTransition(uint8_t state, uint8_t stage, uint8_t room, int16_t x, int16_t y, int16_t z, int16_t yaw,
                              uint8_t fadeIn, uint8_t fadeOut) {
    TransitionRequest& r = transitionRequest();
    r.state = state;
    r.keepPosition = 0;
    r.exitId = 0;
    r.exitByte1 = 0;
    r.destStage = stage;
    r.destRoom = room;
    r.destination.x = x;
    r.destination.y = y;
    r.destination.z = z;
    r.destination.yaw = yaw;
    r.fadeIn = fadeIn;
    r.fadeOut = fadeOut;
}

} // namespace od::port
