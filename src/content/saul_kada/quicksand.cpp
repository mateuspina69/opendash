// The sand pit (pool e0 type 0x23: ST48 sub 1, ST5B, exe 0x427290; hypothesis names): inside its radius around its
// point the player (on the ground) is pulled toward the point (his drag 0xc, the pull of the player's status: its
// strength and radius the pit's) and sand sprays (skid sparks variant 0x1c) from his feet while he moves.
#include "content/saul_kada/quicksand.hpp"
#include "content/saul_kada/quicksand_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/player/players.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"
#include "game/skid_sparks.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace quicksand_state;

namespace {

constexpr int8_t kPitDrag = 0xc;
constexpr uint8_t kSprayVariant = 0x1c;
constexpr uint32_t kFootBones = 3, kFirstFootBone = 0xb; // the feet's bone points: 0xb + foot * 3

void quicksandOn(const PitParams& pit, Player& p) {
    const int16_t playerPoint[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
    const int16_t pitPoint[3] = {pit.point.x, 0, pit.point.z};
    if (planarDistance16(playerPoint, pitPoint) >= int(pit.radius) || p.airborne != 0) return; // exe 0x5e3b40
    p.drag = kPitDrag;
    p.pulled = 1;
    p.pullX = pit.point.x;
    p.pullY = pit.point.y;
    p.pullZ = pit.point.z;
    p.pullStrength = int16_t(pit.pullStrength.get());
    p.pullRadius = int16_t(pit.radius);
    // the sand spray: the player's speed this tick (exe 0x5e3b00 of his position and his previous one)
    const int speed = planarDistance(p.pos, p.prevPos);
    int foot;
    if (speed > 8 || p.sub() == PlayerSub::Run || p.sub() == PlayerSub::Skate) {
        foot = (gameState().frame & 1) + 1;
    } else {
        if (speed <= 4 || !(gameState().frame & 1)) return;
        foot = int(rngStep() & 1) + 1;
    }
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& spray = effectActorAt(slot);
    spray.runFlags |= kActorWarpKept;
    spray.flags = kActorLive;
    spray.type = typeByte(GlobalEffectType::SkidSparks);
    spray.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    spray.variant = kSprayVariant;
    // the foot's bone point (the bone points are << the stage shift), back to world units. Foot 1 is bone 14 (the
    // right foot); foot 2's index, 17, is past the 15 bone points (exe 0xa721b0 + 17 * 8 = 0xa72238): the exe reads the
    // words after the player struct there, x and y two words nothing writes (0) and z the sound queue's count
    // (0xa7223c), so that spray starts near the world's origin
    ShortVec3 footPoint;
    const size_t bone = size_t(foot) * kFootBones + kFirstFootBone;
    if (bone < kPlayerBones) footPoint = p.bonePoints[bone];
    else footPoint = ShortVec3{0, 0, int16_t(uint16_t(soundQueue().count))};
    const int shift = stage().coordinateShift;
    spray.pos.x = int16_t(footPoint.x >> shift);
    spray.pos.y = int16_t(footPoint.y >> shift);
    spray.pos.z = int16_t(footPoint.z >> shift);
    classState<SkidSparkFields>(effectActorAt(slot)).yaw = uint16_t(rngStep() & 0xfff);
}

void quicksand(ControllerActor& actor) {
    const PitParams& pit = classState<PitParams>(actor);
    forEachPlayer([&](Player& p) { quicksandOn(pit, p); });
}

} // namespace

void quicksandUpdate(const PlayerEnv&, uint32_t actor) { quicksand(controllerActorAt(actor)); }

} // namespace od::port
