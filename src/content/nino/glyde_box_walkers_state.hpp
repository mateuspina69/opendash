// The class states of glyde_box_walkers.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::glyde_box_walkers_state {

// The walker's own values: the Actor lanes it keeps them in and its class block
struct WalkerFields : ActorClassState {
    explicit WalkerFields(AnimatedActor& actor) : plannedCell(actor.velocity.spare), spreadLeft(actor.acceleration.x), spreadRight(actor.acceleration.z), gunPitch(actor.acceleration.spare), footstepTicks(actor.spin.x), hurtSoundTicks(actor.spin.y), action(actor.shownParts), cellX(actor.hopperBytes[4]), cellZ(actor.hopperBytes[5]), routeStep(actor.hopperTimer) {}
    int16_t& plannedCell; // the velocity's spare lane: the target cell when a plan started
    int16_t& spreadLeft; // the acceleration's lanes: the guns' yaw spread (left / right) ...
    int16_t& spreadRight;
    int16_t& gunPitch; // ... and their pitch
    FieldAlias<uint16_t, int16_t> footstepTicks; // the spin's lanes: the footsteps' countdown ...
    FieldAlias<uint16_t, int16_t> hurtSoundTicks; // ... and the hurt sound's
    uint8_t& action; // WalkerAction
    FieldAlias<int8_t, uint8_t> cellX;
    FieldAlias<int8_t, uint8_t> cellZ;
    uint8_t& routeStep;
    uint32_t target = 0; // the player or a box (an address; 0: none)
    int16_t ticks = 0;
    int16_t lastCell = 0;
    HitBoxHead hit;
    uint8_t flags = 0; // kWalker*
    int16_t heading = 0; // the planned heading (-1: none)
    uint8_t actionStep = 0;
    int16_t actionTicks = 0;
    int16_t headYaw = 0;
    int8_t shotCount = 0;
    void setAction(uint8_t a) { set_actionWord_(uint32_t(a)); }

private:
    uint16_t get_actionWord_() const { return uint16_t(uint32_t(std::make_unsigned_t<uint8_t>(actionStep)) << 8 | uint32_t(std::make_unsigned_t<uint8_t>(action)) << 0); }
    void set_actionWord_(uint32_t v) { actionStep = uint8_t(std::make_unsigned_t<uint8_t>(v >> 8)); action = uint8_t(std::make_unsigned_t<uint8_t>(v >> 0)); }
public:

    void clearBlock() override {
        target = 0;
        ticks = 0;
        lastCell = 0;
        hit = {};
        flags = 0;
        heading = 0;
        actionStep = 0;
        actionTicks = 0;
        headYaw = 0;
        shotCount = 0;
    }
};

// The rolling bomb's fields: its thrower, its class block (its hit box, the roll sound's countdown)
struct BombFields : ActorClassState {
    explicit BombFields(ProjectileActor& actor) : thrower(actor.hitBox.result) {}
    uint32_t& thrower;
    HitBox hit; // its shape asks for the contact output: the hit list writes it after the head
    uint16_t rollSoundTicks = 0;
    StateArray<uint8_t, 18> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBlockBytes() {
        hit = {};
        rollSoundTicks = 0;
        clearedPad0 = {};
    }
};

// The path search's globals: the target's cell and the depth that found the way (8: none)
struct WalkerSearch {
    int32_t targetZ = 0; // 0xa59e54
    int32_t targetX = 0; // 0xa59e58
    int16_t depth = 0; // 0xa59e5c
};
inline WalkerSearch& search() {
    static WalkerSearch s;
    return s;
}

} // namespace od::port::glyde_box_walkers_state
