// The class states of st3c_gold_shockwave.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st3c_gold_shockwave_state {

// The actor's own bytes (its block): a timer, the scale's steps (x / z, y); its scale x / y as the dword the exe
// stores
struct WaveFields : ActorClassState {
    explicit WaveFields(StaticActor& actor) : stepY(actor.hitBox.previousX), scale(actor.scale) {}
    FieldAlias<uint16_t, int16_t> stepY;
    ShortVec3& scale; // the slot's scale (its x / y stored as one dword)
    void setScaleXY(uint32_t word) {
        scale.x = int16_t(uint16_t(word));
        scale.y = int16_t(uint16_t(word >> 16));
    }
    uint16_t timer = 0;
    uint16_t stepXZ = 0;
    PartField<uint8_t, uint16_t> timerLow{timer, 0};

    void clearBlock() override {
        timer = 0;
        stepXZ = 0;
    }
};

// The exe globals st3c_gold_shockwave.cpp keeps (their exe addresses in the comments)
struct St3cGoldShockwaveGlobals {
    uint32_t waveBlock = 0; // 0xa5a930
};
inline St3cGoldShockwaveGlobals& st3cGoldShockwaveGlobals() {
    static St3cGoldShockwaveGlobals s;
    return s;
}

} // namespace od::port::st3c_gold_shockwave_state
