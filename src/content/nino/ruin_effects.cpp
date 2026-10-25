// The effects of the Nino ruin's stage tables (ST36 / ST38 / ST50), one routine per exe routine; the names are
// hypotheses from the observed behaviour.
//   0x5cbc60  pool 0x40 stage type 0x20: a glow blob flying along its rotation and falling (its size growing); a hit
//             entry while its parameter asks for one (a bump bursts it); its life over -> it fades; a wall / a bump ->
//             it bursts (8 frames). A billboard (draw command 0x2c) every tick.
//   0x431820  pool 0xa0 stage type 8: a flash -- variant 0 two billboards (8 ticks, sound 0x9b), else three rings
//             fading (6 ticks); draw commands
//   0x431e90  pool 0xa0 stage type 0xa: a per-variant effect ([0x88cb04]: 0..6 nothing, 7 / 8 the storm ribbon's start
//             / draw -- game/storm_ribbon; no ST36 record uses them: ST36 uses variant 1)
#include "content/nino/ruin_effects.hpp"
#include "content/nino/ruin_effects_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/draw_commands.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/storm_ribbon.hpp"
#include "game/story.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace ruin_effects_state;

namespace {

// the first dword of an s16 point (x | y << 16), as the draw commands take it
uint32_t xyWord(const ShortVec3& p) { return uint32_t(uint16_t(p.x)) | uint32_t(uint16_t(p.y)) << 16; }

// ---- 0x5cbc60: the glow blob ---------------------------------------------------------------------------------------
enum class BlobState : uint8_t { Start = 0, Fly = 1, Fade = 2, Burst = 3 };
const DataTable<CollisionBoxRecord> blobBox{TableId::RuinEffectsBlobBox};
constexpr uint16_t kBlobSound = 0xc8;

// the 0x2c billboard: the frame of the 32-texel strip, the brightness
void drawBlob(const ProjectileActor& blob, const BlobFields& fields) {
    const uint32_t b = uint8_t(fields.brightness);
    const uint32_t colour = (((((b | 0x2e00u) << 8) | b) << 8) | b);
    const uint32_t u0 = uint32_t(int32_t(fields.burstFrame)) << 5, u1 = u0 + 0x1f;
    dc::emit({0, colour, (uint32_t(int32_t(blob.pos.y.whole)) << 16) | uint16_t(blob.pos.x.whole), u0 | 0x3c388000u,
              uint16_t(blob.pos.z.whole), u1 | 0x2e8000u, uint32_t(int32_t(fields.size)), u0 | 0x9f00u, 0,
              u1 | 0x9f00u},
             blob.at, false);
}
// exe 0x5cbc60
void glowBlob(const PlayerEnv& env, ProjectileActor& blob) {
    BlobFields& fields = classState<BlobFields>(blob);
    blob.savePreviousPose();
    blob.wasAirborne = blob.airborne;
    switch (BlobState(blob.state)) {
    case BlobState::Start:
        fields.clearBlockBytes(); // exe 0x40dc40(+0xbc, +0xd8)
        fields.ticksLeft = uint16_t(int32_t(fields.life));
        fields.size = fields.startSize;
        fields.brightness = 0x80;
        blob.velocity.y = 0;
        blob.collisionBox.pointAt(blobBox.stateAddress());
        if (blob.variant == 0) soundPlayAtPos(kBlobSound, blob.pos);
        blob.enterState(uint8_t(BlobState::Fly));
        [[fallthrough]];
    case BlobState::Fly:
        moveAlongRotation(blob.pos, blob.rotation, 0, blob.velocity.y, blob.velocity.z); // exe 0x5e37b0
        blob.velocity.y = int16_t(uint16_t(blob.velocity.y) - 4u);
        fields.size = int16_t(uint16_t(fields.size) + uint16_t(fields.growth));
        fields.ticksLeft -= 1u;
        if (fields.ticksLeft == 0) blob.enterState(uint8_t(BlobState::Fade));
        if (int32_t(actorTestMove(env, blob)) < 0) { // exe 0x40f570
            blob.velocity.z = 0;
            blob.enterState(uint8_t(BlobState::Burst));
        }
        if (fields.hits != 0) {
            if (fields.hit.result & kHitAttackBump) {
                blob.velocity.z = 0;
                blob.enterState(uint8_t(BlobState::Burst));
            } else {
                fields.hit.result = 0;
                const int16_t pos[3] = {blob.pos.x.whole, blob.pos.y.whole, blob.pos.z.whole};
                fields.hit.previousX = blob.prevPos.x.whole;
                fields.hit.previousZ = blob.prevPos.z.whole;
                const uint32_t radius = uint32_t(int32_t(int16_t(fields.size >> 2)));
                hitListSubmit(pos, fields.hit, hitOwner(blob.slot) | radius | hitGroup(kHitGroupHeavyThrown),
                              uint32_t(int32_t(fields.attack)) | kHitAttackStrong); // exe 0x5e4120
            }
        }
        break;
    case BlobState::Fade:
        integrateVelocity(blob, 0, blob.velocity.y, blob.velocity.z);
        blob.velocity.y = int16_t(uint16_t(blob.velocity.y) - 4u);
        fields.brightness = int16_t(uint16_t(fields.brightness) - 0x10u);
        if (int32_t(actorTestMove(env, blob)) < 0) blob.velocity.z = 0;
        if (fields.brightness <= 0) {
            freeActor(blob); // exe 0x5e1500
            return;
        }
        break;
    case BlobState::Burst:
        integrateVelocity(blob, 0, blob.velocity.y, blob.velocity.z);
        blob.velocity.y = int16_t(uint16_t(blob.velocity.y) - 4u);
        fields.burstFrame = int16_t(uint16_t(fields.burstFrame) + 1u);
        if (fields.burstFrame > 7) {
            freeActor(blob);
            return;
        }
        break;
    default: break;
    }
    drawBlob(blob, fields);
}

// ---- 0x431820: the flash (states [0x88cadc]) -----------------------------------------------------------------------
enum class FlashState : uint8_t { Start = 0, Billboards = 1, Rings = 2 };
constexpr uint16_t kFlashSound = 0x9b;

// exe 0x431930: two 0x2c billboards (grey 0x60 / bright 0xc0, the second at 3/4 of the size)
void flashBillboards(const EffectActor& flash, const FlashFields& fields) {
    const uint32_t u0 = (0x100u - (uint32_t(uint16_t(fields.ticks)) << 5)) & 0xffff, u1 = (u0 + 0x1f) & 0xffff;
    const uint32_t pos = xyWord(flash.pos), z = uint32_t(int32_t(flash.pos.z));
    const uint32_t offset = uint32_t(int32_t(fields.depthOffset)) << 16;
    dc::emit({0, 0x2e606060u, pos, u0 | 0x7e10c000u, z, u1 | 0x2fc000u, offset | fields.size, u0 | 0xdf00u, 0,
              u1 | 0xdf00u},
             flash.at, false);
    dc::emit({0, 0x2ec0c0c0u, pos, u0 | 0x7e10c000u, z, u1 | 0x2fc000u, offset | ((uint32_t(fields.size) * 3) >> 2),
              u0 | 0xdf00u, 0, u1 | 0xdf00u},
             flash.at, false);
}
// exe 0x4319f0: three 0xf0 rings, their colour fading with the ticks left
void flashRings(const EffectActor& flash, const FlashFields& fields) {
    const int32_t t = fields.ticks;
    const int32_t d = (int32_t(fields.size) * (0xc - t)) >> 4;
    const uint32_t e = uint32_t(t << 5);
    const uint32_t high = ((uint32_t(fields.blue) * e) & 0xffffff00u) | 0xfff00000u;
    const uint32_t colour = (high << 8) | ((uint32_t(fields.green) * e) & 0xffffff00u) | ((uint32_t(fields.red) * e) >> 8);
    const uint32_t pos = xyWord(flash.pos), z = uint32_t(uint16_t(flash.pos.z)) | 0x20000u;
    dc::emit({0x4000, colour, pos, 0x22000000u, z, (uint32_t(d + 0xfff0) << 16) | uint32_t(d)}, flash.at);
    dc::emit({0x4000, colour, pos, 0x22000000u, z, (uint32_t(d + 0x10) << 16) | uint32_t(d)}, flash.at);
    dc::emit({0x4000, colour, pos, 0x22000000u, z, uint32_t(d + 0xfff0) << 16}, flash.at);
}
// exe 0x431820
void ruinFlash(EffectActor& flash) {
    FlashFields& fields = classState<FlashFields>(flash);
    switch (FlashState(flash.state)) {
    case FlashState::Start: // exe 0x431840
        flash.runFlags |= kActorWarpKept;
        fields.red = 0x7f;
        fields.green = 0x7f;
        fields.blue = 0x7f;
        if (flash.variant == 0) {
            soundPlayAtPosS16(kFlashSound, flash.pos); // exe 0x5d5420
            fields.ticks = 8;
            fields.depthOffset = int16_t(rngStep() & 0x3f);
            flashBillboards(flash, fields);
            flash.state = uint8_t(FlashState::Billboards);
        } else {
            fields.ticks = 6;
            flashRings(flash, fields);
            flash.state = uint8_t(FlashState::Rings);
        }
        return;
    case FlashState::Billboards: // exe 0x4318d0
    case FlashState::Rings:      // exe 0x431900
        fields.ticks = int16_t(uint16_t(fields.ticks) - 1u);
        if (fields.ticks == 0) {
            freeActor(flash); // exe 0x5e1580
            return;
        }
        if (FlashState(flash.state) == FlashState::Billboards) flashBillboards(flash, fields);
        else flashRings(flash, fields);
        return;
    default: notPortedAt(kStoryPort, "0x431820 state (past the table)", flash.state);
    }
}

// ---- 0x431e90: the per-variant effect (states [0x88caf8]) ----------------------------------------------------------
enum class VariantEffect : uint8_t { RibbonStart = 7, RibbonDraw = 8 };

// exe 0x431f20: [0x88cb04 + variant * 4](actor, block) unless the frame draws nothing
void variantEffectTick(EffectActor& fx) {
    if (display().drawingOff != 0) return;
    switch (fx.variant) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: return; // exe 0x52ef00 (ret)
    case uint8_t(VariantEffect::RibbonStart): stormRibbonStart(fx); return; // exe 0x431f70
    case uint8_t(VariantEffect::RibbonDraw): stormRibbonDraw(fx); return;   // exe 0x431fb0
    default: notPortedAt(kStoryPort, "0x431e90 variant (past the table)", fx.variant);
    }
}
// exe 0x431e90
void ruinVariantEffect(EffectActor& fx) {
    VariantEffectFields& fields = classState<VariantEffectFields>(fx);
    switch (VariantEffectState(fx.state)) {
    case VariantEffectState::Start: // exe 0x431eb0
        fx.runFlags |= kActorWarpKept;
        fx.state = uint8_t(VariantEffectState::Run);
        fields.tickA = 0;
        fields.tickB = 0;
        fields.size = uint16_t(uint32_t(fields.param & 0xf) << 6);
        fields.side = uint16_t(uint32_t(fields.param & 0x10) << 8);
        fields.kind = fx.variant != 0 ? 2 : 1;
        variantEffectTick(fx);
        return;
    case VariantEffectState::Run: variantEffectTick(fx); return; // exe 0x431f20
    case VariantEffectState::Free: freeActor(fx); return;        // exe 0x4358c0 -> 0x5e1580
    default: notPortedAt(kStoryPort, "0x431e90 state (past the table)", fx.state);
    }
}

} // namespace

void ruinShotHitSpark(const Actor& shot) {
    const uint32_t e = allocActorA0(0); // exe 0x5e13b0
    if (!e) return;
    EffectActor& spark = effectActorAt(e);
    HitSparkFields& fields = classState<HitSparkFields>(effectActorAt(e));
    spark.type = typeByte(GlobalEffectType::HitSpark);
    spark.variant = kSparkVariantHit;
    spark.flags = kActorLiveDrawn;
    spark.loopFlags |= kActorSweepGroup2;
    spark.pos.x = shot.pos.x.whole;
    spark.pos.y = shot.pos.y.whole;
    spark.pos.z = shot.pos.z.whole;
    fields.size = kHitSparkSize;
    fields.frameTicks = 1;
}
void ruinShotFlash(const Actor& shot, uint8_t variant) {
    const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!e) return;
    EffectActor& flash = effectActorAt(e);
    flash.flags = kActorLiveDrawn;
    flash.variant = variant;
    flash.loopFlags |= kActorSweepGroup2;
    flash.type = kRuinFlashType;
    flash.pos.x = shot.pos.x.whole;
    flash.pos.y = shot.pos.y.whole;
    flash.pos.z = shot.pos.z.whole;
    RuinFlashParams(effectActorAt(e)).size = 0x80;
}

void glowBlobUpdate(const PlayerEnv& env, uint32_t actor) { glowBlob(env, projectileActorAt(actor)); }
void ruinFlashUpdate(const PlayerEnv&, uint32_t actor) { ruinFlash(effectActorAt(actor)); }
void ruinVariantEffectUpdate(const PlayerEnv&, uint32_t actor) { ruinVariantEffect(effectActorAt(actor)); }

} // namespace od::port
