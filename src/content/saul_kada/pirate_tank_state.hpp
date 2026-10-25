// The class states of pirate_tank.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::pirate_tank_state {

// Its own bytes (0x20 cleared at the bind): its hit box, the pitch / roll targets (the ground ahead / behind, left /
// right), its drive's yaw, a byte set at the reset (not read), its treads' bits, a sway direction (not read), the
// turret's step and acceleration, its timer, a step timer, the turret's state, its status, its flags, a count, its turn
// timer, the bullet's yaw offset, its burst's state, the shell's near lob; the turret's yaw (the rotation's spare
// lane) and its target (the velocity's spare lane), the roll step's growth (the spin's spare lane)
struct TankFields : ActorClassState {
    explicit TankFields(AnimatedActor& actor) : timer(actor.shownParts), turretState(actor.hopperBytes[0]), status(actor.hopperBytes[1]), flags(actor.hopperBytes[2]), turnTimer(actor.hopperBytes[5]), turretYaw(actor.rotation.spare), turretTarget(actor.velocity.spare), rollGrowth(actor.spin.spare) {}
    int8_t turretAccel = 0;
    uint8_t& timer;
    uint8_t& turretState;
    uint8_t& status;
    uint8_t& flags;
    int8_t count = 0;
    uint8_t& turnTimer;
    int8_t burst = 0;
    int16_t& turretYaw;
    int16_t& turretTarget;
    int16_t& rollGrowth;
    HitBoxHead hitBox;
    int16_t pitchTarget = 0;
    int16_t rollTarget = 0;
    int16_t driveYaw = 0;
    uint8_t resetMark = 0;
    uint8_t treads = 0;
    uint16_t swayDir = 0;
    int8_t turretStep = 0;
    uint8_t stepTimer = 0;
    int16_t bulletYaw = 0;
    int8_t shellNear = 0;

    void clearBlock() override {
        turretAccel = 0;
        count = 0;
        burst = 0;
        hitBox = {};
        pitchTarget = 0;
        rollTarget = 0;
        driveYaw = 0;
        resetMark = 0;
        treads = 0;
        swayDir = 0;
        turretStep = 0;
        stepTimer = 0;
        bulletYaw = 0;
        shellNear = 0;
    }
};

} // namespace od::port::pirate_tank_state
