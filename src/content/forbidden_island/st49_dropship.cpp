// The ST49 dropship, its cargo and its cable (see st49_dropship.hpp). States are their exe tables' ([0x896be4] the
// dropship's, [0x896c0c] the cargo's, [0x88d53c] the cable's).
#include "content/forbidden_island/st49_dropship.hpp"
#include "content/forbidden_island/st49_dropship_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/draw_commands.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st49_dropship_state;

namespace {

constexpr const char* kDropshipArea = "island story port"; // the area of the dropship's port-gap errors
constexpr const char* kCargoArea = "st49 dropship cargo port";


// ---- the dropship (exe 0x4f23a0) ---------------------------------------------------------------------------------------
enum class DropshipState : uint8_t {
    Bind = 0,     // exe 0x4f2430: the variant's animation, scale and state
    Parked = 1,   // exe 0x52ef00 (nothing)
    Dropped = 2,  // exe 0x4f2610: falls ever faster (variant 1)
    FarBelow = 3, // exe 0x4f2680: falls steadily (variant 2)
    Landed = 4,   // exe 0x52ef00 (variant 3)
    Sinking = 5,  // exe 0x4f26a0: the cargo's lowering, variant 4
    Rising = 6,   // exe 0x4f28e0: variant 5
};
// the parts the lowering hides (and the one it shows)
constexpr size_t kHatchPart = 1, kCargoParts[3] = {4, 5, 6}, kLoweringShownPart = 7;
constexpr uint8_t kCargoType = 0x6e;  // pool 0x20 type 0x6e (exe 0x4f2ae0)
constexpr uint8_t kCableType = 0x17;  // pool 0xa0 stage type 0x17 (exe 0x439060)
constexpr uint8_t kLoweringVariant = 4; // variants 4 / 5: the cargo's variant is the dropship's - 4
constexpr uint8_t kDustVariant = 0x1a;  // the skid sparks' variant of the dust it raises
constexpr int16_t kDropScale = 0x100, kFarScale = 0x40;
constexpr int16_t kDropAcceleration = 0x10, kDropTopSpeed = 0x400, kFarSpeed = 0x100;
constexpr int16_t kSinkAcceleration = 3;
constexpr int16_t kRiseSpeed = -0x320, kRiseDeceleration = 4, kSettleAnimSpeed = -0x2a0;

void hideCargoParts(AnimatedActor& a) {
    for (size_t p : kCargoParts) a.parts[p].flags |= kPartHidden;
}
void setScale(AnimatedActor& a, int16_t s) {
    a.scale.x = s;
    a.scale.y = s;
    a.scale.z = s;
}
// the variant 4 / 5 case of the bind: the cargo (pool 0x20) and its cable (pool 0xa0), animation 5, the cargo's parts
// hidden; variant 4 sinks, 5 rises at full scale
void startLowering(AnimatedActor& a, int32_t variant) {
    const uint32_t cargoSlot = allocActor20(); // exe 0x5e1160
    AnimatedActor* cargo = cargoSlot ? &animatedActorAt(cargoSlot) : nullptr;
    if (cargo) {
        cargo->flags = kActorLiveDrawn;
        cargo->setTypeWord(kCargoType);
        cargo->setParamsWord(uint32_t(variant - kLoweringVariant));
        cargo->rotation = a.rotation;
        CargoFields& fields = classState<CargoFields>(*cargo);
        fields.dropship = &a;
    }
    if (const uint32_t cableSlot = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
        EffectActor& cable = effectActorAt(cableSlot);
        cable.flags = kActorLive;
        cable.loopFlags |= kActorLateLoop;
        cable.setTypeWord(kCableType);
        cable.setParamsWord(0);
        CableFields& fields = classState<CableFields>(cable);
        fields.dropship = &a;
        fields.cargo = cargo;
    }
    a.anim = 5;
    if (variant == kLoweringVariant) {
        a.enterStateKeepingStep(uint8_t(DropshipState::Sinking));
    } else {
        a.enterStateKeepingStep(uint8_t(DropshipState::Rising));
        setScale(a, kDropScale);
    }
    hideCargoParts(a);
}
// the hatch and the cargo's parts hidden, the lowering's part shown
void openForLowering(AnimatedActor& a) {
    a.parts[kHatchPart].flags |= kPartHidden;
    hideCargoParts(a);
    a.parts[kLoweringShownPart].flags &= uint8_t(~kPartHidden);
}
// state 5 (variant 4; the sub-state its phase): a wait, then it shakes in place (the camera and the pad shake, dust),
// then it sinks
void sinking(AnimatedActor& a, DropshipFields& f) {
    switch (a.subState) {
    case 0:
        openForLowering(a);
        f.counter = 0x1e;
        ++a.subState;
        return;
    case 1:
        --f.counter;
        if (f.counter != 0) return;
        f.counter = 0x3c;
        ++a.subState;
        return;
    case 2: {
        --f.counter;
        if (f.counter == 0) {
            a.rotation.x = 0;
            a.rotation.z = 0;
            a.velocity.y = 0;
            f.counter = 0x1e;
            ++a.subState;
            return;
        }
        const uint32_t r = rngStep() & 3; // the shake: pitch +8 only for r == 3, roll always -8
        a.rotation.x = int16_t(((((r >> 1) & r) & 1) << 4) - 8u);
        a.rotation.z = -8;
        if (f.counter & 3) return;
        cameraShake(1, uint32_t(0x400 - int32_t(int16_t(f.counter)) * 8), 0x80);         // exe 0x40ea80
        padVibrate(0x301, ((0xffffffc0u - uint32_t(f.counter)) & 0xff) | 0x100c00u); // exe 0x55cd10
        const uint32_t slot = allocActorA0(0); // exe 0x5e13b0: dust (the global skid sparks)
        if (!slot) return;
        EffectActor& dust = effectActorAt(slot);
        dust.flags = kActorLive;
        dust.setTypeWord(uint32_t(typeByte(GlobalEffectType::SkidSparks)) | uint32_t(kDustVariant) << 16);
        dust.loopFlags |= kActorLateLoop;
        dust.pos.x = a.pos.x.whole;
        dust.pos.y = int16_t(a.pos.y.whole + 0x80);
        dust.pos.z = a.pos.z.whole;
        const uint32_t r2 = rngStep();
        auto&& spark = classState<SkidSparkFields>(dust);
        spark.spareWord = 0;
        spark.yaw = uint16_t(r2 & 0xfff);
        return;
    }
    case 3:
        if (f.counter != 0) {
            const uint16_t n = uint16_t(f.counter - 1u);
            f.counter = n;
            if (!(n & 3)) {
                cameraShake(1, uint32_t((int32_t(int16_t(n)) + 0x20) << 4), 0x80);
                padVibrate(0x301, ((uint32_t(f.counter) * 2 + 0x40) & 0xff) | 0x100c00u);
            }
        }
        a.velocity.y = int16_t(a.velocity.y - kSinkAcceleration);
        integrateVelocity(a, 0, a.velocity.y, 0); // exe 0x5e3690
        return;
    default: return;
    }
}
// state 6 (variant 5): a wait, then it rises and settles (animation 6 on the way)
void rising(AnimatedActor& a, DropshipFields& f) {
    switch (a.subState) {
    case 0:
        a.velocity.y = 0;
        openForLowering(a);
        f.counter = 0x3c;
        ++a.subState;
        break;
    case 1:
        --f.counter;
        if (f.counter != 0) break;
        a.velocity.y = kRiseSpeed;
        ++a.subState;
        break;
    case 2:
        a.velocity.y = int16_t(a.velocity.y + kRiseDeceleration);
        if (a.velocity.y >= 1) {
            a.velocity.y = 0;
            a.subState = 3;
        } else if (a.velocity.y == kSettleAnimSpeed) {
            a.anim = 6;
        }
        break;
    default: break;
    }
    integrateVelocity(a, 0, a.velocity.y, 0);
}
// state 0 (exe 0x4f2430): the model bound (none: freed), level, still; the variant's animation, scale and state
void bindDropship(AnimatedActor& a) {
    if (!bindActorModel(a, 0)) { // exe 0x5e0ba0
        freeActor(a);
        return;
    }
    a.rotation.x = 0;
    a.rotation.z = 0;
    a.velocity.clearWords();
    const int32_t variant = int8_t(a.params[0]);
    switch (variant) { // jump table 0x4f25ec (movsx, ja: negative or > 5 -> the common tail only)
    case 0:
        a.anim = 2;
        a.enterStateKeepingStep(uint8_t(DropshipState::Parked));
        break;
    case 1:
        a.anim = 3;
        setScale(a, kDropScale);
        a.enterStateKeepingStep(uint8_t(DropshipState::Dropped));
        break;
    case 2:
        a.anim = 4;
        setScale(a, kFarScale);
        a.enterStateKeepingStep(uint8_t(DropshipState::FarBelow));
        break;
    case 3:
        a.anim = 5;
        a.enterStateKeepingStep(uint8_t(DropshipState::Landed));
        break;
    case 4:
    case 5: startLowering(a, variant); break;
    default: break;
    }
    a.prevAnim = kNoAnimShown;
    a.animKey.frame = 0;
}
// state 2 (exe 0x4f2610): 0x23 ticks, then animation 4; falls faster by 0x10 a tick up to 0x400
void dropped(AnimatedActor& a, DropshipFields& f) {
    switch (a.subState) {
    case 0:
        f.counter = 0x23;
        ++a.subState;
        break;
    case 1:
        --f.counter;
        if (f.counter == 0) {
            a.anim = 4;
            a.animKey.frame = 0;
            ++a.subState;
        }
        break;
    default: break;
    }
    a.velocity.y = int16_t(a.velocity.y + kDropAcceleration);
    if (a.velocity.y > kDropTopSpeed) a.velocity.y = kDropTopSpeed;
    integrateVelocity(a, 0, a.velocity.y, 0); // exe 0x5e3690
}
void dropship(AnimatedActor& a) {
    DropshipFields& f = classState<DropshipFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    switch (DropshipState(a.state)) {
    case DropshipState::Bind: bindDropship(a); break;
    case DropshipState::Parked:
    case DropshipState::Landed: break;
    case DropshipState::Dropped: dropped(a, f); break;
    case DropshipState::FarBelow: integrateVelocity(a, 0, kFarSpeed, 0); break;
    case DropshipState::Sinking: sinking(a, f); break;
    case DropshipState::Rising: rising(a, f); break;
    default: notPortedAt(kDropshipArea, "0x4f23a0 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateAsked(a, a.animKey.frame); // exe 0x5e1c30 / 0x5e1e80
}

// ---- the cargo (exe 0x4f2ae0) ------------------------------------------------------------------------------------------
enum class CargoState : uint8_t {
    Bind = 0,   // exe 0x4f2b70
    Follow = 1, // exe 0x4f2c10
};
constexpr int16_t kSmallHangDepth = 0x300, kHangDepth = 0x600; // the small cargo (+0xc != 0) / the big one
constexpr int16_t kSmallCargoScale = 0x100;
constexpr uint8_t kCargoAnim = 0;

// exe 0x4f2c10: the dropship's position, hangDepth below it
void followDropship(AnimatedActor& cargo, CargoFields& fields) {
    if (fields.dropship == nullptr) notPorted(kCargoArea, "0x4f2c10: no parent at +0x14c (the exe reads address 0x10)");
    const AnimatedActor& from = *fields.dropship;
    cargo.pos.x = from.pos.x;
    cargo.pos.y = int32_t(uint32_t(from.pos.y) + uint32_t(int32_t(fields.hangDepth) * -0x10000));
    cargo.pos.z = from.pos.z;
}
// exe 0x4f2b70: the model bound (none: freed), the small cargo scaled, hung under the dropship, its animation
void bindCargo(AnimatedActor& cargo, CargoFields& fields) {
    if (!bindActorModel(cargo, 0)) {
        freeActor(cargo);
        return;
    }
    if (cargo.params[0] != 0) {
        setScale(cargo, kSmallCargoScale);
        fields.hangDepth = kSmallHangDepth;
    } else {
        fields.hangDepth = kHangDepth;
    }
    if (fields.dropship == nullptr) notPorted(kCargoArea, "0x4f2b70: no parent at +0x14c (the exe reads address 0x10)");
    followDropship(cargo, fields);
    cargo.anim = kCargoAnim;
    cargo.prevAnim = kNoAnimShown;
    cargo.animKey.frame = 0;
    cargo.enterStateKeepingStep(uint8_t(CargoState::Follow));
}
void cargo(AnimatedActor& a) {
    CargoFields& fields = classState<CargoFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    switch (CargoState(a.state)) { // [0x896c0c]
    case CargoState::Bind: bindCargo(a, fields); break;
    case CargoState::Follow: followDropship(a, fields); break;
    default: notPortedAt(kCargoArea, "0x4f2ae0 state past [0x896c0c]", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    animateAsked(a, a.animKey.frame);
}

// ---- the cable (exe 0x439060) ------------------------------------------------------------------------------------------
constexpr uint8_t kCableState = 0;        // the state table's only entry (+8 never changes)
constexpr int16_t kCableNearDepth = 0x30; // a cable nearer than this OT depth is not drawn
// the line's two end colours: white with the LINE_G2 code (semi-transparent) on the first
constexpr uint32_t kCableColours[2] = {0x52ffffffu, 0xffffffu};
constexpr float kNoScreenOffset[2][2] = {};

// exe 0x4390a0: the line between the two actors' integer positions, linked at the depth of the cargo's end when it is
// within kCableNearDepth .. the effects' far depth
void drawCable(const EffectActor& cable) {
    const CableFields& fields = classState<CableFields>(cable);
    if (fields.dropship == nullptr || fields.cargo == nullptr)
        notPorted(kStoryPort, "0x4390a0: a null actor pointer (the exe reads through it)");
    const AnimatedActor& from = *fields.dropship;
    const AnimatedActor& to = *fields.cargo;
    const int16_t ends[2][3] = {{from.pos.x.whole, from.pos.y.whole, from.pos.z.whole},
                                {to.pos.x.whole, to.pos.y.whole, to.pos.z.whole}};
    const CameraPoint cargoEnd = cameraPoint(ends[1]); // the RTPT exe 0x40c730 / 0x40c930 (v0, v1, v1)
    const int16_t otz = int16_t(cameraPointDepth(cargoEnd) >> 2); // exe 0x40d010
    if (int32_t(otz) > int32_t(stage().effectsFar) || otz < kCableNearDepth) return;
    dc::otLineG2(cable.at, kCableColours, 1, ends, kNoScreenOffset);
}

} // namespace

void st49DropshipUpdate(const PlayerEnv&, uint32_t actor) { dropship(animatedActorAt(actor)); }
void st49DropshipCargoUpdate(const PlayerEnv&, uint32_t actor) { cargo(animatedActorAt(actor)); }
void st49DropshipCableUpdate(const PlayerEnv&, uint32_t actor) {
    const EffectActor& cable = effectActorAt(actor);
    if (cable.state != kCableState) notPortedAt(kStoryPort, "0x439060 state (table 0x88d53c)", cable.state);
    drawCable(cable);
}

} // namespace od::port
