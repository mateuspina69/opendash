// The remote players (see remote_player.hpp).
#include "game/player/remote_player.hpp"

#include <array>
#include <optional>

#include "game/actor_pools.hpp"
#include "game/event.hpp"
#include "game/player/player_animation.hpp"
#include "game/player/player_input.hpp"
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
    // its owner's buttons come as what they do (applyNetArm: the special held): the game's map names their bits here
    copyDefaultButtonMap(playerById(id));
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
    remotePlayerLiftRefused(id); // what it held is let go
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
    if (g_remote[0] && playerPresent(0)) {
        // the scene plays this Mega Man here from now: its channels read its owner's two frame bytes (remotePlayerState),
        // not an animation's keys -- each starts its animation over on its keys (one this world cannot read: the idle)
        Player& p = playerById(0);
        const auto playable = [&](uint8_t anim) {
            return anim != uint8_t(PlayerAnim::None) && (anim < kStageAnims || p.stageAnims != 0) ? anim : uint8_t(PlayerAnim::Idle);
        };
        startUpperAnimation(p, uint8_t(p.channelMix), PlayerAnim(playable(p.upper.anim)), 0);
        startBodyAnimation(p, PlayerAnim(playable(p.body.anim)), 0);
    }
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

namespace {
// a remote player's lift: picked (a class may take it this tick or the next ones), taken by a class (its owner's game
// lifts the actor's copy, however late its state says so), held there
enum class RemoteLift : uint8_t { None, Picked, Taken, Held };
struct RemoteLiftState {
    RemoteLift phase = RemoteLift::None;
    uint16_t ticks = 0;
    bool placeTold = false; // its owner's game was told how the class places it at the hands
};
std::function<void(size_t, const ActorHeader&, int, int32_t, int)> g_remoteCarry;
constexpr uint16_t kLiftPickTicks = 4; // a class takes a lift on its own update: the next tick at the latest
std::array<RemoteLiftState, kPlayerSlots> g_lift{};
std::function<void(size_t, const ActorHeader&, uint8_t)> g_remoteLift;
} // namespace

void remotePlayerLift(size_t id) {
    Player& p = playerById(id);
    if (g_lift[id].phase == RemoteLift::None && p.interactionTarget.none() && pickLiftTargetFor(p) != 0)
        g_lift[id] = {RemoteLift::Picked, kLiftPickTicks};
}

void remotePlayerLiftRefused(size_t id) {
    if (id >= kPlayerSlots || g_lift[id].phase == RemoteLift::None) return;
    g_lift[id] = {};
    playerById(id).interactionTarget.clear(); // the carried class reads it as thrown: let go where it is
}

void setRemoteCarryHook(std::function<void(size_t, const ActorHeader&, int, int32_t, int)> hook) { g_remoteCarry = std::move(hook); }

void remotePlayerCarries(size_t id, const ActorHeader& actor, int x, int32_t yWord, int z) {
    if (id >= kPlayerSlots || !g_remote[id]) return;
    RemoteLiftState& lift = g_lift[id];
    if (lift.phase == RemoteLift::None || lift.placeTold) return;
    lift.placeTold = true;
    if (g_remoteCarry) g_remoteCarry(id, actor, x, yWord, z);
}

void setRemoteLiftHook(std::function<void(size_t, const ActorHeader&, uint8_t)> hook) { g_remoteLift = std::move(hook); }

void remotePlayerLifted(size_t id, const ActorHeader& actor, uint8_t mode) {
    if (id >= kPlayerSlots || !g_remote[id]) return;
    g_lift[id] = {RemoteLift::Taken, 0};
    if (g_remoteLift) g_remoteLift(id, actor, mode);
}

void remotePlayersTalkDone() {
    for (size_t id = 0; id < kPlayerSlots; ++id) {
        if (!g_remote[id] || !playerPresent(id)) continue;
        Player& p = playerById(id);
        RemoteLiftState& lift = g_lift[id];
        // the owner's game holds what it lifted: its target stays; once it held it, its throw (or a drop) lets it go,
        // which the carried class reads as thrown (game/carry carryReleased)
        const bool holds = (p.carryFlags & kCarryHolding) && !(p.carryFlags & (kCarryThrowing | kCarryReleased));
        switch (lift.phase) {
        case RemoteLift::Picked:
            if (lift.ticks == 0 || --lift.ticks == 0) lift = {}; // no class took it
            break;
        case RemoteLift::Taken:
            if (holds) lift.phase = RemoteLift::Held;
            break;
        case RemoteLift::Held:
            if (!holds) lift = {};
            break;
        default: break;
        }
        if (lift.phase == RemoteLift::None && !holds) p.interactionTarget.clear();
    }
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
