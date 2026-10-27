// The ruin maps of the status sub-screen (the stages' second room callbacks).
#pragma once

#include "engine/routine_ids.hpp"

namespace od::port {

// The callback was a ruin map and was drawn.
bool statusMapTilesDraw(RoutineId callback);

} // namespace od::port
