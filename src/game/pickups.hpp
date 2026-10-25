// The pickups (static type 0 of the global table, exe 0x562240): the zenny, weapon energy and life a defeated enemy
// drops (exe 0x5f1270, by its drop table) -- launched in a random direction, bouncing, resting with a bob, rolling to
// the player near it or pulled by the vacuum arm, collected within reach of a living player, gone after a timeout.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port {

// what a pickup gives (its +0xc; the drop table's row)
enum class PickupKind : int8_t { Zenny = 0, WeaponEnergy = 1, Life = 2 };

// The pickup's fields (its static slot past the mesh layout): what it gives, and the pose the pickup draw list
// (0x6d006c, render output) reads
struct PickupFields : ActorClassState {
    explicit PickupFields(StaticActor& actor) : kind(actor.params[0]), size(actor.params[1]), drawRoll(actor.hitBox.previousX), spinSpeed(actor.gunshipPitchTarget()) {}
    FieldAlias<int8_t, uint8_t> kind; // PickupKind
    FieldAlias<int8_t, uint8_t> size; // 0..6: the amount's index
    FieldAlias<uint16_t, int16_t> drawRoll;
    FieldAlias<int8_t, uint8_t> spinSpeed;
    uint16_t drawPitch = 0; // the drawn tilt (in flight: the timeout's low byte << 8)
    uint16_t drawYaw = 0; // the drawn spin
    int16_t bob = 0; // the drawn height offset of the rest's bob
    uint8_t sprite = 0; // the zenny's size, kEnergySprite, kLifeSprite
    uint16_t timeout = 0; // ticks left (blinking below 0x41; never set by the allocator)
    uint16_t bobPhase = 0; // 0..0x3f
    void setDrawPose(uint16_t pitch, uint16_t yaw) { set_poseWord_(uint32_t(uint32_t(pitch) | uint32_t(yaw) << 16)); }

private:
    uint32_t get_poseWord_() const { return uint32_t(uint32_t(std::make_unsigned_t<uint16_t>(drawPitch)) << 0 | uint32_t(std::make_unsigned_t<uint16_t>(drawYaw)) << 16); }
    void set_poseWord_(uint32_t v) { drawPitch = uint16_t(std::make_unsigned_t<uint16_t>(v >> 0)); drawYaw = uint16_t(std::make_unsigned_t<uint16_t>(v >> 16)); }
public:

    void clearBlock() override {
        drawPitch = 0;
        drawYaw = 0;
        bob = 0;
        sprite = 0;
        timeout = 0;
        bobPhase = 0;
    }
};

// the pickup's states (+8)
enum class PickupState : uint8_t {
    Launch = 0,    // exe 0x562320
    Bounce = 1,    // exe 0x562440: falling, bouncing off the floor until it settles
    Rest = 2,      // exe 0x5624f0: the bob, the roll to the player
    Collected = 3, // exe 0x562620: given, then rising into the player
};
constexpr uint32_t kFlagPickupsStay = 0x244; // the pickups do not time out (nor blink)
constexpr int16_t kPickupBlinkTimeout = 0x41; // below this timeout a pickup is drawn every other tick (exe 0x562810)

// a launched pickup's aim (exe 0x5f1400: the spare lane of the velocity it is given): with kPickupAimed its yaw is the
// aim (& kAngleMask), jittered, not a random one
constexpr uint16_t kPickupAimed = 0x8000;

// exe 0x5f1270(pos, table): the drops of drop table `table` at the 16.16 point (none while the no-drops flag is set)
void spawnDrops(const Fixed16Vec3& pos, uint32_t table);
// ... at a point given as 16.16 values (the exe's stack point)
void spawnDrops(const int32_t pos[3], uint32_t table);
// exe 0x5f1400(pos, velocity, kind, size): one pickup (variant 1) launched from the 16.16 point with the velocity's
// four lanes; its yaw random, or (kPickupAimed in the spare lane) one of eight spread yaws + the aim + a jitter of
// -0x7f..0x80 (none while the no-drops flag is set)
void spawnLaunchedPickup(const Fixed16Vec3& pos, const ShortVec4& velocity, uint8_t kind, uint8_t size);
// ... from a point and a velocity given as values (the exe's stack locals: the velocity's two dwords x | y << 16,
// z | spare << 16)
void spawnLaunchedPickup(const int32_t pos[3], uint32_t velocityXY, uint32_t velocityZSpare, uint8_t kind,
                         uint8_t size);
// exe 0x5f1360(pos, velocity, table): the drops of drop table `table` as launched pickups (exe 0x5f1400)
void spawnLaunchedDrops(const Fixed16Vec3& pos, const ShortVec4& velocity, uint32_t table);
// ... from a point and a velocity given as values (the exe's stack locals, as spawnLaunchedPickup's)
void spawnLaunchedDrops(const int32_t pos[3], uint32_t velocityXY, uint32_t velocityZSpare, uint32_t table);
// exe 0x562240 (static type 0): the pickup -- launched (variant 0: a random direction), bouncing, resting with a bob
// (rolling to the player within 0xa0), pulled by the vacuum arm; collected within 0x40 of a living player (the zenny,
// the weapon energy or the life by its size, times the difficulty's multiplier), else gone when its timeout runs out
void pickupUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
