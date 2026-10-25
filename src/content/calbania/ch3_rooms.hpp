// The room-script handlers of chapter 3 (content/calbania/ch3_story's ch3RoomHandler asks each subject's file in
// turn): fn = the handler, run = tick it (false: only "is it known"); false when the file has no such handler. Internal
// to content/calbania.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// ST17 / ST18 (Nino island, the return and Glyde's last attack): content/calbania/nino_return_rooms
bool ninoReturnRoomHandler(RoutineId fn, bool run);
// ST1F / ST20 / ST23 (the square, the town, the Flutter's dock): content/calbania/calbania_story_rooms
bool calbaniaStoryRoomHandler(RoutineId fn, bool run);
// ST30 / ST31 (Glyde's base): content/calbania/glyde_base_rooms
bool glydeBaseRoomHandler(RoutineId fn, bool run);

// exe 0x57fa10(v): ST18's four cells (0x40 / 0x3f, 0x40 / 0x3f) to variant v (its room handlers and script 0x3a)
void st18CellsToVariant(uint32_t v);

} // namespace od::port
