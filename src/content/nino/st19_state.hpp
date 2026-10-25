// The class states of st19.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::st19_state {

// The machine's fields: the rattle timer (odd: its value is the rattle's offset), the tilt of part 1 and the
// companion static
struct MachineFields : ActorClassState {
    explicit MachineFields(AnimatedActor&) {}
    uint16_t timer = 0;
    int16_t partTilt = 0;
    ActorRef<StaticActor> companion;

    void clearBlock() override {
        timer = 0;
        partTilt = 0;
        companion = nullptr;
    }
};

// the flying spark's start offset from bone 1 and its target offsets (exe data the spark's bone transform shifts in
// place as it reads them: patched points, game/patched_data)
constexpr TableId kSparkPoints[3] = {TableId::St19StateSparkFrom, TableId::St19StateDropSparkTo, TableId::St19StateSlideSparkTo};

} // namespace od::port::st19_state
