// The class states of st09_kick_ball.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st09_kick_ball_state {

// the ball's bytes
struct BallFields : ActorClassState {
    explicit BallFields(StaticActor& actor) : kicks(actor.step, actor.stepCounter), box(actor.hitBox) {}
    BytesOfField<int16_t> kicks; // the kicks paid (over the step bytes)
    HitBoxHead& box; // its hit-list result box
    uint16_t flagsWord_ = 0;
    uint16_t timer = 0; // the kick's cool-down / the goal's delay
    int16_t heading = 0; // the yaw it rolls along
    int16_t playerAngle = 0;
    PartField<uint8_t, uint16_t> flags{flagsWord_, 0}; // kBall*
    bool rollingWord() const { return (flagsWord_ & kRollingBit) != 0; }
    void clearRollingWord() { flagsWord_ = uint16_t(flagsWord_ & uint16_t(~kRollingBit)); }
    static constexpr uint16_t kRollingBit = 0x2;

    void clearBlock() override {
        flagsWord_ = 0;
        timer = 0;
        heading = 0;
        playerAngle = 0;
    }
};

} // namespace od::port::st09_kick_ball_state
