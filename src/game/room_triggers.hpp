// The room's trigger volumes (the collision volumes of shape >= 0x100, game/collision triggerScan) and what the actor
// standing in one does: the warps, the exits, the doors and the passages arm the transition request (game/transition
// runs it) from the room's exit table, the event volumes start their event, the status floors give the player a status.
#pragma once

#include <cstdint>

#include "game/transition_state.hpp"

namespace od::port {

// The trigger the scan found (exe 0x413c80 writes it): its type (the volume's shape >> 8), its flags (the volume's mask
// word) and its centre and facing
struct TriggerRecord {
    uint16_t type = 0;
    uint16_t flags = 0;
    RoomPlace centre;
};
// the trigger record (one block for the program)
inline TriggerRecord& trigger() {
    static TriggerRecord t;
    return t;
}

struct Actor;
struct PlayerEnv;

// exe 0x413310: the trigger volume the actor stands in (exe 0x413c80, its record at 0xa7b260) and what it does (only the
// player's input-free tick and the exits' conditions arm anything); returns the trigger's type (0: none)
uint32_t roomTriggers(const PlayerEnv& env, Actor& actor);

} // namespace od::port
