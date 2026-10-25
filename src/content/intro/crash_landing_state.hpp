// The class states of crash_landing.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::crash_landing_state {

// Its kind (the spawn's parameter) and its class block: the height it starts at, its own height, the lowering's tick
// and length
struct SkyShipFields : ActorClassState {
    explicit SkyShipFields(StaticActor& actor) : kind(actor.params[0]), slot_(actor) {}
    uint8_t& kind;
    Fixed16 raisedY;
    int32_t groundY = 0;
    int16_t tick = 0;
    int16_t lowerTicks = 0;

    // the class block cleared as the exe does (exe 0x40dc40: four dword stores)
    void clear() { slot_.clearClassBlock(); }

    void clearBlock() override {
        raisedY = 0;
        groundY = 0;
        tick = 0;
        lowerTicks = 0;
    }

private:
    StaticActor& slot_;
};

} // namespace od::port::crash_landing_state
