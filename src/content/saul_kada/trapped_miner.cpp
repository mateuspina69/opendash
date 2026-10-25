// The miner trapped in the mine (ST2B sub 1: pool 0x20 type 0 variant 0, exe 0x4452d0; hypothesis names): he idles
// through the animations 3 / 5 / 0 (random pauses [0x88e154]); turns toward the player within the yaw arc 0x200 ..
// 0x600; while the player is outside the arc (and before flag 0x583 / enemy 1 down) he calls for help: within 0x600
// the cry 0x239, then for 0x384 ticks one of [0x88e174] every [0x88e184] ticks, afterwards one of [0x88e17c].
// States: 0 bind 0x445360, 1 run 0x4453e0.
#include "content/saul_kada/trapped_miner.hpp"
#include "content/saul_kada/trapped_miner_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace trapped_miner_state;

namespace {

enum class MinerState : uint8_t { Bind = 0, Run = 1 };
enum class IdleStep : uint8_t { Start = 0, Look = 1, Shrug = 2, Pause = 3 };
enum class CallPhase : uint8_t { Quiet = 0, Calling = 1, Pleading = 2 };
const DataTable<ScalarTable<uint16_t, 0x10>> idlePauses{TableId::TrappedMinerIdlePauses};
const DataTable<ScalarTable<uint16_t, 4>> callCries{TableId::TrappedMinerCallCries};
const DataTable<ScalarTable<uint16_t, 4>> pleadCries{TableId::TrappedMinerPleadCries};
const DataTable<ScalarTable<uint16_t, 0x10>> cryIntervals{TableId::TrappedMinerCryIntervals};
constexpr int kArcStart = 0x200, kArcEnd = 0x600;
constexpr uint32_t kFlagMinerFreed = 0x583;
constexpr uint32_t kGuardEnemyId = 1;
constexpr int kHearingDistance = 0x600;
constexpr uint16_t kFirstCry = 0x239;
constexpr uint16_t kFirstCryTicks = 0x3c, kCallTicks = 0x384;

// one of the cries every few ticks
void cry(const AnimatedActor& actor, MinerFields& miner, const DataTable<ScalarTable<uint16_t, 4>>& cries) {
    --miner.cryTicks;
    if (miner.cryTicks != 0) return;
    soundPlayAtPos(cries[rngStep() & 3], actor.pos); // exe 0x5d5380
    miner.cryTicks = cryIntervals[rngStep() & 0xf];
}
// exe 0x445360
void bind(AnimatedActor& actor) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor); // exe 0x5e1490
        return;
    }
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.velocity.clearWords();
    actor.collisionBox.pointAt(TableId::TrappedMinerBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock();
    actor.enterState(uint8_t(MinerState::Run));
}
// exe 0x4453e0
void run(AnimatedActor& actor, MinerFields& miner) {
    switch (IdleStep(actor.subState)) {
    case IdleStep::Start:
        actor.anim = 3;
        actor.subState = uint8_t(IdleStep::Look);
        break;
    case IdleStep::Look:
        if (actor.animEnded()) {
            actor.anim = 5;
            actor.subState = uint8_t(IdleStep::Shrug);
        }
        break;
    case IdleStep::Shrug:
        if (actor.animEnded()) {
            const uint16_t pause = idlePauses[rngStep() & 0xf];
            miner.pauseTicks = pause;
            if (pause) {
                actor.anim = 0;
                actor.subState = uint8_t(IdleStep::Pause);
            } else {
                actor.subState = uint8_t(IdleStep::Start);
            }
        }
        break;
    case IdleStep::Pause:
        --miner.pauseTicks;
        if (miner.pauseTicks == 0) actor.subState = uint8_t(IdleStep::Start);
        break;
    default: break;
    }
    const int bearing = angleOf(player().pos, actor.pos); // exe 0x5e3bc0
    if (uint32_t(bearing - kArcStart) < uint32_t(kArcEnd - kArcStart)) {
        actor.rotation.y = int16_t(bearing);
        return;
    }
    if (bearing < kArcStart) actor.rotation.y = kArcStart;
    if (bearing > kArcEnd) actor.rotation.y = kArcEnd;
    if (storyFlag(kFlagMinerFreed) || enemyDefeated(kGuardEnemyId)) return;
    switch (CallPhase(miner.callPhase)) {
    case CallPhase::Quiet:
        if (planarDistance(player().pos, actor.pos) >= kHearingDistance) break; // exe 0x5e3b00
        soundPlayAtPos(kFirstCry, actor.pos);
        miner.cryTicks = kFirstCryTicks;
        miner.callPhase = uint8_t(CallPhase::Calling);
        miner.callTicks = kCallTicks;
        break;
    case CallPhase::Calling:
        cry(actor, miner, callCries);
        --miner.callTicks;
        if (miner.callTicks == 0) miner.callPhase = uint8_t(CallPhase::Pleading);
        break;
    case CallPhase::Pleading: cry(actor, miner, pleadCries); break;
    default: break;
    }
}

// exe 0x4452d0
void miner(AnimatedActor& actor) {
    MinerFields& miner = classState<MinerFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (MinerState(actor.state)) {
    case MinerState::Bind: bind(actor); break;
    case MinerState::Run: run(actor, miner); break;
    default: notPortedAt(kStoryPort, "0x4452d0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animateAsked(actor, 0); // exe 0x5e1e80 / 0x5e1c30
}

} // namespace

void minerUpdate(const PlayerEnv&, uint32_t actor) { miner(animatedActorAt(actor)); }

} // namespace od::port
