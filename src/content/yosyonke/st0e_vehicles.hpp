// ST0E (the vehicle chase, see st0e_boss_vehicle.hpp): what its classes share -- the room's two vehicles (the room
// records' actors 0 / 1: the boss vehicle and the vehicle the player defends), a bone point offset, the facing test
// of exe 0x5036e0 and the vehicles' voices.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/stage_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

inline constexpr const char* kSt0ePort = "story port"; // the area of the ST0E classes' port-gap errors

// the boss vehicle (the room records' actor 0, exe 0x501d70) / the defended vehicle (actor 1, exe 0x5b5e70); the exe
// reads their fields unconditionally: a port gap when the slot is empty
AnimatedActor& bossVehicle();
StaticActor& defendedVehicle();
// the defended vehicle's slot word (0: none)
uint32_t defendedVehicleAddress();
// ST0E's stage block (game/stage_state StageState::block): the defended vehicle's hit points carried into the second
// room (flag 0x5a1; the turret's end stores them, the vehicle's bind reads their low half)
struct VehicleChaseBlock : StagePart<VehicleChaseBlock> {
    int32_t carriedHp = 0; // +0x00
};

// an s16 point
struct BonePoint {
    int16_t v[3];
};
// exe 0x5e1970(actor, bone, v, out): the offset (x, y, z) through the bone's matrix plus its point
BonePoint bonePointAt(const AnimatedActor& actor, uint32_t bone, int x, int y, int z);
// a 16.16 point of an s16 one (the low words 0)
void fixedOfPoint(const BonePoint& p, int32_t out[3]);

// exe 0x5036e0(actor, point, r): the direction to the 16.16 point, relative to the actor's yaw, within r of straight
// behind (a half turn)
bool facesAwayFrom(const Actor& actor, const int32_t point[3], int r);
bool facesAwayFrom(const Actor& actor, const Fixed16Vec3& point, int r);

// exe 0x5d5340(id, 0, 0): a voice (the sound, flat)
void vehicleVoice(uint16_t id);

// ---- the boss vehicle's and the turret's shared steps ----------------------------------------------------------------
// exe 0x5e1810(actor, bone) + 6 = 0x7fff after the tint 0: the hurt flash of a bone
void flashBone(AnimatedActor& actor, uint32_t bone);
// the player's direction to an s16 point (exe 0x5e3bc0(the player, the point << 16))
int angleFromPlayer(const BonePoint& p);
// the clamped aim step (exe 0x503400 / 0x504a10 / 0x505c00 / 0x505ec0): d = exe 0x5e3d80(yaw + aim, angle); |d| <= 8
// added, else +-8
void aimStep(const Actor& actor, int16_t& aim, int angle);
// the jitter of the spark positions: ((r & 0x1ff) * 0x180 - 0x18000) >> 8 (wide) / ((r & 0xff) * 0x180 - 0xc000) >> 8
int sparkJitter(uint32_t r, bool wide);
// a hit spark (pool 0xa0 global type 0) at the bone point, each coordinate jittered, its size byte (rng & 0x7f) + 0x40
void spawnBoneSpark(AnimatedActor& actor, uint32_t bone, int x, int y, int z, bool wide);
// The fire emitter (static stage type 3 = exe 0x592050, content/first_ruin): its spawn parameters -- its bone mode in
// the spawn byte, its lifetime, the actor it burns on (+0xbc); its sub-state kFireEmitterEnd ends it
struct FireEmitterParams {
    explicit FireEmitterParams(const StaticActor& actor) : FireEmitterParams(const_cast<StaticActor&>(actor)) {}
    explicit FireEmitterParams(StaticActor& actor) : paramA(actor.params[0]), paramB(actor.params[1]), lifetime(actor.params, 2), owner(actor.hitBox.result) {}
    uint8_t& paramA;
    uint8_t& paramB;
    BytesField<uint16_t, 4> lifetime;
    uint32_t& owner;
};
constexpr uint8_t kFireEmitterType = 3;
constexpr uint8_t kFireEmitterEnd = 2;
// a fire emitter on the actor: its variant, bone mode, 16.16 offset, lifetime (exe 0x5e12f0: from the stage's table);
// nullptr when the pool is full
StaticActor* spawnFireEmitter(Actor& owner, uint8_t variant, uint8_t mode, uint32_t x, uint32_t y, uint32_t z,
                              uint16_t lifetime);
// a blast (static global type 7, game/explosions) at the bone point, its size
void spawnBoneBlast(AnimatedActor& actor, uint32_t bone, int x, int y, int z, uint16_t size);
// a hit-owner watcher (pool 0xe0 type 0, late, its parameter word the parent): the owner of a part's hit entries
ControllerActor& spawnHitWatcher(Actor& parent);

} // namespace od::port
