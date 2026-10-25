// The rising puff of smoke / dust (ST48 pool 0x40 type 0xb, exe 0x5c5650; hypothesis names): it rises (speed from
// -0x200, +0x10 a tick) for its life: two grey billboards (frames of a 4 x 2 sheet, the first three ticks, then
// alternating) that grow and fade, and a hit-list entry of the same size; freed at its end.
#include "content/saul_kada/smoke_puff.hpp"
#include "content/saul_kada/smoke_puff_state.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace smoke_puff_state;

namespace {

enum class PuffState : uint8_t { Start = 0, Rise = 1 };
enum class PuffFrames : uint8_t { Opening = 0, Alternating = 1 };
constexpr uint16_t kPuffSound = 0xc2;
constexpr int16_t kRiseSpeed = int16_t(0xfe00);
constexpr int kStartAhead = 0x400;
// its hit entry's attack: a strong hit (0x4000: its other bit, not traced)
constexpr uint32_t kPuffAttack = kHitAttackStrong | 0x4000u;

// exe 0x5c56a0
void start(ProjectileActor& actor, PuffFields& puff) {
    actor.setDrawn(false);
    actor.runFlags |= kActorWarpKept;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = kRiseSpeed;
    actor.prevPos = actor.pos;
    moveAlongRotation(actor.prevPos, actor.rotation, 0, 0, kStartAhead); // exe 0x5e3810
    actor.collisionBox.pointAt(TableId::SmokePuffBox);
    actor.actorHit = 0;
    soundPlayAtPos(kPuffSound, actor.pos); // exe 0x5d5380
    puff.hitBox.result = 0;
    puff.tick = 0;
    ++actor.state;
}
// exe 0x5c5730: true at the end of its life
bool rise(ProjectileActor& actor, PuffFields& puff) {
    actor.savePreviousPose();
    const int n = puff.tick;
    uint32_t u, v;
    if (PuffFrames(actor.subState) == PuffFrames::Opening) {
        u = uint32_t(n & 3) * 0x30;
        v = uint32_t((n >> 2) & 1) * 0x30;
        if (n > 2) actor.subState = uint8_t(PuffFrames::Alternating);
    } else if (puff.tickLow & 1) {
        u = 0x90;
        v = 0;
    } else {
        u = 0;
        v = 0x30;
    }
    const int life = SmokePuffParams(actor).life;
    if (life == 0) {
        notPorted(kStoryPort, "0x5c5650: life 0 (the exe divides by zero)");
        return false;
    }
    const int grey = (life - n) * 0xc0 / life;
    const uint32_t colour1 = uint32_t(grey) | uint32_t(grey) << 8 | uint32_t(grey) << 16;
    const int dimmer = (grey * 3) >> 2;
    const uint32_t colour2 = uint32_t(dimmer) | uint32_t(dimmer) << 8 | uint32_t(dimmer) << 16;
    const int size = n * 0x180 / life + 0x10;
    if (n >= life) return true;
    actor.velocity.z += 0x10u;
    if (actor.velocity.z > 0) actor.velocity.setXY(0, 0);
    moveAlongRotation(actor.pos, actor.rotation, 0, 0, actor.velocity.z);
    // the two billboards (0x2c records, word 0 unwritten)
    const uint32_t xy = uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(int32_t(actor.pos.y.whole)) << 16;
    const uint32_t z = uint32_t(int32_t(actor.pos.z.whole));
    const uint32_t sizeWord = ((uint32_t(life) << 22) & 0xfff0000u) | uint32_t(size);
    const uint32_t uv1 = (v + 0x2f) << 8 | u, uv3 = (v + 0x2f) << 8 | (u + 0x2f);
    dc::emit({0, colour1 | 0x2e000000u, xy, (v | 0x7d1100u) << 8 | u, z, (v | 0x2e00u) << 8 | (u + 0x2f), sizeWord, uv1,
              0, uv3},
             actor.at, false);
    dc::emit({0, colour2 | 0x2e000000u, xy, (v | 0x7c9100u) << 8 | u, z, (v | 0x4e00u) << 8 | (u + 0x2f), sizeWord, uv1,
              0, uv3},
             actor.at, false);
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    // the entry: group 3, its radius the size / 8
    hitListSubmit(pos, puff.hitBox, hitOwner(actor.slot) | uint32_t(int32_t(size | 0x3000000) >> 3), kPuffAttack);
    puff.hitBox.result = 0;
    puff.hitBox.previousX = actor.prevPos.x.whole;
    puff.hitBox.previousZ = actor.prevPos.z.whole;
    actor.airborne = 1;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    ++puff.tick;
    return false;
}
// exe 0x5c5650
void smokePuff(const PlayerEnv& env, ProjectileActor& actor) {
    PuffFields& puff = classState<PuffFields>(actor);
    bool ended = false;
    switch (PuffState(actor.state)) {
    case PuffState::Start: start(actor, puff); break;
    case PuffState::Rise: ended = rise(actor, puff); break;
    default: notPortedAt(kStoryPort, "0x5c5650 state", actor.state);
    }
    if (ended) {
        freeActor(actor); // exe 0x5e1500
        return;
    }
    if (int32_t(actorTestMove(env, actor)) < 0) actorPushOut(env, actor); // exe 0x40f570 / 0x410960
}

} // namespace

void smokePuffUpdate(const PlayerEnv& env, uint32_t actor) { smokePuff(env, projectileActorAt(actor)); }

} // namespace od::port
