// The light of the Nino stages (their pool-0xa0 stage type 6): a coloured glow its spawners place and recolour -- the
// bars' lights (content/nino/bars_and_gate) and ST19's gate-lock lights (content/nino/ch2_rooms).
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kStageLightType = 6;

// The light's fields (the effect slot's header bytes after the state and its position words): its size, its mode, a
// word its spawners clear, its two colour words
// The light's fields (the effect slot's header bytes after the state and its position words): its size, its mode, a
// word its spawners clear, its two colour words
struct StageLightFields {
    explicit StageLightFields(const EffectActor& actor) : StageLightFields(const_cast<EffectActor&>(actor)) {}
    explicit StageLightFields(EffectActor& actor)
        : size(actor.step), mode(actor.stepCounter), pulse(actor.params, 0), colourA(actor.pos.x, actor.pos.y),
          colourB(actor.pos.z, actor.posSpare) {}
    uint8_t& size;
    uint8_t& mode;
    BytesField<uint32_t, 4> pulse;
    PiecesField<uint32_t> colourA;
    PiecesField<uint32_t> colourB;
};
// A light setting (exe data, 8 bytes): the two colour words, the mode in the second one's third byte
struct StageLightSetting : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint32_t colourA = 0; // +0x0
    uint32_t colourB = 0; // +0x4
    uint8_t mode = 0; // +0x6
    template <class R> static StageLightSetting load(const R& r) {
        StageLightSetting v;
        v.colourA = r.template read<uint32_t>(0x0);
        v.colourB = r.template read<uint32_t>(0x4);
        v.mode = r.template read<uint8_t>(0x6);
        return v;
    }
};

} // namespace od::port
