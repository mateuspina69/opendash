// The spray class 0x5c7420 of ST15 / ST16 (pool 0x40 type 0x13; one routine per exe routine). Its sprites go through
// the draw-command buffer (game/draw_commands dc::emit).
//
// Three puffs, one born a tick at the actor (its yaw, speed (0, -0x10, -0x200)); they rise / slow with age, dim after
// 0x40, die after 0x80. A flying puff is a hit-list entry (radius from its age); hit by a solid it is knocked aside
// (a random yaw of +-(0x80..0x8f) * 4, a rise speed from the table 0x8d133c). Each live puff is one semi-transparent
// 0x2c billboard. The actor is freed once the three are gone.
#include "content/nino/st15_spray.hpp"
#include "content/nino/st15_spray_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st15_spray_state;

namespace {

enum class PuffState : uint8_t { Gone = 0, Flying = 1, Knocked = 2 };


// [0x8d1334]
enum class SprayState : uint8_t { Init = 0, Run = 1 };
// the sub-state of Run
enum class SpraySub : uint8_t { Emitting = 0, Fading = 1 };

const DataTable<CollisionBoxRecord> sprayBox{TableId::St15SprayBox};
const DataTable<ScalarTable<int8_t, 4>> knockRiseSigns{TableId::St15SprayKnockRiseSigns}; // the knocked puff's rise speed factor by a random pick
constexpr int16_t kPuffRise = -0x10, kPuffForward = -0x200;
constexpr uint8_t kPuffFirstAge = 0x10, kPuffBrightness = 0xf0;
constexpr uint8_t kSlowAge = 0x60, kDimAge = 0x40, kDeathAge = 0x80;
constexpr uint8_t kDimStep = 0x18;

// exe 0x5c74c0: a puff born (until the three are), then each puff aged, knocked, moved, submitted and drawn
void sprayPuffs(ProjectileActor& actor, SprayFields& spray) {
    if (SpraySub(actor.subState) == SpraySub::Emitting) {
        const uint8_t n = spray.puffCount;
        spray.puffCount = uint8_t(n + 1);
        auto&& puff = spray.puffs[n];
        puff.state = uint8_t(PuffState::Flying);
        puff.pos.x = actor.pos.x.whole;
        puff.pos.y = actor.pos.y.whole;
        puff.pos.z = actor.pos.z.whole;
        puff.yaw = actor.rotation.y;
        puff.age = kPuffFirstAge;
        puff.frame = 0;
        puff.brightness = kPuffBrightness;
        puff.riseSpeed = kPuffRise;
        puff.forwardSpeed = kPuffForward;
        if (spray.puffCount == 3) actor.subState = uint8_t(SpraySub::Fading);
    } else if (SpraySub(actor.subState) == SpraySub::Fading && spray.puffCount == 0) {
        freeActor(actor); // exe 0x5e1500
        return;
    }
    for (uint32_t k = 0; k < 3; ++k) {
        auto&& puff = spray.puffs[k];
        if (PuffState(puff.state) == PuffState::Gone) continue;
        if (puff.age > kSlowAge) {
            puff.riseSpeed += 0x10u;
            puff.forwardSpeed += 4u;
        }
        if (puff.age > kDimAge && puff.brightness > kDimStep - 1) puff.brightness -= kDimStep;
        if (PuffState(puff.state) == PuffState::Flying && (puff.hitBox.result & kHitAttackSolid)) {
            rngStep();
            const uint32_t r = rngStep();
            puff.yaw += uint32_t(int32_t((r & 0xf) + 0x80) * (int32_t(r & 2) - 1)) << 2;
            rngStep();
            const uint32_t q = rngStep();
            puff.state = uint8_t(PuffState::Knocked);
            puff.riseSpeed = int16_t(uint32_t(int32_t(knockRiseSigns[q & 3]) * int32_t((q & 0xf) + 0x30)) << 2);
        }
        moveAlongYaw16(puff.pos, puff.yaw, 0, puff.riseSpeed, puff.forwardSpeed); // exe 0x5e3750
        puff.age += 8u;
        if (puff.age > kDeathAge) {
            puff.state = uint8_t(PuffState::Gone);
            spray.puffCount -= 1u;
            continue;
        }
        puff.hitBox.result = 0;
        puff.hitBox.previousX = actor.pos.x.whole;
        puff.hitBox.previousZ = actor.pos.z.whole;
        if (PuffState(puff.state) == PuffState::Flying) {
            const int16_t v[3] = {puff.pos.x, puff.pos.y, puff.pos.z};
            hitListSubmit(v, puff.hitBox,
                          hitOwner(actor.slot) | uint32_t(puff.age >> 2) | hitGroup(kHitGroupPlayerAttacks),
                          kHitAttackBump | kHitAttackWater);
        }
        const uint32_t f = puff.frame;
        puff.frame = uint8_t(f + 0x20);
        const uint32_t c = puff.brightness;
        const uint32_t g = f + 0x1f;
        dc::emit({0, ((c | 0x2e00u) << 8 | c) << 8 | c,
                  uint32_t(uint16_t(puff.pos.x)) | uint32_t(int32_t(puff.pos.y)) << 16, f | 0x7c924000u,
                  uint32_t(uint16_t(puff.pos.z)), g | 0x2f4000u, puff.age, f | 0x5f00u, 0, g | 0x5f00u},
                 actor.at, false);
    }
}

// exe 0x5c7460: the slot set up (no motion, one hit point, its box, airborne), the puffs cleared, then the first tick
void sprayInit(ProjectileActor& actor, SprayFields& spray) {
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.onPlatform = 0;
    actor.hitPoints = 1;
    actor.maxHitPoints = 1;
    actor.collisionBox.pointAt(sprayBox.stateAddress());
    actor.airborne = 1;
    spray.clear(); // exe 0x40dc40
    actor.enterState(uint8_t(SprayState::Run));
    sprayPuffs(actor, spray);
}

} // namespace

void st15SprayUpdate(const PlayerEnv&, uint32_t address) { // exe 0x5c7420
    ProjectileActor& actor = projectileActorAt(address);
    auto&& spray = classState<SprayFields>(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (SprayState(actor.state)) {
    case SprayState::Init: sprayInit(actor, spray); break;
    case SprayState::Run: sprayPuffs(actor, spray); break;
    default: notPortedAt(kStoryPort, "0x5c7420 state (table 0x8d1334)", actor.state);
    }
}

} // namespace od::port
