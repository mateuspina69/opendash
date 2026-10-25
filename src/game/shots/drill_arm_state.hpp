// The class states of drill_arm.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"

namespace od::port::drill_arm_state {

constexpr int16_t kConeFull = 0x10, kConeGrowth = 2;

// ---- the cone (static global type 0x19) -------------------------------------------------------------------------------
// The cone's fields: the drill shot (the spawn's parameter), its strength (its brightness, up to kConeFull) and the
// shot it turns with (the parameter's copy)
struct DrillConeFields : ActorClassState {
    explicit DrillConeFields(StaticActor& actor) : shot(actor.params, 0), strength(actor.rotation.x) {}
    BytesField<uint32_t, 4> shot;
    int16_t& strength;
    uint32_t parent = 0;
};

// ---- the drill bit ----------------------------------------------------------------------------------------------------
// Its block (+0x74): the stats, the sparks' cooldown, the grinding sound's countdown
struct DrillFields : ActorClassState {
    explicit DrillFields(ShotActor& actor) : sparkTicks(actor.rootMatrix.e[3]), soundTicks(actor.rootMatrix.e[4]), soundPad(actor.rootMatrix.e[5]) {}
    int16_t& sparkTicks;
    int16_t& soundTicks;
    FieldAlias<uint16_t, int16_t> soundPad; // cleared at the spawn (no reader)
    ShotStats stats;
};

} // namespace od::port::drill_arm_state
