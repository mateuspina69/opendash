// The dust the enemies raise (see step_dust.hpp).
#include "game/field_alias.hpp"
#include "game/step_dust.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"

namespace od::port {

namespace {

// the ground kinds that raise dust: the stage's dust effect / the hit spark's ground-dust variant
constexpr int32_t kStageDustGround = 0;
constexpr int32_t kSparkDustGrounds[2] = {1, 4};
// the stage's dust effect: its type in the stage's effect table, its parameters, its fields
constexpr uint8_t kStageDustType = 1; // a stage table type (per class: game/global_class_types.hpp)
constexpr uint32_t kStageDustParams = 0xff409000;
// the class's value in the short after the effect's position (EffectActor leaves it to the class views to name)
struct StageDustFields {
    explicit StageDustFields(const EffectActor& actor) : StageDustFields(const_cast<EffectActor&>(actor)) {}
    explicit StageDustFields(EffectActor& actor) : phase(actor.posSpare) {}
    FieldAlias<uint16_t, int16_t> phase; // the dust's random phase
};
constexpr uint32_t kDustPhaseMask = 0x1f, kDustPhaseShift = 7;

constexpr uint8_t kStageDustVariant = 0, kTreadStageDustVariant = 1;
// the hit spark's variant of a vehicle's tread dust (exe 0x46a130's first spark) and its size
constexpr uint8_t kSparkVariantTreadDust = 7;
constexpr uint16_t kTreadSparkSize = 0xa0;

// the global hit spark's `variant` of `size` at the point, the first byte `flags` (exe 0x5e13b0 + the stores)
void sparkVariant(uint8_t flags, uint8_t variant, int16_t x, int16_t y, int16_t z, uint16_t size = kHitSparkSize) {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    const uint8_t loop = e.loopFlags;
    e.flags = flags;
    e.loopFlags = uint8_t(loop | kActorSweepGroup2);
    e.pos.x = x;
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.variant = variant;
    e.pos.y = y;
    e.pos.z = z;
    classState<HitSparkFields>(e).size = size;
}
// the stage's dust effect of `variant` at the point (exe 0x5e13a0 + the stores)
void stageDust(uint8_t variant, int16_t x, int16_t y, int16_t z) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!address) return;
    EffectActor& e = effectActorAt(address);
    const uint8_t loop = e.loopFlags;
    e.pos.x = x;
    e.loopFlags = uint8_t(loop | kActorSweepGroup2);
    e.flags = kActorLive;
    e.type = kStageDustType;
    e.variant = variant;
    e.pos.y = y;
    e.pos.z = z;
    const uint32_t r = rngStep();
    e.setParamsWord(kStageDustParams);
    StageDustFields(e).phase = uint16_t((r & kDustPhaseMask) << kDustPhaseShift);
}

} // namespace

void spawnDustSpark(int16_t x, int16_t y, int16_t z) { sparkVariant(kActorLive, kSparkVariantDust, x, y, z); }

void spawnStepDust(int16_t x, int16_t y, int16_t z, int32_t groundKind) {
    if (groundKind == kStageDustGround) {
        stageDust(kStageDustVariant, x, y, z);
        return;
    }
    if (groundKind == kSparkDustGrounds[0] || groundKind == kSparkDustGrounds[1])
        sparkVariant(kActorLiveDrawn, kSparkVariantGroundDust, x, y, z);
}

void spawnTreadDust(int16_t x, int16_t y, int16_t z) {
    int32_t kind = groundKindAt(x, z);
    if (kind < 0) kind = kSparkDustGrounds[0];
    sparkVariant(kActorLiveDrawn, kSparkVariantTreadDust, x, y, z, kTreadSparkSize);
    if (kind == kStageDustGround) {
        stageDust(kTreadStageDustVariant, x, y, z);
        return;
    }
    if (kind == kSparkDustGrounds[0] || kind == kSparkDustGrounds[1])
        sparkVariant(kActorLiveDrawn, kSparkVariantGroundDust, x, y, z);
}

} // namespace od::port
