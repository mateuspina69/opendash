// The Blade Arm (weapon 11, pool 0x10 type 10, exe 0x56ee10): a blade out of the hand along the forearm, swung while
// the arm's swing lives (the player's specialFiring, set by game/player/player_weapons). A ring of the last 8 blade
// poses (its root point, pitch and yaw) feeds the trail (effect type 5, exe 0x55e870; drawn by game/effects_draw from
// the ring); each tick the blade tests the room along its length (the breakable walls take its attack) and is a capsule
// in the hit list.
#include "game/actor_state.hpp"
#include "game/shots/blade_arm.hpp"

#include "engine/data_source.hpp"
#include "game/global_class_types.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/shots/shot_common.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

inline constexpr const char* kBladeArea = "blade arm";

// [0x8c4a1c]
enum class BladeState : uint8_t { Spawn = 0, Swing = 1 }; // exe 0x56ee30 / 0x56f010

const DataTable<CollisionBoxRecord> bladeBox{TableId::BladeArmBox}; // exe data, resized by every wall test step
constexpr int kBladeRoot = -0x160;     // the blade's root: this far from the hand along the forearm
constexpr uint32_t kWallTestSteps = 8;
constexpr uint16_t kSwingSound = 0xfd, kSwingBackSound = 0xfe;
constexpr uint8_t kSwingCue = 1, kSwingBackCue = 2; // the body animation's key events that play them
constexpr uint32_t kHitRumble = 0x301, kHitRumbleShape = 0x2005ff;

// ring entry i from the arm's pose: the forearm's angles, then the point kBladeRoot out along them
void ringEntry(BladeFields& fields, uint32_t i) {
    const Player& p = player();
    const auto& hand = p.bonePoints[kLeftHandBone];
    const auto& forearm = p.bonePoints[kLeftForearmBone];
    const int16_t from[3] = {hand.x, hand.y, hand.z};
    const int16_t to[3] = {forearm.x, forearm.y, forearm.z};
    fields.ringPitches[i] = int16_t(pitchOf(from, to)); // exe 0x5e3d10
    fields.ringYaws[i] = int16_t(angleOf(hand, forearm) + kHalfTurn);
    const int16_t rot[3] = {fields.ringPitches[i], fields.ringYaws[i], 0};
    int32_t o[3];
    rotateVector(rot, 0, 0, kBladeRoot, o);
    const int sh = stage().coordinateShift & 0x1f;
    auto&& point = fields.ringPoints[i];
    point.x = int16_t((int32_t(hand.x) + (o[0] >> 16)) >> sh);
    point.y = int16_t((int32_t(hand.y) + (o[1] >> 16)) >> sh);
    point.z = int16_t((int32_t(hand.z) + (o[2] >> 16)) >> sh);
}

void spawn(ShotActor& shot, BladeFields& fields) { // exe 0x56ee30
    const Player& p = player();
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    const uint32_t e = allocActorA0(0); // exe 0x5e13b0
    if (e == 0) {
        freeActor(shot);
        return;
    }
    EffectActor& trail = effectActorAt(e);
    trail.type = typeByte(GlobalEffectType::BladeTrail);
    trail.flags = kActorLive;
    trail.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    trail.variant = 0;
    classState<BladeTrailFields>(trail).swingShot = &shot;
    PlayerShotFields(shot).hitBox.result = 0;
    fillShotStats(fields.stats);
    fields.ringIndex = 0;
    if (weaponLevel(p, player().activeSpecial, WeaponStat::Energy) < kFreeEnergyLevel)
        paySpecialEnergy(player()); // the swing costs energy
    for (uint32_t i = 0; i < kBladeRingSize; ++i) ringEntry(fields, i);
    shot.collisionBox.pointAt(bladeBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPlayer(kSwingSound);
    ++shot.state;
}

void swing(const PlayerEnv& env, ShotActor& shot, BladeFields& fields) { // exe 0x56f010
    const Player& p = player();
    auto&& shotFields = PlayerShotFields(shot);
    shot.savePreviousPose();
    if (shotFields.hitBox.result & (kHitAttackSolid | kHitAttackDeflects))
        padVibrate(kHitRumble, kHitRumbleShape); // a hit (and a white screen flash: output)
    const uint32_t i = (uint8_t(fields.ringIndex) + 1u) & (kBladeRingSize - 1);
    fields.ringIndex = uint16_t(i);
    const uint32_t prev = (i - 1u) & (kBladeRingSize - 1);
    ringEntry(fields, i);
    if (p.specialFiring == 0) { // the arm let go
        freeActor(shot);
        return;
    }
    const int16_t rot[3] = {fields.ringPitches[i], fields.ringYaws[i], 0};
    int32_t v[3];
    rotateVector(rot, 0, 0, -int32_t(fields.stats.range) * 2, v); // the whole blade: twice the range
    const auto& root = fields.ringPoints[i];
    const int16_t from16[3] = {root.x, root.y, root.z};
    // the wall test: 8 steps whose box grows; every step tests the same far point (n / 8 of the blade, n = the ring
    // index + 1), the first from the blade's root
    const int32_t n = int32_t(i) + 1;
    int32_t q[3], from[3];
    for (int k = 0; k < 3; ++k) {
        q[k] = int32_t(uint32_t(int32_t(from16[k])) << 16) + (int32_t(uint32_t(n) * uint32_t(v[k])) >> 3);
        from[k] = int32_t(uint32_t(int32_t(from16[k])) << 16);
    }
    const uint32_t mask = collisionLayerMask(shot.slot);
    for (uint32_t s = 0; s < kWallTestSteps; ++s) {
        auto&& box = *shot.collisionBox;
        const int16_t lo = int16_t((-8 - int(s)) * 8), hi = int16_t(s * 8 + 0x40);
        box.minZ = lo;
        box.minY = lo;
        box.minX = lo;
        box.maxZ = hi;
        box.maxY = hi;
        box.maxX = hi;
        const int16_t q16[3] = {int16_t(q[0] >> 16), int16_t(q[1] >> 16), int16_t(q[2] >> 16)};
        const int16_t f16[3] = {int16_t(from[0] >> 16), int16_t(from[1] >> 16), int16_t(from[2] >> 16)};
        const uint32_t r = testMoveBlocked(env, q16, f16, shot.collisionBox, mask);
        if (int32_t(r) < 0) queueBreakableWallHit(r, fields.stats.attack);
        for (int k = 0; k < 3; ++k) from[k] = q[k];
    }
    const int16_t end[3] = {wrap16(from16[0] + int16_t(v[0] >> 16)), wrap16(from16[1] + int16_t(v[1] >> 16)),
                            wrap16(from16[2] + int16_t(v[2] >> 16))};
    // the blade's capsule: its radius from the range at both ends, the other's last move as the contact
    const uint16_t range = fields.stats.range;
    hitListSubmitCapsule(from16, uint16_t(range >> 7), end, uint16_t((int32_t(range) * 7) >> 9), shotFields.hitBox,
                         hitOwner(shot.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitCapsule(1),
                         fields.stats.attack | kHitAttackByPlayer | kHitAttackStrongPush);
    shotFields.hitBox.result = 0;
    const auto& last = fields.ringPoints[prev];
    shotFields.hitBox.previousX = last.x;
    shotFields.hitBox.previousZ = last.z;
    const uint8_t cue = p.body.key.events;
    if (cue == kSwingCue) soundPlayAtPlayer(kSwingSound);
    if (cue == kSwingBackCue) soundPlayAtPlayer(kSwingBackSound);
}

} // namespace

void bladeArmShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    BladeFields& fields = classState<BladeFields>(shot);
    switch (BladeState(shot.state)) {
    case BladeState::Spawn: spawn(shot, fields); return;
    case BladeState::Swing: swing(env, shot, fields); return;
    default: notPortedAt(kBladeArea, "0x56ee10 state", shot.state);
    }
}

void bladeTrailUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& trail = effectActorAt(address);
    switch (BladeTrailState(trail.state)) {
    case BladeTrailState::Start:
        trail.runFlags |= kActorWarpKept;
        ++trail.state;
        return;
    case BladeTrailState::Follow: // drawn (game/effects_draw) while the arm is up and swinging, else freed
        if (!(player().armFlags & kArmSpecial) || player().specialFiring == 0) freeActor(trail);
        return;
    default: notPortedAt(kBladeArea, "0x55e870 state", trail.state);
    }
}

} // namespace od::port
