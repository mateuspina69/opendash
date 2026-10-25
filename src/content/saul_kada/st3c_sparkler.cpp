// The sparkles at the player in the gold statue's room (ST3C pool 0xe0 type 0x28, exe 0x427a70; hypothesis names): while
// the player is below its height and not airborne, his wading depth / drag set (0x18 / 0xc); then, unless he is in his
// state 0 or 0xd, every other tick a sparkle (the global skid sparks, variant 0x1f) at his position extrapolated one
// tick and a half ahead.
#include "content/saul_kada/st3c_sparkler.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

// The controller's bytes: its record point's height
struct SparklerFields {
    explicit SparklerFields(const ControllerActor& actor) : SparklerFields(const_cast<ControllerActor&>(actor)) {}
    explicit SparklerFields(ControllerActor& actor) : height(actor.recordPoint.y) {}
    int16_t& height;
};
constexpr int8_t kWadeDepth = 0x18, kWadeDrag = 0xc;
constexpr uint8_t kSparkleVariant = 0x1f;
constexpr uint8_t kPlayerIdleState = 0, kPlayerDownState = 0xd; // the player's sub-states without sparkles

void sparkler(ControllerActor& actor) { // exe 0x427a70
    Player& p = player();
    if (SparklerFields(actor).height >= p.pos.y.whole) return;
    if (p.airborne != 0) return;
    const uint8_t state = p.subState;
    p.wadeDepth = kWadeDepth;
    p.drag = kWadeDrag;
    if (state == kPlayerIdleState || state == kPlayerDownState) return;
    if (!(rngStep() & 1)) return;
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& spark = effectActorAt(slot);
    spark.runFlags |= kActorWarpKept;
    spark.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    spark.flags = kActorLive;
    spark.type = typeByte(GlobalEffectType::SkidSparks);
    spark.variant = kSparkleVariant;
    const int shift = stage().coordinateShift & 31;
    spark.pos.x = int16_t(uint32_t((int32_t(p.pos.x.whole) * 3 - int32_t(p.prevPos.x.whole) * 2) >> shift));
    spark.pos.y = int16_t(uint32_t(int32_t(p.pos.y.whole) >> shift));
    spark.pos.z = int16_t(uint32_t((int32_t(p.pos.z.whole) * 3 - int32_t(p.prevPos.z.whole) * 2) >> shift));
    classState<SkidSparkFields>(effectActorAt(slot)).yaw = uint16_t(rngStep() & 0xfff);
}

} // namespace

void sparklerUpdate(const PlayerEnv&, uint32_t actor) { sparkler(controllerActorAt(actor)); }

} // namespace od::port
