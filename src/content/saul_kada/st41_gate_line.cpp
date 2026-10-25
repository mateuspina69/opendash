// The ST41 / ST42 gate line (pool e0 type 0x2a, exe 0x427500; hypothesis names): its yaw ((variant + 4) << 8); 6 posts
// (static type 0x56 variant 2) at its point, pitched 0xc00, moved along the yaw from -0xc80 in 0x500 steps. Each tick:
// the side of the line the player is on (bit 0x800 of the angle difference); crossing it within 0x200 -> flags 0x153 /
// 0x154 cleared, 0x155 set, sound 0x2bf.
#include "content/saul_kada/st41_gate_line.hpp"
#include "content/saul_kada/st41_gate_line_state.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/player/player_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st41_gate_line_state;

namespace {

enum class LineState : uint8_t { Start = 0, Watch = 1 };
constexpr uint8_t kPostType = 0x56, kPostVariant = 2;
constexpr uint32_t kPosts = 6;
constexpr int kFirstPost = -0xc80, kPostSpacing = 0x500;
constexpr int16_t kPostPitch = 0xc00;
constexpr int kCrossingReach = 0x200;
constexpr uint16_t kSideBit = 0x800;
constexpr uint32_t kFlagGateA = 0x153, kFlagGateB = 0x154, kFlagGateCrossed = 0x155;
constexpr uint16_t kCrossSound = 0x2bf;

// exe 0x5e3b40: the planar distance of two s16 points
int distanceXZ(int x0, int z0, int x1, int z1) {
    const int16_t from[3] = {int16_t(x0), 0, int16_t(z0)}, to[3] = {int16_t(x1), 0, int16_t(z1)};
    return planarDistance16(from, to);
}

void gateLine(ControllerActor& actor) {
    LineFields& line = classState<LineFields>(actor);
    if (LineState(actor.state) == LineState::Start) {
        actor.state = uint8_t(LineState::Watch);
        line.yaw = uint16_t((actor.variant + 4u) << 8);
        int z = kFirstPost;
        for (uint32_t k = 0; k < kPosts; ++k, z += kPostSpacing) {
            const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (!slot) break;
            StaticActor& post = staticActorAt(slot);
            post.flags = kActorLive;
            post.type = kPostType;
            post.variant = kPostVariant;
            post.pos.x.whole = line.point.x;
            post.pos.y.whole = line.point.y;
            post.pos.z.whole = line.point.z;
            post.rotation.x = kPostPitch;
            post.rotation.y = 0;
            post.rotation.z = 0;
            moveAlongYaw(post.pos, int16_t(line.yaw), 0, 0, z); // exe 0x5e36f0
        }
    } else if (LineState(actor.state) != LineState::Watch) {
        return;
    }
    // exe 0x5e3c00(player, line) on the 16.16 points of their integer parts, 0x5e3d80(angle, yaw), 0x5e3b40
    const Player& p = player();
    const int d = angleDiff(uint32_t(angleOf(int32_t(uint32_t(int32_t(p.pos.x.whole)) << 16),
                                             int32_t(uint32_t(int32_t(p.pos.z.whole)) << 16),
                                             int32_t(uint32_t(int32_t(line.point.x)) << 16),
                                             int32_t(uint32_t(int32_t(line.point.z)) << 16))),
                            line.yaw);
    if (distanceXZ(p.pos.x.whole, p.pos.z.whole, line.point.x, line.point.z) < kCrossingReach &&
        ((line.side & kSideBit) ^ (uint32_t(d) & kSideBit)) != 0) {
        clearStoryFlag(kFlagGateA);
        clearStoryFlag(kFlagGateB);
        setStoryFlag(kFlagGateCrossed);
        soundPlayFlat(kCrossSound); // exe 0x5d51f0
    }
    line.side = uint16_t(d);
}

} // namespace

void gateLineUpdate(const PlayerEnv&, uint32_t actor) { gateLine(controllerActorAt(actor)); }

} // namespace od::port
