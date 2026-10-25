// The door leaves: the static actors the door transitions spawn (game/exit_transitions; static pool types 1 hinged, 2
// double hinged, 3 sliding: handlers 0x5628b0 / 0x562b50 / 0x562e10 of the global table 0x8b1ff4), their states (0
// bind the model and place the leaf in the player's quadrant, 1 swing / slide by +9, 2 free) and the frame body's pass
// that updates them (the part of exe 0x55bce0 the port runs).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct PlayerEnv;

// the static pool types of the leaves
constexpr uint8_t kHingedLeafType = 1, kDoubleLeafType = 2, kSlidingLeafType = 3;

// +8
enum class DoorLeafState : uint8_t {
    Init = 0,   // the model bound (none: freed), the leaf placed and turned
    Moving = 1, // the swing / slide of +9
    Free = 2,   // freed (exe 0x5afdd0)
};
// +9 of the hinged leaves (types 1 / 2)
enum class SwingLeafSub : uint8_t {
    Idle = 0,
    FollowHand = 1, // open as far as the player's hand pushes it
    SwingBack = 2,  // swing back closed, one bounce
    SwingOn = 3,    // swing on by its speed until the speed turns
};
// +9 of the sliding leaves (type 3)
enum class SlidingLeafSub : uint8_t {
    Stop = 0,
    Open = 1,
    Close = 2,
};

// The leaf's spawn parameters (+0xc..+0xf, written by the door transitions): its offset from the trigger in the door's
// frame (turned into the player's quadrant at its Init; odd variants mirrored)
struct DoorLeafParams {
    explicit DoorLeafParams(const ActorHeader& actor) : DoorLeafParams(const_cast<ActorHeader&>(actor)) {}
    explicit DoorLeafParams(ActorHeader& actor) : offsetX(actor.params, 0), offsetZ(actor.params, 2) {}
    BytesField<int16_t, 4> offsetX;
    BytesField<int16_t, 4> offsetZ;
    void clear() {
        offsetX = 0;
        offsetZ = 0;
    }
};

// The hinged leaves' class bytes: the opening (the angle swung from closed) and the closed yaw; the swing's
// acceleration is kept in the spin's spare lane, its speed is the spin's yaw
struct SwingLeafFields : ActorClassState, DoorLeafParams {
    explicit SwingLeafFields(StaticActor& leaf) : DoorLeafParams(leaf), swingAcceleration(leaf.spin.spare) {}
    int16_t& swingAcceleration;
    int16_t opening = 0;
    int16_t closedYaw = 0;

    void clearBlock() override {
        opening = 0;
        closedYaw = 0;
    }
};
// The sliding leaves' class bytes: the slide's end point (x / z, integer parts)
struct SlidingLeafFields : ActorClassState, DoorLeafParams {
    explicit SlidingLeafFields(StaticActor& leaf) : DoorLeafParams(leaf) {}
    int16_t targetX = 0;
    int16_t targetZ = 0;

    void clearBlock() override {
        targetX = 0;
        targetZ = 0;
    }
};

// the leaf update (the class handlers of the three types; game/actors registers it)
void doorLeafUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x55bce0 (frame body, between the player dispatch and the actor collision pass) for the door leaves: each live
// static leaf of the global class table (+3 & kActorStageClass clear) not on the late loop updated; every other static
// actor is skipped (their updates are not ported). Returns the number of leaves updated.
int staticActorsUpdate();
// whether staticActorsUpdate runs the update of the static actor at `actor`
bool staticActorPorted(uint32_t actor);

} // namespace od::port
