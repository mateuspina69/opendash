// The class states of glyde_missiles.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::glyde_missiles_state {

struct MissileShipFields : ActorClassState {
    explicit MissileShipFields(StaticActor& actor) : launch(actor.hitBox.result), countdown(actor.hitBox.previousX) {}
    uint32_t& launch; // the current step of the launch script (exe data)
    FieldAlias<uint16_t, int16_t> countdown;
    uint8_t missilesLeft = 0; // in the burst
    uint8_t nextVariant = 0; // the index in the missiles' variant cycle

    void clearBlock() override {
        missilesLeft = 0;
        nextVariant = 0;
    }
};

struct MissileFields : ActorClassState {
    explicit MissileFields(StaticActor& actor) : hit(actor.hitBox), target(actor.gunshipCooldown()), trail(actor.gunshipAttackCooldown()) {}
    HitBoxHead& hit;
    uint8_t& target; // the thrown one's target slot (0: none)
    uint8_t& trail; // its trail's slot (0: none)
    uint8_t hitKind = 0; // kMissileThrown | kMissileHeld (0: the missile's own attack)
    uint16_t homing = 0; // the ticks before it homes / the thrown one's life
    uint16_t life = 0;

    void clearBlock() override {
        hitKind = 0;
        homing = 0;
        life = 0;
    }
};

// The glow's fields (0x4370b0 below; the missile spawns its flare with them)
// The glow's fields (0x4370b0 below; the missile spawns its flare with them): its spawn bytes (the slot of the actor
// followed -- 0: freed --, the bone, the size), the spawn's offset (the position words), then its own bytes (cleared at
// its start): the offset, the actor followed, the angle and the mode
struct GlowFields : ActorClassState {
    explicit GlowFields(EffectActor& actor)
        : slot(actor.params[0]), bone(actor.params[1]), size(actor.params, 2), slot_(actor) {}
    FieldAlias<int8_t, uint8_t> slot;
    FieldAlias<int8_t, uint8_t> bone;
    BytesField<int16_t, 4> size;
    ShortVec4 offset;
    ActorRef<ActorHeader> followed;
    int16_t angle = 0;
    uint8_t mode = 0;
    uint8_t spare = 0; // cleared with them (no reader)

    // the spawn's offset: the position and the short after it
    ShortVec4 start() const { return {slot_.pos.x, slot_.pos.y, slot_.pos.z, slot_.posSpare}; }
    // exe 0x40dc40 over the own bytes
    void clearOwn() {
        offset = {};
        followed = nullptr;
        angle = 0;
        mode = 0;
        spare = 0;
    }

private:
    EffectActor& slot_;
};

} // namespace od::port::glyde_missiles_state
