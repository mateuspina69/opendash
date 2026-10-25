// The class states of clut_animation.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::clut_animation_state {

// The room-flag spawner's fields (pool 0xe0): the first sequence and the slot of each flag's CLUT animation (0: none)
struct RoomFlagClutFields : ActorClassState {
    explicit RoomFlagClutFields(ControllerActor& actor) : firstSequence(actor.params[0]) {}
    uint8_t& firstSequence;
    StateArray<uint8_t, 0x10> animationSlots{};
};

// The colour tables (0x20 bytes a table): the stage's (exe 0xa74400: the copies of the CLUT rows the uploads make, which
// the host fills) and the live ones the animations write (exe 0xa76400, which the frame uploads)
constexpr uint32_t kStageCluts = 0xa74400, kLiveCluts = 0xa76400, kClutTableSize = 0x20, kClutTablesBytes = 0x2000;
struct ClutTables {
    StateArray<uint32_t, kClutTablesBytes / 4> stage{}; // 0xa74400
    StateArray<uint32_t, kClutTablesBytes / 4> live{};  // 0xa76400
};
inline ClutTables& clutTables() {
    static ClutTables s;
    return s;
}

constexpr uint8_t kClutOwnTable = 0x01;
constexpr uint8_t kClutSizeMask = 0x06, kClutSize64 = 0x02, kClutSize128 = 0x04;

// The effect's fields (+0x18..)
struct ClutAnimationFields : ActorClassState {
    explicit ClutAnimationFields(EffectActor&) {}
    uint32_t sequence = 0; // ClutSequence
    uint32_t key = 0; // ClutKey
    int8_t keyIndex = 0;
    uint8_t ticksLeft = 0;
    uint8_t source = 0; // the table blended from / its kClutOwnTable bit
    uint8_t sourceFlags = 0;
    uint8_t destination = 0;
    uint8_t destinationFlags = 0;
    uint8_t live = 0; // the live table written
    uint8_t size = 0; // the tables' size (& kClutSizeMask)
    void setDestination(uint16_t tableWord) { set_destinationWord_(uint32_t(tableWord)); }
    void setLive(uint16_t tableWord) { set_liveWord_(uint32_t(tableWord)); }
    void sourceFromDestination() { set_sourceWord_(uint32_t(get_destinationWord_())); }

private:
    uint16_t get_sourceWord_() const { return uint16_t(uint32_t(std::make_unsigned_t<uint8_t>(source)) << 0 | uint32_t(std::make_unsigned_t<uint8_t>(sourceFlags)) << 8); }
    void set_sourceWord_(uint32_t v) { source = uint8_t(std::make_unsigned_t<uint8_t>(v >> 0)); sourceFlags = uint8_t(std::make_unsigned_t<uint8_t>(v >> 8)); }
    uint16_t get_destinationWord_() const { return uint16_t(uint32_t(std::make_unsigned_t<uint8_t>(destination)) << 0 | uint32_t(std::make_unsigned_t<uint8_t>(destinationFlags)) << 8); }
    void set_destinationWord_(uint32_t v) { destination = uint8_t(std::make_unsigned_t<uint8_t>(v >> 0)); destinationFlags = uint8_t(std::make_unsigned_t<uint8_t>(v >> 8)); }
    uint16_t get_liveWord_() const { return uint16_t(uint32_t(std::make_unsigned_t<uint8_t>(live)) << 0 | uint32_t(std::make_unsigned_t<uint8_t>(size)) << 8); }
    void set_liveWord_(uint32_t v) { live = uint8_t(std::make_unsigned_t<uint8_t>(v >> 0)); size = uint8_t(std::make_unsigned_t<uint8_t>(v >> 8)); }
};

} // namespace od::port::clut_animation_state
