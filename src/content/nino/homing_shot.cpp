// The homing shot of the Nino ruin (pool 0x40 stage type 0x23, exe 0x5cc490), one routine per exe routine; the names
// are hypotheses from the observed behaviour. States [0x8d166c]: 0x5cc4d0 start, 0x5cc540 flight (its step: the rise,
// then the homing), 0x5d0bc0 freed. Its ends: content/nino/ruin_effects (the hit spark 0x5cc7b0, the flash 0x5cc800).
#include "content/nino/homing_shot.hpp"
#include "content/nino/homing_shot_state.hpp"
#include "content/nino/ruin_effects.hpp"

#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace homing_shot_state;

namespace {

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

enum class ShotState : uint8_t { Start = 0, Fly = 1, End = 2 };
enum class FlightPhase : uint8_t { Rise = 0, Home = 1 };
const DataTable<CollisionBoxRecord> shotBox{TableId::HomingShotBox};
constexpr int16_t kChestHeight = 0x80;

// exe 0x5cc4d0: the flight, the box, kept through warps, the hit box's previous place, the spin random
void shotStart(ProjectileActor& shot, ShotFields& fields) {
    shot.enterState(uint8_t(ShotState::Fly));
    shot.collisionBox.pointAt(shotBox.stateAddress());
    shot.runFlags |= kActorWarpKept;
    fields.hit.previousX = shot.pos.x.whole;
    fields.hit.previousZ = shot.pos.z.whole;
    fields.hit.result = 0;
    fields.spin = int16_t((rngStep() & 0x1f) << 7);
}
// exe 0x5cc540
void shotFly(const PlayerEnv& env, ProjectileActor& shot, ShotFields& fields) {
    HomingShotParams params(shot);
    Player& p = player();
    switch (FlightPhase(shot.step)) {
    case FlightPhase::Rise: // up (or down) by its acceleration until it slows to a stop, then it turns back along its z
        if (HomingShotVariant(shot.variant) == HomingShotVariant::Aimed ||
            HomingShotVariant(shot.variant) == HomingShotVariant::Spread)
            shot.rotation.y = int16_t(angleOf(p.pos, shot.pos));
        shot.velocity.y = int16_t(uint16_t(shot.velocity.y) + uint16_t(shot.acceleration.y));
        integrateVelocity(shot, 0, shot.velocity.y, 0); // exe 0x5e3690
        if (std::abs(int(shot.velocity.y)) <= std::abs(int(shot.acceleration.y))) {
            shot.velocity.z = t16(-shot.velocity.z);
            shot.step += 1u;
        }
        break;
    case FlightPhase::Home: { // toward the player's chest (0x80 above its feet) for its homing ticks, then a flash
        turnYawTo(shot.rotation.y, uint32_t(angleOf(p.pos, shot.pos)), 0x10);
        const int16_t chest[3] = {p.pos.x.whole, t16(p.pos.y.whole - kChestHeight), p.pos.z.whole};
        const int16_t at[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
        const int pitch = pitchOf(chest, at);
        const int speed = shot.velocity.z;
        integrateVelocity(shot, 0, (int32_t(fixedSin(pitch)) * speed) >> 12, (int32_t(fixedCos(pitch)) * speed) >> 12);
        if (params.homingTicks > 0) {
            params.homingTicks = int16_t(uint16_t(params.homingTicks) - 1u);
            if (params.homingTicks == 0) {
                ruinShotFlash(shot, 1); // exe 0x5cc800
                shot.enterState(uint8_t(ShotState::End));
                return;
            }
        }
        break;
    }
    default: break;
    }
    if (playerShieldBlocks(shot, 0x20)) {
        shot.enterState(uint8_t(ShotState::End));
        return;
    }
    if (int32_t(actorTestMove(env, shot)) < 0) { // exe 0x40f570
        ruinShotFlash(shot, 0);
        shot.enterState(uint8_t(ShotState::End));
        return;
    }
    const int16_t pos[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    if (fields.hit.result & kHitAttackBump) {
        ruinShotHitSpark(shot); // exe 0x5cc7b0
        shot.enterState(uint8_t(ShotState::End));
        return;
    }
    fields.hit.previousX = shot.prevPos.x.whole;
    fields.hit.previousZ = shot.prevPos.z.whole;
    fields.hit.result = 0;
    hitListSubmit(pos, fields.hit,
                  hitOwner(shot.slot) | hitGroup(kHitGroupHeavyThrown) | kHitContactMotion | hitRadius(0x18),
                  uint32_t(int32_t(params.damage)) | kHitAttackStrong); // exe 0x5e4120
    dc::emit({0u, 0x2e808080u, uint32_t(uint16_t(shot.pos.x.whole)) | (uint32_t(uint16_t(shot.pos.y.whole)) << 16),
              0x7d1060e0u, uint32_t(int32_t(shot.pos.z.whole)), 0x2f60ffu,
              (uint32_t(int32_t(fields.spin)) << 16) | 0x2du, 0x7fe0u, 0u, 0x7fffu},
             shot.at, false);
    fields.spin = int16_t(uint16_t(fields.spin) + 0x100u);
}
// exe 0x5cc490
void homingShot(const PlayerEnv& env, ProjectileActor& shot) {
    ShotFields& fields = classState<ShotFields>(shot);
    shot.savePreviousPose();
    shot.wasAirborne = shot.airborne;
    switch (ShotState(shot.state)) {
    case ShotState::Start: shotStart(shot, fields); return;
    case ShotState::Fly: shotFly(env, shot, fields); return;
    case ShotState::End: freeActor(shot); return; // exe 0x5d0bc0 -> 0x5e1500
    default: notPortedAt(kStoryPort, "0x5cc490 state (table 0x8d166c)", shot.state);
    }
}

} // namespace

void homingShotUpdate(const PlayerEnv& env, uint32_t actor) { homingShot(env, projectileActorAt(actor)); }

} // namespace od::port
