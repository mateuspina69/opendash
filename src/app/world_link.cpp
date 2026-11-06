// The link between the coordinator and a world process (see world_link.hpp).
#include "app/world_link.hpp"

#include <algorithm>

#include "net/wire.hpp"

namespace od::app {

namespace {
// a datagram: its kind, then (Reliable) the sequence and the message, (Ack) the sequence, (Latest) the message
enum class Kind : uint8_t { Reliable = 1, Ack = 2, Latest = 3 };
} // namespace

std::vector<uint8_t> encodeBrief(const WorldBrief& b) {
    od::net::ByteWriter w;
    w.u8(b.bossFight ? 1 : 0);
    w.u8(uint8_t(b.players.size()));
    for (const WorldBrief::Entry& e : b.players) {
        w.u8(e.id);
        w.i16(e.x);
        w.i16(e.y);
        w.i16(e.z);
        w.i16(e.yaw);
        w.i16(e.life);
        w.u8(e.downed ? 1 : 0);
    }
    return w.take();
}

std::optional<WorldBrief> decodeBrief(std::span<const uint8_t> bytes) {
    od::net::ByteReader r(bytes);
    WorldBrief b;
    b.bossFight = r.u8() != 0;
    const size_t n = r.u8();
    for (size_t i = 0; i < n; ++i) {
        WorldBrief::Entry e;
        e.id = r.u8();
        e.x = r.i16();
        e.y = r.i16();
        e.z = r.i16();
        e.yaw = r.i16();
        e.life = r.i16();
        e.downed = r.u8() != 0;
        b.players.push_back(e);
    }
    if (!r.done()) return std::nullopt;
    return b;
}

std::vector<uint8_t> encodeWorldMessage(const WorldMessage& m) {
    od::net::ByteWriter w;
    w.u8(uint8_t(m.type));
    switch (m.type) {
    case WorldMessageType::Join:
    case WorldMessageType::State:
        w.u8(m.playerId);
        w.blob(m.playerState);
        break;
    case WorldMessageType::Leave: w.u8(m.playerId); break;
    case WorldMessageType::StageState: w.blob(m.stageRecord); break;
    case WorldMessageType::Report:
        w.u64(m.tick);
        w.u32(m.liveActors);
        w.u8(m.players);
        w.u64(m.digest);
        break;
    case WorldMessageType::NetJoin:
    case WorldMessageType::NetLeave: w.u8(m.playerId); break;
    case WorldMessageType::NetAck:
        w.u8(m.playerId);
        w.u16(m.netSequence);
        break;
    case WorldMessageType::NetEvents:
    case WorldMessageType::NetUpdate:
    case WorldMessageType::NetFire:
    case WorldMessageType::NetReport:
    case WorldMessageType::NetHit:
    case WorldMessageType::NetShotHit:
    case WorldMessageType::NetPlayer:
    case WorldMessageType::NetPlayerAck:
    case WorldMessageType::NetEnter:
    case WorldMessageType::NetSave:
    case WorldMessageType::NetEquip:
    case WorldMessageType::GlobalChange:
    case WorldMessageType::NetGift:
    case WorldMessageType::NetUse:
    case WorldMessageType::NetEventStart:
    case WorldMessageType::NetEventEnd:
    case WorldMessageType::NetSpeaker:
    case WorldMessageType::PlayerBrief:
    case WorldMessageType::NetRespawnAsk:
    case WorldMessageType::NetRespawnAt:
    case WorldMessageType::NetRevive:
    case WorldMessageType::NetRevived:
    case WorldMessageType::NetGameOver:
    case WorldMessageType::NetPresent:
    case WorldMessageType::NetSceneStart:
    case WorldMessageType::NetScenePad:
    case WorldMessageType::NetSceneEnd:
    case WorldMessageType::NetWorldExit:
    case WorldMessageType::NetMoveTo:
    case WorldMessageType::NetHold:
    case WorldMessageType::NetGateAsk:
    case WorldMessageType::NetGateLeave:
    case WorldMessageType::NetGateForce:
    case WorldMessageType::NetNotice:
    case WorldMessageType::NetBossFight:
    case WorldMessageType::NetSceneGate:
    case WorldMessageType::NetSceneGo:
    case WorldMessageType::NetWarp:
    case WorldMessageType::NetScreen:
    case WorldMessageType::NetGateGlow:
    case WorldMessageType::NetReload:
    case WorldMessageType::NetRoomPlayer:
    case WorldMessageType::NetAlert:
    case WorldMessageType::NetGauges:
        w.u8(m.playerId);
        w.blob(m.netState);
        break;
    default: break;
    }
    return w.take();
}

std::optional<WorldMessage> decodeWorldMessage(std::span<const uint8_t> bytes) {
    od::net::ByteReader r(bytes);
    WorldMessage m;
    const uint8_t type = r.u8();
    if (type < uint8_t(WorldMessageType::Hello) || type > uint8_t(kLastWorldMessageType)) return std::nullopt;
    m.type = WorldMessageType(type);
    switch (m.type) {
    case WorldMessageType::Join:
    case WorldMessageType::State:
        m.playerId = r.u8();
        m.playerState = r.blob();
        break;
    case WorldMessageType::Leave: m.playerId = r.u8(); break;
    case WorldMessageType::StageState: m.stageRecord = r.blob(); break;
    case WorldMessageType::Report:
        m.tick = r.u64();
        m.liveActors = r.u32();
        m.players = r.u8();
        m.digest = r.u64();
        break;
    case WorldMessageType::NetJoin:
    case WorldMessageType::NetLeave: m.playerId = r.u8(); break;
    case WorldMessageType::NetAck:
        m.playerId = r.u8();
        m.netSequence = r.u16();
        break;
    case WorldMessageType::NetEvents:
    case WorldMessageType::NetUpdate:
    case WorldMessageType::NetFire:
    case WorldMessageType::NetReport:
    case WorldMessageType::NetHit:
    case WorldMessageType::NetShotHit:
    case WorldMessageType::NetPlayer:
    case WorldMessageType::NetPlayerAck:
    case WorldMessageType::NetEnter:
    case WorldMessageType::NetSave:
    case WorldMessageType::NetEquip:
    case WorldMessageType::GlobalChange:
    case WorldMessageType::NetGift:
    case WorldMessageType::NetUse:
    case WorldMessageType::NetEventStart:
    case WorldMessageType::NetEventEnd:
    case WorldMessageType::NetSpeaker:
    case WorldMessageType::PlayerBrief:
    case WorldMessageType::NetRespawnAsk:
    case WorldMessageType::NetRespawnAt:
    case WorldMessageType::NetRevive:
    case WorldMessageType::NetRevived:
    case WorldMessageType::NetGameOver:
    case WorldMessageType::NetPresent:
    case WorldMessageType::NetSceneStart:
    case WorldMessageType::NetScenePad:
    case WorldMessageType::NetSceneEnd:
    case WorldMessageType::NetWorldExit:
    case WorldMessageType::NetMoveTo:
    case WorldMessageType::NetHold:
    case WorldMessageType::NetGateAsk:
    case WorldMessageType::NetGateLeave:
    case WorldMessageType::NetGateForce:
    case WorldMessageType::NetNotice:
    case WorldMessageType::NetBossFight:
    case WorldMessageType::NetSceneGate:
    case WorldMessageType::NetSceneGo:
    case WorldMessageType::NetWarp:
    case WorldMessageType::NetScreen:
    case WorldMessageType::NetGateGlow:
    case WorldMessageType::NetReload:
    case WorldMessageType::NetRoomPlayer:
    case WorldMessageType::NetAlert:
    case WorldMessageType::NetGauges:
        m.playerId = r.u8();
        m.netState = r.blob();
        break;
    default: break;
    }
    if (!r.done()) return std::nullopt;
    return m;
}

WorldLink::WorldLink() : socket_(0) {}
WorldLink::WorldLink(uint16_t to) : socket_(0), peer_(od::net::Endpoint::loopback(to)) {}

void WorldLink::sendRaw(std::span<const uint8_t> bytes) {
    if (peer_) socket_.sendTo(*peer_, bytes);
}

void WorldLink::sendReliable(const WorldMessage& m) {
    od::net::ByteWriter w;
    w.u8(uint8_t(Kind::Reliable));
    w.u32(nextSend_);
    w.raw(encodeWorldMessage(m));
    unacked_.push_back({nextSend_++, w.take()});
    sendRaw(unacked_.back().bytes);
}

void WorldLink::sendLatest(const WorldMessage& m) {
    od::net::ByteWriter w;
    w.u8(uint8_t(Kind::Latest));
    w.raw(encodeWorldMessage(m));
    sendRaw(w.bytes());
}

std::vector<WorldMessage> WorldLink::poll(uint64_t nowNs) {
    if (lastHeardNs_ == 0) lastHeardNs_ = nowNs;
    std::vector<WorldMessage> in;
    for (const od::net::Datagram& d : socket_.receive()) {
        if (!peer_) peer_ = d.from;
        if (d.from != *peer_ || d.bytes.empty()) continue;
        lastHeardNs_ = nowNs;
        od::net::ByteReader r(d.bytes);
        const uint8_t kind = r.u8();
        if (kind == uint8_t(Kind::Ack)) {
            const uint32_t acked = r.u32();
            if (!r.done()) continue;
            std::erase_if(unacked_, [&](const Unacked& u) { return u.sequence <= acked; });
        } else if (kind == uint8_t(Kind::Reliable)) {
            const uint32_t sequence = r.u32();
            if (!r.ok()) continue;
            const std::span<const uint8_t> body = std::span(d.bytes).subspan(5);
            if (sequence == nextReceive_) {
                if (auto m = decodeWorldMessage(body)) in.push_back(std::move(*m));
                ++nextReceive_;
            }
            od::net::ByteWriter ack; // what came in order so far, also when this one was a repeat or came early
            ack.u8(uint8_t(Kind::Ack));
            ack.u32(nextReceive_ - 1);
            sendRaw(ack.bytes());
        } else if (kind == uint8_t(Kind::Latest)) {
            if (auto m = decodeWorldMessage(std::span(d.bytes).subspan(1))) in.push_back(std::move(*m));
        }
    }
    if (nowNs - lastSendNs_ >= kResendNs) {
        lastSendNs_ = nowNs;
        for (const Unacked& u : unacked_) sendRaw(u.bytes);
    }
    return in;
}

bool WorldLink::silent(uint64_t nowNs) const { return lastHeardNs_ != 0 && nowNs - lastHeardNs_ > kSilenceNs; }

} // namespace od::app
