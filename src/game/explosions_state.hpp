// The class state of explosions.cpp's blast: the fields it keeps in its slot (game/actor_state ActorClassState).
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port::explosions_state {

// A colour row of the blast (exe data 0x8c4218, 0xc bytes): the colour mask and the texture words of the sphere's bands
// and of the flashes
struct BlastColourRow : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint32_t mask = 0; // +0x0
    uint16_t bandTexture0 = 0; // +0x4
    uint16_t bandTexture1 = 0; // +0x6
    uint16_t flashTexture0 = 0; // +0x8
    uint16_t flashTexture1 = 0; // +0xa
    template <class R> static BlastColourRow load(const R& r) {
        BlastColourRow v;
        v.mask = r.template read<uint32_t>(0x0);
        v.bandTexture0 = r.template read<uint16_t>(0x4);
        v.bandTexture1 = r.template read<uint16_t>(0x6);
        v.flashTexture0 = r.template read<uint16_t>(0x8);
        v.flashTexture1 = r.template read<uint16_t>(0xa);
        return v;
    }
};

// The blast's fields (in the exe its class block reuses the slot's model fields): its ticks, four flashes (life, tilt,
// radius, spread), the colour row; its collision box (the actor's box points at it) and its hit box
struct BlastFields : ActorClassState {
    explicit BlastFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    BoxExtents box;
    int16_t ticks = 0;
    StateArray<int8_t, 4> flashLives{};
    StateArray<int8_t, 4> flashTilts{};
    StateArray<int8_t, 4> flashRadii{};
    StateArray<int8_t, 4> flashSpreads{};
    DataPtr<BlastColourRow> colours;
    HitBoxHead& hitBox;

    // the four flash lives cleared (the exe's two word stores)
    void clearFlashLives() { flashLives = {}; }
};

} // namespace od::port::explosions_state
