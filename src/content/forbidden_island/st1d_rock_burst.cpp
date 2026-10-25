// The ST1D rock burst (controller class exe 0x41aa10, see st1d_rock_burst.hpp): four rocks thrown up and eight dust
// sparks around its point, then it frees itself.
#include "content/forbidden_island/st1d_rock_burst.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/random.hpp"

namespace od::port {

namespace {

// The burst's fields (its controller slot): the spawn record's point
struct RockBurstFields {
    explicit RockBurstFields(const ControllerActor& actor) : RockBurstFields(const_cast<ControllerActor&>(actor)) {}
    explicit RockBurstFields(ControllerActor& actor) : x(actor.recordPoint.x), y(actor.recordPoint.y), z(actor.recordPoint.z) {}
    int16_t& x;
    int16_t& y;
    int16_t& z;
};

constexpr uint32_t kRocks = 4, kSparks = 8;
constexpr uint8_t kRockType = 0x20;       // the stage's static type 0x20 (content/forbidden_island/elephant_boss)
constexpr uint8_t kRockFirstVariant = 2;  // variant 2 or 3 at random
constexpr uint16_t kRockScaleBase = 0x1400;
constexpr int16_t kRockGravity = 0xc0;
constexpr uint8_t kRockSparkVariant = 3;  // the hit spark's dust variant
constexpr uint16_t kRockSparkSizeBase = 0x100;

// exe 0x41aa10: the rocks (stage statics, scattered around the point and thrown up) and the sparks, then freed
void rockBurst(ControllerActor& burst) {
    const RockBurstFields point(burst);
    for (uint32_t i = 0; i < kRocks; ++i) {
        const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (slot == 0) continue;
        StaticActor& rock = staticActorAt(slot);
        rock.flags = kActorLive;
        rock.loopFlags |= kActorSweepGroup2;
        rock.type = kRockType;
        uint32_t n = rngStep();
        rock.variant = uint8_t((n & 1u) + kRockFirstVariant);
        rock.pos.x = int32_t(uint32_t(int32_t(point.x) - int32_t((n >> 1) & 0xff) + 0x80) << 16);
        rock.pos.z = int32_t(uint32_t(int32_t(point.z) - int32_t((n >> 9) & 0x7f) - 0x40) << 16);
        rock.velocity.y = int16_t(0xfffffa00u - ((n >> 16) & 0x3ff));
        rock.velocity.z = int16_t(0xfffffda0u - ((n >> 22) & 0x1ff));
        n = rngStep();
        const uint16_t size = uint16_t((n & 0x7ff) + kRockScaleBase);
        rock.scale.z = int16_t(size);
        rock.scale.y = int16_t(size);
        rock.scale.x = int16_t(size);
        const int32_t y = int32_t((n >> 11) & 0xf) - int32_t(int16_t(size) >> 6) + int32_t(point.y) -
                          int32_t(0x40u << i);
        rock.acceleration.y = kRockGravity;
        rock.pos.y = int32_t(uint32_t(y) << 16);
        rock.params[0] = 0;
        rock.rotation.y = int16_t((n >> 15) & 0xfff);
    }
    for (uint32_t i = 0; i < kSparks; ++i) {
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0: the global hit spark (exe 0x55d210)
        if (slot == 0) continue;
        EffectActor& spark = effectActorAt(slot);
        spark.flags = kActorLive;
        spark.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
        spark.type = typeByte(GlobalEffectType::HitSpark);
        spark.variant = kRockSparkVariant;
        const uint32_t n = rngStep();
        spark.pos.x = int16_t(uint32_t(uint16_t(point.x)) - (n & 0xff) + 0x80);
        spark.pos.y = int16_t(uint32_t(uint16_t(point.y)) - ((n >> 8) & 0x7f));
        spark.pos.z = int16_t(uint32_t(uint16_t(point.z)) - ((n >> 15) & 0xff) + 0x80);
        auto&& sparkFields = classState<HitSparkFields>(spark);
        sparkFields.size = uint16_t(((n >> 23) & 0x1f) + kRockSparkSizeBase);
    }
    freeActor(burst); // exe 0x5e15c0
}

} // namespace

void rockBurstUpdate(const PlayerEnv&, uint32_t actor) { rockBurst(controllerActorAt(actor)); }

} // namespace od::port
