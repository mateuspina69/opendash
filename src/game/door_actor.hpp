// The door actor, class 0x59ce50 (static pool type 0x19 through the stage tables; 60 actors in 59 rooms): a door you
// talk to. Its event asks "go through?"; "yes" (event flag 0x702, set by the message) starts the door transition
// (kind 14 of game/exit_transitions: flag 0x700), in which the door carries the player through the floor or the
// ceiling (the lifts). A locked door (flag 0x710 + its lock index) has another message.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kDoorActorType = 0x19; // its static pool type (the stages' class tables)

// +8: the door's state
enum class DoorState : uint8_t {
    Init = 0,   // bind the model, the boxes (the talk box by the door's facing)
    Closed = 1, // the player in front and talking -> the event
    Talk = 2,   // the event runs; "yes" -> the door transition
};


// the door's spawn parameters
struct DoorFields {
    explicit DoorFields(const StaticActor& actor) : DoorFields(const_cast<StaticActor&>(actor)) {}
    explicit DoorFields(StaticActor& actor) : message(actor.params[0]), descends(actor.params[1]), lock(actor.params[2]), lockedMessage(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> message; // its event's message
    uint8_t& descends; // its transition carries the player down (y grows); 0: up
    FieldAlias<int8_t, uint8_t> lock; // the lock: event flag 0x710 + lock set = locked
    FieldAlias<int8_t, uint8_t> lockedMessage; // the message while locked
};

// exe 0x59ce50: the door's update (ActorUpdateFn)
void doorActorUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
