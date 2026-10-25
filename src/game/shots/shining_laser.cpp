// The Shining Laser (weapon 12, pool 0x10 type 11, exe 0x56f410): a beam from the hand along the player's aim while the
// special button is held. Its block (+0x74) is shared with the beam effect (effect type 8, exe 0x55fa10), which
// charges for 8 ticks, puts the beam out (the shot then traces it through the room in steps of 0x100, the breakable
// walls taking its attack, a wall spark -- effect type 9, exe 0x560b70 -- where it stops) and fades it when the arm
// lets go. Every traced step is a capsule in the hit list.
//
// The draw: the muzzle's discs and the fade's are draw commands; the beam, the muzzle's 4 random spikes and the charge
// sparks are drawn by game/effects_draw from the block (the spikes' RNG values handed over by laserSpikes, a
// presentation side channel: the RNG is consumed here, on the effect's tick, as the exe does).
#include "game/actor_state.hpp"
#include "game/shots/shining_laser.hpp"
#include "game/shots/shining_laser_state.hpp"

#include "engine/data_source.hpp"
#include "game/global_class_types.hpp"
#include "game/shots/player_shots.hpp"

#include <algorithm>
#include <array>
#include <map>

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace shining_laser_state;

namespace {

inline constexpr const char* kLaserArea = "shining laser";

// [0x8c4a34]
enum class LaserState : uint8_t { Spawn = 0, Fire = 1, Fade = 2 }; // exe 0x56f450 / 0x56f500 / 0x56f590
// the wall spark's phases (its step byte)
enum class WallSparkStep : uint8_t { Start = 0, Shrink = 1, Done = 2 };

const DataTable<CollisionBoxRecord> laserBox{TableId::ShiningLaserBox};
constexpr int kMuzzleReach = -0x500; // the muzzle this far along the aim from the hand
constexpr int kBeamStep = -0x1000;   // one traced step (<< 4)
constexpr uint16_t kChargeSound = 0x101, kBeamSound = 0x102, kFadeSound = 0x103;
constexpr uint16_t kChargeTicks = 8, kGlowSize = 0x10, kHumTicks = 6, kFadeTicks = 4;
constexpr uint8_t kWallSparkFade = 7;

// the spikes' RNG values per effect slot (presentation side channel; laserSpikes)
std::map<uint32_t, std::array<uint16_t, 8>>& spikeChannel() {
    static std::map<uint32_t, std::array<uint16_t, 8>> spikes;
    return spikes;
}

// the muzzle as the draw commands' xy word (y << 16 | x)
uint32_t muzzleXY(const LaserBlock& block) {
    return uint32_t(uint16_t(block.muzzle.x)) | uint32_t(uint16_t(block.muzzle.y)) << 16;
}

// exe 0x56f5c0(shot, block): the muzzle out along the player's aim; while the beam is out its steps traced through the
// room from the hand (a blocked step queues the wall hit and spawns the wall spark, ending the beam), then one capsule
// per step reached (4/5 of a step, radius (level * 8 + 0x20) / 4) in the hit list
void traceBeam(const PlayerEnv& env, ShotActor& shot, LaserBlock& block) {
    const Player& p = player();
    block.aim.x = p.rotation.x;
    block.aim.y = p.rotation.y;
    block.aim.z = 0;
    const int16_t rot[3] = {block.aim.x, block.aim.y, 0};
    const auto& hand = p.bonePoints[kLeftHandBone];
    const int16_t handPoint[3] = {hand.x, hand.y, hand.z};
    int32_t o[3];
    rotateVector(rot, 0, 0, kMuzzleReach, o);
    block.muzzle.x = wrap16(int32_t(o[0] >> 16) + handPoint[0]);
    block.muzzle.y = wrap16(int32_t(o[1] >> 16) + handPoint[1]);
    block.muzzle.z = wrap16(int32_t(o[2] >> 16) + handPoint[2]);
    if (block.beamOut == 0) return;
    int32_t from[3], step[3];
    for (int k = 0; k < 3; ++k) from[k] = int32_t(uint32_t(int32_t(handPoint[k])) << 16);
    rotateVector(rot, 0, 0, kBeamStep, step);
    const uint32_t mask = collisionLayerMask(shot.slot);
    uint32_t i = 0;
    for (; i < block.stats.range; ++i) {
        int32_t to[3];
        for (int k = 0; k < 3; ++k) to[k] = int32_t(uint32_t(from[k]) + uint32_t(step[k]));
        const int16_t to16[3] = {int16_t(to[0] >> 16), int16_t(to[1] >> 16), int16_t(to[2] >> 16)};
        const int16_t from16[3] = {int16_t(from[0] >> 16), int16_t(from[1] >> 16), int16_t(from[2] >> 16)};
        const uint32_t r = testMoveBlocked(env, to16, from16, shot.collisionBox, mask);
        if (int32_t(r) < 0) {
            queueBreakableWallHit(r, block.stats.attack);
            if (const uint32_t e = allocActorA0(0)) { // exe 0x5e13b0: the wall spark
                EffectActor& spark = effectActorAt(e);
                spark.flags = kActorLive;
                spark.type = typeByte(GlobalEffectType::LaserWallSpark);
                spark.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
                spark.variant = 0;
                spark.pos.x = to16[0];
                spark.pos.y = to16[1];
                spark.pos.z = to16[2];
                classState<LaserWallSparkFields>(effectActorAt(e)).size = uint8_t((uint8_t(block.stats.level) + 4u) * 4u);
            }
            break;
        }
        for (int k = 0; k < 3; ++k) from[k] = to[k];
    }
    const uint32_t n = (i + 1u) & 0xffffu;
    block.drawnSteps = uint16_t(n);
    int16_t a[3] = {block.muzzle.x, block.muzzle.y, block.muzzle.z};
    int16_t fourFifths[3];
    for (int k = 0; k < 3; ++k) fourFifths[k] = int16_t(int32_t(uint32_t(step[k] >> 14) & 0xfffffffcu) / 5);
    const uint16_t radius = uint16_t((uint32_t(uint16_t(block.stats.level)) * 8 + 0x20) >> 2);
    auto&& hitBox = PlayerShotFields(shot).hitBox;
    for (uint32_t s = 0; s < n; ++s) {
        const int16_t b[3] = {wrap16(a[0] + fourFifths[0]), wrap16(a[1] + fourFifths[1]), wrap16(a[2] + fourFifths[2])};
        hitListSubmitCapsule(a, radius, b, radius, hitBox,
                             hitOwner(shot.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint | hitCapsule(1),
                             block.stats.attack | kHitAttackByPlayer | kHitAttackStrongPush);
        for (int k = 0; k < 3; ++k) a[k] = wrap16(a[k] + wholePart(step[k]));
    }
    hitBox.result = 0;
    hitBox.previousX = block.muzzle.x;
    hitBox.previousZ = block.muzzle.z;
}

// exe 0x55fc00(effect, block): the muzzle's glow -- four discs at the muzzle (sizes by the counter); the RNG the 4
// spikes consume (2 steps each) when the muzzle is within the effects' depth range
void drawMuzzle(const EffectActor& beam, const LaserBlock& block) {
    const uint32_t n = block.counter, xy = muzzleXY(block), z = uint16_t(block.muzzle.z);
    dc::emitDisc(beam.at, dc::ringColour(0xffffff), xy, z | dc::ringStep(2), n << 0x11);
    dc::emitDisc(beam.at, dc::ringColour(0xffffff), xy, z | dc::ringStep(2), n << 0x12);
    dc::emitDisc(beam.at, dc::ringColour(0x78b858), xy, z | dc::ringStep(2), n * 0x60000u);
    dc::Ring halo;
    halo.inner = dc::ringColour(0x0c1808);
    halo.xy = xy;
    halo.outer = dc::ringRim(0x0c1808);
    halo.zStep = z | dc::ringStep(1);
    halo.radii = dc::ringRadii(n << 3, n * 8 + 1);
    dc::emit(halo, beam.at);
    const int16_t v[3] = {block.muzzle.x, block.muzzle.y, block.muzzle.z};
    std::array<uint16_t, 8>& spikes = spikeChannel()[beam.at];
    spikes[0] = 0xffff; // not drawn
    if (effectDepthVisible(cameraPoint(v)))
        for (uint32_t k = 0; k < 4; ++k) {
            spikes[k * 2] = uint16_t(rngStep() & 0xfff);
            spikes[k * 2 + 1] = uint16_t((rngStep() & 0x7f) + 0x60);
        }
}

// exe 0x5608f0(effect, block): the fade -- a disc (its colour by the counter), one small disc per charge spark (two per
// step reached, up to 32) spread around the beam's axis with RNG sizes, and a closing ring
void drawFade(const EffectActor& beam, const LaserBlock& block) {
    const uint32_t n = block.counter, xy = muzzleXY(block), z = uint16_t(block.muzzle.z);
    uint32_t r = (n * 0x78) >> 2, g = (n * 0xb8) >> 2, b = (n * 0x58) >> 2;
    dc::emitDisc(beam.at, ((r | 0xfffff000u) << 8 | g) << 8 | b, xy, z | dc::ringStep(2), 0x400000u);
    uint32_t count = uint32_t(block.drawnSteps) * 2;
    if (count > kLaserChargeSparks) count = kLaserChargeSparks;
    r >>= 2;
    g >>= 2;
    b >>= 2;
    const uint32_t sparkColour = ((r | 0xfffff000u) << 8 | g) << 8 | b;
    const int16_t rot[3] = {block.aim.x, block.aim.y, block.aim.z};
    for (uint32_t k = 0, d = 4; k < count; ++k, d += 2) {
        const uint32_t angle = block.sparkAngles[k], length = uint32_t(block.sparkLengths[k] >> 3);
        int32_t o[3];
        rotateVector(rot, (int32_t(cos64(angle)) * int32_t(length)) >> 8,
                     (int32_t(sin64(angle)) * int32_t(length)) >> 8, int32_t((n - d) * 0x800u), o);
        const uint32_t x = uint16_t(int16_t(uint16_t(uint32_t(o[0]) >> 16)) + block.muzzle.x);
        const uint32_t y = uint32_t((o[1] >> 16) + int32_t(block.muzzle.y));
        const uint32_t sz = uint16_t(int16_t(uint16_t(uint32_t(o[2]) >> 16)) + block.muzzle.z);
        const uint32_t size = rngStep() & 0x1f;
        dc::emitDisc(beam.at, sparkColour, y << 16 | x, sz | dc::ringStep(3), (size + 4) << 16);
    }
    r = r * 2 / 3;
    g = g * 2 / 3;
    b = b * 2 / 3;
    // the exe's arithmetic: the ring's mode bytes (0xf0 / kSemiAdditive) ORed in above r, which may carry into them;
    // the radii 0x140 - 0x40 n / 0x13f - 0x40 n, each wrapping on its own
    dc::Ring glow;
    glow.inner = ((r | 0xfffff000u) << 8 | g) << 8 | b;
    glow.xy = xy;
    glow.outer = ((r & 0xffffu | 0x2200u) << 8 | g) << 8 | b;
    glow.zStep = z | dc::ringStep(1);
    glow.radii = (n * uint32_t(-0x40) + 0x140u) | (n * uint32_t(-0x400000) + 0x13f0000u);
    dc::emit(glow, beam.at);
}

} // namespace

bool laserSpikes(uint32_t effect, uint16_t out[8]) {
    const auto it = spikeChannel().find(effect);
    if (it == spikeChannel().end() || it->second[0] == 0xffff) return false;
    std::copy(it->second.begin(), it->second.end(), out);
    return true;
}

void shiningLaserShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    LaserBlock& block = classState<LaserBlock>(shot);
    dropDeadTarget(shot);
    switch (LaserState(shot.state)) {
    case LaserState::Spawn: { // the beam effect, the stats, the first muzzle
        shot.flags &= uint8_t(~kActorDrawn);
        shot.runFlags |= kActorWarpKept;
        const uint32_t e = allocActorA0(0); // exe 0x5e13b0
        if (e == 0) return;
        EffectActor& beam = effectActorAt(e);
        beam.flags = kActorLive;
        beam.type = typeByte(GlobalEffectType::LaserBeam);
        beam.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
        beam.variant = 0;
        classState<LaserBeamFields>(effectActorAt(e)).laserShot = &shot;
        PlayerShotFields(shot).hitBox.result = 0;
        fillShotStats(block.stats);
        block.drawnSteps = 0;
        block.beamOut = 0;
        shot.collisionBox.pointAt(laserBox.stateAddress());
        shot.actorHit = 0;
        traceBeam(env, shot, block);
        ++shot.state;
        return;
    }
    case LaserState::Fire: { // while the special button is held and the arm is up; below the energy level 3 the beam
                             // pays every tick
        Player& p = player();
        if (p.holds(PlayerButton::Special) && (p.armFlags & kArmSpecial)) {
            if (block.beamOut == 0) {
                traceBeam(env, shot, block);
                return;
            }
            bool paid = weaponLevel(p, player().activeSpecial, WeaponStat::Energy) >= kFreeEnergyLevel;
            if (!paid) {
                p.gauge = wrap16(p.gauge - p.shotCost);
                paid = p.shotCost <= p.gauge;
            }
            if (paid) {
                p.specialFiring = 1;
                traceBeam(env, shot, block);
                return;
            }
            p.armFlags &= kArmSpecialSpentMask;
        }
        ++shot.state;
        traceBeam(env, shot, block);
        return;
    }
    case LaserState::Fade: // until the beam effect has faded out
        if (block.counter != 0) {
            traceBeam(env, shot, block);
            return;
        }
        freeActor(shot);
        return;
    default: notPortedAt(kLaserArea, "0x56f410 state", shot.state);
    }
}

void shiningLaserBeamUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& beam = effectActorAt(address);
    LaserBlock& block = classState<LaserBlock>(*classState<LaserBeamFields>(beam).laserShot);
    const Player& p = player();
    switch (BeamState(beam.state)) {
    case BeamState::Start: // the charge sparks
        beam.runFlags |= kActorWarpKept;
        block.counter = 1;
        for (uint32_t k = 0; k < kLaserChargeSparks; ++k) {
            block.sparkLengths[k] = uint8_t((rngStep() & 0x3f) + 0x10);
            block.sparkAngles[k] = uint8_t(rngStep() & 0x3f);
        }
        soundPlayAtPlayer(kChargeSound);
        drawMuzzle(beam, block);
        ++beam.state;
        return;
    case BeamState::Charge:
        if (!(p.armFlags & kArmSpecial)) {
            block.counter = 0;
            freeActor(beam);
            return;
        }
        drawMuzzle(beam, block);
        ++block.counter;
        if (block.counter != kChargeTicks) return;
        block.counter = kGlowSize;
        block.humTicks = kHumTicks;
        block.beamOut = 1;
        dc::emitDisc(beam.at, dc::ringColour(0xffffff), muzzleXY(block),
                     uint32_t(uint16_t(block.muzzle.z)) | dc::ringStep(1), 0x800000u);
        soundPlayAtPlayer(kBeamSound);
        ++beam.state;
        return;
    case BeamState::Beam: // until the arm is down or the shot stops firing
        if (!(p.armFlags & kArmSpecial) || p.specialFiring == 0) {
            block.beamOut = 0;
            block.counter = kFadeTicks;
            soundPlayAtPlayer(kFadeSound);
            drawFade(beam, block);
            ++beam.state;
            return;
        }
        drawMuzzle(beam, block);
        --block.humTicks;
        if (block.humTicks == 0) {
            soundPlayAtPlayer(kBeamSound);
            block.humTicks = kHumTicks;
        }
        return;
    case BeamState::Fade:
        --block.counter;
        if (block.counter != 0) {
            drawFade(beam, block);
            return;
        }
        freeActor(beam);
        return;
    default: notPortedAt(kLaserArea, "0x55fa10 state", beam.state);
    }
}

// exe 0x560b70: two discs (screen-space 0xf4 draw commands, which game/draw_commands does not draw: not emitted); the
// disc shrinks and its colour fades over 7 ticks, then freed
void laserWallSparkUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& spark = effectActorAt(address);
    auto&& fields = classState<LaserWallSparkFields>(spark);
    if (spark.state == 0) { // exe 0x560b90
        const uint16_t size = fields.size;
        spark.flags |= kActorDrawn;
        spark.runFlags |= kActorWarpKept;
        spark.state = 1;
        spark.step = uint8_t(WallSparkStep::Start);
        spark.stepCounter = 0;
        fields.shrink = uint16_t(size / 7u);
        fields.discSize = uint16_t(size / 7u + size);
        fields.size = uint16_t(uint32_t(size) * 3u);
        fields.fadeTicks = 0;
    } else if (spark.state != 1) {
        notPortedAt(kLaserArea, "0x560b70 state", spark.state);
    }
    const WallSparkStep phase = WallSparkStep(spark.step);
    if (phase == WallSparkStep::Done) {
        freeActor(spark);
        return;
    }
    if (phase == WallSparkStep::Start) {
        fields.fadeTicks = kWallSparkFade;
        spark.step = uint8_t(WallSparkStep::Shrink);
    }
    if (phase <= WallSparkStep::Shrink) { // exe 0x560bf0 / 0x560d90
        fields.discSize = uint16_t(fields.discSize - fields.shrink);
        --fields.fadeTicks;
        if (fields.fadeTicks == 0) ++spark.step;
    }
}

} // namespace od::port
