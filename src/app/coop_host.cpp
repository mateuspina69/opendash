// The way from a player's game to their world (see coop_host.hpp).
#include "app/coop_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <fstream>
#include <utility>

#include "engine/dat.hpp"
#include "game/save_image.hpp"

namespace od::app {

namespace {

constexpr size_t kSaveChunk = 1000; // a NetSave's bytes: the rest of the datagram is the headers

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
    case WorldMessageType::NetMoveTo:
    case WorldMessageType::NetHold:
    case WorldMessageType::NetGateAsk:
    case WorldMessageType::NetGateLeave:
    case WorldMessageType::NetGateForce:
    case WorldMessageType::NetNotice:
    case WorldMessageType::NetBossFight:
    case WorldMessageType::NetWarp:
    case WorldMessageType::NetGateGlow:
    case WorldMessageType::NetReload:
    case WorldMessageType::NetAlert:
    case WorldMessageType::NetRoomPlayer: return true;
    default: return false;
    }
}

} // namespace

namespace {
std::vector<uint8_t> readSave(const std::filesystem::path& storySave) {
    return !storySave.empty() && std::filesystem::exists(storySave) ? od::readFile(storySave) : std::vector<uint8_t>{};
}
} // namespace

CoopHost::CoopHost(std::filesystem::path exe, std::filesystem::path storySave)
    : worlds_(exe, storySave, od::port::saveZenny(readSave(storySave))), save_(readSave(storySave)), written_(save_),
      savePath_(storySave) {
    lastSavePath_ = exe.parent_path() / "opendash-session-save.dat";
}

void CoopHost::hostSaved(std::span<const uint8_t> save) { written_.assign(save.begin(), save.end()); }

const std::vector<uint8_t>& CoopHost::liveSave() {
    if (live_)
        if (std::optional<std::vector<uint8_t>> now = live_(); now && now->size() == od::port::kSaveSize) save_ = std::move(*now);
    return save_;
}

void CoopHost::liveSaveFile() {
    const std::vector<uint8_t>& save = liveSave();
    if (save.empty()) return;
    std::ofstream out(lastSavePath_, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(save.data()), std::streamsize(save.size()));
    if (out) worlds_.setSavePath(lastSavePath_);
}

void CoopHost::hostLoaded(std::span<const uint8_t> save, uint64_t nowNs) {
    save_.assign(save.begin(), save.end());
    written_ = save_;
    hostPlaying_ = true;
    {
        std::ofstream out(lastSavePath_, std::ios::binary);
        out.write(reinterpret_cast<const char*>(save_.data()), std::streamsize(save_.size()));
    }
    worlds_.reloadFrom(save_, lastSavePath_);
    SDL_Log("coordinator: the session starts from the host's game (ST%02X sub %d)", unsigned(save_.size() > 0x129 ? save_[0x128] : 0),
            int(save_.size() > 0x129 ? save_[0x129] : 0));
    if (!session_) return;
    for (const uint8_t id : told_) {
        stale_.insert(id);
        WorldMessage m{WorldMessageType::NetReload};
        m.playerId = id;
        session_->sendWorld(nowNs, id, true, encodeWorldMessage(m));
        sendSave(id, nowNs);
        placeNext_.insert(id);
    }
}

std::optional<std::vector<uint8_t>> CoopHost::takeReload() { return std::exchange(reload_, std::nullopt); }

void CoopHost::enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state) {
    liveSaveFile(); // a world started now starts from the host's state as it is
    worlds_.enter(playerId, room, state);
}

void CoopHost::sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    if (type == WorldMessageType::NetJoin) worlds_.netJoin(playerId);
    else if (type == WorldMessageType::GlobalChange) worlds_.globalFromPlayer(playerId, bytes);
    else if (type == WorldMessageType::NetRespawnAsk) worlds_.respawnAsk(playerId);
    else if (type == WorldMessageType::NetGateAsk) worlds_.gateAsk(playerId, bytes);
    else if (type == WorldMessageType::NetGateLeave) worlds_.gateLeave(playerId);
    else if (type == WorldMessageType::NetGateForce) worlds_.gateForce(SDL_GetTicksNS());
    else worlds_.netSend(playerId, type, std::move(bytes));
}

void CoopHost::sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    if (type == WorldMessageType::NetAck && bytes.size() == 2) worlds_.netAck(playerId, uint16_t(bytes[0] | bytes[1] << 8));
    else worlds_.netSendLatest(playerId, type, std::move(bytes));
}

std::vector<WorldMessage> CoopHost::take(uint8_t playerId) { return worlds_.takeNet(playerId); }

void CoopHost::sendSave(uint8_t id, uint64_t nowNs) {
    // the host's game: what exists is its state now, in chunks; the last one is shorter than kSaveChunk
    const std::vector<uint8_t>& save = liveSave();
    for (size_t at = 0; at <= save.size(); at += kSaveChunk) {
        WorldMessage m{WorldMessageType::NetSave};
        m.playerId = id;
        const size_t n = std::min(kSaveChunk, save.size() - at);
        m.netState.assign(save.begin() + ptrdiff_t(at), save.begin() + ptrdiff_t(at + n));
        session_->sendWorld(nowNs, id, true, encodeWorldMessage(m));
    }
}

void CoopHost::fromClient(uint8_t id, const WorldMessage& m, uint64_t nowNs) {
    if (!hostPlaying_) return; // no world is anyone's before the host's game is on
    if (!told_.contains(id)) return; // not given the session's game yet: what it sends is of its own
    if (stale_.contains(id)) {
        // a game from before the session's save: nothing of it counts until it enters the save's room
        const bool saveRoom = m.type == WorldMessageType::NetEnter && m.netState.size() > 3 && save_.size() > 0x129 &&
                              m.netState[0] == save_[0x128] && m.netState[1] == save_[0x129];
        if (!saveRoom) return;
        stale_.erase(id);
    }
    switch (m.type) {
    case WorldMessageType::NetEnter:
        if (m.netState.size() > 3) {
            const bool first = clients_.insert(id).second, stays = m.netState[2] != 0;
            if (told_.insert(id).second) sendSave(id, nowNs);
            const bool place = first || placeNext_.erase(id) != 0;
            liveSaveFile(); // a world started now starts from the host's state as it is
            worlds_.enter(id, WorldRoom{m.netState[0], m.netState[1]}, std::vector<uint8_t>(m.netState.begin() + 3, m.netState.end()));
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
    case WorldMessageType::NetEventEnd:
    case WorldMessageType::NetSpeaker:
    case WorldMessageType::NetRevive:
    case WorldMessageType::NetScenePad: worlds_.netSend(id, m.type, m.netState); break;
    case WorldMessageType::NetRespawnAsk: worlds_.respawnAsk(id); break;
    case WorldMessageType::NetGateAsk: worlds_.gateAsk(id, m.netState); break;
    case WorldMessageType::NetGateLeave: worlds_.gateLeave(id); break;
    case WorldMessageType::GlobalChange: worlds_.globalFromPlayer(id, m.netState); break;
    case WorldMessageType::NetPlayer:
    case WorldMessageType::NetPlayerAck: worlds_.netSendLatest(id, m.type, m.netState); break;
    default: break;
    }
}

void CoopHost::poll(uint64_t nowNs) {
    worlds_.poll(nowNs);
    if (worlds_.takeEveryoneDown()) {
        // the worlds start from the file: the host's last save (a game over's way back)
        {
            std::ofstream out(lastSavePath_, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(written_.data()), std::streamsize(written_.size()));
        }
        worlds_.reloadFrom(written_, lastSavePath_);
        save_ = written_;
        reload_ = written_;
        if (session_)
            for (const uint8_t id : clients_) {
                stale_.insert(id);
                WorldMessage m{WorldMessageType::NetReload};
                m.playerId = id;
                session_->sendWorld(nowNs, id, true, encodeWorldMessage(m));
                sendSave(id, nowNs);
            }
    }
    if (!session_) return;
    for (const od::net::ReceivedWorld& d : session_->takeWorld())
        if (const auto m = decodeWorldMessage(d.payload)) fromClient(d.fromId, *m, nowNs);
    // a client that has gone is out of its world
    const od::net::SessionStatus status = session_->status(nowNs);
    for (const od::net::PlayerStatus& p : status.players) {
        if (p.playerId < WorldHost::kPlayers) worlds_.setPlayerName(p.playerId, p.name);
        // a game that connects is told the session's save once the host's game is on: at its title it starts from it
        // (the host back at its title or lobby: no game to give; the one that connects waits in its lobby)
        if (hostPlaying_ && p.playerId != 0 && p.playerId < WorldHost::kPlayers && !told_.contains(p.playerId) &&
            (!live_ || live_().has_value()) && !liveSave().empty()) {
            told_.insert(p.playerId);
            stale_.insert(p.playerId);
            sendSave(p.playerId, nowNs);
        }
    }
    const auto gone = [&](uint8_t id) {
        return std::none_of(status.players.begin(), status.players.end(), [&](const od::net::PlayerStatus& p) { return p.playerId == id; });
    };
    std::erase_if(told_, gone);
    std::erase_if(stale_, gone);
    for (auto it = clients_.begin(); it != clients_.end();) {
        const bool there = std::any_of(status.players.begin(), status.players.end(), [&](const od::net::PlayerStatus& p) { return p.playerId == *it; });
        if (there) {
            ++it;
        } else {
            worlds_.leave(*it);
            it = clients_.erase(it);
        }
    }
    for (const uint8_t id : clients_)
        for (WorldMessage& m : worlds_.takeNet(id)) session_->sendWorld(nowNs, id, isReliable(m.type), encodeWorldMessage(m));
}

std::optional<uint8_t> SessionPipe::playerId() const {
    const od::net::SessionStatus s = session_.status(nowNs_);
    return s.connection == od::net::Connection::Connected ? std::optional(s.localId) : std::nullopt;
}

void SessionPipe::enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state) {
    WorldMessage m{WorldMessageType::NetEnter};
    m.playerId = playerId;
    m.netState = {uint8_t(room.stage), uint8_t(room.sub), uint8_t(stays_ ? 1 : 0)};
    m.netState.insert(m.netState.end(), state.begin(), state.end());
    session_.sendWorld(nowNs_, 0, true, encodeWorldMessage(m));
}

void SessionPipe::sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    WorldMessage m{type};
    m.playerId = playerId;
    m.netState = std::move(bytes);
    session_.sendWorld(nowNs_, 0, true, encodeWorldMessage(m));
}

void SessionPipe::sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    WorldMessage m{type};
    m.playerId = playerId;
    if (type == WorldMessageType::NetAck && bytes.size() == 2) m.netSequence = uint16_t(bytes[0] | bytes[1] << 8);
    else m.netState = std::move(bytes);
    session_.sendWorld(nowNs_, 0, false, encodeWorldMessage(m));
}

void SessionPipe::poll(uint64_t nowNs) {
    nowNs_ = nowNs;
    for (const od::net::ReceivedWorld& d : session_.takeWorld())
        if (auto m = decodeWorldMessage(d.payload)) inbox_.push_back(std::move(*m));
}

std::vector<WorldMessage> SessionPipe::take(uint8_t) { return std::exchange(inbox_, {}); }

} // namespace od::app
