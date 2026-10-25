// The skid sparks (effect type 3 of the global effect table, exe 0x55dec0): states 0 / 1 a spark thrown along the
// spawner's yaw by its variant's entry (exe data 0x8c3c78: colours, size, life, speeds), falling, its sprite pair
// cycling through a 4 x 2 sheet (draw commands) -- the player's skid (game/player/player_effects); states 2..4 are the
// suction effect's (game/suction_effect) on the slot's own bytes: a jet attached to the player's hand.
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// The variant's entry (exe data 0x8c3c78, 0x14 bytes)
struct SkidSparkEntry : DataRecord {
    static constexpr uint32_t kBytes = 0x14;
    uint32_t colour0 = 0; // +0x0
    uint32_t colour1 = 0; // +0x4
    int16_t size = 0; // +0x8
    uint16_t life = 0; // +0xa
    int16_t speedX = 0; // +0xc
    int16_t speedY = 0; // +0xe
    int16_t speedZ = 0; // +0x10
    int16_t gravity = 0; // +0x12
    template <class R> static SkidSparkEntry load(const R& r) {
        SkidSparkEntry v;
        v.colour0 = r.template read<uint32_t>(0x0);
        v.colour1 = r.template read<uint32_t>(0x4);
        v.size = r.template read<int16_t>(0x8);
        v.life = r.template read<uint16_t>(0xa);
        v.speedX = r.template read<int16_t>(0xc);
        v.speedY = r.template read<int16_t>(0xe);
        v.speedZ = r.template read<int16_t>(0x10);
        v.gravity = r.template read<int16_t>(0x12);
        return v;
    }
};
// The spark's fields (states 0 / 1): the spawner's yaw, the entry and its speeds (copied), its life and its tick
struct SkidSparkFields : ActorClassState {
    explicit SkidSparkFields(EffectActor& actor) : yaw(actor.params, 0), spareWord(actor.params, 2), speedX(actor.trail[2]), speedY(actor.trail[3]), speedZ(actor.trail[4]), gravity(actor.trail[5]) {}
    BytesField<uint16_t, 4> yaw;
    BytesField<uint16_t, 4> spareWord; // the word after the yaw (a spawner clears it; its reader is not traced)
    int16_t& speedX;
    int16_t& speedY;
    int16_t& speedZ;
    int16_t& gravity;
    DataPtr<SkidSparkEntry> entry;
    int16_t life = 0;
    int16_t tick = 0;
};

// exe 0x55dec0 (effect type 3)
void skidSparksUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
