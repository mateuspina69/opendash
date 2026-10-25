// The pool-0xe0 classes the sub-ruins' rooms spawn their encounter groups with (the rooms: content/subruins/
// subruin_rooms):
//   st4eGroupSpawnerUpdate     0x4227a0: ST4E type 0xa, states [0x88bc88], its variants [0x88bc90] by its variant
//   st50GroupSpawnerUpdate     0x422cb0: ST50 type 0xa, states [0x88bcb8], its variants [0x88bcc0] (all ret)
//   subruinLineTriggerUpdate   0x424fb0: type 0x13, the line trigger -> the encounter group of its parameter (0x425050)
//   subruinDropSpawnerUpdate   0x424e10: type 0x12, the player enters its box / crosses its facing -> its count (0: 4)
//                              of pool-0x20 actors type 0x2a variant 2 dropped 0x3000 ahead
//
// What they spawn (pool-0x20 types through the stage's class table; their updates are NOT here):
//   0x4227a0 v1: record 0x88bb08 (type 0x15 sub 1 v3, id 9), then 0x88bb20 x 5 (type 0x31); v3: 0x88bb98 x 4 (type
//   0x31); v4: 0x88bbf8 (type 0x56 v1, id 0x21) or e0 type 0x12; v7: 0x88bc10 x 5 (type 0x2a v2); v2 / v4: e0 type
//   0x12 (0x424e10 -> pool 0x20 type 0x2a v2). ST4F sub 7: 0x8cd730 (type 0x56 v1, id 0x40). ST4F sub 11: records
//   0x8cd6f0 x 2 (type 0x53), sub 12: 0x8cd718 (type 0x6f); ST50 sub 10: 0x8cd7a0 (type 0x6f). The groups themselves
//   are the layout's group tables (game/encounter_tables.inc).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

void st4eGroupSpawnerUpdate(const PlayerEnv& env, uint32_t actor);
void st50GroupSpawnerUpdate(const PlayerEnv& env, uint32_t actor);
void subruinLineTriggerUpdate(const PlayerEnv& env, uint32_t actor);
void subruinDropSpawnerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
