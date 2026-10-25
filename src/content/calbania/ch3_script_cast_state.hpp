// The class states of ch3_script_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "content/calbania/appo_dah_face.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::ch3_script_cast_state {

// =====================================================================================================================
// ---- exe 0x4c6fe0: ST1F pool 0x20 type 0x43 sub 4 (script 0x20 records 0x8a3940 / 0x8a3954, or 0x8a3968 / 0x8a397c
// with flag 0x580): Appo / Dah of the last phase of the square's defence. The variant routine [0x894ed0 + s8 +0xc * 4]
// = 0x4c70c0 (+0xc = 0 only) runs only when the step tick changed (or is 1). 0x4c70c0 by the state: 0 bind (variant +6,
// box 0x894ed8, the mouth closed, animation 0x1c / 0x1d by its pose, flag 0x680 + variant -> grey), then waits for the
// script's state; 1..3 follow the step sub; 4 placed by flag 0x580 == variant (animations 0x1e / 0x1f), walks in
// (0x35..0x37), talks (exe 0x4c7300) until tick 0x8c.
struct Script20AppoDahFields : ActorClassState {
    explicit Script20AppoDahFields(AnimatedActor& actor) : routine(actor.params[0]), seated(actor.params[1]), noGround(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> routine; // the variant routine (0 only)
    uint8_t& seated; // the first animation 0x1d (else 0x1c)
    uint8_t& noGround; // not kept on the ground
    uint8_t castFlags = 0; // kAppoDahRestart
    uint8_t animKey = 0;
    int8_t lastTick = 0;
    AppoDahMouth mouth;

    void clearBlock() override {
        castFlags = 0;
        animKey = 0;
        lastTick = 0;
        mouth = {};
    }
};

// =====================================================================================================================
// ---- exe 0x4e2da0: ST46 pool 0x20 type 0x5b sub 0 (script 0x3c record 0x8a7498, slot 0). States [0x895e64]: 0
// 0x4e2e80 bind (box 0x895e58, texture page 4, some parts hidden), 1 0x4e2f40 -> the action [0x895e6c + +9 * 4]: 0
// 0x4e2f60 (animation 3, decoded only until the script's cast starts; then animation 4; step sub 1 -> action 2), 1
// 0x4e2fe0 (walks toward the origin until within 0x40, animation 2; step sub 2 -> action 2), 2 0x4e3080 (animation 5
// placed at (-0x48, 0x18)).
struct Script3cCastFields : ActorClassState {
    explicit Script3cCastFields(AnimatedActor&) {}
    uint8_t unread153 = 0; // cleared by the first action (its reader is not traced)
    uint8_t holdPose = 0; // decoded without stepping

    void clearBlock() override {
        unread153 = 0;
        holdPose = 0;
    }
};

} // namespace od::port::ch3_script_cast_state
