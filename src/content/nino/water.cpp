// Water of the Nino ruin (ST35..ST38): the effect classes the player's water code spawns and the underwater bubbles,
// one routine per exe routine.
//   pool 0x60 type 0x48 0x5b3c40  the splash / bubble burst: up to `count` particles (one more each tick) thrown from
//                                 the source along a random direction (exe 0x5e3870), jittered, faded out over their
//                                 last 8 ticks; one camera-facing 0x2c draw command each (0x5b3ce0)
//   pool 0xe0 type 0x20 0x426cc0  the player in water and no script running: 1 tick in 64 a bubble burst at the
//                                 player's head point (0x426d10)
// Names are hypotheses from the observed behaviour.
#include <cstdint>

#include "content/nino/water.hpp"
#include "game/water_splash_state.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/vectors.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

// ---- 0x5b3c40: the splash ------------------------------------------------------------------------------------------

// the variant >> 6: where the particles start
enum class SplashSource : uint8_t {
    Own = 0,   // its own position
    Actor = 1, // the followed actor's position
    Point = 2, // an s16 point (a bone point) with the offset
    Unset = 3, // the exe's point is uninitialised stack
};
// [0x8d083c]
enum class SplashState : uint8_t { Init = 0, Run = 1 };

constexpr uint32_t kSplashColour = 0x808080;
constexpr int16_t kFadeTicks = 8;

// exe 0x5b3ce0: a particle spawned (until `count`), then each live one moved along its rotation, jittered, faded and
// drawn
void splashTick(StaticActor& actor, WaterSplashFields& splash) {
    uint32_t r = rngStep();
    int x = 0, y = 0, z = 0;
    const SplashSource kind = SplashSource(actor.variant >> 6);
    switch (kind) {
    case SplashSource::Own:
        x = int(r & 0x1f) + actor.pos.x.whole - 0x10;
        y = int((r >> 8) & 0x1f) + actor.pos.y.whole - 0x10;
        z = int((r >> 16) & 0x1f) + actor.pos.z.whole - 0x10;
        break;
    case SplashSource::Actor: {
        const Actor& source = *splash.followed;
        x = source.pos.x.whole + int(r & 0x1f) - 0x10;
        y = source.pos.y.whole + int((r >> 8) & 0x1f) - 0x10;
        z = source.pos.z.whole + int((r >> 16) & 0x1f) - 0x10;
        break;
    }
    case SplashSource::Point: { // (the x offset is +0x16, the y offset +0x14: as the exe)
        const ShortVec3& point = *splash.point;
        x = int(r & 0x1f) + splash.offsetX + point.x - 0x10;
        y = int((r >> 8) & 0x1f) + point.y + splash.offsetY - 0x10;
        z = int((r >> 16) & 0x1f) + point.z + splash.offsetZ - 0x10;
        break;
    }
    default: break; // only an error when a particle is spawned
    }
    if (splash.spawned < splash.count) {
        if (kind == SplashSource::Unset)
            notPorted(kStoryPort, "0x5b3ce0 source kind 3 (+6 >> 6 == 3: uninitialised point in the exe)");
        auto&& p = splash.particles[size_t(int32_t(splash.spawned))];
        p.point.x = int16_t(x);
        p.point.y = int16_t(y);
        p.point.z = int16_t(z);
        p.life = int16_t(splash.particleLife);
        p.rotation.x = int16_t(uint16_t((rngStep() & 0x1ff) + uint16_t(actor.rotation.x) - 0x100u));
        p.rotation.y = int16_t(uint16_t((rngStep() & 0x1ff) + uint16_t(actor.rotation.y) - 0x100u));
        p.rotation.z = 0;
        p.size = int16_t((uint32_t(int32_t(splash.sizeMask)) & rngStep()) + 1u);
        splash.spawned += 1u;
        splash.alive += 1u;
        splash.life = uint8_t(splash.particleLife);
    }
    for (int k = 0; k < splash.spawned; ++k) {
        auto&& p = splash.particles[size_t(k)];
        const int16_t life = p.life;
        if (life == 0) {
            p.life = -1;
            splash.alive -= 1u;
            continue;
        }
        if (life < 0) continue;
        int32_t o[3]; // exe 0x5e3870(point, rotation, 0, 0, -speed): the s16 point += (Ry Rx Rz(rot) * v << 12) >> 16
        rotateVector(p.rotation, 0, 0, -int(splash.speed), o);
        p.point.x = int16_t(p.point.x + (o[0] >> 16));
        p.point.y = int16_t(p.point.y + (o[1] >> 16));
        p.point.z = int16_t(p.point.z + (o[2] >> 16));
        r = rngStep();
        p.point.x = int16_t(p.point.x + ((int(r & 0xff) - 0x80) >> 4));
        p.point.y = int16_t(p.point.y + ((int((int32_t(r) >> 8) & 0x3f) - 0x100) >> 4));
        p.point.z = int16_t(p.point.z + ((int((r >> 16) & 0xff) - 0x80) >> 4));
        const int16_t l = p.life;
        uint32_t col = kSplashColour;
        if (l < kFadeTicks) {
            const uint32_t c = uint32_t(int32_t(l)) << 4;
            col = c | (c << 8 | c) << 8;
        }
        const uint32_t q = (uint32_t(l) & 3) << 5;
        // the particle's sprite: a 0x2c draw command (word 0 not written), 32 x 32 cells of the effect page
        dc::emit({0, col | 0x2e000000u, p.point.xyWord(), q | 0x3cb8c000u, uint32_t(uint16_t(p.point.z)),
                  (q + 0x1f) | 0x2ec000u, uint32_t(int32_t(p.size)), q | 0xdf00u, 0u, (q + 0x1f) | 0xdf00u},
                 actor.at, false);
        p.life = int16_t(l - 1);
    }
}

// exe 0x5b3c40
void splash(StaticActor& actor) {
    WaterSplashFields& splash = classState<WaterSplashFields>(actor);
    switch (SplashState(actor.state)) {
    case SplashState::Init: // exe 0x5b3c60: not drawn by the model pass, kept through warps, the particles cleared
        actor.flags &= uint8_t(~kActorDrawn);
        actor.runFlags |= kActorWarpKept;
        for (size_t i = 0; i < splash.particles.size(); ++i) splash.particles[i].life = 0;
        splash.spawned = 0;
        splash.alive = 0;
        splashTick(actor, splash);
        actor.state += 1u;
        return;
    case SplashState::Run: // exe 0x5b3cb0
        if (splash.alive != 0) splashTick(actor, splash);
        else freeActor(actor); // exe 0x5e1540
        return;
    default: notPortedAt(kStoryPort, "0x5b3c40 state", actor.state);
    }
}

// ---- 0x426cc0: the underwater bubbles -------------------------------------------------------------------------------
constexpr uint8_t kSplashAtPoint = uint8_t(SplashSource::Point) << 6;
constexpr size_t kPlayerHeadBone = 1;
constexpr uint8_t kBubbleSizeMask = 7;

// exe 0x426d10: a splash (at a point) on the player's head point, 0x20 behind it, rising
void bubbleBurst(Player& p) {
    const uint32_t a = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (a == 0) return;
    StaticActor& burst = staticActorAt(a);
    WaterSplashFields& splash = classState<WaterSplashFields>(burst);
    burst.flags = kActorLive;
    burst.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    burst.type = kSplashType;
    burst.variant = kSplashAtPoint;
    splash.point = &p.bonePoints[kPlayerHeadBone];
    const uint32_t ang = (uint16_t(p.rotation.y) - 0x800u) & 0xfffu;
    splash.offsetX = int16_t((fixedSin(int(ang)) << 5) >> 12);
    splash.offsetY = -8;
    splash.offsetZ = int16_t((fixedCos(int(ang)) << 5) >> 12);
    burst.rotation.x = 0;
    burst.rotation.z = 0;
    burst.rotation.y = p.rotation.y;
    splash.speed = 0;
    splash.sizeMask = kBubbleSizeMask;
    splash.particleLife = int8_t((rngStep() & 0xf) + 0x10);
    splash.count = int8_t((rngStep() & 3) + 2);
}

// exe 0x426cc0 (the controller itself is not read): not while the player is knocked back / down
void bubbles() {
    Player& p = player();
    const PlayerSub sub = PlayerSub(p.subState);
    if (sub == PlayerSub::KnockBack || sub == PlayerSub::KnockDown) return;
    if (scriptState().flags != 0 || p.inWater == 0) return;
    if ((rngStep() & 0x3f) == 0) bubbleBurst(p);
}

} // namespace

void waterSplashUpdate(const PlayerEnv&, uint32_t actor) { splash(staticActorAt(actor)); }
void waterBubbleControllerUpdate(const PlayerEnv&, uint32_t) { bubbles(); }

} // namespace od::port
