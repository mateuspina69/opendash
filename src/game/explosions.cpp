// The explosions (see explosions.hpp).
#include "game/explosions.hpp"
#include "game/explosions_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"

namespace od::port {

using namespace explosions_state;

namespace {

// the flashes' sprite sheet: CLUT 0x7c10, tpage 0x2e (additive), 48 x 48 frames
constexpr uint16_t kFlashClut = 0x7c10, kFlashTpage = 0x2e;

inline constexpr const char* kExplosionsArea = "explosions";

// ---- the death explosion (static type 8) ----------------------------------------------------------------------------
// the phase (the header's sub-state): the flashes, the shock wave, the wait for the last flash
enum class ExplosionSub : uint8_t { Flashes = 0, ShockWave = 1, Fading = 2 };
constexpr uint8_t kExplosionShockWave = 1; // a variant bit: a blast (static type 7) when the flashes are done
constexpr int8_t kFlashLife = 8;
constexpr uint16_t kFlashSound = 0x89;

// exe 0x5645b0: each live flash's camera-facing quad (a 0x2c draw command, word 0 left: the cell of its age on a 2 x 4
// grid of 0x2f texels, tpage 0x2e, CLUT 0x7c10), then it ages (the last tick of a flash takes it off the alive count)
void drawFlashes(StaticActor& actor) {
    auto&& fields = classState<ExplosionFields>(actor);
    for (uint32_t k = 0; k < 4; ++k) {
        const int8_t c = fields.lives[k];
        if (c == 0) continue;
        const ExplosionFlash flash = fields.flashes[k];
        const uint32_t age = uint32_t(kFlashLife - int32_t(c));
        const uint32_t u0 = (age & 3) * 0x30, v0 = (age & 4) * 12, u1 = u0 + 0x2f, v1 = v0 + 0x2f;
        dc::Billboard b;
        b.colour = dc::billboardColour(0x808080);
        b.xy = dc::packXY(flash.point.x, flash.point.y);
        // the exe writes the size in the z word's high half too (the decoder reads only the low half)
        b.z = dc::zWord(flash.point.z) | uint32_t(uint16_t(flash.size)) << 16;
        b.clut = kFlashClut;
        b.tpage = kFlashTpage;
        b.uv[0] = dc::texel(u0, v0);
        b.uv[1] = dc::texel(u1, v0);
        b.uv[2] = dc::texel(u0, v1);
        b.uv[3] = dc::texel(u1, v1);
        b.sizeRotation = dc::sizeRotation(uint16_t(flash.size), 0);
        dc::emit(b, actor.at);
        fields.lives[k] = int8_t(c - 1);
        if (int8_t(c - 1) == 0) --fields.flashesAlive;
    }
}

// ---- the blast ring (static type 7) -------------------------------------------------------------------------------
constexpr uint32_t kBlastColourRowSize = 0xc;
enum class BlastState : uint8_t { Start = 0, Grow = 1 };
constexpr uint32_t kHitGroupBlastBoth = 6; // the blast of a sweep in both groups hits the player and the bodies
// the variant's bits
constexpr uint8_t kBlastFlashes = 0x01;   // flashes (and the louder sound)
constexpr uint8_t kBlastColourRow = 0x02; // the second colour row
constexpr uint8_t kBlastQuiet = 0x10;     // no shake / vibration
constexpr uint8_t kBlastSilent = 0x20;    // no sound
constexpr int16_t kBlastTicks = 0xc;
constexpr int16_t kBlastGrowTicks = 0x10;
constexpr uint16_t kBlastSound = 0x8a, kBlastLoudSound = 0x8b;

// exe 0x563f10(a, p, r): the blast's sphere in the draw-command buffer -- 8 textured gouraud bands (0xf8: tilt 0x400 ..
// 0x2000, grey ticks * 8, the colour row's band textures, v rows 0x7f + 0x20 k, mirrored past 0x1400) and two discs
// (0xf0) at the centre, radius r
void drawSphere(const StaticActor& actor, const BlastFields& blast, uint32_t r) {
    const uint32_t t = uint32_t(int32_t(blast.ticks));
    const BlastColourRow row = *blast.colours;
    const uint32_t grey = ((t << 8 | t) << 8 | t) << 3;
    const uint32_t xy = dc::packXY(actor.pos.x.whole, actor.pos.y.whole);
    const uint32_t z = dc::zWord(actor.pos.z.whole);
    const uint32_t frame = gameState().frame & 0x3fu;
    dc::Band band;
    band.ring.inner = dc::bandColour(grey);
    band.ring.xy = xy;
    band.ring.outer = dc::ringRim(grey);
    band.ring.zStep = z | dc::ringStep(3);
    band.ring.radii = dc::ringRadii(r << 2, r << 2);
    uint32_t v = 0x7f, k = 0;
    // the texels' u past 0xff carries into v as the exe's OR does (dc::texel keeps it)
    for (uint32_t tilt = 0x400; tilt < 0x2400; tilt += 0x400, k += 4, v += 0x20) {
        band.ring.arc = dc::bandRepeat(2) | dc::ringAngle(frame) | dc::ringArc(int(k), int(tilt >> 8));
        band.clutTexel = dc::pageTexel(row.bandTexture1, tilt < 0x1400 ? dc::texel(v - 0x1f, 0x20)
                                                                        : dc::texel(0x140u - k * 8, 0x3f));
        band.tpageTexel = dc::pageTexel(row.bandTexture0, tilt < 0x1400 ? dc::texel(v, 0x3f)
                                                                         : dc::texel(0x1deu - v, 0x20));
        dc::emit(band, actor.at);
    }
    // the exe's colour arithmetic: t's bits shifted up into the three channels, the ring's mode byte (kRingOpcode /
    // kSemiAdditive: 0x440 << 19) carried in from the high bits and masked by the row
    const uint32_t c = t | 0xfffffe00u, mask = row.mask | dc::kRingOpcode;
    dc::emitDisc(actor.at, ((((c << 10) | t) << 6 | t) << 3) & mask, xy, z | dc::ringStep(2), dc::ringRadii(r * 4, r * 5));
    dc::Ring ring;
    ring.inner = ((((c << 9) | t) << 7 | t) << 3) & mask;
    ring.xy = xy;
    ring.outer = ((((t | 0x440u) << 9) | t) << 7 | t) << 3;
    ring.zStep = z | dc::ringStep(2);
    ring.radii = dc::ringRadii(0, r << 2);
    dc::emit(ring, actor.at);
}
// exe 0x564200's records for flash i (before it ages): two 0xf8 bands, grey life * 16 / life * 8, radius (radius_i *
// r) >> 2, the tilt from its tilt and spread, the colour row's flash textures
void drawBlastFlash(const StaticActor& actor, const BlastFields& blast, uint32_t r, uint32_t i) {
    const uint32_t life = uint32_t(int32_t(blast.flashLives[i]));
    const uint32_t full = ((life << 8 | life) << 8 | life) << 4, half = full >> 1;
    const uint32_t k = uint32_t(int32_t(blast.flashRadii[i]) * int32_t(r) >> 2);
    const uint32_t s = uint32_t(int32_t(blast.flashSpreads[i])), e = uint32_t(int32_t(blast.flashTilts[i])) + s;
    const uint32_t frame = gameState().frame & 0x3fu;
    const BlastColourRow row = *blast.colours;
    dc::Band band;
    band.ring.xy = dc::packXY(actor.pos.x.whole, actor.pos.y.whole);
    band.ring.zStep = dc::zWord(actor.pos.z.whole) | dc::ringStep(2) | dc::kRingFront;
    band.ring.radii = dc::ringRadii(k, k);
    // the arc words as the exe computes them (a tilt below the spread borrows into the angle byte)
    band.ring.arc = (frame << 8 | e) << 8 | (e - s);
    band.ring.inner = dc::bandColour(half);
    band.ring.outer = dc::ringRim(full);
    band.clutTexel = dc::pageTexel(row.flashTexture1, dc::texel(0xe0, 0x20));
    band.tpageTexel = dc::pageTexel(row.flashTexture0, dc::texel(0xff, 0x3f));
    dc::emit(band, actor.at);
    band.ring.arc = dc::ringAngle(frame) | (s + e) << 8 | e;
    band.ring.inner = dc::bandColour(full);
    band.ring.outer = dc::ringRim(half);
    band.clutTexel = dc::pageTexel(row.flashTexture1, dc::texel(0xe0, 0x3f));
    band.tpageTexel = dc::pageTexel(row.flashTexture0, dc::texel(0xff, 0x20));
    dc::emit(band, actor.at);
}

} // namespace

void explosionUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& fields = classState<ExplosionFields>(actor);
    const ExplosionParams params(actor);
    switch (ExplosionState(actor.state)) {
    case ExplosionState::Start: // exe 0x564390
        actor.runFlags |= kActorWarpKept;
        fields.clearLives();
        fields.flashesLeft = params.flashes;
        fields.flashesAlive = 0;
        actor.subState = uint8_t(ExplosionSub::Flashes);
        ++actor.state;
        return;
    case ExplosionState::Run: break; // exe 0x5643c0
    default: notPortedAt(kExplosionsArea, "explosion state", actor.state);
    }
    const ExplosionSub phase = ExplosionSub(actor.subState);
    if (phase == ExplosionSub::Flashes) {
        if (int16_t(fields.flashesLeft) != 0) {
            if (fields.flashesLeft & 1) { // every other tick a new flash in a free slot
                uint32_t k = 0;
                while (fields.lives[k] != 0) {
                    if (++k > 3) {
                        --fields.flashesLeft;
                        drawFlashes(actor);
                        return;
                    }
                }
                fields.lives[k] = kFlashLife;
                auto&& flash = fields.flashes[k];
                const uint32_t spread = params.spread;
                uint32_t r = rngStep();
                flash.point.x =
                    int16_t(uint16_t(uint32_t(((r & 0x1ff) - 0x100) * spread >> 8) + uint16_t(actor.pos.x.whole)));
                r = rngStep();
                flash.point.y =
                    int16_t(uint16_t(uint32_t(((r & 0x1ff) - 0x100) * spread >> 8) + uint16_t(actor.pos.y.whole)));
                r = rngStep();
                flash.point.z =
                    int16_t(uint16_t(uint32_t(((r & 0x1ff) - 0x100) * spread >> 8) + uint16_t(actor.pos.z.whole)));
                r = rngStep();
                flash.size = int16_t((r & 0x7f) + 0x40);
                ++fields.flashesAlive;
                soundPlayAtPosS16(kFlashSound, flash.point);
            }
            --fields.flashesLeft;
            drawFlashes(actor);
            return;
        }
        actor.subState = uint8_t(ExplosionSub::ShockWave);
    } else {
        if (phase == ExplosionSub::ShockWave) {
            if (actor.variant & kExplosionShockWave) { // exe 0x5e1300: a blast at the same place
                if (const uint32_t s = allocActor60(0)) {
                    StaticActor& blast = staticActorAt(s);
                    blast.flags = kActorLive;
                    blast.type = typeByte(GlobalStaticType::Blast);
                    blast.variant = 0;
                    blast.loopFlags |= uint8_t(actor.loopFlags & (kActorSweepGroup1 | kActorSweepGroup2));
                    blast.pos = actor.pos;
                    auto&& blastParams = BlastParams(blast);
                    blastParams.size = uint16_t(int32_t(uint32_t(params.spread) * 3) >> 1);
                    blastParams.attack = uint16_t((params.spread >> 4) + 1u);
                }
            }
            ++actor.subState;
        } else if (phase != ExplosionSub::Fading) {
            drawFlashes(actor);
            return;
        }
        if (fields.flashesAlive == 0) {
            freeActor(actor);
            return;
        }
    }
    drawFlashes(actor);
}

void blastUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    BlastFields& blast = classState<BlastFields>(actor);
    const BlastParams params(actor);
    if (BlastState(actor.state) == BlastState::Start) { // exe 0x563b30
        actor.runFlags |= kActorWarpKept;
        if (!(actor.variant & kBlastQuiet)) {
            cameraShakeAt(0x2000, 0x300, 0x100, 0x1c00, actor.pos);
            padVibrateAt(0x201, 0x208b0, 0x100, 0x1c00, actor.pos);
        }
        if (!(actor.variant & kBlastSilent))
            soundPlayAtPos((actor.variant & kBlastFlashes) ? kBlastLoudSound : kBlastSound, actor.pos);
        blast.clearFlashLives();
        blast.ticks = kBlastTicks;
        blast.colours.pointAt(stateAddress(TableId::ExplosionsBlastColours) + ((actor.variant >> 1) & 1) * kBlastColourRowSize);
        drawSphere(actor, blast, (uint32_t(params.size) * 3) >> 5); // exe 0x563f10
        actor.collisionBox.pointTo(blast.box);
        ++actor.state;
        return;
    }
    if (BlastState(actor.state) != BlastState::Grow) notPortedAt(kExplosionsArea, "blast state", actor.state);
    // exe 0x563c00: the ring grows -- radius (size * (0x10 - ticks)) >> 5
    if (blast.ticks == 0) {
        freeActor(actor); // exe 0x5e1540
        return;
    }
    const int32_t grown = int32_t(uint32_t(params.size) * uint32_t(kBlastGrowTicks - blast.ticks));
    const uint32_t r = uint32_t(grown >> 5);
    drawSphere(actor, blast, r); // exe 0x563f10
    if (actor.variant & kBlastFlashes) {
        const uint32_t n = rngStep();
        if ((n & 1) == 0 && blast.ticks > 4) { // exe 0x564160: a free flash slot
            for (uint32_t i = 0; i < 4; ++i)
                if (blast.flashLives[i] == 0) {
                    blast.flashLives[i] = 6;
                    blast.flashTilts[i] = int8_t((rngStep() & 3) + 4);
                    blast.flashRadii[i] = int8_t((rngStep() & 3) + 0x11);
                    blast.flashSpreads[i] = int8_t((rngStep() & 1) + 2);
                    break;
                }
        }
        for (uint32_t i = 0; i < 4; ++i) // exe 0x564200: each live flash drawn, then it ages
            if (blast.flashLives[i] != 0) {
                drawBlastFlash(actor, blast, r, i);
                --blast.flashLives[i];
                blast.flashTilts[i] = int8_t(blast.flashTilts[i] + 3);
            }
    }
    --blast.ticks;
    // the box +-2r, the walls hit by the ring (the centre and +-r on each axis: the breakable walls take attack >> 6)
    const uint16_t span = uint16_t(r << 1);
    actor.collisionBox->minZ = int16_t(-int16_t(span));
    actor.collisionBox->minY = actor.collisionBox->minZ;
    actor.collisionBox->minX = actor.collisionBox->minY;
    actor.collisionBox->maxZ = int16_t(span);
    actor.collisionBox->maxY = int16_t(span);
    actor.collisionBox->maxX = int16_t(span);
    const uint32_t mask = collisionLayerMask(actor.slot);
    int damage = int(params.attack >> 6);
    if (damage == 0) damage = 1;
    const int16_t c[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const int16_t reach = int16_t(r);
    const CollisionBoxRef& box = actor.collisionBox;
    auto test = [&](int16_t x, int16_t y, int16_t z) {
        const int16_t q[3] = {x, y, z};
        const uint32_t t = testMoveBlocked(env, q, c, box, mask);
        if (int32_t(t) < 0) queueBreakableWallHit(t, damage);
    };
    test(c[0], c[1], c[2]);
    test(wrap16(c[0] - reach), c[1], c[2]);
    test(wrap16(c[0] + reach), c[1], c[2]);
    test(c[0], wrap16(c[1] - reach), c[2]);
    test(c[0], wrap16(c[1] + reach), c[2]);
    test(c[0], c[1], wrap16(c[2] - reach));
    test(c[0], c[1], wrap16(c[2] + reach));
    // the hit entry, by the sweep group: group 6 (solid + the player's), group 3 (solid) or group 1 (the player's)
    const uint8_t sweep = actor.loopFlags & (kActorSweepGroup1 | kActorSweepGroup2);
    if (!sweep) return;
    uint32_t group, attackKind;
    if (sweep == (kActorSweepGroup1 | kActorSweepGroup2)) {
        group = hitGroup(kHitGroupBlastBoth);
        attackKind = kHitAttackSolid | kHitAttackByPlayer;
    } else if (!(sweep & kActorSweepGroup1)) {
        group = hitGroup(kHitGroupHeavyThrown);
        attackKind = kHitAttackSolid;
    } else {
        group = hitGroup(kHitGroupPlayerAttacks);
        attackKind = kHitAttackByPlayer;
    }
    hitListSubmit(c, blast.hitBox,
                  uint32_t(int32_t(uint32_t(grown >> 3) | 0x200000u) >> 2) | hitOwner(actor.slot) | group,
                  params.attack | attackKind);
    blast.hitBox.result = 0;
    blast.hitBox.previousX = actor.pos.x.whole;
    blast.hitBox.previousZ = actor.pos.z.whole;
}

namespace {

// a global static of the type at the s16 point (the spawners' store order); its slot (0: the pool is full)
uint32_t spawnGlobalStaticAt(GlobalStaticType type, int16_t x, int16_t y, int16_t z) {
    const uint32_t slot = allocActor60(0); // exe 0x5e1300
    if (!slot) return 0;
    StaticActor& s = staticActorAt(slot);
    s.flags = kActorLive;
    s.type = typeByte(type);
    s.variant = 0;
    s.loopFlags |= kActorSweepGroup2;
    s.pos.x.whole = x;
    s.pos.y.whole = y;
    s.pos.z.whole = z;
    return slot;
}

} // namespace

void spawnBlastAt(int16_t x, int16_t y, int16_t z, uint16_t size, uint16_t attack) {
    const uint32_t slot = spawnGlobalStaticAt(GlobalStaticType::Blast, x, y, z);
    if (!slot) return;
    BlastParams params(actorHeaderAt(slot));
    params.size = size;
    params.attack = attack;
}

void spawnDeathExplosionAt(int16_t x, int16_t y, int16_t z, uint16_t spread, uint16_t flashes) {
    const uint32_t slot = spawnGlobalStaticAt(GlobalStaticType::Explosion, x, y, z);
    if (!slot) return;
    ExplosionParams params(actorHeaderAt(slot));
    params.spread = spread;
    params.flashes = flashes;
}

} // namespace od::port
