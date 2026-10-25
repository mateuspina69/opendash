// The class states of st1f_birdbot_tank.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st1f_birdbot_tank_state {

struct BirdbotTankFields : ActorClassState {
    explicit BirdbotTankFields(AnimatedActor& actor) : circleBack(actor.params[0]), appoSlot(actor.params[2]), dahSlot(actor.params[3]), headYaw(actor.rotation.spare), pitchStep(actor.spin.x), rollStep(actor.spin.y), pitchGrowth(actor.spin.z), rollGrowth(actor.spin.spare), fireTicks(actor.shownParts), contact(actor.hopperBytes[0]), mode(actor.hopperBytes[1]), fireRequest(actor.hopperBytes[2]), aimTicks(actor.hopperBytes[3]), fireCycle(actor.townBirdHits), stepTicks(actor.townBirdHitsHigh) {}
    uint8_t& circleBack; // circles the arena the other way
    FieldAlias<int8_t, uint8_t> appoSlot; // the pair's slots: its targets besides the player
    FieldAlias<int8_t, uint8_t> dahSlot;
    int16_t& headYaw; // the turret's yaw (the rotation's spare lane)
    int16_t& pitchStep; // the tilt's steps toward the slope and their growth (the spin lanes)
    int16_t& rollStep;
    int16_t& pitchGrowth;
    int16_t& rollGrowth;
    uint8_t& fireTicks; // the fire cycle's ticks (its 4th ticks fire)
    uint8_t& contact; // kTankWall | kTankOffFloor | kTankTerritory
    uint8_t& mode; // kTank* of the sub-state
    uint8_t& fireRequest;
    uint8_t& aimTicks; // ticks off target before a re-aim
    int8_t homeCellX = 0; // the cell it keeps to (0x100 units)
    int8_t homeCellZ = 0;
    uint8_t& fireCycle; // FireCycle
    uint8_t& stepTicks;
    HitBoxHead hitBox;
    int16_t slopePitch = 0; // the ground's slope under it (the tilt's targets)
    int16_t slopeRoll = 0;
    int16_t steerYaw = 0;
    uint8_t gait = 0; // TankGait
    uint8_t gaitBits = 0; // kGait*
    uint16_t neighbour = 0; // the neighbour word (game/enemy_neighbours)
    int8_t turnRate = 0; // the re-aim's turn rate / the dying ticks

    void clearBlock() override {
        homeCellX = 0;
        homeCellZ = 0;
        hitBox = {};
        slopePitch = 0;
        slopeRoll = 0;
        steerYaw = 0;
        gait = 0;
        gaitBits = 0;
        neighbour = 0;
        turnRate = 0;
    }
};

} // namespace od::port::st1f_birdbot_tank_state
