// The remote players (see remote_player.hpp).
#include "game/player/remote_player.hpp"

#include <array>

#include "game/geometry_scratch.hpp"
#include "game/player/player_pose.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_update.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {
// where each remote player's two animation frame bytes are mapped in the loaded files
constexpr uint32_t kRemoteFrameBytes = 0x7b000000;
std::array<bool, kPlayerSlots> g_remote{};

uint32_t frameBytesOf(size_t id) { return kRemoteFrameBytes + uint32_t(id) * 2; }
} // namespace

void remotePlayerJoin(size_t id, const PlayerSnapshot& s) {
    joinPlayer(id);
    g_remote[id] = true;
    Player& p = playerById(id);
    p.enterState(PlayerState::Playing);
    p.collisionBox = player().collisionBox;
    p.maxHitPoints = player().maxHitPoints;
    p.hitPoints = p.maxHitPoints;
    remotePlayerState(id, s);
}

void remotePlayerState(size_t id, const PlayerSnapshot& s) {
    Player& p = playerById(id);
    p.prevPos = p.pos;
    applyPlayerSnapshot(p, frameBytesOf(id), s);
}

void remotePlayerLeave(size_t id) {
    g_remote[id] = false;
    leavePlayer(id);
}

bool playerIsRemote(size_t id) { return g_remote[id]; }

void remotePlayersUpdate() {
    for (size_t id = 0; id < kPlayerSlots; ++id) {
        if (!g_remote[id] || !playerPresent(id)) continue;
        Player& p = playerById(id);
        const ScopedActingPlayer acting(id);
        // the pose scratch and the matrix stack are the local player's, put back after it (game/ghost)
        const GeometryScratch scratch = geometryScratch();
        const PoseStack stack = poseStack();
        playerPose(p);
        geometryScratch() = scratch;
        poseStack() = stack;
        playerSubmitHit(p);
    }
}

} // namespace od::port
