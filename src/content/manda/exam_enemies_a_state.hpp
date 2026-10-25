// The class states of exam_enemies_a.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::exam_enemies_a_state {

namespace hopper {
struct Fields : ActorClassState {
    explicit Fields(AnimatedActor& actor) : mode(actor.hopperBytes[0]), hitsLeft(actor.hopperBytes[1]), thinkTimer(actor.townBirdHits) {}
    uint8_t& mode; // kMode*
    uint8_t& hitsLeft; // the hits before a knock into the air
    uint8_t& thinkTimer;
    HitBox hitBox; // its contact.x: the hit's direction
    uint16_t timer = 0;
    int16_t turn = 0; // the knock's direction / the turn's rate / the look's side
    uint8_t contact = 0; // kContact*
    uint16_t side = 0;
    int16_t throwYaw = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        turn = 0;
        contact = 0;
        side = 0;
        throwYaw = 0;
    }
};
} // namespace hopper

namespace tumbler {
struct Fields : ActorClassState {
    explicit Fields(AnimatedActor& actor) : bouncesLeft(actor.params[0]), contact(actor.shownParts), shell(actor.hopperBytes[0]), alert(actor.hopperBytes[1]), mode(actor.hopperBytes[2]), stun(actor.hopperBytes[3]) {}
    uint8_t& bouncesLeft; // the throw's bounces (spawn parameter 0)
    uint8_t& contact; // ... its bits (kContact*) set a byte at a time
    uint8_t& shell; // kShell*
    uint8_t& alert; // kAlert*
    uint8_t& mode; // kMode*
    uint8_t& stun; // the damage before it flips
    HitBox hitBox;
    uint16_t timer = 0;
    int16_t targetYaw = 0; // the turn's target / the knock's direction
    uint16_t contactWord = 0; // cleared as a word by the collision ...
    int32_t ground = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        targetYaw = 0;
        contactWord = 0;
        ground = 0;
    }
};
} // namespace tumbler

namespace gunner {
struct Fields : ActorClassState {
    explicit Fields(AnimatedActor& actor) : startKey(actor.params[3]), shotTimer(actor.rotation.spare), flightYaw(actor.rotation.spare), sight(actor.hopperBytes[2]), mode(actor.hopperBytes[3]), mayFall(actor.hopperBytes[4]), counter(actor.hopperTimer) {}
    FieldAlias<int8_t, uint8_t> startKey; // the next animation's first key (cleared after each tick)
    FieldAlias<uint16_t, int16_t> shotTimer; // the rotation's spare lane: the shots' alternation timer ...
    int16_t& flightYaw; // ... and the knock's / the throw's direction
    uint8_t& sight; // the line-of-sight probe's result (game/line_of_sight)
    uint8_t& mode; // the low nibble kMode*, the shield's kShield* bits
    uint8_t& mayFall; // 1: losing the ground starts the fall
    uint8_t& counter;
    HitBox hitBox; // its contact.x: the hit's direction
    HitBoxHead weakBox; // the shield's weak point
    union {
        uint16_t timer = 0; // also the hit's direction
        int16_t hitYaw;
    };
    uint8_t contact = 0; // kContact*

    void clearBlock() override {
        hitBox = {};
        weakBox = {};
        timer = 0;
        contact = 0;
    }
};
// The spawn parameters it gives its shots (pool 0x40 stage types 5 / 0x20 / 0x22): the attack, the side it fires from,
// the life, the size (the rotation's spare lane) and its growth (the velocity's spare lane)
struct ShotParams {
    explicit ShotParams(const ProjectileActor& actor) : ShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit ShotParams(ProjectileActor& actor) : attack(actor.params, 0), side(actor.params[2]), life(actor.params[3]), size(actor.rotation.spare), growth(actor.velocity.spare) {}
    BytesField<uint16_t, 4> attack;
    uint8_t& side;
    uint8_t& life;
    int16_t& size;
    int16_t& growth;
};
// The shield's spawn parameter: its flight yaw
struct ShieldParams {
    explicit ShieldParams(const StaticActor& actor) : ShieldParams(const_cast<StaticActor&>(actor)) {}
    explicit ShieldParams(StaticActor& actor) : yaw(actor.params, 0) {}
    BytesField<uint16_t, 4> yaw;
};

} // namespace gunner

// the gunner's shot: its spawn parameters, the hit box it keeps first in its tail, its ticks, brightness, radius,
// texel u (its low byte read alone) and colour phase
namespace gunnerShot {
struct Fields : ActorClassState, gunner::ShotParams {
    explicit Fields(ProjectileActor& actor) : gunner::ShotParams(actor), hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    uint16_t ticks = 0;
    int16_t brightness = 0;
    int16_t radius = 0;
    int16_t texelU = 0;
    PartField<uint8_t, int16_t> texelUByte{texelU, 0};
    uint8_t phase = 0;

    void clearBlock() override {
        ticks = 0;
        brightness = 0;
        radius = 0;
        texelU = 0;
        phase = 0;
    }
};
} // namespace gunnerShot

// the knocked-off shield: its spawn parameter, its flight yaw, bounced flag and fade (over the hit box's bytes)
namespace shield {
struct Fields : ActorClassState, gunner::ShieldParams {
    explicit Fields(StaticActor& actor) : gunner::ShieldParams(actor) {}
    int16_t flightYaw = 0;
    uint8_t bounced = 0;
    uint8_t fade = 0;

    void clearBlock() override {
        flightYaw = 0;
        bounced = 0;
        fade = 0;
    }
};
} // namespace shield

} // namespace od::port::exam_enemies_a_state
