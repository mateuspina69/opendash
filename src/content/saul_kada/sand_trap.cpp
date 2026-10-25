// The sand trap (pool 0x20 type 0x61: ST27 / 34 / 40..42, exe 0x4e75d0; hypothesis names), one routine per exe routine.
// States [0x89638c]: 0 bind 0x4e7690, 1 the sub-states [0x896394] 0x4e7720; the table goes on with the sub-states 0..3
// as states 2..5. Sub-states: 0 hidden until the player stands within 0x40 (exe 0x573e20 -> held), 1 rising (16 ticks,
// growing to scale 0x210, sand puffs), 2 waiting (re-grab, or gives up), 3 holding (drains 1 hit point every 4 ticks;
// at 0 hit points its hit entry hurts; 0x20 presses free the player: exe 0x573ed0), 4 letting go (to the animation's
// end), 5 sinking (16 ticks) -> 0.
#include "content/saul_kada/sand_trap.hpp"
#include "content/saul_kada/sand_trap_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_carry.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/skid_sparks.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace sand_trap_state;

namespace {

enum class TrapState : uint8_t { Bind = 0, Run = 1, FirstSub = 2, LastSub = 5 };
// [0x896394]
enum class TrapSub : uint8_t { Hidden = 0, Rise = 1, Wait = 2, Hold = 3, LetGo = 4, Sink = 5 };
enum class HoldStep : uint8_t { Start = 0, Bite = 1, Settle = 2, Drain = 3 };
enum TrapAnim : uint8_t { kAnimHidden = 0, kAnimBite = 1, kAnimLetGo = 2 };
constexpr int kGrabRange = 0x40;
constexpr uint16_t kRiseTicks = 0x10, kEscapePresses = 0x20;
constexpr int16_t kFullScale = 0x210;
constexpr uint16_t kGrabSound = 0x203, kBiteSound = 0x204, kSpraySound = 0x205;
constexpr uint8_t kSandPuffType = 0x14;
constexpr uint8_t kSprayVariant = 0x18;
constexpr int8_t kLetGoKeyBase = 0xd; // the let-go animation starts at 0xd - the bite's key
// its hit entry while the player has no hit points left: a body (radius 0x30), solid, 1 damage
constexpr uint32_t kTrapShape = hitGroup(kHitGroupBodies) | hitRadius(0x30);
constexpr uint32_t kTrapAttack = kHitAttackSolid | 1;

void setScale(AnimatedActor& actor, int16_t x, int16_t y, int16_t z) {
    actor.scale.x = x;
    actor.scale.y = y;
    actor.scale.z = z;
}
void enterSub(AnimatedActor& actor, TrapSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
// the rising sand: a puff (stage effect type 0x14) at a random point around the trap
void sandPuff(const AnimatedActor& actor) {
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!slot) return;
    EffectActor& puff = effectActorAt(slot);
    puff.flags = kActorLive;
    puff.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    puff.type = kSandPuffType;
    puff.variant = 0;
    puff.pos.x = int16_t(int16_t(fixedCos(int(rngStep() & 0xfff)) >> 6) + actor.pos.x.whole);
    puff.pos.y = int16_t(actor.pos.y.whole - int16_t(rngStep() & 0x7f));
    puff.pos.z = int16_t(int16_t(fixedSin(int(rngStep() & 0xfff)) >> 6) + actor.pos.z.whole);
}
// a spray (skid sparks variant 0x18) a quarter turn aside, 0x50 above the trap
void spray(const AnimatedActor& actor, int turn) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& sand = effectActorAt(slot);
    sand.flags = kActorLive;
    sand.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    sand.type = typeByte(GlobalEffectType::SkidSparks);
    sand.variant = kSprayVariant;
    const uint32_t angle = (uint16_t(actor.rotation.y) + uint32_t(turn)) & 0xfff;
    sand.pos.x = int16_t(actor.pos.x.whole - int16_t(fixedSin(int(angle)) >> 6));
    sand.pos.y = int16_t(actor.pos.y.whole - 0x50);
    classState<SkidSparkFields>(effectActorAt(slot)).yaw = uint16_t(angle);
    sand.pos.z = int16_t(actor.pos.z.whole - int16_t(fixedCos(int(angle)) >> 6));
}
// exe 0x4e7c68: lets go of the player (the let-go animation from the mirrored key)
void letGo(AnimatedActor& actor) {
    actor.anim = kAnimLetGo;
    actor.animKey.frame = int8_t(uint8_t(kLetGoKeyBase - uint8_t(actor.animKey.frame)));
    enterSub(actor, TrapSub::LetGo);
}
// the player is knocked back / down
bool playerKnocked() {
    const PlayerSub sub = player().sub();
    return sub == PlayerSub::KnockBack || sub == PlayerSub::KnockDown;
}

// exe 0x4e7740
void hidden(AnimatedActor& actor) {
    if (actor.step == 0) {
        actor.anim = kAnimHidden;
        actor.setDrawn(false);
        actor.animKey.frame = 0;
        setScale(actor, 0, 0, 0);
        actor.step = 1;
        return;
    }
    if (playerGrab(actor, kGrabRange, 1)) { // exe 0x573e20
        soundPlayAtPos(kGrabSound, actor.pos); // exe 0x5d5380
        actor.rotation.y = player().rotation.y;
        enterSub(actor, TrapSub::Rise);
    }
}
// exe 0x4e77c0
void rise(AnimatedActor& actor, TrapFields& trap) {
    if (actor.step == 0) {
        actor.flags |= kActorDrawn;
        trap.timer = kRiseTicks;
        ++actor.step;
    }
    --trap.timer;
    if (trap.timer == 0) {
        Player& p = player();
        if (p.sub() == PlayerSub::Held) {
            ++p.step;
            actor.anim = kAnimBite;
            actor.animKey.frame = 0;
            actor.subState = uint8_t(TrapSub::Hold);
        } else {
            actor.subState = uint8_t(TrapSub::Wait);
        }
        actor.restartStep();
    }
    sandPuff(actor);
    const int16_t s = int16_t(uint16_t(kFullScale - (trap.timer << 5)));
    actor.scale.y = s;
    actor.scale.z = s;
}
// exe 0x4e7910
void wait(AnimatedActor& actor) {
    if (actor.step == 0) {
        const PlayerSub sub = player().sub();
        if (sub == PlayerSub::Held) return;
        if (sub != PlayerSub::KnockBack && sub != PlayerSub::KnockDown) {
            actor.step = 1;
            return;
        }
    } else {
        if (playerGrab(actor, kGrabRange, 0)) {
            actor.anim = kAnimBite;
            actor.animKey.frame = 0;
            enterSub(actor, TrapSub::Hold);
            return;
        }
        if (player().airborne != 0) return;
    }
    enterSub(actor, TrapSub::Sink);
}
// exe 0x4e7970
void hold(AnimatedActor& actor, TrapFields& trap) {
    switch (HoldStep(actor.step)) {
    case HoldStep::Start:
        actor.step = uint8_t(HoldStep::Bite);
        actor.scale.x = kFullScale;
        break;
    case HoldStep::Bite:
        if (actor.keyIndex == 7) {
            soundPlayAtPos(kBiteSound, actor.pos);
            ++actor.step;
        }
        break;
    case HoldStep::Settle:
        if (actor.animKey.flags == kKeyHold) {
            trap.escapePresses = kEscapePresses;
            ++actor.step;
        }
        break;
    case HoldStep::Drain: {
        const int angle = int((uint32_t(gameState().frame & 0xf) << 8) & 0xfff);
        actor.scale.x = int16_t((fixedSin(angle) >> 8) + kFullScale);
        actor.scale.y = int16_t((fixedCos(angle) >> 8) + kFullScale);
        actor.scale.z = int16_t((fixedSin(angle) >> 8) + kFullScale);
        Player& p = player();
        if (p.hitPoints > 0) {
            if (!(gameState().frame & 3)) hitPointsChange(p, -1); // exe 0x5e5320
        } else { // the player out of hit points: the trap's hit entry in front of him
            const int yaw = uint16_t(p.rotation.y) & 0xfff;
            const int16_t sine = fixedSin(yaw), cosine = fixedCos(yaw);
            const int16_t pos[3] = {int16_t(actor.pos.x.whole - (sine >> 8)), actor.pos.y.whole,
                                    int16_t(actor.pos.z.whole - (cosine >> 8))};
            trap.hitBox.result = 0;
            trap.hitBox.previousX = int16_t(pos[0] - (sine >> 7));
            trap.hitBox.previousZ = int16_t(pos[2] - (cosine >> 7));
            hitListSubmit(pos, trap.hitBox, hitOwner(actor.slot) | kTrapShape, kTrapAttack); // exe 0x5e4120
        }
        break;
    }
    default: break;
    }
    if (playerKnocked() || playerGrabHold(actor, trap.escapePresses, actor.keyIndex > 5 ? 1 : 0)) { // exe 0x573ed0
        letGo(actor);
        return;
    }
    if (HoldStep(actor.step) != HoldStep::Drain || !(gameState().frame & 8)) return;
    soundPlayAtPos(kSpraySound, actor.pos);
    spray(actor, -0x400);
    spray(actor, 0x400);
}
// exe 0x4e7ca0
void lettingGo(AnimatedActor& actor) {
    if (actor.animKey.flags == kKeyHold) {
        actor.subState = uint8_t(TrapSub::Sink);
        actor.scale.x = 0;
        actor.restartStep();
    }
}
// exe 0x4e7cc0
void sink(AnimatedActor& actor, TrapFields& trap) {
    if (actor.step == 0) {
        trap.timer = kRiseTicks;
        ++actor.step;
    }
    --trap.timer;
    if (trap.timer == 0) enterSub(actor, TrapSub::Hidden);
    const int16_t s = int16_t(uint16_t((trap.timer << 5) + 0x10));
    actor.scale.y = s;
    actor.scale.z = s;
}
void runSub(AnimatedActor& actor, TrapFields& trap, TrapSub sub) {
    switch (sub) {
    case TrapSub::Hidden: hidden(actor); return;
    case TrapSub::Rise: rise(actor, trap); return;
    case TrapSub::Wait: wait(actor); return;
    case TrapSub::Hold: hold(actor, trap); return;
    case TrapSub::LetGo: lettingGo(actor); return;
    case TrapSub::Sink: sink(actor, trap); return;
    default: notPortedAt(kStoryPort, "0x4e75d0 sub-state", uint32_t(sub));
    }
}

// exe 0x4e7690
void bind(AnimatedActor& actor, TrapFields& trap) {
    if (!bindActorModel(actor, actor.spawnByte)) {
        freeActor(actor); // exe 0x5e1490
        return;
    }
    actor.setDrawn(false);
    actor.rotation.x = 0;
    actor.rotation.z = 0;
    actor.velocity.clearWords();
    setScale(actor, 0, 0, 0);
    actor.anim = kAnimHidden;
    actor.prevAnim = kNoAnimShown;
    actor.animKey.frame = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(TableId::SandTrapBox);
    trap.hitBox.previousX = actor.prevPos.x.whole;
    trap.hitBox.result = 0;
    trap.hitBox.previousZ = actor.prevPos.z.whole;
    actor.enterState(uint8_t(TrapState::Run));
}

// exe 0x4e75d0
void sandTrap(AnimatedActor& actor) {
    TrapFields& trap = classState<TrapFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (TrapState(actor.state)) {
    case TrapState::Bind: bind(actor, trap); break;
    case TrapState::Run: runSub(actor, trap, TrapSub(actor.subState)); break; // 0x4e7720
    default:
        if (actor.state >= uint8_t(TrapState::FirstSub) && actor.state <= uint8_t(TrapState::LastSub)) {
            runSub(actor, trap, TrapSub(actor.state - uint8_t(TrapState::FirstSub))); // the sub-states 0..3 directly
            break;
        }
        notPortedAt(kStoryPort, "0x4e75d0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, actor.animKey.frame); // exe 0x5e1c30
    else animStepPose(actor);                                                             // exe 0x5e1e80
}

} // namespace

void sandTrapUpdate(const PlayerEnv&, uint32_t actor) { sandTrap(animatedActorAt(actor)); }

} // namespace od::port
