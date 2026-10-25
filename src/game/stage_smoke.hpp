// The smoke / dust static of the stages (their static stage type 3: the Nino stages, the Saul Kada pirate attack's ST29 /
// ST48): its type and the spawn parameters its spawners write.
#pragma once

#include <cstdint>

#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kSmokeType = 3;

// The smoke's spawn parameters: its drift (x / z bytes, or both as one store) and its life
struct SmokeParams {
    explicit SmokeParams(const ActorHeader& actor) : SmokeParams(const_cast<ActorHeader&>(actor)) {}
    explicit SmokeParams(ActorHeader& actor)
        : driftX(actor.params[0]), driftZ(actor.params[1]), drift(actor.params, 0), life(actor.params, 2),
          lifeBytes(actor.params, 2) {}
    uint8_t& driftX;
    uint8_t& driftZ;
    BytesField<uint16_t, 4> drift;
    BytesField<uint16_t, 4> life;
    ArrayFields<uint8_t, 2, uint8_t, 4> lifeBytes; // the life as two byte stores (the spawners that write it so)
};

} // namespace od::port
