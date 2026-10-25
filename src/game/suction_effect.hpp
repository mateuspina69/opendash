// The suction effect (effect type 4 of the global effect table, exe 0x55df40: the vacuum arm's pull) and its states
// shared with the skid sparks' states 2..4 (game/skid_sparks, the player-attached jet): a cone of 8 rings of quads (OT
// polygons) along the player's aim, pulsing with the frame counter, and 16 particles drawn in toward the hand (draw
// commands); it grows while the player's suction bit 0 is set, then fades.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The vacuum shot's state: the suction's 16 particles (the angle step 0..0x3f, an unread random byte, the life, the
// brightness of each)
struct VacuumShotFields : ActorClassState {
    explicit VacuumShotFields(ShotActor&) {}
    StateArray<uint8_t, 0x10> angle{};
    StateArray<uint8_t, 0x10> spare{};
    StateArray<uint8_t, 0x10> life{};
    StateArray<uint8_t, 0x10> brightness{};
};

// One byte of a particle block: a byte member, or a byte of a short member (its low / high half)
class ParticleByte {
public:
    explicit ParticleByte(uint8_t& b) : byte_(&b) {}
    ParticleByte(int16_t& lane, unsigned half) : lane_(&lane), shift_(8 * half) {}
    uint8_t get() const { return byte_ ? *byte_ : uint8_t(uint16_t(*lane_) >> shift_); }
    operator uint8_t() const { return get(); }
    ParticleByte& operator=(uint8_t v) {
        if (byte_) *byte_ = v;
        else *lane_ = int16_t(uint16_t((uint16_t(*lane_) & ~(0xffu << shift_)) | uint32_t(v) << shift_));
        return *this;
    }
    ParticleByte& operator=(const ParticleByte& o) { return *this = o.get(); }

private:
    uint8_t* byte_ = nullptr;
    int16_t* lane_ = nullptr;
    unsigned shift_ = 0;
};

// The 16 particles a suction draws (a block of 4 x 16 bytes, the four rows): the vacuum shot's, or -- for the skid
// sparks' jet, whose block starts in its effect slot's trail shorts and runs past the slot as in the exe -- the bytes of
// that slot's trail and of the next effect slots' header, position and trail
class SuctionParticles {
public:
    explicit SuctionParticles(VacuumShotFields& shot);
    // the jet's block from effect slot `slot`'s trail (an error naming it when the block runs past the effect pool)
    explicit SuctionParticles(EffectActor& slot);
    SuctionParticles(const SuctionParticles&) = delete;
    SuctionParticles& operator=(const SuctionParticles&) = delete;

    struct Row {
        const SuctionParticles* self;
        size_t base;
        ParticleByte operator[](size_t k) const {
            if (k >= 16) stateIndexError(k, 16);
            return self->bytes_[base + k];
        }
    };
    const Row angle{this, 0x00};
    const Row spare{this, 0x10};
    const Row life{this, 0x20};
    const Row brightness{this, 0x30};

private:
    std::vector<ParticleByte> bytes_;
};

// The effect's fields: the spawner's range level, its strength (1..8) and sound timer
struct SuctionFields : ActorClassState {
    explicit SuctionFields(EffectActor& actor) : rangeLevel(actor.params[0]), strength(actor.params[2]), soundTicks(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> rangeLevel;
    FieldAlias<int8_t, uint8_t> strength;
    uint8_t& soundTicks;
    ActorRef<ShotActor> vacuumShot; // type 4: the vacuum shot whose particles it draws
};
// exe 0x55df60(effect, particles): strength 1, the 16 particles seeded, drawn, sound 0xf7, the next state
void suctionStart(EffectActor& effect, SuctionParticles& particles);
// exe 0x55e030: while the player's suction bit 0 is set: sound 0xf8 every 9 ticks, at the hand, the strength up to 8;
// else sound 0xf9 and the next state; drawn
void suctionHold(EffectActor& effect, SuctionParticles& particles);
// exe 0x55e0c0: the strength down by 1 a tick, freed at 0; else at the hand and drawn
void suctionFade(EffectActor& effect, SuctionParticles& particles);
// exe 0x55df40 (effect type 4; states 0 / 1 / 2 above on the block its +0x18 points at)
void suctionUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
