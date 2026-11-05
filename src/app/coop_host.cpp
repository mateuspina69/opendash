// The way from a player's game to their world (see coop_host.hpp).
#include "app/coop_host.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

#include "engine/dat.hpp"

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
    case WorldMessageType::NetGameOver: return true;
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
    : worlds_(std::move(exe), storySave, od::port::saveZenny(readSave(storySave))), save_(readSave(storySave)) {}

void CoopHost::enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state) { worlds_.enter(playerId, room, state); }

void CoopHost::sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    if (type == WorldMessageType::NetJoin) worlds_.netJoin(playerId);
    else if (type == WorldMessageType::GlobalChange) worlds_.globalFromPlayer(playerId, bytes);
    else if (type == WorldMessageType::NetRespawnAsk) worlds_.respawnAsk(playerId);
    else worlds_.netSend(playerId, type, std::move(bytes));
}

void CoopHost::sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) {
    if (type == WorldMessageType::NetAck && bytes.size() == 2) worlds_.netAck(playerId, uint16_t(bytes[0] | bytes[1] << 8));
    else worlds_.netSendLatest(playerId, type, std::move(bytes));
}

std::vector<WorldMessage> CoopHost::take(uint8_t playerId) { return worlds_.takeNet(playerId); }

void CoopHost::sendSave(uint8_t id, uint64_t nowNs) {
    // the host's game: what exists comes from its save, in chunks; the last one is shorter than kSaveChunk
    for (size_t at = 0; at <= save_.size(); at += kSaveChunk) {
        WorldMessage m{WorldMessageType::NetSave};
        m.playerId = id;
        const size_t n = std::min(kSaveChunk, save_.size() - at);
        m.netState.assign(save_.begin() + ptrdiff_t(at), save_.begin() + ptrdiff_t(at + n));
        session_->sendWorld(nowNs, id, true, encodeWorldMessage(m));
    }
}

void CoopHost::fromClient(uint8_t id, const WorldMessage& m, uint64_t nowNs) {
    switch (m.type) {
    case WorldMessageType::NetEnter:
        if (m.netState.size() > 2) {
            if (clients_.insert(id).second) sendSave(id, nowNs);
            worlds_.enter(id, WorldRoom{m.netState[0], m.netState[1]}, std::vector<uint8_t>(m.netState.begin() + 2, m.netState.end()));
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
    case WorldMessageType::NetRevive: worlds_.netSend(id, m.type, m.netState); break;
    case WorldMessageType::NetRespawnAsk: worlds_.respawnAsk(id); break;
    case WorldMessageType::GlobalChange: worlds_.globalFromPlayer(id, m.netState); break;
    case WorldMessageType::NetPlayer:
    case WorldMessageType::NetPlayerAck: worlds_.netSendLatest(id, m.type, m.netState); break;
    default: break;
    }
}

void CoopHost::poll(uint64_t nowNs) {
    worlds_.poll(nowNs);
    if (!session_) return;
    for (const od::net::ReceivedWorld& d : session_->takeWorld())
        if (const auto m = decodeWorldMessage(d.payload)) fromClient(d.fromId, *m, nowNs);
    // a client that has gone is out of its world
    const od::net::SessionStatus status = session_->status(nowNs);
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
    m.netState = {uint8_t(room.stage), uint8_t(room.sub)};
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
