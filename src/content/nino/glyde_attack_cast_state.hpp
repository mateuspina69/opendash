// The class states of glyde_attack_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"

namespace od::port::glyde_attack_cast_state {

// ---- 0x498860 (ST17 type 0x2d sub 2: the Birdbots of script 0x39). Model [0x892ed4 + variant], animation [0x892edc +
// variant]; states [0x892ecc]; the cues by the variant [0x892ee4]: 0 / 1 the animation runs, 4 the part pair
// [0x892f00 + variant * 4] blinking every 3rd tick of the step ticks 0xf..0x3c, off at 0x3d, others none. Not
// stepping: the current frame decoded only (frozen).
struct Script39BirdbotFields : ActorClassState {
    explicit Script39BirdbotFields(AnimatedActor& actor) : startKey(actor.hopperBytes[1]), stepping(actor.townBirdHits), pairFlipped(actor.townBirdHitsHigh) {}
    uint8_t& startKey; // the key a new animation starts at (cleared once used)
    uint8_t& stepping; // the animation runs (else the frame is frozen)
    uint8_t& pairFlipped; // the blinking pair's phase
    uint16_t slotPhase = 0; // variant 5: the slot * 8 - 0x100 (not read by the class)

    void clearBlock() override {
        slotPhase = 0;
    }
};

// ---- 0x498bf0 (ST18 type 0x2d sub 3: the Birdbots of script 0x3b). Model [0x892f4c + variant], animation [0x892f54 +
// variant]; states [0x892f44]; the cues by the variant [0x892f58]: 0 hops (a 0xff00 jump, turning by its yaw-step
// list on each landing), 1 / 2 fly up, turn, animation 0xa, then sparks on bone 2 at its frame 9; 3 none.
struct Script3bBirdbotFields : ActorClassState {
    explicit Script3bBirdbotFields(AnimatedActor&) {}
    uint32_t nextYawStep = 0; // the hop's yaw-step list: the next step (exe data, s16 each, 0-ended)
    uint32_t firstYawStep = 0; // ... and its first (the list restarts there at its end)
    uint8_t startKey = 0; // the key a new animation starts at (cleared once used)
    uint8_t ticks = 0; // the fly's countdown

    void clearBlock() override {
        nextYawStep = 0;
        firstYawStep = 0;
        startKey = 0;
        ticks = 0;
    }
};

// ---- 0x4e30f0 (ST17 type 0x5b, script 0x39 cast). Model variant 0, box 0x895e78. States [0x895e84]; variant 0 by the
// sub-state [0x895e8c] (waits for the release, comes down 0x88 ticks (animation 1 -> 2), lands, then the part pair
// 1 / 17 blinks in windows of the step ticks); the other variants: the pair blinks from the step tick 0x19 to 0xfa of
// the step sub 4. Frozen: the frame is not stepped.
struct BlinkerFields : ActorClassState {
    explicit BlinkerFields(AnimatedActor&) {}
    int16_t ticks = 0; // the descent's countdown / the blink's tick count
    uint8_t flipped = 0; // the pair's phase
    uint8_t frozen = 0; // the frame held (decoded, not stepped)
    uint8_t blinking = 0;

    void clearBlock() override {
        ticks = 0;
        flipped = 0;
        frozen = 0;
        blinking = 0;
    }
};

// ---- 0x4e34f0 (ST17 type 0x5c, script 0x39 cast). Model variant 0, box 0x895e98, texture track 0 [0x895eec]
// (sequences 0x895ea4 / 0x895eac / 0x895ecc / 0x895ea8). States [0x895efc]; variant 0 by the sub-state [0x895f04]
// (waits for the release, comes down 0x9f ticks (animation 1 -> 2), lands (animation 3, 5 loops, frozen), then texture
// cells on the step ticks 0x1c2..0x274); variant 1 by the sub-state [0x895f10] (animations 1..5 and texture cells timed
// on the step ticks from the step sub 5). Frozen: the frame is not stepped.
struct DescenderFields : ActorClassState {
    explicit DescenderFields(AnimatedActor&) {}
    KeyframeTracks tracks;
    uint16_t ticks = 0; // the descent's countdown
    uint8_t frozen = 0; // the frame held (decoded, not stepped)

    void clearBlock() override {
        tracks = {};
        ticks = 0;
        frozen = 0;
    }
};

// ---- 0x44acc0 (ST17 type 2, script 0x39 cast). Model variant 0, box 0x88e4d0, texture tracks [0x88e51c] / [0x88e528].
// States [0x88e538]; variant 0 by the sub-state [0x88e540] (animations 4..6 and cells on the step ticks of the step subs
// 1 / 2); the other variants by the sub-state [0x88e54c] (the step sub 5: animations 1..3, then a rise (animation 4)
// moving away until 0x7d0 from the player).
struct RiserFields : ActorClassState {
    explicit RiserFields(AnimatedActor& actor) : riseTicks(actor.rotation.spare) {}
    int16_t& riseTicks; // the rotation's spare lane: the rise's countdown
    KeyframeTracks tracks;

    void clearBlock() override {
        tracks = {};
    }
};

} // namespace od::port::glyde_attack_cast_state
