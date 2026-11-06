// The remote players (see remote_player.hpp).
#include "game/player/remote_player.hpp"

#include <array>
#include <optional>

#include "game/actor_pools.hpp"
#include "game/event.hpp"
#include "game/player/player_interaction.hpp"
#include "game/world_net_state.hpp"
#include "game/world_view.hpp"
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
    // what the world's first player holds (it is this one when the id is 0: read before its slot starts anew)
    const CollisionBoxRef box = player().collisionBox;
    const int16_t maxHitPoints = player().maxHitPoints;
    joinPlayer(id);
    g_remote[id] = true;
    setWorldPlayerCamera(id, {});
    Player& p = playerById(id);
    p.enterState(PlayerState::Playing);
    p.collisionBox = box;
    p.maxHitPoints = maxHitPoints;
    p.hitPoints = p.maxHitPoints;
    remotePlayerState(id, s);
}

void remotePlayerState(size_t id, const PlayerSnapshot& s) {
    Player& p = playerById(id);
    p.prevPos = p.pos;
    applyPlayerSnapshot(p, frameBytesOf(id), s);
}

namespace {
// what the leaving player owned or was held by goes to another player: a grab reads its victim's state, and lets go
template <class Pool> void reassignOwner(Pool& pool, size_t from, size_t to) {
    for (size_t i = 0; i < Pool::size(); ++i)
        if (pool[i].ownerPlayer == from) pool[i].ownerPlayer = uint8_t(to);
}
} // namespace

void remotePlayerLeave(size_t id) {
    g_remote[id] = false;
    setWorldPlayerCamera(id, {});
    leavePlayer(id);
    size_t heir = 0;
    while (heir < kPlayerSlots && !playerPresent(heir)) ++heir;
    if (heir == kPlayerSlots) heir = 0;
    reassignOwner(shotPool(), id, heir);
    reassignOwner(animatedPool(), id, heir);
    reassignOwner(projectilePool(), id, heir);
    reassignOwner(staticPool(), id, heir);
    reassignOwner(effectPool(), id, heir);
    reassignOwner(controllerPool(), id, heir);
}

bool playerIsRemote(size_t id) { return g_remote[id]; }

namespace {
bool g_scene = false;
} // namespace

void remotePlayersSceneStart() {
    g_scene = true;
    g_remote[0] = false;
}

void remotePlayersSceneEnd() {
    g_scene = false;
    g_remote[0] = playerPresent(0);
}

void remotePlayerUse(size_t id) {
    Player& p = playerById(id);
    if (pickTalkTargetFor(p, netClientKeepsAnyActor) != 0) setEventOwner(id);
}

void remotePlayersTalkDone() {
    for (size_t id = 0; id < kPlayerSlots; ++id)
        if (g_remote[id] && playerPresent(id) && playerById(id).carryFlags == 0) playerById(id).interactionTarget.clear();
}

namespace {
std::function<LoadedFileData*(size_t id)> g_remoteFiles;
} // namespace

void setRemotePlayerFiles(std::function<LoadedFileData*(size_t id)> filesOf) { g_remoteFiles = std::move(filesOf); }

LoadedFileData* remotePlayerFiles(size_t id) {
    return id < kPlayerSlots && g_remote[id] && g_remoteFiles ? g_remoteFiles(id) : nullptr;
}

void remotePlayersUpdate() {
    for (size_t id = 0; id < kPlayerSlots; ++id) {
        if (!g_remote[id] || !playerPresent(id)) continue;
        Player& p = playerById(id);
        const ScopedActingPlayer acting(id);
        // the pose reads the look's data: this player's own files (their weapon, their costume) when they are known
        LoadedFileData* files = g_remoteFiles ? g_remoteFiles(id) : nullptr;
        // (whose files are not built yet keeps the bones it has: posed on this machine's own it would read a look's
        // data they do not hold)
        if (files || !g_remoteFiles) {
            std::optional<ScopedLoadedFiles> bound;
            if (files) bound.emplace(*files);
            // the pose scratch and the matrix stack are the local player's, put back after it (game/ghost)
            const GeometryScratch scratch = geometryScratch();
            const PoseStack stack = poseStack();
            playerPose(p);
            geometryScratch() = scratch;
            poseStack() = stack;
        }
        if (p.hitPoints >= 0 && !g_scene) playerSubmitHit(p); // one who is down is out of the fight
    }
}

} // namespace od::port
