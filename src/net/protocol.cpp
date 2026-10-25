// The netplay messages' layout (see protocol.hpp).
#include "net/protocol.hpp"

#include "net/wire.hpp"

namespace od::net {

namespace {

template <class> constexpr bool kAlways = false;

void writePeer(ByteWriter& w, const PeerInfo& p) {
    w.u8(p.playerId);
    w.string(p.name);
}
PeerInfo readPeer(ByteReader& r) {
    PeerInfo p;
    p.playerId = r.u8();
    p.name = r.string();
    if (p.playerId >= kMaxPlayers) r.fail();
    return p;
}

void writeBody(ByteWriter& w, const Message& m) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Hello>) {
                w.u16(v.protocolVersion); // first: a host of any version can read it
                w.string(v.buildId);
                w.u64(v.gameFilesHash);
                w.u64(v.exeHash);
                w.u64(v.sessionKeyHash);
                w.u32(v.nonce);
                w.string(v.name);
            } else if constexpr (std::is_same_v<T, Welcome>) {
                w.u8(v.playerId);
                w.u8(uint8_t(v.peers.size()));
                for (const PeerInfo& p : v.peers) writePeer(w, p);
            } else if constexpr (std::is_same_v<T, Refuse>) {
                w.u8(uint8_t(v.reason));
                w.string(v.text);
            } else if constexpr (std::is_same_v<T, PeerJoined> || std::is_same_v<T, PeerLeft>) {
                writePeer(w, v.peer);
            } else if constexpr (std::is_same_v<T, PlayerState>) {
                w.u8(v.playerId);
                w.u32(v.sequence);
                w.u32(v.tick);
                w.blob(v.payload);
            } else if constexpr (std::is_same_v<T, WorldState>) {
                w.u32(v.sequence);
                w.u32(v.tick);
                w.blob(v.payload);
            } else if constexpr (std::is_same_v<T, Ping>) {
                w.u64(v.sentNs);
            } else if constexpr (std::is_same_v<T, Pong>) {
                w.u64(v.echoNs);
            } else if constexpr (std::is_same_v<T, Bye>) {
            } else if constexpr (std::is_same_v<T, Ack>) {
                w.u32(v.next);
            } else if constexpr (std::is_same_v<T, Event>) {
                w.blob(v.payload);
            } else {
                static_assert(kAlways<T>, "a message type without a layout");
            }
        },
        m);
}

std::optional<Message> readBody(MessageType t, ByteReader& r) {
    Message m;
    switch (t) {
    case MessageType::Hello: {
        Hello h;
        h.protocolVersion = r.u16();
        h.buildId = r.string();
        h.gameFilesHash = r.u64();
        h.exeHash = r.u64();
        h.sessionKeyHash = r.u64();
        h.nonce = r.u32();
        h.name = r.string();
        m = h;
        break;
    }
    case MessageType::Welcome: {
        Welcome w;
        w.playerId = r.u8();
        const uint8_t n = r.u8();
        if (w.playerId >= kMaxPlayers || n >= kMaxPlayers) return std::nullopt;
        for (uint8_t i = 0; i < n && r.ok(); ++i) w.peers.push_back(readPeer(r));
        m = w;
        break;
    }
    case MessageType::Refuse: {
        Refuse f;
        const uint8_t reason = r.u8();
        if (reason < uint8_t(RefuseReason::Version) || reason > uint8_t(RefuseReason::Full)) return std::nullopt;
        f.reason = RefuseReason(reason);
        f.text = r.string();
        m = f;
        break;
    }
    case MessageType::PeerJoined: m = PeerJoined{readPeer(r)}; break;
    case MessageType::PeerLeft: m = PeerLeft{readPeer(r)}; break;
    case MessageType::PlayerState: {
        PlayerState s;
        s.playerId = r.u8();
        s.sequence = r.u32();
        s.tick = r.u32();
        s.payload = r.blob();
        if (s.playerId >= kMaxPlayers) return std::nullopt;
        m = std::move(s);
        break;
    }
    case MessageType::WorldState: {
        WorldState s;
        s.sequence = r.u32();
        s.tick = r.u32();
        s.payload = r.blob();
        m = std::move(s);
        break;
    }
    case MessageType::Ping: m = Ping{r.u64()}; break;
    case MessageType::Pong: m = Pong{r.u64()}; break;
    case MessageType::Bye: m = Bye{}; break;
    case MessageType::Ack: m = Ack{r.u32()}; break;
    case MessageType::Event: m = Event{r.blob()}; break;
    default: return std::nullopt;
    }
    if (!r.done()) return std::nullopt;
    return m;
}

} // namespace

bool isReliable(MessageType t) {
    switch (t) {
    case MessageType::Hello:
    case MessageType::Welcome:
    case MessageType::Refuse:
    case MessageType::PeerJoined:
    case MessageType::PeerLeft:
    case MessageType::Bye:
    case MessageType::Event: return true;
    default: return false;
    }
}

const char* refuseReasonName(RefuseReason r) {
    switch (r) {
    case RefuseReason::Version: return "protocol version differs";
    case RefuseReason::Build: return "OpenDash build differs";
    case RefuseReason::GameFiles: return "game files differ";
    case RefuseReason::Key: return "session key differs";
    case RefuseReason::Full: return "session full";
    }
    return "?";
}

MessageType typeOf(const Message& m) {
    // the variant's alternatives are in MessageType order from Hello = 1
    return MessageType(uint8_t(m.index() + 1));
}

std::vector<uint8_t> encode(const Message& m, uint32_t sequence) {
    ByteWriter w;
    w.u32(kMagic);
    w.u16(kProtocolVersion);
    const MessageType t = typeOf(m);
    w.u8(uint8_t(t));
    if (isReliable(t)) w.u32(sequence);
    writeBody(w, m);
    return w.take();
}

std::optional<Decoded> decode(std::span<const uint8_t> bytes) {
    ByteReader r(bytes);
    if (r.u32() != kMagic) return std::nullopt;
    Decoded d;
    d.version = r.u16();
    const MessageType t = MessageType(r.u8());
    if (!r.ok()) return std::nullopt;
    if (isReliable(t)) d.sequence = r.u32();
    if (d.version != kProtocolVersion) {
        // another version: only what the frozen layouts let us read (the Hello's version, a whole Refuse)
        if (t == MessageType::Hello) {
            Hello h;
            h.protocolVersion = r.u16();
            if (!r.ok()) return std::nullopt;
            d.message = h;
            return d;
        }
        if (t != MessageType::Refuse) return std::nullopt;
    }
    auto m = readBody(t, r);
    if (!m) return std::nullopt;
    d.message = std::move(*m);
    return d;
}

uint64_t sessionKeyHash(const std::string& key) {
    uint64_t h = 0xcbf29ce484222325ull; // FNV-1a 64 offset basis
    for (const char c : key) {
        h ^= uint8_t(c);
        h *= 0x100000001b3ull; // FNV-1a 64 prime
    }
    return h;
}

} // namespace od::net
