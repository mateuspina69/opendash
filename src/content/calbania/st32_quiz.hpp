// ST32's quiz show: script 0x2b (exe 0x525af0, its quiz kept in the stage block) and the host / contestant class
// exe 0x4e1700, content/calbania/st32_quiz.cpp.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/routine_ids.hpp"
#include "engine/screen_prim.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/stage_state.hpp"
#include "game/transition_state.hpp"

namespace od::port {

// ST32's stage block (StageState::block, 0xa6d298): the quiz's state machine and score, the player's place kept while
// the show runs, and the three contestants the room keeps (the room script and the show's end spawn them)
struct QuizShowBlock : StagePart<QuizShowBlock> {
    uint8_t course = 0; // +0x0: 0..3 (story flags 0x681 / 0x682 / 0x683, else the long course)
    uint8_t questionSet = 0; // +0x1: the set of questions (random, never the previous one)
    StateArray<uint8_t, 2> headPad{}; // +0x02..+0x03: set with the first word by the room helper (no reader)
    uint8_t state = 0; // +0x4: QuizState
    uint8_t subState = 0; // +0x5: QuizPhase in the questions state
    uint8_t step = 0; // +0x6
    uint8_t subStep = 0; // +0x7
    uint8_t questions = 0; // +0x8: the course's questions
    uint8_t rightAnswers = 0; // +0x9
    uint8_t question = 0; // +0xa: the question asked (in the question bank)
    uint8_t doneFlags = 0; // +0xb: kQuizDone
    ActorRef<ActorHeader> host; // +0xc: the host's actor (the script actor of its record)
    uint8_t messageBase = 0; // +0x10: the course's first host line
    uint8_t misses = 0; // +0x11
    uint8_t rank = 0; // +0x12
    RoomPlace savedPlace; // +0x14: the player's x / y / z / yaw before the show
    StateArray<ActorRef<ActorHeader>, 3> contestants{}; // +0x1c: the contestants' actors (spawn records tracked)
    // state s at its first sub-state (the exe's dword store at +4)
    void enterState(uint8_t s) {
        state = s;
        subState = 0;
        step = 0;
        subStep = 0;
    }
    // the step n with the sub-step `sub` (the exe's word store at +6)
    void enterStep(uint8_t n, uint8_t sub = 0) {
        step = n;
        subStep = sub;
    }
    // the state machine and the counters +4..+0xb cleared (the exe's two dword stores)
    void clearStateWords() {
        enterState(0);
        questions = 0;
        rightAnswers = 0;
        question = 0;
        doneFlags = 0;
    }
};
constexpr uint8_t kQuizDone = 0x01; // QuizShowBlock::doneFlags: the quiz is over (the script ends)

// ST32 pool 0x20 types 0x58 / 0x5f / 0x82 (records 0x8a4bf0.. / 0x8a4c58..): the quiz host and the contestants
// (registered in content/calbania/register)
void quizCastUpdate(const PlayerEnv& env, uint32_t actor);

// The quiz's 2D packets (their builders in content/calbania/st32_quiz.cpp, through game/menu_draw), collected only
// into a set sink (null = not collected): the answer mark / remaining-question counter (exe 0x526840 / 0x5268f0:
// 0x84 glyphs in OT +0x78, over the windows)
void setQuizDraw(std::vector<ScreenPrim>* draw);
// The quiz of the language: the pack's (the USA quiz of its disc: its question bank redirected by the caller, its
// answers at `packAnswers` in loaded memory, the USA's mark and counter) or, with 0, the data source's own
void setQuizLocale(uint32_t packAnswers);

// script 0x2b, by its handler [0x89b6d0 + id * 4]
bool st32QuizScriptPorted(RoutineId handler);
bool st32QuizScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
