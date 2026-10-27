// The rooms of the sub-ruins ST4D / ST4E / ST4F / ST50 (the four stages whose stage setup draws an encounter layout
// from the RNG, game/encounter encounterStageLayout): their room helpers and the room scripts' stage cases (exe
// 0x512890) with every handler they dispatch to. The pool-0xe0 classes their rooms spawn the groups with:
// content/subruins/subruin_spawners.
//
//   subruinRoomHelper        0x58b520 (ST4D) / 0x58b850 (ST4E) / 0x58b980 (ST4F) / 0x58bb90 (ST50): the per-stage
//                            helper of the room setup [0x8d397c + stage * 4] (the handler word cleared, exe 0x55c000(-1,
//                            0, 0), a flag in sub 0; ST4E sub 4: a cell switched; ST50: records 0x8cd778 x 2)
//   subruinRoomScripts       the room scripts' cases 0x4d (the handler tables 0x8cd624 / 0x8cd648 / 0x8cd66c by the
//                            layout flag 0xb0 / 0xb1 / 0xb2, then the door test), 0x4e [0x8cd690 + sub * 4], 0x4f
//                            [0x8cd6b8 + sub * 4], 0x50 [0x8cd748 + sub * 4] then the pace 3 and the player in water;
//                            handlers 0x58b5e0 / 0x58b600 / 0x58b610 / 0x58b6e0 / 0x58b760 / 0x58b830 / 0x58b900 /
//                            0x58b9d0 / 0x58ba30 / 0x58ba70 / 0x58bab0 / 0x58bb40 / 0x58bc10..0x58bdd0 (step 0x40) /
//                            0x58be10 / 0x58be50 / 0x58bf10 / 0x582490
//   subruinRoomScriptsKnown  the current stage / sub / layout flag reaches only transcribed handlers
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

// [0x8d397c + stage * 4]'s helper fn (0x58b520 / 0x58b850 / 0x58b980 / 0x58bb90); run = false only answers whether
// fn is one of them (the contract of game/spawn's roomHelper)
bool subruinRoomHelper(RoutineId fn, bool run);
// the room scripts' case for the running stage in 0x4d..0x50 (the handler's step byte 0xa714e0), the door test
// included where the exe has it (case 0x4d inline; the handlers of 0x4e / 0x4f / 0x50 do their own). false (nothing
// done) for the other stages. Throws std::logic_error on a handler that is not transcribed (subruinRoomScriptsKnown
// says beforehand).
bool subruinRoomScripts();
bool subruinRoomScriptsKnown();
// a room handler of the content's chain (game/room_scripts): exe 0x58b830, which ST14 reaches too
bool subruinLockRoomHandler(RoutineId fn, bool run);

} // namespace od::port
