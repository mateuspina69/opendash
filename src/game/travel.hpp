// The Flutter's travel map: ST01 (its room case starts script 7), script 7 (exe 0x517f30: the Flutter icon steered over
// the map, the destinations of the story's map, a destination's callback, then the request to its docking stage) and
// the map icons (ST01 static type 0x33, exe 0x5ad1c0). The island the Flutter is docked at is GameState::flutterDock
// (exe 0x5d5a80: the stage's dock), the dock counters advance by it (exe 0x5d5ae0); the Flutter's door is rewritten by
// it (game/spawn). Bit-exact against tests/data/story_*travel*.bin (test_story).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"


namespace od::port {

// the travel map's script (its handler: RoutineId::TravelScript)
constexpr uint8_t kTravelScriptId = 7; // the travel map's script

// The travel map as the host draws it (OpenDash's draw of the map's pictures and icons): the map camera's target, the
// story's marker and the destinations of the story's map (their index in the map's list and their place on the map)
struct TravelMapIcon {
    uint32_t index;
    int16_t mapX, mapZ;
};
struct TravelMapDraw {
    int16_t cameraX = 0, cameraZ = 0;
    uint8_t storyMarker = 0;
    std::vector<TravelMapIcon> destinations; // the list read while it is mapped; destination 2 is not drawn
};
// the travel map's draw state while script 7 runs its choosing state (false: no map shown)
bool travelMapDraw(TravelMapDraw& out);

// script `id` one tick when it is the travel map (false: id is not script 7)
bool travelScriptTick(uint8_t id);
// ST01 static type 0x33: the map icons (the Flutter, the second ship and their shadows)
void travelIconUpdate(const PlayerEnv& env, uint32_t actor);
// ST01's room case (exe 0x57bb10: its state byte 0: script 7, then 1)
void travelRoomCase();
// exe 0x5d5a80's dock part at a stage load (the stage's dock of the exe table, unless the table keeps it) and
// exe 0x5d5ae0: docked at another island past the first stages, the dock counters (not 0 / 0xff) advance by one
void flutterDockUpdate();

} // namespace od::port
