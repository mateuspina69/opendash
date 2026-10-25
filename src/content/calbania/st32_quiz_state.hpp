// The class states of st32_quiz.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st32_quiz_state {

// ---- the host and the contestants (exe 0x4e1700: ST32 pool 0x20 types 0x58 / 0x5f / 0x82) -----------------------------
// States 0x4e1870 (bind) / 0x4e1900 (run) / 0x468fa0 (free); the run's sub-states 0x4e1950 (stand, face the player,
// "use" -> its talk event (param 0x12, its message); the quiz's cues) / 0x4e1ab0 (talking: the event's animation
// requests, the head on the player). The head angles (s8, added to the decoded frame's part 2 angles, type 0x82: part
// 3), their targets, a look point and mode, the cue's step. Every tick: the box push-out and the ground (unless the
// variant is set), the animation, the hit-list entry, the collision queues.
struct QuizCastFields : ActorClassState {
    explicit QuizCastFields(AnimatedActor& actor) : cue(actor.params[0]), holdPose(actor.params[2]), message(actor.params[3]), castFlags(actor.townBirdHits), lookMode(actor.townBirdHitsHigh), headHold(actor.hopperTimer) {}
    uint8_t& cue; // QuizCue (the show sets it)
    uint8_t& holdPose; // posed on its current frame without stepping
    uint8_t& message; // its talk event's message (kNoMessage: none)
    int8_t headYawTarget = 0;
    uint8_t& castFlags; // kQuizCast*
    uint8_t& lookMode; // LookMode
    uint8_t& headHold; // ticks the head keeps its target after a talk
    HitBoxHead hitBox;
    uint8_t animTaken = 0; // the event's animation request was played
    int8_t headPitch = 0; // the head's angles (/ 16) and their targets
    int8_t headYaw = 0;
    int8_t headPitchTarget = 0;
    int16_t cueStep = 0; // the two-part reactions' / the repeated reaction's count
    int16_t lookX = 0; // a look point (LookMode::Point*)
    int16_t lookZ = 0;
    uint8_t contact = 0; // kQuizContact*: this tick's collisions

    void clearBlock() override {
        headYawTarget = 0;
        hitBox = {};
        animTaken = 0;
        headPitch = 0;
        headYaw = 0;
        headPitchTarget = 0;
        cueStep = 0;
        lookX = 0;
        lookZ = 0;
        contact = 0;
    }
};

// the quiz's globals: the rank flag set last (the records of the three courses: courseRecord)
struct QuizRecords {
    uint16_t rankFlag = 0; // 0xa7b02c
};
inline QuizRecords& quizRecords() {
    static QuizRecords s;
    return s;
}

} // namespace od::port::st32_quiz_state
