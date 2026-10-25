// The footstep dust (static type 0x32 of the stages that have it, exe 0x5acbc0, spawned by exe 0x5ad170): up to four
// puffs of dust kicked up at a walker's foot (the player's, a boss's, a burning foot's), each two billboards drifting
// apart while they grow and fade; the actor ends with its last puff.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/state_array.hpp"

namespace od::port {

constexpr uint8_t kFootstepDustType = 0x32; // a stage table type (per class: game/global_class_types.hpp)
constexpr uint32_t kDustPuffs = 4;

// One puff (0x14 bytes): its two points (s16; their spare lanes the drift x / z along the walker's side), its life
// (counted down from kDustPuffLife) and its age (frames, kDustPuffAge at its end)
struct DustPuff {
    ShortVec3 first;
    int16_t driftX = 0;
    ShortVec3 second;
    int16_t driftZ = 0;
    int16_t life = 0;
    uint16_t age = 0;
};
// The dust's fields (its static slot past the mesh layout): the walker (the player or an actor), the puffs, the puffs
// made, no more puffs (the walker stopped)
struct FootstepDustFields : ActorClassState {
    explicit FootstepDustFields(StaticActor&) {}
    ActorRef<Actor> parent;
    StateArray<DustPuff, kDustPuffs> puffs{};
    int8_t made = 0;
    uint8_t stopped = 0;

    // the static class block (the last puff's second point on, the counts) cleared
    void clearBlock() override {
        DustPuff& last = puffs[kDustPuffs - 1];
        last.second = {};
        last.driftZ = 0;
        last.life = 0;
        last.age = 0;
        made = 0;
        stopped = 0;
    }
};
enum class FootstepDustState : uint8_t { Start = 0, Run = 1, Free = 2 }; // exe 0x5acbe0 / 0x5acc30 / 0x5afdd0
// the variant: kDustAtFoot follows the walker's foot (the player's ground point, an actor's bone 3), else the dust
// starts at the spawn's point (the player's: at height 0)
constexpr uint8_t kDustAtFoot = 0;

// exe 0x5ad170(parent, variant, point): a footstep dust of the walker `parent` (an actor or the player) following its
// foot (kDustAtFoot)
void footstepDustAtFoot(ActorHeader& parent);
// ... of `variant` starting at the s16 point (not read for kDustAtFoot)
void footstepDustAt(const ActorHeader& parent, uint8_t variant, const int16_t point[3]);
// exe 0x5acbc0 (static type 0x32)
void footstepDustUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
