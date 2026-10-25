// The line-of-sight probe (see line_of_sight.hpp).
#include "game/actor_state.hpp"
#include "game/line_of_sight.hpp"
#include "game/line_of_sight_state.hpp"


#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/player/player_state.hpp"
#include "game/stage_state.hpp"
#include "game/vectors.hpp"

using namespace od::port::line_of_sight_state;

namespace od::port {

using namespace line_of_sight_state;

namespace {


constexpr uint32_t kBoxHalfMask = 0xffff, kStepShift = 12, kStepMask = 0xfffffff0;
constexpr uint32_t kTargetBoneShift = 16;
constexpr uint8_t kOwnerLoopBits = 7; // the owner's sweep group and late-loop bits the probe takes
constexpr const char* kSightArea = "line of sight"; // the area of its port-gap errors

// exe 0x418bd0's unchecked owner read: the slot byte +0xf read as a signed byte, its actor's address by exe 0x5e0fa0
// and that address's flags byte. The exe checks nothing: a static owner's slot 0x80..0x9f (or any slot byte >= 0x80)
// gives an address 0x100 slots before its pool's (actor_pools.hpp actorBySlot), inside the exe's .data (always
// mapped), whose first byte is read as the flags. The slots 0..0x7f are their pools' actors; the other 128 bytes are
// the game state's.
uint8_t ownerFlags(int8_t slot) {
    if (slot >= 0) return actorHeaderAt(actorBySlot(uint32_t(slot))).flags;
    return dataScalar<uint8_t>(actorBySlot(uint32_t(slot)));
}

} // namespace

int actorPointOfSlot(uint32_t slot, uint32_t bone, int16_t out[3]) {
    switch (actorSlotClass(slot)) {
    case kPlayerClass: {
        const ShortVec3& p = player().bonePoints[bone];
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
        return 0;
    }
    case kAnimatedClass: {
        const ShortVec4& p = bonePoint(animatedPool()[slot - kAnimatedClass], bone, kSightArea);
        out[0] = p.x;
        out[1] = p.y;
        out[2] = p.z;
        return 0;
    }
    case kStaticClass: {
        const StaticActor& s = staticPool()[slot - kStaticClass];
        out[0] = s.pos.x.whole;
        out[1] = s.pos.y.whole;
        out[2] = s.pos.z.whole;
        return 0;
    }
    default: return -1;
    }
}

void lineOfSightSpawn(const ActorHeader& owner, uint16_t mask, const ShortVec4& point, uint32_t box, uint32_t target,
                      uint8_t& result, uint8_t steps) {
    const uint32_t at = allocActorE0(); // exe 0x5e1440
    if (at == 0) return;
    ControllerActor& probe = controllerActorAt(at);
    LineOfSightFields& f = classState<LineOfSightFields>(probe);
    const uint8_t loop = probe.loopFlags;
    probe.flags = kActorLive;
    probe.type = kLineOfSightType;
    probe.variant = 0;
    probe.loopFlags = uint8_t(loop | (owner.loopFlags & kOwnerLoopBits));
    f.point = point;
    f.collisionMask = int16_t(mask);
    f.boxHalf = int16_t(box & kBoxHalfMask);
    f.stepsPerTick = int8_t(steps);
    f.ownerSlot = int8_t(owner.slot);
    f.result = &result;
    f.targetSlot = uint8_t(target);
    f.targetBone = uint8_t(target >> kTargetBoneShift);
    f.stepLength = int16_t((box >> kStepShift) & kStepMask);
    result = kSightRunning;
}

void lineOfSightUpdate(const PlayerEnv& env, uint32_t actor) {
    ControllerActor& probe = controllerActorAt(actor);
    LineOfSightFields& f = classState<LineOfSightFields>(probe);
    LineOfSightState& s = sight();
    if (probe.variant == 0) {
        // the target point a zeroed local first (content/yosyonke/town_story's transcription: for a slot of no point
        // the target is 0; the exe hands exe 0x5e5840 the global itself, left as it was)
        int16_t t[3] = {};
        actorPointOfSlot(f.targetSlot, f.targetBone, t);
        s.target.x = t[0];
        s.target.y = t[1];
        s.target.z = t[2];
    } else {
        s.target.setWords(f.fixedTargetXY, f.fixedTargetZ);
    }
    const int16_t target[3] = {s.target.x, s.target.y, s.target.z};
    const uint32_t mask = uint32_t(int32_t(f.collisionMask));
    const int16_t h = f.boxHalf;
    s.box = {{}, int16_t(-h), h, int16_t(-h), h, int16_t(-h), h};
    const int32_t step = f.stepLength;
    const int16_t start[3] = {f.point.x, f.point.y, f.point.z};
    int32_t d[3];
    rotateByYaw(angleOf(target, start), 0, 0, -step, d); // exe 0x5e3c00, 0x5e38d0
    for (size_t k = 0; k < 3; ++k) s.step[k] = d[k];
    int32_t n = int32_t(uint32_t(planarDistance16(start, target)) << 4) / step; // exe 0x5e3b40
    uint8_t& result = *f.result;
    if (n == 0) {
        result = kSightReached;
        freeActor(probe); // exe 0x5e15c0
        return;
    }
    int32_t cur[3] = {int32_t(uint32_t(int32_t(start[0])) << 16), int32_t(uint32_t(int32_t(start[1])) << 16),
                      int32_t(uint32_t(int32_t(start[2])) << 16)};
    int32_t prev[3] = {cur[0], cur[1], cur[2]};
    const int32_t ty = int32_t(target[1]) >> (stage().coordinateShift & 0x1f);
    const int32_t sy = (int32_t(uint32_t(ty) << 16) - cur[1]) / n;
    if (n > int32_t(f.stepsPerTick)) n = f.stepsPerTick;
    uint8_t seen = kSightRunning;
    for (;;) {
        const int32_t c = n--;
        if (c == 0) break;
        cur[0] = prev[0] + d[0];
        cur[1] = prev[1] + sy;
        cur[2] = prev[2] + d[2];
        const int16_t to[3] = {int16_t(uint32_t(cur[0]) >> 16), int16_t(uint32_t(cur[1]) >> 16),
                               int16_t(uint32_t(cur[2]) >> 16)};
        const int16_t from[3] = {int16_t(uint32_t(prev[0]) >> 16), int16_t(uint32_t(prev[1]) >> 16),
                                 int16_t(uint32_t(prev[2]) >> 16)};
        CollisionBoxRef box;
        box.pointTo(s.box);
        if (int32_t(testMoveBlocked(env, to, from, box, mask)) < 0) { // exe 0x40f5a0
            seen = kSightBlocked;
            break;
        }
        for (int k = 0; k < 3; ++k) prev[k] = cur[k];
    }
    f.point.x = int16_t(uint32_t(cur[0]) >> 16);
    f.point.y = int16_t(uint32_t(cur[1]) >> 16);
    f.point.z = int16_t(uint32_t(cur[2]) >> 16);
    if (ownerFlags(f.ownerSlot) == 0) { // exe 0x5e0fa0
        freeActor(probe);
        return;
    }
    result = seen;
    if (seen & kSightDone) freeActor(probe);
}

} // namespace od::port
