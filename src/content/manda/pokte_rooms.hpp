// Pokte Village's rooms in the room scripts (ST24 / ST25, Manda Island): their room-script handlers -- Tron at the gate
// (script 0x1b) and her tank, the four houses she damages and their rebuilding from the donations, the villagers, the
// gate of the Manda ruin (script 0x14). Dispatched through the content registry's room-handler chain.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::content {

// the handler fn of one of these rooms (run = false: only whether it is transcribed)
bool pokteRoomHandler(RoutineId fn, bool run);

} // namespace od::content
