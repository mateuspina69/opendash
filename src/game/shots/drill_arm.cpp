// The drill arm (weapon 17, pool 0x10 type 16, exe 0x571280): a drill bit 0x800 out of the hand along the forearm,
// running while the arm is up and the special button held (the energy drains every tick). Its cone (static global type
// 0x19, exe 0x56b970: 16 rings of additive quads from the hand along the bit, growing while the drill runs) follows the
// shot; the bit tests the room (the only attack with the highest break level: it breaks every breakable wall), throws
// sparks (static global type 0xd) where it grinds and is a capsule in the hit list.
#include "engine/data_source.hpp"
#include "game/shots/drill_arm_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/effect_polygons.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace drill_arm_state;

namespace {

inline constexpr const char* kDrillArea = "drill arm";

// [0x8c48e0]
enum class ConeState : uint8_t { Start = 0, Grow = 1, Fade = 2 }; // exe 0x56b990 / 0x56b9d0 / 0x56ba00
constexpr int kConeRings = 0x10, kConeRingQuads = 0x10;

// exe 0x56ba30(actor, b): from the left hand along the shot's rotation, 16 rings (z = -16 i) of 16 additive POLY_G4
// between a random radius ((rand & 0x1f) + 0x18) * (16 - i) / 8 and that + rand + 2 i, grey by the strength
void coneDraw(StaticActor& actor, const DrillConeFields& f) {
    const Actor& parent = actorLayoutAt(f.parent);
    const ShortVec3& hand = player().bonePoints[kLeftHandBone];
    const int16_t org[3] = {hand.x, hand.y, hand.z};
    const LocalFrame frame = localFrame(parent.rotation.x, parent.rotation.y, parent.rotation.z, org);
    const int sh = (0xc - int(stage().coordinateShift)) & 0x1f;
    for (int i = 0; i < kConeRings; ++i) {
        const uint32_t r1 = rngStep(), r2 = rngStep(), r3 = rngStep();
        const int rin = int((((r1 & 0x1f) + 0x18) * uint32_t(0x10 - i)) >> 3);
        const int rout = rin + int(r2 & 7) + i * 2;
        const uint32_t g = (uint32_t((i >> 1) + int(r3 & 7) + 2) * uint32_t(int32_t(f.strength))) >> 4;
        const uint32_t grey = g | (g << 8 | g) << 8;
        const int16_t z = wrap16(-i << 4);
        int16_t pi[3], po[3];
        frame.point(wrap16(fixedCos64(0) * rin >> sh), wrap16(fixedSin64(0) * rin >> sh), z, pi);
        frame.point(wrap16(fixedCos64(0) * rout >> sh), wrap16(fixedSin64(0) * rout >> sh), z, po);
        uint32_t ang = 0;
        for (int k = 0; k < kConeRingQuads; ++k) {
            ang = (ang + 4) & 0x3f;
            int16_t ni[3], no[3];
            frame.point(wrap16(fixedCos64(ang) * rin >> sh), wrap16(fixedSin64(ang) * rin >> sh), z, ni);
            frame.point(wrap16(fixedCos64(ang) * rout >> sh), wrap16(fixedSin64(ang) * rout >> sh), z, no);
            const uint32_t c[4] = {dc::packetColour(dc::kPolyG4Semi, grey), grey, dc::packetColour(dc::kPolyG4Semi, grey),
                                   grey};
            const int16_t v[4][3] = {{pi[0], pi[1], pi[2]}, {po[0], po[1], po[2]}, {ni[0], ni[1], ni[2]},
                                     {no[0], no[1], no[2]}};
            worldG4(actor.at, c, v);
            for (int q = 0; q < 3; ++q) {
                pi[q] = ni[q];
                po[q] = no[q];
            }
        }
    }
}
// exe 0x56b990: kept through the warps, the first strength, the shot it follows
void coneStart(StaticActor& actor, DrillConeFields& f) {
    actor.runFlags |= kActorWarpKept;
    f.strength = 1;
    f.parent = f.shot;
    coneDraw(actor, f);
    actor.state += 1u;
}
// exe 0x56b9d0: growing up to kConeFull while the drill runs (the player's special weapon firing), then fading
void coneGrow(StaticActor& actor, DrillConeFields& f) {
    if (f.strength < kConeFull) f.strength += uint32_t(kConeGrowth);
    if (player().specialFiring == 0) actor.state += 1u;
    coneDraw(actor, f);
}
// exe 0x56ba00: fading (none left: freed)
void coneFade(StaticActor& actor, DrillConeFields& f) {
    --f.strength;
    if (f.strength == 0) {
        freeActor(actor);
        return;
    }
    coneDraw(actor, f);
}

// [0x8c4abc]
enum class DrillState : uint8_t { Spawn = 0, Run = 1 }; // exe 0x5712a0 / 0x571380

const DataTable<CollisionBoxRecord> drillBox{TableId::DrillArmBox}; // the wall test box (-0x20..0x20 per axis)
constexpr int kBitReach = -0x800;      // the bit's tip this far from the hand along the forearm
constexpr uint8_t kGrindSparksVariant = 1;
constexpr int16_t kSparkTicks = 3;
constexpr int16_t kStartSoundTicks = 0xc, kSoundTicks = 5;
constexpr uint16_t kStartSound = 0x305, kRunSound = 0x306, kStopSound = 0x307, kGrindSound = 0x308;
constexpr uint16_t kRootRadius = 8, kTipRadius = 4;

// the shot's rotation: the forearm's pitch, the player's yaw turned back
void aimAlongForearm(ShotActor& shot) {
    const Player& p = player();
    const auto& hand = p.bonePoints[kLeftHandBone];
    const auto& forearm = p.bonePoints[kLeftForearmBone];
    const int16_t from[3] = {hand.x, hand.y, hand.z};
    const int16_t to[3] = {forearm.x, forearm.y, forearm.z};
    shot.rotation.x = int16_t(pitchOf(from, to)); // exe 0x5e3d10
}

// exe 0x571460(shot, block): the drill's aim, its wall test (from the hand to half the bit), the sparks on a hit
// (kSparkTicks apart) and its capsule (the hand, radius 8, to the tip, radius 4); true = it grinds on something
bool drillTick(const PlayerEnv& env, ShotActor& shot, DrillFields& fields) {
    const Player& p = player();
    bool grinding = false;
    aimAlongForearm(shot);
    shot.rotation.y = wrap16(p.rotation.y + kHalfTurn);
    shot.rotation.z = 0;
    const auto& hand = p.bonePoints[kLeftHandBone];
    const int16_t root[3] = {hand.x, hand.y, hand.z};
    const int16_t rot[3] = {shot.rotation.x, shot.rotation.y, shot.rotation.z};
    int32_t v[3];
    rotateVector(rot, 0, 0, kBitReach, v); // exe 0x5e3970
    const int16_t tip[3] = {wrap16(root[0] + wholePart(v[0])), wrap16(root[1] + wholePart(v[1])),
                            wrap16(root[2] + wholePart(v[2]))};
    int32_t mid[3];
    for (int k = 0; k < 3; ++k) mid[k] = (v[k] >> 1) + int32_t(uint32_t(int32_t(root[k])) << 16);
    const int16_t mid16[3] = {int16_t(mid[0] >> 16), int16_t(mid[1] >> 16), int16_t(mid[2] >> 16)};
    const uint32_t r = testMoveBlocked(env, mid16, root, shot.collisionBox, collisionLayerMask(shot.slot));
    if (int32_t(r) < 0) queueBreakableWallHit(r, int((uint16_t(fields.stats.level) + 1u) | breakLevel(kBreakLevelMax)));
    auto&& hitBox = PlayerShotFields(shot).hitBox;
    if (fields.sparkTicks == 0) {
        if (r != 0 || (hitBox.result & (kHitAttackSolid | kHitAttackDeflects))) {
            if (const uint32_t e = allocActor60(0)) { // exe 0x5e1300: the sparks
                StaticActor& sparks = staticActorAt(e);
                sparks.flags = kActorLive;
                sparks.type = typeByte(GlobalStaticType::HurtSparks);
                sparks.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
                sparks.variant = kGrindSparksVariant;
                sparks.pos.x = mid[0];
                sparks.pos.y = mid[1];
                auto&& params = classState<HurtSparksFields>(staticActorAt(e));
                params.pitch = 0;
                sparks.pos.z = mid[2];
                params.yaw = uint16_t(shot.rotation.y);
                fields.sparkTicks = kSparkTicks;
            }
            grinding = true;
        }
    } else {
        fields.sparkTicks = wrap16(fields.sparkTicks - 1);
    }
    hitListSubmitCapsule(root, kRootRadius, tip, kTipRadius, hitBox,
                         hitOwner(shot.slot) | hitGroup(kHitGroupPlayerAttacks) | hitCapsule(1),
                         fields.stats.attack | kHitAttackByPlayer | kHitAttackStrongPush);
    hitBox.result = 0;
    const auto& forearm = p.bonePoints[kLeftForearmBone];
    hitBox.previousX = forearm.x;
    hitBox.previousZ = forearm.z;
    return grinding;
}

void spawn(ShotActor& shot, DrillFields& fields) { // exe 0x5712a0: no cone slot -> tried again next tick
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    const uint32_t e = allocActor60(0); // exe 0x5e1300
    if (e == 0) return;
    StaticActor& cone = staticActorAt(e);
    cone.flags = kActorLive;
    cone.type = typeByte(GlobalStaticType::DrillCone);
    cone.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    cone.variant = 0;
    classState<DrillConeFields>(staticActorAt(e)).shot = shot.at;
    PlayerShotFields(shot).hitBox.result = 0;
    aimAlongForearm(shot);
    shot.rotation.z = 0;
    shot.rotation.y = wrap16(player().rotation.y + kHalfTurn);
    fillShotStats(fields.stats);
    fields.sparkTicks = 0;
    fields.soundTicks = kStartSoundTicks;
    fields.soundPad = 0;
    shot.collisionBox.pointAt(drillBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPlayer(kStartSound);
    ++shot.state;
}

void run(const PlayerEnv& env, ShotActor& shot, DrillFields& fields) { // exe 0x571380
    Player& p = player();
    if (!(p.armFlags & kArmSpecial) || !p.holds(PlayerButton::Special)) {
        soundPlayAtPlayer(kStopSound);
        freeActor(shot);
        return;
    }
    p.gauge = wrap16(p.gauge - p.shotCost);
    if (p.gauge < p.shotCost) {
        p.armFlags &= kArmSpecialSpentMask;
        soundPlayAtPlayer(kStopSound);
        freeActor(shot);
        return;
    }
    p.specialFiring = 1;
    const bool grinding = drillTick(env, shot, fields);
    fields.soundTicks = wrap16(fields.soundTicks - 1);
    if (fields.soundTicks == 0) {
        fields.soundTicks = kSoundTicks;
        soundPlayAtPlayer(kRunSound);
        if (grinding) soundPlayAtPlayer(kGrindSound);
    }
}

} // namespace

void drillConeUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<DrillConeFields>(actor);
    switch (ConeState(actor.state)) {
    case ConeState::Start: coneStart(actor, f); return;
    case ConeState::Grow: coneGrow(actor, f); return;
    case ConeState::Fade: coneFade(actor, f); return;
    default: notPortedAt(kDrillArea, "0x56b970 state", actor.state);
    }
}

void drillArmShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<DrillFields>(shot);
    switch (DrillState(shot.state)) {
    case DrillState::Spawn: spawn(shot, fields); return;
    case DrillState::Run: run(env, shot, fields); return;
    default: notPortedAt(kDrillArea, "0x571280 state", shot.state);
    }
}

} // namespace od::port
