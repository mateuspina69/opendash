// The attract intro's cast in ST03 (script 4; content/intro/st03_attract_cast.cpp): the crew, the Flutter, the craft
// and the props that key their animations, faces and motion on the script's step sub and tick, each with its own
// state machine per variant (the spawn's variant byte); and the scenery script 4 spawns (static type 0x16).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// The scenery's bytes (exe 0x59b690, static type 0x16; script 4 spawns it): its kind and index in the record table of
// its kind, and its yaw (<< 4)
struct AttractSceneryFields {
    explicit AttractSceneryFields(const StaticActor& actor) : AttractSceneryFields(const_cast<StaticActor&>(actor)) {}
    explicit AttractSceneryFields(StaticActor& actor) : kind(actor.params[0]), index(actor.params[1]), yaw(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> kind;
    FieldAlias<int8_t, uint8_t> index;
    FieldAlias<int8_t, uint8_t> yaw;
};
constexpr uint8_t kAttractSceneryType = 0x16; // the scenery's stage class type in ST02 / ST03

// The classes (registered in content/intro/register.cpp)
void st03PieceCarrierUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x47eb30 (pool 0x20 type 0x1c)
void st03WalkerUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x481100 (pool 0x20 type 0x1f)
void st03FlutterUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x49d8c0 (pool 0x20 type 0x30)
void st03PlacedModelUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x58d9c0 (static type 1)
void st03StillModelUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x5ad760 (static type 0x34)
void st03SceneryUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x59b690 (static type 0x16)
void st03RollUpdate(const PlayerEnv& env, uint32_t actor);         // exe 0x476f80 (pool 0x20 type 0x16)
void st03BarrelUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x47c790 (pool 0x20 type 0x1a)
void st03WatcherUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x47fa80 (pool 0x20 type 0x1d)
void st03CraftBodyUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x499650 (pool 0x20 type 0x2f)
void st03LightsUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x4da1e0 (pool 0x20 type 0x4e)
void st03BlinkerUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x484c30 (pool 0x20 type 0x23)
void st03StanderUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x4890f0 (pool 0x20 type 0x25)
void st03PartBlinkerUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x499310 (pool 0x20 type 0x2e)
void st03TalkerUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x480a70 (pool 0x20 type 0x1e)
void st03FaceUpdate(const PlayerEnv& env, uint32_t actor);         // exe 0x482210 (pool 0x20 type 0x21)
void st03SpeakerUpdate(const PlayerEnv& env, uint32_t actor);      // exe 0x48b390 (pool 0x20 type 0x28)
void st03ScreenUpdate(const PlayerEnv& env, uint32_t actor);       // exe 0x4b5df0 (pool 0x20 type 0x3c)
void st03CraftUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x4baec0 (pool 0x20 type 0x3f)

} // namespace od::port
