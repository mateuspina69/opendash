// Nino Island's field enemy spawners (pool 0xe0 type 0xa): ST15 exe 0x41ca90, ST16 exe 0x41d320, ST17 exe 0x41dd70,
// ST18 exe 0x41e7e0, and the ST18 repair gauge's picture: the classes that spawn the stage's enemies through the
// encounter slots (game/encounter) or directly, track them and keep their defeated ids and the story flags. The state
// dispatches the class's table: state 0 initialises, state 1 runs the variant; sub-states in the sub-state / step.
// One routine per exe routine; their records, positions and routes are read from the exe image.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/hud.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The ST16 box spawner (variant 2 of exe 0x41d320): the boxes alive, the lever's slot (a static type 1), the crane's slot
// (pool 0x20 type 0x44), the crane called, some boxes missing (the crane idle), the four lift boxes' slots (static type
// 0x25; the box carrier fetches them: content/nino/field_enemies)
struct BoxSpawnerFields : ActorClassState {
    explicit BoxSpawnerFields(ControllerActor& actor)
        : boxesAlive(actor.stepCounter), lever(actor.params[0]), crane(actor.params[1]), callCrane(actor.params[2]),
          boxesMissing(actor.params[3]), slot_(actor) {}
    uint8_t& boxesAlive;
    uint8_t& lever;
    FieldAlias<int8_t, uint8_t> crane;
    uint8_t& callCrane;
    uint8_t& boxesMissing;
    StateArray<uint16_t, 4> boxes{};
    // exe 0x40dc40(+0x10, +0x1c): the boxes and the slot's rider word cleared
    void clearBoxesAndTail() {
        boxes = {};
        slot_.rider = nullptr;
    }

private:
    ControllerActor& slot_;
};

// exe 0x41ca90: ST15 e0 type 0xa: pool-0x60 type-0x1b waves, flags 0x680 / 0x681 / 0x28
void spawnerSt15Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41d320: ST16 e0 type 0xa: type-0x49 respawns, pool-0x60 type-0x25 blocks, timers 0x10..0x14
void spawnerSt16Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41dd70: ST17 e0 type 0xa: type-0x2d walkers on routes, grid-cell flags 0x32..0x34
void spawnerSt17Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41e7e0: ST18 e0 type 0xa: type-0x2d / pool-0x60 type-0x1b, repair gauge (picture od::hud::ninoGaugeDraw)
void spawnerSt18Update(const PlayerEnv& env, uint32_t actor);
// exe 0x41ecd0's packets (the ST18 spawner's variant 3, state 1): the repair gauge -- 3 plates from the exe
// records 0x88abf0 then its 4 leading decimal digits (100000 / 10000 / 1000 / 100) at the x of 0x88ac20;
// tpage 0x0e, clut 0x3c38, colour 0x808080, or 0x2020f0 while the machine is hit (its +0xd 1: the frame &
// 0x10, 2: & 4)
std::vector<hud::Prim> ninoGaugeDraw();

} // namespace od::port
