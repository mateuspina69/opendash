// The lava floor of the fire ruins (ST27 / ST28 / ST34: pool e0 type 0x16, exe 0x4251e0) and its plumes (pool 0x60
// type 0x2e, exe 0x5abc30, defined with the mission targets' smoke in content/nino/mission_targets). See
// content/saul_kada/lava_vents.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// The lava floor's spawn parameter: the list of the segment kinds that raise plumes (an index of the exe's lists)
struct LavaVentsParams {
    explicit LavaVentsParams(const ControllerActor& actor) : LavaVentsParams(const_cast<ControllerActor&>(actor)) {}
    explicit LavaVentsParams(ControllerActor& actor) : ventList(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> ventList;
};
constexpr uint8_t kLavaVentsType = 0x16;

// exe 0x4251e0 (pool e0 type 0x16: ST27 / ST28 / ST34): the lava floor -- burns the player standing below the
// variant's height (exe 0x5720f0) and, every 40..103 ticks, raises a lava plume on a lava cell / a listed segment near
// the player
void lavaVentsUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5abc30 (pool 0x60 type 0x2e): 8 rising billboards (the states of the mission targets' smoke)
void lavaPlumeUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
