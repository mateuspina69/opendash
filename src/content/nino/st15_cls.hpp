// The ST15 / ST16 group member (pool 0x20 class 0x4b6280, type 0x3d), content/nino/st15_cls.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kGroupMemberType = 0x3d; // its animated type in ST15 / ST16's tables

// The member's parameters: the points it may spray at (bits 0x10 << k / 1 << k, written by its spawner:
// content/nino/field_spawners), its group's e0 slot, the kind of the last hit it took (the attack word >> 16)
struct GroupMemberParams {
    explicit GroupMemberParams(const ActorHeader& actor) : GroupMemberParams(const_cast<ActorHeader&>(actor)) {}
    explicit GroupMemberParams(ActorHeader& actor) : pointMask(actor.params[0]), group(actor.params[2]), lastHitKind(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> pointMask;
    uint8_t& group;
    uint8_t& lastHitKind;
};

// exe 0x4b6280 (pool 0x20, ST15 sub 0 / ST16 sub 0; pool-0x20 type table entry 0x3d sub 0): a member of an e0 group
// that walks the room's node graph, runs to the variant's points (table 0x894778) when its point bits allow, turns there
// spraying (pool-0x40 type 0x13, sounds 0x199 / 0x18e), picks up the group's static boxes (type 0x25, carried between
// bones 3 / 5) or follows / jumps at the group leader, knocked back by hits (balance penalties, hp), can be lifted /
// carried / thrown by the player, runs off when its hp is out and is freed off screen (despawned while carried: -10000
// balance + a pool-e0 type-0x1b trigger 0x16)
void groupMemberUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
