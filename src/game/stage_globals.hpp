// The globals a stage setup writes (exe 0x5d5a80's template and the four RNG setups' rest): the generated tables of
// game/stage_setup and game/encounter name them, game/stage_setup writes each into its system's field.
#pragma once

#include <cstdint>

namespace od::port {

enum class StageGlobal : uint8_t {
    None,                                                                             // not a write (a call)
    AnimatedClasses, StaticClasses, ProjectileClasses, EffectClasses, ControllerClasses, // game/actors' class tables
    EncounterZoneTables, EncounterGroupTables, EncounterFieldTable, EncounterEnemyGroups, // game/encounter
    ClutAnimations, GroundKinds, TownRoutes, TransitionHandlers,                      // game/stage_state
    StageVoices, SecondVoices,                                                        // game/audio_logic
    Script8Callback, Script30Callback, RoomCallback, SecondRoomCallback,              // game/stage_state
    StageBlockHead,                                                                   // the stage block's first word
};

} // namespace od::port
