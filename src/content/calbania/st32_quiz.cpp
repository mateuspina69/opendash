// ST32's quiz show: script 0x2b (exe 0x525af0; its quiz in the stage block, see st32_quiz.hpp) and the host /
// contestant class exe 0x4e1700. Transcribed from dash2.exe one function per exe function.
#include "content/calbania/st32_quiz.hpp"
#include "content/calbania/st32_quiz_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/distances.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/hit_list.hpp"
#include "game/menu_draw.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/shop.hpp"
#include "game/spawn.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/town_npc.hpp"
#include "game/transition.hpp"

using namespace od::port::st32_quiz_state;

namespace od::port {

using namespace st32_quiz_state;

namespace {

using address_forms::E;

std::vector<ScreenPrim>* gQuizDraw = nullptr; // setQuizDraw

// ---- the scripts' 2D packets (game/menu_draw) ----------------------------------------------------------------------
// exe 0x526840(x, y, right): the quiz's answer mark, a 0x84 packet in the overlay entry: font slot 1 (size 0x30), glyph
// 0xb palette 4 (right) or 0xc palette 2, at (x - 10, y - 7)
void quizMarkDraw(int x, int y, bool right, std::vector<ScreenPrim>* draw) {
    using namespace menu_draw;
    begin(draw);
    const size_t c = otOpen(kOtOverlay);
    otGlyph(c, int16_t(x - 10), int16_t(y - 7), right ? 0xb : 0xc, kNeutral, 1, 0x30);
    otLast(c).palette = right ? 4 : 2;
    end();
}
// exe 0x5268f0(n): the quiz's remaining-question counter while window 3 waits: the digits of n (hundreds, tens, ones;
// the leading zeros skipped, the ones always) as 0x84 packets in the overlay entry at x 0x28 / 0x31 / 0x3a, y 25: font
// slot 0, size 0x18, palette 4, glyph '0' + digit
void quizCounterDraw(int n, std::vector<ScreenPrim>* draw) {
    using namespace menu_draw;
    if (!(windowStatus(3) & kWindowStatusWaiting)) return;
    begin(draw);
    int v = int16_t(n), div = 100;
    bool lead = false;
    for (int x = 0x28; x < 0x43; x += 9) {
        const int d = int16_t(v / div);
        v = int16_t(v) % div;
        div /= 10;
        if (d != 0) lead = true;
        else if (!lead && x != 0x3a) continue;
        const size_t c = otOpen(kOtOverlay);
        otGlyph(c, x, 25, uint16_t(d + 0x30), kNeutral, 0, 0x18);
        otLast(c).palette = 4;
    }
    end();
}

// exe 0x53f090: the current step's routine or, at the list's end, the wrap-up (no skip); the sequence tick
void runSteps(void (*step)(const RoutineRef& routine)) {
    ScriptState& s = scriptState();
    if (s.steps->id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(s.steps->routine)) step(routine);
    } else {
        s.enterState(CutsceneState::WrapUp);
    }
    scriptSequenceTick();
}

// ==== script 0x2b (exe 0x525af0, states 0x8a5600 = 0x525b10 / 0x53f090 / 0x525c50; camera 0x8a4ca8, steps 0x8a4cf8 =
// 0x525e00) and the quiz (the stage block, states 0x8a560c = 0x525e30 / 0x525fc0 / 0x526a00) =========================
// ST32 (the 0x5864c0 room tail: flag 0x680 without 0x700 -> script 0x2b, flag 0x680 cleared): the quiz show. State 0:
// fade 0x30, the quiz's state cleared, the sound fade 0xfff, the player's place kept; an event still open is closed;
// the player scripted at (-0x50, -1, 0xb0) yaw 0x800, the camera, actors 0x6dc swept, the window layout 0. The step
// 0x525e00: the quiz's state; its done bit -> state 2. State 2: vibration off, flags 0x680..0x684 cleared, the sound
// fade, sound 0x2e, fade 0x31; actors swept, windows closed, the player back where it was, the three contestants and
// the plate. The quiz: the course (flag 0x681 -> 0, 0x682 -> 1, 0x683 -> 2, else 3), the question set (random, never
// the previous one), 10 questions (100 for course 3), the host (script actor of 0x8a4bf0 + course * 0x14, class
// 0x4e1700) cued through its cue byte, the rank (records 0xa6d1f5..0xa6d1f7: rank | misses << 4). The answer mark
// 0x526840 and the remaining-question counter 0x5268f0 (font packets of OT entry 0x78) are built here into the sink of
// setQuizDraw.
enum class QuizState : uint8_t {
    SetUp = 0,     // exe 0x525e30
    Questions = 1, // exe 0x525fc0
    Record = 2,    // exe 0x526a00: the record kept, the done bit
};
enum class QuizPhase : uint8_t { Opening = 0, Ask = 1, Right = 2, Cleared = 3, Wrong = 4 };
enum class OpeningStep : uint8_t { Close = 0, Open = 1 };
enum class AskStep : uint8_t { Close = 0, Pick = 1, HostLine = 2, ShowQuestion = 3, WaitAnswer = 4 };
enum class RightStep : uint8_t { Check = 0, Close = 1, HostLine = 2, Cue = 3, Sound = 4, Mark = 5, WaitHost = 6 };
// the course cleared and the wrong answer: the window closed (its sub-step: the close, then the next step picked),
// the host's line, the mark
enum class ClosingStep : uint8_t { Close = 0, Line = 1, LongCourseLine = 2, Mark = 3 };
enum class MarkStep : uint8_t { Sound = 0, Mark = 1, WaitHost = 2 };
enum class LineStep : uint8_t { Rank = 0, Line = 1, Cue = 2 };
enum class LongLineStep : uint8_t { Line = 0, Cue = 1 };
// the wrong answer's line (the tick tables 0x5267f4 / 0x526824): the pick, then the first line or the other one
enum class WrongLineStep : uint8_t { Pick = 0, First = 0xa, FirstCue = 0xb, Other = 0x14, OtherCue = 0x15 };

// the host's cues (its cue byte; the host clears it when the reaction played)
enum class QuizCue : uint8_t { None = 0, Right = 1, RightMilestone = 2, Cleared = 3, Wrong = 4, WrongOther = 5 };

constexpr uint8_t kLongCourse = 3;
constexpr uint8_t kQuestionsShort = 10, kQuestionsLong = 100;
constexpr uint8_t kRankMask = 0xf;
constexpr uint8_t kTopRank = 3, kRanks = 4;
constexpr int8_t kTalkWindow = 0, kHostWindow = 4, kCounterWindow = 3;
constexpr uint32_t kMessageBank = 0x73c000;  // the stage's message bank
constexpr int16_t kOpeningMessage = 8;
constexpr uint32_t kHostRecordSize = 0x14;
constexpr uint32_t kContestantRecordSize = 0x14;
constexpr RoutineId kQuizStep = RoutineId::St32QuizStep;
constexpr uint32_t kFlagCourse0 = 0x681, kFlagCourse1 = 0x682, kFlagCourse2 = 0x683;
constexpr uint32_t kFlagQuizFirst = 0x680, kFlagQuizLast = 0x684;
constexpr uint32_t kFlagTopRankCourse0 = 0x11e, kFlagTopRankCourse1 = 0x11f;
constexpr uint32_t kFlagCourse2Misses = 0x11d; // course 2 played with 10 misses or more
constexpr uint8_t kManyMisses = 10;
constexpr uint8_t kRecordMax = 0xf;
constexpr uint32_t kMusic = 0xf;             // the fade masks of the music
constexpr int16_t kMusicFadeStep = 0xfff;
constexpr uint16_t kQuizJingle = 0x2f, kEndJingle = 0x2e, kRightSound = 0x141, kWrongSound = 0x142;
constexpr uint32_t kFadeIn = 0x30, kFadeOut = 0x31;
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr uint32_t kSweepMask = 0x6dc, kSweepKeep = 0x80;

// the quiz's exe tables: the question order of each course and set, the host's line of each question group, the
// answers, the rank flags (four per course), and the last rank flag set
const DataTable<ScalarTable<uint8_t, 8 * kQuestionsShort>> course0Questions{TableId::St32QuizCourse0Questions};
const DataTable<ScalarTable<uint8_t, 8 * kQuestionsShort>> course1Questions{TableId::St32QuizCourse1Questions};
const DataTable<ScalarTable<uint8_t, 16 * kQuestionsShort>> course2Questions{TableId::St32QuizCourse2Questions};
const DataTable<ScalarTable<uint8_t, 16 * kQuestionsLong>> course3Questions{TableId::St32QuizCourse3Questions};
const DataTable<ScalarTable<uint8_t, 0x100>> questionLines{TableId::St32QuizQuestionLines}; // [course + question / 5 * 4]: the host's line
const DataTable<ScalarTable<uint8_t, 0x100>> answers{TableId::St32QuizAnswers}; // the right answer (1..) of each question
const DataTable<ScalarTable<uint16_t, 4 * kRanks>> rankFlags{TableId::St32QuizRankFlags};
// the record of course c (rank | misses << 4): the bytes the save keeps after the buster upgrades (game/shop)
uint8_t& courseRecord(size_t c) { return shopUpgrades().unreadBytes[1 + c]; }

// the host's cast fields (the cue byte the show sets; its class names the rest)
uint8_t& hostCue(const QuizShowBlock& show) { return show.host->params[0]; }

bool opened(int8_t win, uint32_t bank, int16_t id) { return uint8_t(windowOpen(win, bank, id)) != 0xff; }
// exe 0x526840(x, y, right) at window 0's position plus its hand's target
void quizMark(bool right) {
    MessageWindow& w = messageWindow(0);
    quizMarkDraw(int16_t(uint16_t(w.x) + uint16_t(w.handTargetX)), int16_t(uint16_t(w.y) + uint16_t(w.handTargetY)),
                 right, gQuizDraw);
}
// exe 0x525e30: the course, the host, the question set, the record; the jingle once the fade and the music are done
void quizSetUp(QuizShowBlock& show) {
    if (show.subState == 0) {
        scriptActorSpawnRecord(stateAddress(TableId::St32QuizStageRecord));
        uint8_t k;
        if (storyFlag(kFlagCourse0)) {
            show.course = 0;
            k = 0;
            show.messageBase = 0xb4;
        } else if (storyFlag(kFlagCourse1)) {
            show.course = 1;
            k = 1;
            show.messageBase = 0xaa;
            scriptActorSpawnRecord(stateAddress(TableId::St32QuizSecondHostRecord));
        } else if (storyFlag(kFlagCourse2)) {
            show.course = 2;
            k = 2;
            show.messageBase = 0xa0;
        } else {
            show.course = kLongCourse;
            k = 2;
            show.messageBase = 0x28;
        }
        pointAtWord(show.host, scriptActorSpawnRecord(stateAddress(TableId::St32QuizHostRecords) + uint32_t(k) * kHostRecordSize));
        uint32_t v;
        do {
            const uint32_t r = rngStep();
            v = (show.course == 0 || show.course == 1) ? (r & 7) : (r & 0xf);
        } while (show.questionSet == v);
        show.questionSet = uint8_t(v);
        show.questions = show.course == kLongCourse ? kQuestionsLong : kQuestionsShort;
        show.rightAnswers = 0;
        QuizRecords& records = quizRecords();
        uint8_t& record = show.course == 0 ? courseRecord(0) : show.course == 1 ? courseRecord(1) : courseRecord(2);
        show.rank = uint8_t(record & kRankMask);
        show.misses = uint8_t(record >> 4);
        ++show.subState;
    }
    if (fadeState().busy == 0 && soundDone(kMusic)) {
        soundPlayFlatForced(kQuizJingle);
        show.enterState(uint8_t(QuizState::Questions));
    }
}
// the host's line of a right answer: course 3 by the score (0x32 +0, 0x50 +1, 0x5a +2, 0x63 +3, else +4: the tick
// table 0x52674c), else the last question +0, any other +1
uint16_t quizScoreMessage(const QuizShowBlock& show) {
    if (show.course == kLongCourse) {
        switch (show.rightAnswers) {
        case 0x32: return uint16_t(show.messageBase);
        case 0x50: return uint16_t(show.messageBase + 1);
        case 0x5a: return uint16_t(show.messageBase + 2);
        case 0x63: return uint16_t(show.messageBase + 3);
        default: return uint16_t(show.messageBase + 4);
        }
    }
    return uint16_t(show.messageBase +
                    (uint32_t(show.rightAnswers) == uint32_t(show.questions) - 1u ? 0 : 1));
}
// the question n of the course's set
uint8_t questionOf(const QuizShowBlock& show, uint8_t n) {
    const uint32_t set = show.questionSet;
    switch (show.course) {
    case 0: return course0Questions[set * kQuestionsShort + n];
    case 1: return course1Questions[set * kQuestionsShort + n];
    case 2: return course2Questions[set * kQuestionsShort + n];
    default: return course3Questions[set * kQuestionsLong + n];
    }
}
// the opening line in the counter's window
void quizOpening(QuizShowBlock& show) {
    if (OpeningStep(show.step) == OpeningStep::Close) {
        windowClose(kCounterWindow, WindowClose::Force);
        ++show.step;
    } else if (OpeningStep(show.step) != OpeningStep::Open) {
        return;
    }
    if (!opened(kCounterWindow, kMessageBank, kOpeningMessage)) return;
    show.step = 0;
    ++show.subState;
}
// a question: the host's line, the question (its bank) in the talk window, the answer
void quizAsk(QuizShowBlock& show) {
    switch (AskStep(show.step)) {
    case AskStep::Close:
        windowClose(kHostWindow, WindowClose::Force);
        windowClose(kTalkWindow, WindowClose::Force);
        ++show.step;
        [[fallthrough]];
    case AskStep::Pick:
        show.question = questionOf(show, show.rightAnswers);
        ++show.step;
        [[fallthrough]];
    case AskStep::HostLine:
        if (!opened(kHostWindow, kMessageBank, questionLines[show.course + uint32_t(show.question / 5) * 4])) return;
        ++show.step;
        [[fallthrough]];
    case AskStep::ShowQuestion:
        if (!opened(kTalkWindow, stateAddress(TableId::St32QuizQuestionBank), show.question)) return;
        ++show.step;
        return;
    case AskStep::WaitAnswer:
        if (!(windowStatus(kTalkWindow) & kWindowStatusWaiting)) return;
        if (answers[show.question] == messageSystem().answerOf(0) + 1) {
            show.subState = uint8_t(QuizPhase::Right);
            show.step = 0;
            ++show.rightAnswers;
        } else {
            show.subState = uint8_t(QuizPhase::Wrong);
            show.step = 0;
            ++show.misses;
        }
        return;
    default: return;
    }
}
// right: the host's line and cue (RightMilestone on the long course's milestones), sound 0x141; all done -> cleared
void quizRight(QuizShowBlock& show) {
    switch (RightStep(show.step)) {
    case RightStep::Check:
        if (show.questions <= show.rightAnswers) {
            show.subState = uint8_t(QuizPhase::Cleared);
            show.enterStep(0);
            return;
        }
        ++show.step;
        [[fallthrough]];
    case RightStep::Close:
        windowClose(kHostWindow, WindowClose::Force);
        ++show.step;
        [[fallthrough]];
    case RightStep::HostLine:
        if (!opened(kHostWindow, kMessageBank, int16_t(quizScoreMessage(show)))) return;
        ++show.step;
        [[fallthrough]];
    case RightStep::Cue: {
        const uint8_t score = show.rightAnswers;
        const bool milestone =
            show.course == kLongCourse && (score == 0x32 || score == 0x50 || score == 0x5a || score == 0x63);
        hostCue(show) = uint8_t(milestone ? QuizCue::RightMilestone : QuizCue::Right); // the table 0x526788
        ++show.step;
        [[fallthrough]];
    }
    case RightStep::Sound:
        soundPlayFlat(kRightSound); // exe 0x5d51f0
        ++show.step;
        [[fallthrough]];
    case RightStep::Mark:
        quizMark(true);
        if (windowStatus(kHostWindow) == 0) ++show.step;
        return;
    case RightStep::WaitHost:
        if (hostCue(show) == uint8_t(QuizCue::None)) {
            show.subState = uint8_t(QuizPhase::Ask);
            show.step = 0;
        }
        return;
    default: return;
    }
}
// the mark of the answer and the wait for the host's reaction (the cleared course and the wrong answer)
template <class Done> void quizMarkThen(QuizShowBlock& show, uint16_t sound, bool right, Done done) {
    switch (MarkStep(show.subStep)) {
    case MarkStep::Sound:
        soundPlayFlat(sound);
        ++show.subStep;
        [[fallthrough]];
    case MarkStep::Mark:
        quizMark(right);
        if (windowStatus(kHostWindow) == 0) ++show.subStep;
        return;
    case MarkStep::WaitHost: done(); return;
    default: return;
    }
}
// the host's window closed (its sub-step), then the next step picked
template <class Pick> void quizClose(QuizShowBlock& show, Pick pick) {
    if (show.subStep == 0) {
        windowClose(kHostWindow, WindowClose::Force);
        ++show.subStep;
    } else if (show.subStep != 1) {
        return;
    }
    pick();
}
// the course cleared: the rank flag (0x8a55f0) and its line, the host's cue Cleared / RightMilestone, sound 0x141
void quizCleared(QuizShowBlock& show) {
    switch (ClosingStep(show.step)) {
    case ClosingStep::Close:
        quizClose(show, [&] {
            if (show.course <= 1) {
                show.enterStep(uint8_t(ClosingStep::Line));
            } else if (show.course == 2) {
                show.enterStep(uint8_t(ClosingStep::Line));
                show.subStep = uint8_t(LineStep::Line);
            } else {
                show.enterStep(uint8_t(ClosingStep::LongCourseLine));
            }
        });
        return;
    case ClosingStep::Line:
        switch (LineStep(show.subStep)) {
        case LineStep::Rank:
            if (show.rank < kRanks) {
                QuizRecords& records = quizRecords();
                records.rankFlag = rankFlags[uint32_t(show.rank) + uint32_t(show.course) * 4];
                setStoryFlag(records.rankFlag);
                if (show.rank == kTopRank) setStoryFlag(show.course == 0 ? kFlagTopRankCourse0 : kFlagTopRankCourse1);
            }
            ++show.subStep;
            [[fallthrough]];
        case LineStep::Line: {
            const uint16_t id = uint16_t(show.messageBase + ((show.course != 2 && show.rank >= kRanks) ? 5 : 4));
            if (!opened(kHostWindow, kMessageBank, int16_t(id))) return;
            ++show.subStep;
            [[fallthrough]];
        }
        case LineStep::Cue:
            hostCue(show) = uint8_t(QuizCue::RightMilestone);
            show.enterStep(uint8_t(ClosingStep::Mark));
            return;
        default: return;
        }
    case ClosingStep::LongCourseLine:
        switch (LongLineStep(show.subStep)) {
        case LongLineStep::Line:
            if (!opened(kHostWindow, kMessageBank, int16_t(show.messageBase + 7))) return;
            ++show.subStep;
            [[fallthrough]];
        case LongLineStep::Cue:
            hostCue(show) = uint8_t(QuizCue::Cleared);
            show.enterStep(uint8_t(ClosingStep::Mark));
            return;
        default: return;
        }
    case ClosingStep::Mark:
        quizMarkThen(show, kRightSound, true, [&] {
            if (hostCue(show) != uint8_t(QuizCue::None)) return;
            ++show.rank;
            if (show.course == 2) {
                show.rank = 0;
                show.misses = 0;
            }
            show.enterState(uint8_t(QuizState::Record));
        });
        return;
    default: return;
    }
}
// wrong: the host's line (by the course / the last question / the score) and cue Wrong / WrongOther, sound 0x142
void quizWrong(QuizShowBlock& show) {
    switch (ClosingStep(show.step)) {
    case ClosingStep::Close:
        quizClose(show, [&] {
            show.enterStep(uint8_t((show.course == kLongCourse ? 1u : 0u) + uint32_t(ClosingStep::Line)));
        });
        return;
    case ClosingStep::Line:
    case ClosingStep::LongCourseLine: {
        const bool longCourse = ClosingStep(show.step) == ClosingStep::LongCourseLine;
        switch (WrongLineStep(show.subStep)) {
        case WrongLineStep::Pick:
            if (!longCourse)
                show.subStep = uint8_t(uint32_t(show.rightAnswers) != uint32_t(show.questions) - 1u
                                           ? WrongLineStep::First
                                           : WrongLineStep::Other);
            else show.subStep = uint8_t(show.rightAnswers < 0x50 ? WrongLineStep::First : WrongLineStep::Other);
            return;
        case WrongLineStep::First:
            if (!opened(kHostWindow, kMessageBank, int16_t(show.messageBase + (longCourse ? 5 : 2)))) return;
            ++show.subStep;
            [[fallthrough]];
        case WrongLineStep::FirstCue:
            hostCue(show) = uint8_t(QuizCue::Wrong);
            show.enterStep(uint8_t(ClosingStep::Mark));
            return;
        case WrongLineStep::Other:
            if (!opened(kHostWindow, kMessageBank, int16_t(show.messageBase + (longCourse ? 6 : 3)))) return;
            ++show.subStep;
            [[fallthrough]];
        case WrongLineStep::OtherCue:
            hostCue(show) = uint8_t(QuizCue::WrongOther);
            show.enterStep(uint8_t(ClosingStep::Mark));
            return;
        default: return;
        }
    }
    case ClosingStep::Mark:
        quizMarkThen(show, kWrongSound, false, [&] {
            if (hostCue(show) == uint8_t(QuizCue::None)) show.enterState(uint8_t(QuizState::Record));
        });
        return;
    default: return;
    }
}
// exe 0x525fc0: the questions' phases, then the remaining-question counter
void quizQuestions(QuizShowBlock& show) {
    switch (QuizPhase(show.subState)) {
    case QuizPhase::Opening: quizOpening(show); break;
    case QuizPhase::Ask: quizAsk(show); break;
    case QuizPhase::Right: quizRight(show); break;
    case QuizPhase::Cleared: quizCleared(show); break;
    case QuizPhase::Wrong: quizWrong(show); break;
    default: break;
    }
    quizCounterDraw(int16_t(show.questions - show.rightAnswers), gQuizDraw); // exe 0x5268f0
}
// exe 0x526a00: the record and the done bit
void quizRecord(QuizShowBlock& show) {
    if (show.rank >= 0x10) show.rank = kRecordMax;
    if (show.misses >= 0x10) show.misses = kRecordMax;
    const uint8_t v = uint8_t((show.rank & kRankMask) | (show.misses << 4));
    QuizRecords& records = quizRecords();
    if (show.course == 0) {
        courseRecord(0) = v;
    } else if (show.course == 1) {
        courseRecord(1) = v;
    } else {
        courseRecord(2) = v;
        if (show.course == 2 && show.misses >= kManyMisses) setStoryFlag(kFlagCourse2Misses);
    }
    windowClose(kCounterWindow, WindowClose::Force);
    show.doneFlags |= kQuizDone;
}
// exe 0x525e00: the quiz's state; the quiz done -> the wrap-up
void quizStep(const RoutineRef& routine) {
    if (!routine.is(kQuizStep)) notPortedAt(kStoryPort, "script 0x2b step", routine.pointerValue());
    QuizShowBlock& show = stageBlock().part<QuizShowBlock>();
    switch (QuizState(show.state)) {
    case QuizState::SetUp: quizSetUp(show); break;
    case QuizState::Questions: quizQuestions(show); break;
    case QuizState::Record: quizRecord(show); break;
    default: notPortedAt(kStoryPort, "the quiz state (table 0x8a560c has 3)", show.state);
    }
    if (show.doneFlags & kQuizDone) scriptState().enterState(CutsceneState::WrapUp);
}

enum class QuizSetUpSub : uint8_t { Fade = 0, CloseEvent = 1, TakeOver = 2 };
enum class QuizWrapUpSub : uint8_t { Stop = 0, Jingle = 1, Fade = 2, Restore = 3, End = 4 };
constexpr int32_t kShowX = int32_t(0xffb00000u), kShowY = int32_t(0xffff0000u), kShowZ = 0xb00000;
constexpr int16_t kShowYaw = 0x800;

// exe 0x525b10: the show set up
void quizShowSetUp(ScriptState& s) {
    QuizShowBlock& show = stageBlock().part<QuizShowBlock>();
    Player& p = player();
    switch (QuizSetUpSub(s.subState)) {
    case QuizSetUpSub::Fade:
        scriptFade(kFadeIn, 0);
        show.clearStateWords();
        soundFadeOut(kMusic, kMusicFadeStep, 0);
        show.savedPlace.x = p.pos.x.whole;
        show.savedPlace.y = p.pos.y.whole;
        show.savedPlace.z = p.pos.z.whole;
        show.savedPlace.yaw = p.rotation.y;
        ++s.subState;
        return;
    case QuizSetUpSub::CloseEvent:
        if (eventState().active != 0) {
            windowCloseAll(WindowClose::Force);
            p.inputMode = 0;
            p.immunity = 0;
            eventState().active = 0;
        }
        scriptReplayPadOn();
        ++s.subState;
        [[fallthrough]];
    case QuizSetUpSub::TakeOver:
        if (playerToScriptIfFree() != 0) return;
        p.flags &= uint8_t(~kActorQueued);
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        p.clearDirectionOffsetWords();
        p.pos.x = kShowX;
        p.pos.y = kShowY;
        p.pos.z = kShowZ;
        p.rotation.y = kShowYaw;
        playerScriptAnimId(0, 0, 1);
        scriptSequenceStart(stateAddress(TableId::St32QuizCameraScript), stateAddress(TableId::St32QuizSteps));
        actorsSweep(kSweepMask, kSweepKeep);
        windowLayoutVariant(WindowLayout::Dialogue); // exe 0x557860
        s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}
// exe 0x525c50: the show's end: the player back where it was, the contestants and the plate again
void quizShowWrapUp(ScriptState& s) {
    switch (QuizWrapUpSub(s.subState)) {
    case QuizWrapUpSub::Stop:
        padVibrate(0, kVibrationStop);
        playerScriptAnimId(0, 0, 1);
        for (uint32_t f = kFlagQuizFirst; f <= kFlagQuizLast; ++f) clearStoryFlag(f);
        soundFadeOut(kMusic, kMusicFadeStep, 0);
        ++s.subState;
        [[fallthrough]];
    case QuizWrapUpSub::Jingle:
        if (!soundDone(kMusic)) return;
        soundPlayFlatForced(kEndJingle);
        ++s.subState;
        return;
    case QuizWrapUpSub::Fade:
        scriptFade(kFadeOut, 0);
        ++s.subState;
        return;
    case QuizWrapUpSub::Restore: {
        QuizShowBlock& show = stageBlock().part<QuizShowBlock>();
        Player& p = player();
        CameraState& camera = cameraState();
        actorsSweep(kSweepMask, kSweepKeep);
        windowCloseAll(WindowClose::Gentle);
        windowLayoutVariant(WindowLayout::Dialogue);
        camera.setMode(CameraMode::Follow);
        p.flags |= kActorQueued;
        p.pos.x.whole = show.savedPlace.x;
        camera.shake = 0;
        camera.unreadDA = 0;
        camera.shakeAmplitude = 0;
        p.pos.y.whole = show.savedPlace.y;
        p.pos.z.whole = show.savedPlace.z;
        p.rotation.y = show.savedPlace.yaw;
        for (uint32_t i = 0; i < 3; ++i)
            pointAtWord(show.contestants[i], spawnRecordTracked(stateAddress(TableId::St32QuizContestantRecords) + i * kContestantRecordSize));
        spawnRecordTracked(stateAddress(TableId::St32QuizPlateRecord));
        ++s.subState;
        return;
    }
    case QuizWrapUpSub::End:
        if (fadeState().busy != 0) return;
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        scriptEndBlock();
        return;
    default: return;
    }
}
// script 0x2b's state
void quizShowScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: quizShowSetUp(s); return;
    case CutsceneState::Steps: runSteps(quizStep); return;
    case CutsceneState::WrapUp: quizShowWrapUp(s); return;
    default: notPortedAt(kStoryPort, "script 0x2b state", s.state);
    }
}

constexpr uint8_t kNoMessage = 0xff;
constexpr uint8_t kQuizCastTalking = 0x01;
constexpr uint8_t kQuizCastStanding = 0x06;   // the stand's flags: kQuizCastPushOut and bit 1 (its reader is not traced)
constexpr uint8_t kQuizCastPushOut = 0x04;    // a push-out solid
constexpr uint8_t kQuizCastMover = 0x08;      // a mover of the actor collision pass
constexpr uint8_t kQuizCastCueRunning = 0x10;
constexpr uint8_t kQuizContactWall = 0x01, kQuizContactBlocked = 0x08, kQuizContactActor = 0x21;
constexpr uint8_t kHeadHoldTicks = 0x3c;
enum class LookMode : uint8_t { None = 0, Straight = 1, Player = 2, Point = 3, PointPitched = 4 };
enum class QuizCastState : uint8_t { Bind = 0, Run = 1, Free = 2 };
enum class QuizCastSub : uint8_t { Stand = 0, Talk = 1 };
enum class StandStep : uint8_t { Enter = 0, Watch = 1, Facing = 2 };
enum class TalkStep : uint8_t { Enter = 0, Talking = 1, Closing = 2 };
constexpr uint8_t kHostType = 0x58, kContestantType = 0x5f, kPlateType = 0x82;
constexpr uint32_t kHeadBone = 2;
constexpr uint32_t kQuizTalkParam = 0x12;
constexpr uint32_t kQuizHitOwner = 0x400010, kQuizHitAttack = 0x800000;
constexpr int16_t kHitHeight = 0x30;
constexpr uint32_t kWallHitMask = 0xf00;
constexpr uint32_t kTestMoveBlocked = 0x20000000;

// exe 0x4e1f80(slot, out): the head point of the actor in `slot` (the player: its bone point 2, class 0x20: bone 2),
// y >> the stage shift with its low 5 bits cleared
void headPoint(uint32_t slot, int16_t out[3]) {
    const uint32_t cls = actorSlotClass(slot); // exe 0x5e0f70
    const int sh = stage().coordinateShift & 0x1f;
    const Actor* act = nullptr;
    if (cls == kPlayerClass) {
        act = &player();
        out[1] = int16_t(player().bonePoints[2].y >> sh);
    } else if (cls == kAnimatedClass) {
        AnimatedActor& a = animatedPool()[slot - kAnimatedClass];
        act = &a;
        out[1] = int16_t(bonePoint(a, kHeadBone, kStoryPort).y >> sh);
    } else {
        notPortedAt(kStoryPort, "exe 0x4e1f80 slot class (the exe reads address 0)", cls);
    }
    out[1] = int16_t(uint16_t(out[1]) & 0xffe0u);
    out[0] = act->pos.x.whole;
    out[2] = act->pos.z.whole;
}
// exe 0x4e2150(actor, v): the head pitch target from v's height against bone 2
void lookPitch(AnimatedActor& actor, QuizCastFields& f, const int16_t v[3]) {
    const int dy = int(bonePoint(actor, kHeadBone, kStoryPort).y) - int(v[1]);
    int p = 0;
    if (dy >= 0x41) p = -0xc0;
    if (dy <= -0x41) p = 0xc0;
    f.headPitchTarget = int8_t(p >> 4);
}
// exe 0x4e1e70(actor): the head targets by the look mode: straight, the player, the look point when within 0x100 of
// the facing (PointPitched also pitches toward it)
void lookTargets(AnimatedActor& actor, QuizCastFields& f) {
    int16_t v[3] = {f.lookX, 0, f.lookZ};
    const int32_t px = int32_t(uint32_t(int32_t(v[0])) << 16), pz = int32_t(uint32_t(int32_t(v[2])) << 16);
    int d;
    switch (LookMode(f.lookMode)) {
    case LookMode::Straight:
        f.headPitchTarget = 0;
        f.headYawTarget = 0;
        return;
    case LookMode::Player:
        headPoint(kPlayerClass, v);
        npcLookYaw(actor, f.headYawTarget, v); // exe 0x4e20d0
        return;
    case LookMode::Point:
        f.headPitchTarget = 0;
        d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(px, pz, actor.pos.x, actor.pos.z)));
        break;
    case LookMode::PointPitched:
        v[1] = actor.pos.y.whole;
        lookPitch(actor, f, v);
        d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(px, pz, actor.pos.x, actor.pos.z)));
        break;
    default: return;
    }
    if (uint32_t(d + 0x100) <= 0x1ff) {
        npcLookYaw(actor, f.headYawTarget, v); // exe 0x4e20d0
        return;
    }
    f.headYawTarget = 0;
}
// exe 0x5e3280(actor, page) and the animation of a cue's reaction, the cue's run ended
void cueAnim(AnimatedActor& actor, QuizCastFields& f, uint32_t page, uint8_t anim) {
    setTexturePageCell(actor.texturePage, page);
    actor.anim = anim;
    f.castFlags &= uint8_t(~kQuizCastCueRunning);
}
// exe 0x4e1bc0(actor): the quiz cue (Right / RightMilestone, Cleared, Wrong / WrongOther): a face page and an animation
// by the type (the host: two-part reactions counted in the cue's step); 1 once the reaction's animation ended
int quizCue(AnimatedActor& actor, QuizCastFields& f) {
    if (actor.stepCounter != 0) {
        if (!(f.castFlags & kQuizCastCueRunning) && actor.animEnded()) return 1;
    } else {
        f.castFlags |= kQuizCastCueRunning;
        ++actor.stepCounter;
    }
    const uint8_t type = actor.type;
    const QuizCue cue = QuizCue(f.cue);
    if (type != kHostType) {
        if (type != kContestantType) {
            switch (cue) {
            case QuizCue::Right: cueAnim(actor, f, 1, 2); return 0;
            case QuizCue::RightMilestone: cueAnim(actor, f, 1, 3); return 0;
            case QuizCue::Cleared: cueAnim(actor, f, 1, 6); return 0;
            case QuizCue::Wrong: cueAnim(actor, f, 2, 4); return 0;
            case QuizCue::WrongOther: cueAnim(actor, f, 2, 5); return 0;
            default: return 0;
            }
        }
        switch (cue) {
        case QuizCue::Right: cueAnim(actor, f, 2, 4); return 0;
        case QuizCue::RightMilestone: cueAnim(actor, f, 2, 5); return 0;
        case QuizCue::Wrong: cueAnim(actor, f, 3, 6); return 0;
        case QuizCue::WrongOther: cueAnim(actor, f, 3, 7); return 0;
        default: return 0;
        }
    }
    const int16_t n = f.cueStep;
    switch (cue) {
    case QuizCue::Right:
    case QuizCue::Wrong: // a reaction then its follow-up animation
        if (n == 0) {
            setTexturePageCell(actor.texturePage, cue == QuizCue::Right ? 2 : 1);
            actor.anim = cue == QuizCue::Right ? 2 : 5;
            ++f.cueStep;
            return 0;
        }
        if (n == 1) {
            if (actor.animEnded()) {
                actor.anim = cue == QuizCue::Right ? 3 : 6;
                ++f.cueStep;
            }
            return 0;
        }
        if (n != 2) return 0;
        break;
    case QuizCue::RightMilestone:
    case QuizCue::WrongOther: // one animation played 6 times
        if (n == 0) {
            setTexturePageCell(actor.texturePage, cue == QuizCue::RightMilestone ? 2 : 1);
            actor.anim = cue == QuizCue::RightMilestone ? 4 : 7;
            ++f.cueStep;
        }
        if (f.cueStep <= 5) {
            if (actor.animEnded()) ++f.cueStep;
            return 0;
        }
        break;
    default: return 0;
    }
    f.castFlags &= uint8_t(~kQuizCastCueRunning);
    return 0;
}
// the stand's check of the player: near and in front -> facing it
void standFaceCheck(AnimatedActor& actor, QuizCastFields& f) { // exe 0x4e1a15
    f.lookMode = uint8_t(LookMode::Straight);
    if (distance3(actor.pos, player().pos) >= 0x100) return;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
    if (uint32_t(d + 0x200) >= 0x400) return;
    actor.enterStep(uint8_t(StandStep::Facing));
}
// exe 0x4e1950: stand / face the player / the quiz cue
void castStand(AnimatedActor& actor, QuizCastFields& f) {
    if (f.cue != uint8_t(QuizCue::None)) {
        if (quizCue(actor, f) == 0) return;
        setTexturePageCell(actor.texturePage, 0);
        f.cue = uint8_t(QuizCue::None);
        actor.anim = 0;
        actor.stepCounter = 0;
        f.cueStep = 0;
        return;
    }
    switch (StandStep(actor.step)) {
    case StandStep::Enter:
        actor.anim = 0;
        f.castFlags = kQuizCastStanding;
        if (LookMode(f.lookMode) == LookMode::Player) {
            actor.enterStep(uint8_t(StandStep::Facing));
            break;
        }
        actor.enterStep(uint8_t(StandStep::Watch));
        standFaceCheck(actor, f);
        break;
    case StandStep::Watch: standFaceCheck(actor, f); break;
    case StandStep::Facing: {
        f.lookMode = uint8_t(LookMode::Player);
        const int dist = distance3(actor.pos, player().pos); // exe 0x5e3a10
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
        if (dist > 0x180 || uint32_t(d + 0x280) > 0x500) {
            f.headHold = 0;
            actor.enterStep(uint8_t(StandStep::Watch));
        }
        break;
    }
    default: break;
    }
    if (f.headHold != 0) f.headHold = kHeadHoldTicks;
    if (!(f.castFlags & kQuizCastTalking) && player().talksTo(actor) && eventState().active == 0) {
        eventStart(&actor.pos, &actor.rotation, kQuizTalkParam, 0, f.message); // exe 0x543af0
        actor.subState = uint8_t(QuizCastSub::Talk);
        actor.enterStep(uint8_t(TalkStep::Enter));
    }
}
// exe 0x4e1ab0: talking
void castTalk(AnimatedActor& actor, QuizCastFields& f) {
    EventState& event = eventState();
    const TalkStep step = TalkStep(actor.step);
    if (step == TalkStep::Enter) {
        f.castFlags |= kQuizCastTalking;
        actor.anim = 0;
        event.speakerAnimation = kNoSpeakerAnimation;
        f.animTaken = 0;
        f.lookMode = uint8_t(LookMode::Player);
        f.headPitchTarget = 0;
        actor.enterStep(uint8_t(TalkStep::Talking));
    } else if (step != TalkStep::Talking) {
        if (step != TalkStep::Closing || !actor.animEnded()) return;
        f.headHold = kHeadHoldTicks;
        f.castFlags &= uint8_t(~kQuizCastTalking);
        actor.subState = uint8_t(QuizCastSub::Stand);
        actor.enterStep(uint8_t(StandStep::Enter));
        return;
    }
    if (f.animTaken != 0) {
        if (actor.animKey.flags == kKeyHold) actor.anim = actor.type == kContestantType ? 2 : 0;
    } else if (event.speakerAnimation != kNoSpeakerAnimation) {
        actor.anim = event.speakerAnimation;
        event.speakerAnimation = kNoSpeakerAnimation;
        f.animTaken = 1;
    }
    int16_t v[3];
    headPoint(kPlayerClass, v);
    npcLookYaw(actor, f.headYawTarget, v); // exe 0x4e20d0
    if (event.active != 0) return;
    actor.anim = actor.type == kContestantType ? 3 : 0;
    actor.enterStep(uint8_t(TalkStep::Closing));
}
// exe 0x4e1700's body
void quizCast(AnimatedActor& actor) {
    QuizCastFields& f = classState<QuizCastFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    if ((actor.actorHit & kWallHitMask) || actor.lastMover) f.contact |= kQuizContactActor;
    switch (QuizCastState(actor.state)) {
    case QuizCastState::Bind:
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);
            return; // (the exe goes on with the freed actor)
        }
        actor.prevAnim = kNoAnimShown;
        actor.anim = 0;
        actor.velocity.clearWords();
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.actorHit = 0;
        actor.lastMover = nullptr;
        actor.collisionBox.pointAt(TableId::St32QuizCastBox);
        if (f.message != kNoMessage) {
            actor.interactionBox.pointAt(TableId::St32QuizCastTalkBox);
            actor.interaction = kInteractTalk;
        } else {
            actor.interactionBox = nullptr;
            actor.interaction = 0;
        }
        f.holdPose = 0;
        setTexturePageCell(actor.texturePage, 0); // exe 0x5e3280(actor, 0)
        actor.clearClassBlock();
        resetBoneScales(actor); // exe 0x5e3220
        actor.enterState(uint8_t(QuizCastState::Run));
        break;
    case QuizCastState::Run:
        switch (QuizCastSub(actor.subState)) {
        case QuizCastSub::Stand: castStand(actor, f); break;
        case QuizCastSub::Talk: castTalk(actor, f); break;
        default: notPortedAt(kStoryPort, "0x4e1700 sub-state (table 0x895aa8 has 2)", actor.subState);
        }
        if (actor.flags & kActorLive) {
            lookTargets(actor, f); // exe 0x4e1e70
            if (f.headHold != 0) --f.headHold;
            npcEaseHead(f.headYaw, f.headYawTarget, 6);     // exe 0x4e2190
            npcEaseHead(f.headPitch, f.headPitchTarget, 2); // exe 0x4e21e0
        }
        break;
    case QuizCastState::Free: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "0x4e1700 state (table 0x895a9c has 3)", actor.state);
    }
    f.contact = 0;
    if (actor.variant == 0) {
        const uint32_t r = actorTestMove(*E, actor); // exe 0x40f570
        if (r & kTestMoveBlocked) f.contact |= kQuizContactBlocked;
        if (int32_t(r) < 0) {
            const uint32_t p = actorPushOut(*E, actor); // exe 0x410960
            actor.wallHit = p;
            if (int32_t(p) < 0) actor.pos = actor.prevPos;
            if (p & kWallHitMask) f.contact |= kQuizContactWall;
        }
        actor.pos.y.whole = int16_t(actorGround(*E, actor)); // exe 0x40f190
    }
    // exe 0x4e2010: the animation and the head angles on the decoded frame
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, 0); // exe 0x5e1c50
    else if (f.holdPose != 0) animDecodeCurrent(actor);              // exe 0x5e1f30
    else animStep(actor);                                            // exe 0x5e1ea0
    LaneVec4 head = decodedFrame().entries[actor.type != kPlateType ? 3 : 4];
    head.x = int16_t(uint32_t(f.headPitch * 0x10) + uint16_t(head.x));
    head.y = int16_t(uint32_t(f.headYaw * 0x10) + uint16_t(head.y));
    animPose(actor); // exe 0x5e2d90
    // the hit-list entry (exe 0x5e4120: the point 0x30 above, the box with the previous x / z)
    f.hitBox.result = 0;
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - uint32_t(kHitHeight)),
                            actor.pos.z.whole};
    hitListSubmit(pos, f.hitBox, uint32_t(actor.slot) << 24 | kQuizHitOwner, kQuizHitAttack);
    const uint8_t fl = f.castFlags;
    if (fl & kQuizCastPushOut) actorCollisionSubmitSolid(actor, SolidType::PushOut, kNoBone, 0); // exe 0x414050
    else actor.lastMover = nullptr;
    if (fl & kQuizCastMover) actorCollisionSubmitMover(actor); // exe 0x4140b0
    else actor.actorHit = 0;
}

} // namespace

bool st32QuizScriptPorted(RoutineId handler) { return handler == RoutineId::QuizShowScript; }

bool st32QuizScriptTick(const PlayerEnv& env, RoutineId handler) {
    E = &env;
    switch (handler) {
    case RoutineId::QuizShowScript: quizShowScript(); return true; // script 0x2b
    default: return false;
    }
}

void setQuizDraw(std::vector<ScreenPrim>* draw) { gQuizDraw = draw; }

void quizCastUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    quizCast(animatedActorAt(actor));
}

} // namespace od::port
