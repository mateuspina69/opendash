// The class states of mission_gunships.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/gunship_flight.hpp"
#include "game/field_alias.hpp"

namespace od::port::mission_gunships_state {

// The mission gunship's own bytes: its mode (& 0xf: the point's source, kLeaveNow: fly off), its count (bombs / the
// reinforcements still to drop), its reinforcement row, its fly-off heading (angle >> 4); the wave's entry slot (the
// airborne byte's word: none = kNoEntrySlot)
// The mission gunship's own bytes: its mode (& 0xf: the point's source, kLeaveNow: fly off), its count (bombs / the
// reinforcements still to drop), its reinforcement row, its fly-off heading (angle >> 4); the wave's entry slot (the
// airborne byte's word: none = kNoEntrySlot)
struct MissionGunshipFields : GunshipFields {
    explicit MissionGunshipFields(const StaticActor& actor) : MissionGunshipFields(const_cast<StaticActor&>(actor)) {}
    explicit MissionGunshipFields(StaticActor& actor)
        : GunshipFields(actor), mode(actor.params[0]), count(actor.params[1]), reinforcementRow(actor.params[2]),
          leaveHeading(actor.params[3]), entrySlot(actor.airborne) {}
    uint8_t& mode;
    FieldAlias<int8_t, uint8_t> count;
    FieldAlias<int8_t, uint8_t> reinforcementRow;
    FieldAlias<int8_t, uint8_t> leaveHeading;
    FieldAlias<int8_t, uint8_t> entrySlot;
};

// the entry slots of the wave over Glyde's base (the count entering and the mask of the slots taken), the wave's ships
// still to shoot down (an exe data byte the wave counts down) and the slots' points (s16 x / z)
struct WaveEntrySlots {
    int8_t entering = 0; // 0xa5a904
    uint8_t taken = 0; // 0xa5a905
    uint8_t shipsLeft = 0; // 0x8ceaac
};
inline WaveEntrySlots& waveEntrySlots() {
    static WaveEntrySlots s;
    return s;
}

} // namespace od::port::mission_gunships_state
