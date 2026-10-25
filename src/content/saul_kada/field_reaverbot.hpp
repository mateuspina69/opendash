// The field reaverbot the Saul Kada spawners release (the stages' pool 0x20 type 5; its class lives in the stage
// tables, not in this area): its type and its spawn parameters, as the flyer (content/saul_kada/st28_flyer), the ST41
// generator (content/saul_kada/st41_enemy_generator) and the ST2D barrier (content/yosyonke/ice_ruin_flag_barrier) write
// them.
#pragma once

#include <cstdint>

#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kFieldReaverbotType = 5;
constexpr uint16_t kFieldReaverbotNoFlag = 0xffff;
// Its spawn parameters: its mode, a second byte, the story flag it keeps (kFieldReaverbotNoFlag: none)
struct FieldReaverbotParams {
    explicit FieldReaverbotParams(const AnimatedActor& actor) : FieldReaverbotParams(const_cast<AnimatedActor&>(actor)) {}
    explicit FieldReaverbotParams(AnimatedActor& actor) : mode(actor.params[0]), second(actor.params[1]), flag(actor.params, 2) {}
    uint8_t& mode;
    uint8_t& second;
    BytesField<uint16_t, 4> flag;
};

} // namespace od::port
