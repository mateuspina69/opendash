// The class states of island_visit.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port::island_visit_state {

// ---- ST1D controllers ---------------------------------------------------------------------------------------------
// The controllers' record point (the spawn record's s16 point and yaw at +0x10)
struct RecordPointFields : ActorClassState {
    explicit RecordPointFields(ControllerActor& actor) : yaw(actor.recordPoint.spare) {}
    int16_t& yaw;
    ShortVec3 point;
};

// exe 0x424d10 (ST1D pool e0 type 0x11, states [0x88bdbc]): the reaverbot pack. State 0 exe 0x424d30: the tick and the
// delay index cleared. State 1 exe 0x424d50: the tick counts up to the delay [0x88bdc4 + index * 2] (95, 80, 130, 95,
// 80, 130, 95, 80, 95, 145, -1 = wrap); at the delay, with the player in x 0x1000..0x2d00 / z 0x600..0xc00 (exe
// 0x424dd0), the next delay and a pool-0x20 type-0x34 sub-1 actor (its class places it)
struct PackFields : ActorClassState {
    explicit PackFields(ControllerActor&) {}
    uint32_t tickWord = 0; // the tick and the delay index, as the start's dword store
    uint32_t spareWord = 0; // cleared at the start (no reader traced)
    PartField<uint16_t, uint32_t> tick{tickWord, 0};
    PartField<uint16_t, uint32_t> delayIndex{tickWord, 2};
};

} // namespace od::port::island_visit_state
