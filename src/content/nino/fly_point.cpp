// The flying point (pool 0xe0 type 0x1c, exe 0x426820; the end-of-mission scripts 0x34..0x37, the ST19 machine, Glyde's
// last attack on ST18). Names are hypotheses from the observed behaviour.
#include "content/nino/fly_point.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

enum class FlyPointState : uint8_t { Start = 0, Fly = 1 };
constexpr uint8_t kTrailSparkVariant = 8;
constexpr uint16_t kTrailSparkSize = 0x80, kEndBlastSize = 0xc0, kEndBlastAttack = 4;

// one lane of the step: (target - point) / ticks
int16_t stepOf(int16_t to, int16_t from, int ticks) { return int16_t(uint16_t((to - from) / ticks)); }
// a lane of the point in bone-point units (the stage's shift)
int16_t shifted(int16_t v, uint32_t shift) { return int16_t(uint16_t(uint32_t(uint16_t(v)) << shift)); }

// exe 0x426820
void flyPoint(ControllerActor& actor) {
    auto&& fly = classState<FlyPointFields>(actor);
    if (FlyPointState(actor.state) == FlyPointState::Start) {
        const int n = fly.ticks;
        if (n == 0) notPorted(kStoryPort, "0x426820: zero ticks (the exe divides by zero)");
        fly.to.x = stepOf(fly.to.x, fly.from.x, n);
        fly.to.y = stepOf(fly.to.y, fly.from.y, n);
        fly.to.z = stepOf(fly.to.z, fly.from.z, n);
        actor.state = uint8_t(FlyPointState::Fly);
    }
    if (const uint32_t e = allocActorA0(0)) { // exe 0x5e13b0
        const uint32_t shift = stage().coordinateShift & 0x1fu;
        EffectActor& spark = effectActorAt(e);
        spark.flags = kActorLiveDrawn;
        spark.loopFlags |= kActorSweepGroup2;
        spark.type = typeByte(GlobalEffectType::HitSpark);
        spark.variant = kTrailSparkVariant;
        spark.pos.x = shifted(fly.from.x, shift);
        spark.pos.y = shifted(fly.from.y, shift);
        classState<HitSparkFields>(effectActorAt(e)).size = kTrailSparkSize;
        spark.pos.z = shifted(fly.from.z, shift);
    }
    fly.from.x = int16_t(uint16_t(fly.from.x) + uint16_t(fly.to.x));
    fly.from.y = int16_t(uint16_t(fly.from.y) + uint16_t(fly.to.y));
    fly.from.z = int16_t(uint16_t(fly.from.z) + uint16_t(fly.to.z));
    fly.ticks -= 1u;
    if (fly.ticks != 0) return;
    if (fly.flag != kNoFlyFlag) {
        if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(e);
            auto&& params = BlastParams(actorHeaderAt(e));
            blast.flags = kActorLive;
            blast.loopFlags |= kActorSweepGroup2;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.subtype = 0;
            blast.variant = 0;
            blast.pos.x.whole = fly.from.x;
            blast.pos.y.whole = fly.from.y;
            blast.pos.z.whole = fly.from.z;
            params.size = kEndBlastSize;
            params.attack = kEndBlastAttack;
        }
        setStoryFlag(uint16_t(fly.flag)); // exe 0x510e10
    }
    freeActor(actor); // exe 0x5e15c0
}

} // namespace

void flyPointUpdate(const PlayerEnv&, uint32_t actor) { flyPoint(controllerActorAt(actor)); }

} // namespace od::port
