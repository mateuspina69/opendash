// The ST45 enemy spawner (pool 0xe0 type 0xa, exe 0x422260) (see st45_spawner.hpp).
#include "content/late_game/st45_spawner.hpp"
#include "content/late_game/st45_spawner_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/fade_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"

namespace od::port {

using namespace st45_spawner_state;

namespace {

constexpr const char* kEnemySpawnerPort = "enemy spawner port"; // the area of its port-gap errors

enum class SpawnerState : uint8_t { Init = 0, Run = 1 };
// variant 1's steps (the jump table 0x4223a0 by the sub-state)
enum class AftermathStep : uint8_t { WaitBossDown = 0, Pause = 1, ClearArena = 2, FadeBack = 3, Finish = 4 };
constexpr uint8_t kAftermathVariant = 1;
constexpr uint32_t kBossDownFlag = 0x176, kAftermathDoneFlag = 0x174;
constexpr uint32_t kArenaClasses = 0x494; // the classes freed once the screen is covered
constexpr uint8_t kArenaSweepMask = 0x80;
constexpr uint8_t kCoverFade = 0x1a, kUncoverFade = 0xa;


// the countdown run down by one; at 0 the step's action, then the next step
template <class AtZero> void countDown(ControllerActor& actor, SpawnerFields& spawner, AtZero atZero) {
    const uint16_t c = spawner.countdown;
    if (c == 0) {
        atZero();
        ++actor.subState;
    } else {
        spawner.countdown = uint16_t(c - 1u);
    }
}
// the "countdown at 0 -> fade 0xa, next step; else - 1" tail (0x422350 / 0x42236f)
void uncoverTail(ControllerActor& actor, SpawnerFields& spawner) {
    countDown(actor, spawner, [] { scriptFade(kUncoverFade, 0); });
}

// exe 0x4222a0: variant 1, the arena cleared after the boss's death (until flag 0x174): flag 0x176 -> 90 frames ->
// fade 0x1a -> (no fade running) every actor of classes 0x494 freed and the countdown run down twice a frame -> fade
// 0xa -> the player's immunity cleared, flag 0x174
void clearArenaAfterBoss(ControllerActor& actor, SpawnerFields& spawner) {
    if (storyFlag(kAftermathDoneFlag)) return;
    switch (AftermathStep(actor.subState)) {
    case AftermathStep::WaitBossDown:
        if (storyFlag(kBossDownFlag)) {
            spawner.countdown = 0x5a;
            ++actor.subState;
        }
        return;
    case AftermathStep::Pause:
        countDown(actor, spawner, [&] {
            scriptFade(kCoverFade, 0);
            spawner.countdown = 0xa;
        });
        return;
    case AftermathStep::ClearArena:
        if (fadeState().busy != 0) return;
        countDown(actor, spawner, [&] {
            actorsSweep(kArenaClasses, kArenaSweepMask);
            spawner.countdown = 0x14;
        });
        uncoverTail(actor, spawner);
        return;
    case AftermathStep::FadeBack: uncoverTail(actor, spawner); return;
    case AftermathStep::Finish:
        if (fadeState().busy != 0) return;
        player().immunity = 0;
        setStoryFlag(kAftermathDoneFlag);
        ++actor.subState;
        return;
    default: return;
    }
}

// exe 0x422260: states [0x88bab0]: 0 = 0x422cd0 (the scratch cleared, state 1), 1 = 0x422280: variants [0x88bab8] (1 =
// 0x4222a0, 0 / 2..5 = ret)
void st45Spawner(ControllerActor& actor) {
    SpawnerFields& spawner = classState<SpawnerFields>(actor);
    switch (SpawnerState(actor.state)) {
    case SpawnerState::Init:
        spawner.scratch = {};
        actor.enterState(uint8_t(SpawnerState::Run));
        return;
    case SpawnerState::Run: {
        const uint8_t v = actor.variant;
        if (v == 0 || (v >= 2 && v <= 5)) return;
        if (v != kAftermathVariant) notPortedAt(kEnemySpawnerPort, "ST45 spawner variant", v);
        clearArenaAfterBoss(actor, spawner);
        return;
    }
    default: notPortedAt(kEnemySpawnerPort, "ST45 spawner 0x422260 state", actor.state);
    }
}

} // namespace

void spawnerSt45Update(const PlayerEnv&, uint32_t actor) { st45Spawner(controllerActorAt(actor)); }

} // namespace od::port
