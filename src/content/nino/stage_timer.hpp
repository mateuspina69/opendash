// The spawn parameters of the stage's timer controller (pool 0xe0 type 0x1b; its class is not the Nino files'), which
// the Nino classes spawn: its value, its limit, the story flag it sets (the record point's spare lane; -1: none)
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

struct StageTimerParams {
    explicit StageTimerParams(const ControllerActor& actor) : StageTimerParams(const_cast<ControllerActor&>(actor)) {}
    explicit StageTimerParams(ControllerActor& actor)
        : value(actor.params, 0), limit(actor.params[2]), flag(actor.recordPoint.spare) {}
    BytesField<uint16_t, 4> value;
    uint8_t& limit;
    int16_t& flag;
};

} // namespace od::port
