// The way from a player's game to their world (see coop_host.hpp).
#include "app/coop_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

#include "engine/dat.hpp"
#include "game/save_image.hpp"

namespace od::app {

namespace {

bool isReliable(WorldMessageType t) {
    switch (t) {
    case WorldMessageType::NetEvents:
    case WorldMessageType::NetHit:
    case WorldMessageType::NetShotHit:
    case WorldMessageType::NetJoin:
    case WorldMessageType::NetLeave:
    case WorldMessageType::NetFire:
    case WorldMessageType::NetReport:
    case WorldMessageType::NetEnter:
    case WorldMessageType::NetSave:
    case WorldMessageType::NetEquip:
    case WorldMessageType::GlobalChange:
    case WorldMessageType::NetGift:
    case WorldMessageType::NetUse:
    case WorldMessageType::NetSceneLeft:
    case WorldMessageType::NetLift:
    case WorldMessageType::NetLifted:
    case WorldMessageType::NetStageBanks:
    case WorldMessageType::NetStoryMove:
    case WorldMessageType::NetEventStart:
    case WorldMessageType::NetEventEnd:
    case WorldMessageType::NetSpeaker:
    case WorldMessageType::NetRespawnAsk:
    case WorldMessageType::NetRespawnAt:
    case WorldMessageType::NetRevive:
    case WorldMessageType::NetRevived:
    case WorldMessageType::NetGameOver:
    case WorldMessageType::NetPresent:
    case WorldMessageType::NetSceneStart:
    case WorldMessageType::NetSceneEnd:
    case WorldMessageType::NetScenePad:
    case WorldMessageType::NetEventPad:
    case WorldMessageType::NetMoveTo:
    case WorldMessageType::NetHold:
    case WorldMessageType::NetGateAsk:
    case WorldMessageType::NetGateLeave:
    case WorldMessageType::NetGateForce:
    case WorldMessageType::NetNotice:
    case WorldMessageType::NetBossFight:
    case WorldMessageType::NetWarp:
    case WorldMessageType::NetScreen:
    case WorldMessageType::NetReload:
    case WorldMessageType::NetAlert:
    case WorldMessageType::NetRoomPlayer: return true;
    default: return false;
    }
}

// a message through the session, to its player `toId` (from a client: to the host). What must arrive goes whole, in
// parts when one datagram does not hold it; what the next one replaces is one datagram
static_assert(kWorldMessageBytes <= od::net::kMaxWorldBytes, "a world message's datagram share fits the session's bundle");
void sendWorld(od::net::Session& session, uint64_t, uint8_t toId, const WorldMessage& m, bool reliable) {
    if (reliable) {
        for (const std::vector<uint8_t>& part : encodeWorldMessageParts(m)) session.sendWorld(toId, true, part);
        return;
    }
    const std::vector<uint8_t> bytes = encodeWorldMessage(m);
    if (bytes.size() > kWorldMessageBytes) {
        SDL_Log("session: a message of type %u is %zu bytes, more than one datagram holds: not sent", unsigned(m.type), bytes.size());
        return;
    }
    session.sendWorld(toId, false, bytes);
}

} // namespace

namespace {
std::vector<uint8_t> readSave(const std::filesystem::path& storySave) {
    return !storySave.empty() && std::filesystem::exists(storySave) ? od::readFile(storySave) : std::vector<uint8_t>{};
}
} // namespace

CoopHost::CoopHost(std::filesystem::path exe, std::filesystem::path storySave)
    : worlds_(exe, storySave, od::port::saveZenny(readSave(storySave))), save_(readSave(storySave)), written_(save_) {
    relay_ = std::thread([this] { relayLoop(); });
}

CoopHost::~CoopHost() {
    stop_ = true;
    relay_.join();
}

void CoopHost::relayLoop() {
    // a datagram wakes it at once; with none it still turns this often, for what goes by time (the resends, the
    // keep-alives, a gate's countdown, a simulated link's delays)
    constexpr uint64_t kWaitNs = 4'000'000;
    std::vector<uintptr_t> handles;
    while (!stop_) {
        {
            const std::lock_guard lock(mutex_);
            handles = worlds_.socketHandles();
            if (session_) handles.push_back(session_->socketHandle());
        }
        od::net::UdpSocket::waitAnyReadable(handles, kWaitNs);
        if (stop_) break;
        const std::lock_guard lock(mutex_);
        relayTurn(SDL_GetTicksNS());
    }
}

void CoopHost::hostSaved(std::span<const uint8_t> save) {
    const std::lock_guard lock(mutex_);
    written_.assign(save.begin(), save.end());
}

const std::vector<uint8_t>& CoopHost::liveSave() {
    if (live_)
        if (std::optional<std::vector<uint8_t>> now = live_(); now && now->size() == od::port::kSaveSize) save_ = std::move(*now);
    return save_;
}

void CoopHost::hostBound(WorldRoom room) {
    const std::lock_guard lock(mutex_);
    const std::vector<uint8_t>& save = liveSave();
    if (save.empty()) bound_.reset();
    else bound_ = Bound{room, save};
}

void CoopHost::liveSaveForWorlds(WorldRoom room) {
    const std::vector<uint8_t>& live = liveSave();
    const std::vector<uint8_t>& save = bound_ && bound_->room == room ? bound_->save : live;
    if (!save.empty()) worlds_.setSave(save);
}

void CoopHost::hostLoaded(std::span<const uint8_t> save, uint64_t nowNs) {
    const std::lock_guard lock(mutex_);
    nowNs = std::max(nowNs, lastRelayNs_);
    save_.assign(save.begin(), save.end());
    written_ = save_;
    hostPlaying_ = true;
    worlds_.reloadFrom(save_);
    SDL_Log("coordinator: the session starts from the host's game (ST%02X sub %d)", unsigned(save_.size() > 0x129 ? save_[0x128] : 0),
            int(save_.size() > 0x129 ? save_[0x129] : 0));
    if (!session_) return;
    for (const uint8_t id : told_) {
        stale_.insert(id);
        WorldMessage m{WorldMessageType::NetReload};
        m.playerId = id;
        sendWorld(*session_, nowNs, id, m, true);
        sendSave(id, nowNs);
        placeNext_.insert(id);
    }
    session_->flush(nowNs);
}

std::optional<std::vector<uint8_t>> CoopHost::takeReload() {
    const std::lock_guard lock(mutex_);
    return std::exchange(reload_, std::nullopt);
}

void CoopHost::enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state, const std::vector<uint8_t>& banks) {
    const std::lock_guard lock(mutex_);
    liveSaveForWorlds(room); // a world started now starts from the host's state as it is
    worlds_.enter(playerId, room, state, banks, nextEntry(playerId));
}

void CoopHost::sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    const std::lock_guard lock(mutex_);
    if (type == WorldMessageType::NetJoin) worlds_.netJoin(playerId);
    else if (type == WorldMessageType::GlobalChange) worlds_.globalFromPlayer(playerId, bytes);
    else if (type == WorldMessageType::NetRespawnAsk) worlds_.respawnAsk(playerId);
    else if (type == WorldMessageType::NetGateAsk) worlds_.gateAsk(playerId, bytes);
    else if (type == WorldMessageType::NetStoryMove) worlds_.storyMove(playerId, bytes);
    else if (type == WorldMessageType::NetGateLeave) worlds_.gateLeave(playerId);
    else if (type == WorldMessageType::NetGateForce) worlds_.gateForce(SDL_GetTicksNS());
    else worlds_.netSend(playerId, type, std::move(bytes));
}

void CoopHost::sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    const std::lock_guard lock(mutex_);
    if (type == WorldMessageType::NetAck && bytes.size() == 2) worlds_.netAck(playerId, uint16_t(bytes[0] | bytes[1] << 8));
    else worlds_.netSendLatest(playerId, type, std::move(bytes));
}

std::vector<WorldMessage> CoopHost::take(uint8_t playerId) {
    const std::lock_guard lock(mutex_);
    std::vector<WorldMessage> in = worlds_.takeNet(playerId);
    dropStale(in);
    return in;
}

void CoopHost::sendSave(uint8_t id, uint64_t nowNs) {
    // the host's game: what exists is its state now
    const std::vector<uint8_t>& save = liveSave();
    if (save.size() > 0x129) sentRoom_[id] = {save[0x128], save[0x129]};
    WorldMessage m{WorldMessageType::NetSave};
    m.playerId = id;
    m.netState = save;
    sendWorld(*session_, nowNs, id, m, true);
}

void CoopHost::fromClientOrHold(uint8_t id, WorldMessage m, uint64_t nowNs) {
    std::vector<WorldMessage>& held = held_[id];
    if (held.empty() && m.type != WorldMessageType::NetEnter) {
        fromClient(id, m, nowNs);
        return;
    }
    // (of what the next one replaces, the newest is enough: the game's thread may be long in coming)
    if (!isReliable(m.type) && !held.empty() && held.back().type == m.type) held.back() = std::move(m);
    else held.push_back(std::move(m));
}

void CoopHost::fromClient(uint8_t id, const WorldMessage& m, uint64_t nowNs) {
    if (!hostPlaying_) return; // no world is anyone's before the host's game is on
    if (!told_.contains(id)) return; // not given the session's game yet: what it sends is of its own
    if (stale_.contains(id)) {
        // a game from before the session's save: nothing of it counts until it enters the room of the save it was sent
        // (the host may have gone on to another room since)
        const auto sent = sentRoom_.find(id);
        const bool saveRoom = m.type == WorldMessageType::NetEnter && m.netState.size() > 3 && sent != sentRoom_.end() &&
                              m.netState[0] == sent->second.first && m.netState[1] == sent->second.second;
        if (!saveRoom) return;
        stale_.erase(id);
    }
    switch (m.type) {
    case WorldMessageType::NetEnter:
        if (m.netState.size() > 3) {
            const bool first = clients_.insert(id).second, stays = m.netState[2] != 0;
            if (told_.insert(id).second) sendSave(id, nowNs);
            const bool place = first || placeNext_.erase(id) != 0;
            liveSaveForWorlds(WorldRoom{m.netState[0], m.netState[1]}); // a world started now starts from the host's state as it is
            // {stage, sub, stays, the banks' length, the banks, the state}
            const size_t banksLen = m.netState[3];
            if (m.netState.size() < 4 + banksLen) break;
            const std::vector<uint8_t> banks(m.netState.begin() + 4, m.netState.begin() + ptrdiff_t(4 + banksLen));
            worlds_.enter(id, WorldRoom{m.netState[0], m.netState[1]}, std::vector<uint8_t>(m.netState.begin() + ptrdiff_t(4 + banksLen), m.netState.end()), banks,
                          m.entry);
            if (place && !stays) worlds_.placeNextToHost(id); // joining, or the session started over: next to the host
        }
        break;
    case WorldMessageType::NetJoin: worlds_.netJoin(id); break;
    case WorldMessageType::NetAck: worlds_.netAck(id, m.netSequence); break;
    case WorldMessageType::NetLeave: worlds_.leave(id); break;
    case WorldMessageType::NetFire:
    case WorldMessageType::NetReport:
    case WorldMessageType::NetEquip:
    case WorldMessageType::NetUse:
    case WorldMessageType::NetSceneLeft:
    case WorldMessageType::NetLift:
    case WorldMessageType::NetEventEnd:
    case WorldMessageType::NetSpeaker:
    case WorldMessageType::NetRevive:
    case WorldMessageType::NetScenePad:
    case WorldMessageType::NetEventPad: worlds_.netSend(id, m.type, m.netState); break;
    case WorldMessageType::NetRespawnAsk: worlds_.respawnAsk(id); break;
    case WorldMessageType::NetGateAsk: worlds_.gateAsk(id, m.netState); break;
    case WorldMessageType::NetStoryMove: worlds_.storyMove(id, m.netState); break;
    case WorldMessageType::NetGateLeave: worlds_.gateLeave(id); break;
    case WorldMessageType::GlobalChange: worlds_.globalFromPlayer(id, m.netState); break;
    case WorldMessageType::NetPlayer:
    case WorldMessageType::NetPlayerAck: worlds_.netSendLatest(id, m.type, m.netState); break;
    default: break;
    }
}

void CoopHost::poll(uint64_t nowNs) {
    const std::lock_guard lock(mutex_);
    // (the caller read its clock before it had the lock: the relay may have turned since, and time does not go back)
    nowNs = std::max(nowNs, lastRelayNs_);
    relayTurn(nowNs);
    gameTurn(nowNs);
}

void CoopHost::gameTurn(uint64_t nowNs) {
    if (worlds_.takeEveryoneDown()) {
        // the worlds start from the host's last save (a game over's way back)
        worlds_.reloadFrom(written_);
        save_ = written_;
        reload_ = written_;
        if (session_)
            for (const uint8_t id : clients_) {
                stale_.insert(id);
                WorldMessage m{WorldMessageType::NetReload};
                m.playerId = id;
                sendWorld(*session_, nowNs, id, m, true);
                sendSave(id, nowNs);
            }
    }
    if (!session_) return;
    for (const od::net::PeerInfo& p : peers_) {
        // a game that connects is told the session's save once the host's game is on: at its title it starts from it
        // (the host back at its title or lobby: no game to give; the one that connects waits in its lobby)
        if (hostPlaying_ && p.playerId != 0 && p.playerId < WorldHost::kPlayers && !told_.contains(p.playerId) &&
            (!live_ || live_().has_value()) && !liveSave().empty()) {
            told_.insert(p.playerId);
            stale_.insert(p.playerId);
            sendSave(p.playerId, nowNs);
        }
    }
    // what the clients sent that waited for this thread, each one's in its order
    for (size_t id = 0; id < held_.size(); ++id)
        for (const WorldMessage& m : std::exchange(held_[id], {})) fromClient(uint8_t(id), m, nowNs);
    session_->flush(nowNs);
}

void CoopHost::relayTurn(uint64_t nowNs) {
    if (lastRelayNs_ != 0 && nowNs > lastRelayNs_) relayGapMaxNs_ = std::max(relayGapMaxNs_, nowNs - lastRelayNs_);
    lastRelayNs_ = nowNs;
    if (session_) session_->poll(nowNs, false); // what the clients sent, read now (what goes to them leaves below, at once)
    worlds_.poll(nowNs);
    if (!session_) return;
    for (od::net::ReceivedWorld& d : session_->takeWorld()) {
        if (d.fromId >= parts_.size()) continue;
        if (auto m = decodeWorldMessage(d.payload))
            if (auto whole = parts_[d.fromId].take(std::move(*m))) fromClientOrHold(d.fromId, std::move(*whole), nowNs);
    }
    // who is in the session: asked again only when someone joined or left
    if (!peersKnown_ || peersAt_ != session_->membership()) {
        peersAt_ = session_->membership();
        peersKnown_ = true;
        peers_ = session_->peers();
        for (const od::net::PeerInfo& p : peers_)
            if (p.playerId < WorldHost::kPlayers) worlds_.setPlayerName(p.playerId, p.name);
    }
    // a client that has gone is out of its world
    const auto gone = [&](uint8_t id) {
        return std::none_of(peers_.begin(), peers_.end(), [&](const od::net::PeerInfo& p) { return p.playerId == id; });
    };
    std::erase_if(told_, gone);
    std::erase_if(stale_, gone);
    for (size_t id = 0; id < parts_.size(); ++id)
        if (gone(uint8_t(id))) {
            parts_[id] = {}; // (a message it left half sent is not the start of its next one's)
            held_[id].clear();
        }
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (!gone(*it)) {
            ++it;
        } else {
            worlds_.leave(*it);
            it = clients_.erase(it);
        }
    }
    for (const uint8_t id : clients_)
        for (WorldMessage& m : worlds_.takeNet(id)) sendWorld(*session_, nowNs, id, m, isReliable(m.type));
    // what the worlds sent their players leaves now, a datagram a player -- once a world's tick has come whole (its
    // datagrams arrive one after the other, and each wakes the relay)
    if (!worlds_.midBurst()) session_->flush(nowNs);
}

void WorldPipe::dropStale(std::vector<WorldMessage>& in) {
    const size_t before = in.size();
    std::erase_if(in, [&](const WorldMessage& m) { return stale(m); });
    if (in.size() != before) SDL_Log("own world: %zu message(s) of a room left, not played", before - in.size());
}

std::optional<uint8_t> SessionPipe::playerId() const { return session_.localPlayer(); }

void SessionPipe::enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state, const std::vector<uint8_t>& banks) {
    WorldMessage m{WorldMessageType::NetEnter};
    m.playerId = playerId;
    m.entry = nextEntry(playerId);
    m.netState = {uint8_t(room.stage), uint8_t(room.sub), uint8_t(stays_ ? 1 : 0), uint8_t(banks.size())};
    m.netState.insert(m.netState.end(), banks.begin(), banks.end());
    m.netState.insert(m.netState.end(), state.begin(), state.end());
    sendWorld(session_, nowNs_, 0, m, true);
}

void SessionPipe::sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    WorldMessage m{type};
    m.playerId = playerId;
    m.netState = std::move(bytes);
    sendWorld(session_, nowNs_, 0, m, true);
}

void SessionPipe::sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    WorldMessage m{type};
    m.playerId = playerId;
    if (type == WorldMessageType::NetAck && bytes.size() == 2) m.netSequence = uint16_t(bytes[0] | bytes[1] << 8);
    else m.netState = std::move(bytes);
    sendWorld(session_, nowNs_, 0, m, false);
}

void SessionPipe::poll(uint64_t nowNs) {
    nowNs_ = nowNs;
    session_.poll(nowNs, false); // what the host sent, read now (what this game sends leaves with its flush)
    if (!playerId()) parts_ = {}; // (the session was lost: what the host had half sent is not the start of its next one's)
    for (const od::net::ReceivedWorld& d : session_.takeWorld())
        if (auto m = decodeWorldMessage(d.payload))
            if (auto whole = parts_.take(std::move(*m))) inbox_.push_back(std::move(*whole));
}

std::vector<WorldMessage> SessionPipe::take(uint8_t) {
    std::vector<WorldMessage> in = std::exchange(inbox_, {});
    dropStale(in);
    return in;
}

} // namespace od::app
