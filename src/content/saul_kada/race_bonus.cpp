// The race's time-bonus pickup (static ST59 / 5A / 5B type 0x62, exe 0x5bf630; hypothesis names): it bobs (a sine of
// its tick) and spins; the player within 0xb0 (3D, his point raised 0x80) picks it: the race clock frozen 0x3c ticks,
// sound 0x30b, then it shrinks, darkens and rises off his head for 0x10 ticks. States [0x8d0de8]: 0 bind 0x5bf680,
// 1 float 0x5bf710, 2 picked 0x5bf7f0.
#include "content/saul_kada/race_bonus.hpp"
#include "content/saul_kada/race_bonus_state.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

using namespace od::port::race_bonus_state;

namespace od::port {

using namespace race_bonus_state;

namespace {

enum class BonusState : uint8_t { Bind = 0, Float = 1, Picked = 2 };
RaceClock& raceClock() {
    return stageBlock().part<RaceClock>();
}
constexpr int kPickReach = 0xb0;
constexpr uint16_t kFreezeTicks = 0x3c;
constexpr uint16_t kPickSound = 0x30b;
constexpr int16_t kPickedTicks = 0x10;
constexpr uint16_t kPickedTint = 0x2108;

// exe 0x5bf680
void bind(StaticActor& actor, BonusFields& bonus) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.collisionBox.pointAt(TableId::RaceBonusBox);
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.lastMover = nullptr;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.y = 0;
    actor.acceleration.x = 0;
    actor.interaction = 0;
    actor.clearClassBlock();
    bonus.restY = actor.pos.y;
    actor.enterState(uint8_t(BonusState::Float));
}
// exe 0x5bf710
void floating(StaticActor& actor, BonusFields& bonus) {
    actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 0x20u) & 0xfff);
    actor.pos.y = int32_t((uint32_t(int32_t(fixedSin(int(uint8_t(bonus.tick) & 0x7f) << 5))) << 8) +
                          uint32_t(bonus.restY));
    ++bonus.tick;
    const Player& p = player();
    // the exe's stack point: the player's position raised 0x80
    const int32_t head[3] = {p.pos.x, int32_t(uint32_t(p.pos.y) + 0xff800000u), p.pos.z};
    if (uint32_t(distance3(actor.pos, head)) >= kPickReach) return;
    actor.semiTransparency = semiTransparencyBits(1, 1); // exe 0x5e1a70(a, 1, 1)
    actor.tint = kPickedTint;
    actor.pos = p.pos;
    actor.pos.y.whole -= 0x80u;
    raceClock().frozenTicks = kFreezeTicks;
    soundPlayAtPos(kPickSound, actor.pos); // exe 0x5d5380
    bonus.tick = kPickedTicks;
    actor.enterState(uint8_t(BonusState::Picked));
}
// exe 0x5bf7f0
void picked(StaticActor& actor, BonusFields& bonus) {
    const int n = bonus.tick;
    if (n == 0) {
        freeActor(actor);
        return;
    }
    const int16_t s = int16_t(uint16_t(n << 8));
    actor.scale.z = s;
    actor.scale.y = s;
    actor.scale.x = s;
    const int c = int16_t(n >> 1) + 1;
    actor.tint = uint16_t((((c << 5) | c) << 5) | c);
    bonus.rise += 4u;
    --bonus.tick;
    actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 0x80u) & 0xfff);
    actor.pos = player().pos;
    actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + uint16_t(0xff80u - bonus.rise));
}

void raceBonus(StaticActor& actor) {
    BonusFields& bonus = classState<BonusFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BonusState(actor.state)) {
    case BonusState::Bind: bind(actor, bonus); return;
    case BonusState::Float: floating(actor, bonus); return;
    case BonusState::Picked: picked(actor, bonus); return;
    default: notPortedAt(kStoryPort, "0x5bf630 state (table 0x8d0de8)", actor.state);
    }
}

} // namespace

void raceBonusUpdate(const PlayerEnv&, uint32_t actor) { raceBonus(staticActorAt(actor)); }

} // namespace od::port
