// The class states of hopping_drone.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/manda/hopping_drone.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::hopping_drone_state {

// The drone's fields: its hop range (spawn byte << 8), the partner died (set by the partner), the partner's slot index;
// its class block -- the hit box, a timer, the flight yaw, the contact and behaviour bits, the hop's step and distance,
// the home yaw, the lateral drift and its limit, the drift's side, the hunt's beats, the flame effect's slot
struct DroneFields : ActorClassState, HoppingDroneParams {
    explicit DroneFields(AnimatedActor& actor) : HoppingDroneParams(actor), side(actor.townBirdHits), beats(actor.townBirdHitsHigh), flameSlot(actor.hopperTimer), paramsWord_(actor.params, 0), velocity(actor.velocity) {}
    uint8_t& side;
    uint8_t& beats;
    uint8_t& flameSlot;
    BytesField<uint32_t, 4> paramsWord_;
    ShortVec4& velocity; // the slot's velocity (the death's byte stores clear its x / y / z)
    HitBoxHead hitBox;
    int16_t timer = 0;
    uint16_t flightYaw = 0;
    uint8_t contact = 0;
    uint8_t behaviour = 0;
    int16_t hopStepX = 0;
    int16_t hopStepZ = 0;
    int16_t hopDistance = 0;
    uint16_t homeYaw = 0;
    int16_t drift = 0;
    int16_t driftLimit = 0;
    void copyParamsFrom(const DroneFields& o) { paramsWord_ = o.paramsWord_; }
    void clearVelocityBytes() {
        velocity.x = 0;
        velocity.y = 0;
        velocity.z = 0;
    }

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        flightYaw = 0;
        contact = 0;
        behaviour = 0;
        hopStepX = 0;
        hopStepZ = 0;
        hopDistance = 0;
        homeYaw = 0;
        drift = 0;
        driftLimit = 0;
    }
};

} // namespace od::port::hopping_drone_state
