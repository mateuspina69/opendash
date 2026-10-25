// The dropped item of the pirate attack (static ST2B / ST3B type 8, exe 0x596670), content/saul_kada/dropped_item.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kDroppedItemType = 8; // the stage static type (ST2B / ST3B)
// The item's spawn parameters (its spawners write them): the flag it sets, its kind, its message
struct DroppedItemParams {
    explicit DroppedItemParams(const StaticActor& actor) : DroppedItemParams(const_cast<StaticActor&>(actor)) {}
    explicit DroppedItemParams(StaticActor& actor) : flag(actor.params, 0), kind(actor.params[2]), message(actor.params[3]) {}
    BytesField<uint16_t, 4> flag;
    FieldAlias<int8_t, uint8_t> kind;
    FieldAlias<int8_t, uint8_t> message;
};

// exe 0x596670 (ST2B / ST3B static type 8): a dropped item (tossed / floating; touched -> its flag and its message)
void droppedItemUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
