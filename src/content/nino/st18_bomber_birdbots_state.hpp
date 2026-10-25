// The class states of st18_bomber_birdbots.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/birdbot.hpp"
#include "game/field_alias.hpp"

namespace od::port::st18_bomber_birdbots_state {

// The bomber's spawn parameter and class block
struct BomberFields : BirdbotBody {
    explicit BomberFields(const AnimatedActor& actor) : BomberFields(const_cast<AnimatedActor&>(actor)) {}
    explicit BomberFields(AnimatedActor& actor) : BirdbotBody(actor), trackId(actor.params[3]), timer(actor.guardianBits), unused160(actor.shownParts), animKeyStart(actor.hopperBytes[1]), hitKind(actor.hopperBytes[2]), oddTicks(actor.hopperBytes[3]) {}
    FieldAlias<int8_t, uint8_t> trackId; // the encounter id the remains take over (0: none)
    uint8_t& timer;
    uint8_t& unused160; // cleared at its init (no reader)
    uint8_t& animKeyStart;
    uint8_t& hitKind; // the last hit: 1 strong, 2 a push
    uint8_t& oddTicks; // counted down on odd frames (never set)
    HitBox hitBox;
    int16_t groundHeight = 0;
    ActorRef<StaticActor> bomb;

    void clearBlock() override {
        BirdbotBody::clearBlock();
        hitBox = {};
        groundHeight = 0;
        bomb = nullptr;
    }
};

} // namespace od::port::st18_bomber_birdbots_state
