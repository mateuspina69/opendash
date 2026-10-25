// The town's room scripts: the stage-case handlers of ST05 / ST06 / ST0A / ST0B / ST0C / ST47
// (content/yosyonke/town_room_cases.cpp).
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// the handler fn of one of these stage cases (run = false: only whether it is transcribed). Besides the table
// handlers, the whole stage cases whose body is more than "handler + door test" are accepted by their own address
// (they dispatch to their handlers themselves and contain their door test): 0x57c0d0 (ST05), 0x57c390 (ST06),
// 0x57d050 (ST0B)
bool townRoomCaseHandler(RoutineId fn, bool run);

} // namespace od::port
