// The stage and room setup: the stage load's per-stage stage setup [0x8d3808 + stage * 4] (exe 0x5d5a80 calls it: the
// per-stage class tables of the actor pools, the zone / group / field encounter tables, the town routes, the
// transition kinds, the voice tables, the encounter slots) and the room setup's per-stage room function [0x8d397c +
// stage * 4] (exe 0x5d5b30: the room's spawn list, its room helper (the island's room list, flags, cell switches),
// the encounter slots' room, the exits, the weapon mode). the parts of a stage's functions beyond the template are
// the content's (ContentRegistry::roomHelpers / roomFunctions).
#pragma once

#include <cstdint>
#include <string>


namespace od::port {

// What of the running room's stage / room setup and room script is transcribed
struct SpawnCoverage {
    bool stage = false;      // the stage setup's actor part (no actor global behind a branch)
    bool room = false;       // the room function (its helpers and its code beyond the template)
    bool roomScript = false; // the room script's case for the current story / sub-room
    std::string why;         // what is missing (log)
    bool all() const { return stage && room && roomScript; }
};
// for the stage, the sub-room and the story of the game state
SpawnCoverage spawnCoverage();
// [0x8d3808 + stage * 4](): the table's writes and the encounter slots (exe 0x50f280); the four RNG layout stages
// through game/encounter. Throws a port gap for the stages with other branches.
void stageSetup();
// [0x8d397c + stage * 4](param)'s actor part: the spawn list, the room helpers, the encounter slots' room (exe
// 0x50f320), the stage's code beyond the template, the exits, the weapon mode and the stage's tail. Not: the room
// parameters, the sky, the room camera (the host's).
void roomSetupActors(int32_t param);

} // namespace od::port
