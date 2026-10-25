// The class states of st27_mission.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st27_mission_state {

// The controller's own bytes: Bon, Tron, the valve, the hint timer and its flags
struct MissionFields : ActorClassState {
    explicit MissionFields(ControllerActor& actor) : valve(actor.rider) {}
    ActorRef<Actor>& valve;
    ActorRef<AnimatedActor> bon;
    ActorRef<AnimatedActor> tron;
    int16_t hintTicks = 0;
    uint8_t hintFlags = 0; // kFirstHintSaid
};

} // namespace od::port::st27_mission_state
