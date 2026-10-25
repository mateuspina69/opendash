// The class states of st24_ruin_gate.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st24_ruin_gate_state {

// ---- the guardian (exe 0x487d50; states [0x8922c8]: init 0x487e00, act 0x487ed0) ----------------------------------
// Its fields: the lean's target (the rotation's spare lane), the head's turn (the velocity's spare lane), and its
// class block -- the roll's lean speed, its flags (kNoLean / kReflect / kFading / kNoHeadTurn), the collision's
// results, the fade's counter, the bob switch and the dust's phase
struct GateGuardianFields : ActorClassState {
    explicit GateGuardianFields(AnimatedActor& actor) : leanTarget(actor.rotation.spare), headTurn(actor.velocity.spare), hits(actor.shownParts), bobs(actor.hopperBytes[4]), dustPhase(actor.townBirdHitsHigh) {}
    int16_t& leanTarget;
    int16_t& headTurn;
    uint8_t& hits;
    int8_t fadeCounter = 0;
    uint8_t& bobs;
    uint8_t& dustPhase;
    int16_t rollSpeed = 0;
    uint8_t flags = 0;

    void clearBlock() override {
        fadeCounter = 0;
        rollSpeed = 0;
        flags = 0;
    }
};

} // namespace od::port::st24_ruin_gate_state
