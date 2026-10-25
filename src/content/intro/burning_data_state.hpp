// The class states of burning_data.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::burning_data_state {

// Data's class block (+0x14c; the exe's pointer 0xa59e80): his hit box, the flame on his head, his flags, a timer, the
// path he runs and its step, the cry timer
struct DataFields : ActorClassState {
    explicit DataFields(AnimatedActor& actor) : slot_(actor) {}
    HitBoxHead hitBox;
    uint32_t flame = 0;
    uint16_t flags = 0;
    int16_t timer = 0;
    uint8_t path = 0;
    uint8_t pathStep = 0;
    int16_t cryTimer = 0;

    // the flags' low byte, which the exe sets with a byte read-modify-write
    void setCatchingFire() { flags = uint16_t(flags | kCatchingFire); }
    bool has(uint16_t bit) const { return (uint8_t(flags) & bit) != 0; }

    static constexpr uint16_t kDone = 0x01; // every fire out and home: no more hit entry
    static constexpr uint16_t kBurning = 0x02;
    static constexpr uint16_t kCatchingFire = 0x04; // the catch's first steps: the water does not hurt yet

    // the class block cleared as the exe does (exe 0x40dc40: eight dword stores)
    void clear() { slot_.clearClassBlock(); }

    void clearBlock() override {
        hitBox = {};
        flame = 0;
        flags = 0;
        timer = 0;
        path = 0;
        pathStep = 0;
        cryTimer = 0;
    }

private:
    AnimatedActor& slot_;
};

} // namespace od::port::burning_data_state
