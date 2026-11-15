// The netplay protocol's messages and their byte layout (net/wire). Every datagram starts with the header
// {magic "ODNP", protocol version, message type}; the reliable control messages then carry their sequence number on
// the sender's reliable channel (net/session acknowledges and resends them). The header and the Refuse message keep
// their layout across protocol versions, so a host can always tell an older or newer client why it is refused.
//
// The game's traffic travels as opaque bytes, every message of a turn in one datagram (Bundle): the protocol knows no
// game struct (the app encodes them).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace od::net {

constexpr uint32_t kMagic = 0x504e444f; // "ODNP" little endian
constexpr uint16_t kProtocolVersion = 3;
constexpr int kMaxPlayers = 8;          // the host (player 0) and up to 7 clients
constexpr uint8_t kHostPlayerId = 0;

enum class MessageType : uint8_t {
    Hello = 1,       // client -> host: who joins (reliable)
    Welcome = 2,     // host -> client: the client's player id and who is there (reliable)
    Refuse = 3,      // host -> client: why the client may not join (reliable)
    PeerJoined = 4,  // host -> clients: a player joined (reliable)
    PeerLeft = 5,    // host -> clients: a player left (reliable)
    PlayerState = 6, // a player's own character (unreliable, newest wins; the host forwards it unchanged)
    Bundle = 7,      // a turn's traffic between a player's game and the coordinator, and the link's acknowledgement
    Ping = 8,        // round-trip probe (unreliable)
    Pong = 9,        // its echo (unreliable)
    Bye = 10,        // leaving (reliable)
    Probe = 11,      // a lobby looking for hosts on the local network (broadcast, unreliable)
    Offer = 12,      // host -> that lobby: its session (unreliable)
};
// the types that travel on the reliable channel (they carry a sequence number after the header)
bool isReliable(MessageType t);

enum class RefuseReason : uint8_t {
    Version = 1,   // protocol version differs
    Build = 2,     // OpenDash build id differs
    GameFiles = 3, // dash2.exe / the .DAT set differ
    Key = 4,       // session key differs
    Full = 5,      // kMaxPlayers already in the session
};
const char* refuseReasonName(RefuseReason r);

struct Hello {
    uint16_t protocolVersion = kProtocolVersion;
    std::string buildId;
    uint64_t gameFilesHash = 0; // engine/game_files_hash: dash2.exe + the .DAT set
    uint64_t exeHash = 0;       // dash2.exe alone (with gameFilesHash: the refusal names which one differs)
    uint64_t sessionKeyHash = 0;
    uint32_t nonce = 0; // this connection attempt (a restarted client on the same address is a new connection)
    std::string name;
    bool operator==(const Hello&) const = default;
};
struct PeerInfo {
    uint8_t playerId = 0;
    std::string name;
    bool operator==(const PeerInfo&) const = default;
};
struct Welcome {
    uint8_t playerId = 0;        // the client's
    std::vector<PeerInfo> peers; // everyone else in the session (the host included)
    bool operator==(const Welcome&) const = default;
};
struct Refuse {
    RefuseReason reason = RefuseReason::Version;
    std::string text; // for the player: what differs
    bool operator==(const Refuse&) const = default;
};
struct PeerJoined {
    PeerInfo peer;
    bool operator==(const PeerJoined&) const = default;
};
struct PeerLeft {
    PeerInfo peer;
    bool operator==(const PeerLeft&) const = default;
};
struct PlayerState {
    uint8_t playerId = 0; // the sender's (the host checks a client's against the id it gave)
    uint32_t sequence = 0;
    uint32_t tick = 0;    // the sender's game tick counter
    std::vector<uint8_t> payload;
    bool operator==(const PlayerState&) const = default;
};
// One datagram of a link's traffic: every reliable message received in order so far (below `ackNext`), this datagram's
// place among the sender's (`order`: a receiver drops the newest-wins items of one older than the newest it has seen), and
// the items. A reliable item carries its sequence on the sender's reliable channel, shared with the control messages.
// Layout: u32 ackNext, u16 order, u8 the item count, then per item u16 its length (top bit: reliable), u32 its sequence
// when reliable, its bytes.
constexpr size_t kMaxBundleItem = 0x7fff;
struct Bundle {
    struct Item {
        bool reliable = false;
        uint32_t sequence = 0;
        std::vector<uint8_t> payload;
        bool operator==(const Item&) const = default;
    };
    uint32_t ackNext = 0;
    uint16_t order = 0;
    std::vector<Item> items;
    bool operator==(const Bundle&) const = default;
};
// the bytes of a datagram's header with a Bundle's own fields, of one item's head, and of a reliable item's sequence
constexpr size_t kBundleHeadBytes = 4 + 2 + 1 + 4 + 2 + 1, kBundleItemHeadBytes = 2, kBundleSequenceBytes = 4;
constexpr size_t kMaxBundleItems = 255;
struct Ping {
    uint64_t sentNs = 0; // the sender's clock
    bool operator==(const Ping&) const = default;
};
struct Pong {
    uint64_t echoNs = 0; // the Ping's sentNs
    bool operator==(const Pong&) const = default;
};
struct Bye {
    bool operator==(const Bye&) const = default;
};

struct Probe {
    bool operator==(const Probe&) const = default;
};
struct Offer {
    std::string name;    // the host player's
    std::string buildId; // the host's build (a lobby of another build shows it as such)
    uint8_t players = 1; // in the session, the host included
    uint16_t port = 0;   // the session's
    bool operator==(const Offer&) const = default;
};

using Message = std::variant<Hello, Welcome, Refuse, PeerJoined, PeerLeft, PlayerState, Bundle, Ping, Pong, Bye, Probe, Offer>;
static_assert(std::is_same_v<std::variant_alternative_t<0, Message>, Hello> &&
              std::is_same_v<std::variant_alternative_t<size_t(MessageType::Bundle) - 1, Message>, Bundle> &&
              std::variant_size_v<Message> == size_t(MessageType::Offer),
              "Message lists the types in MessageType order from Hello = 1 (typeOf)");
MessageType typeOf(const Message& m);

// A decoded datagram: its header's version, the message and, for a reliable one, its sequence number
struct Decoded {
    uint16_t version = kProtocolVersion;
    uint32_t sequence = 0; // reliable messages only
    Message message;
};

// one datagram: the header, the sequence (reliable types: `sequence`), the body
std::vector<uint8_t> encode(const Message& m, uint32_t sequence = 0);
// nullopt: not a datagram of this protocol (magic), an unknown type, a short or overlong body, a bad field. A datagram
// of another protocol version decodes only when it is a Hello (its first body field is the version, so the host can
// refuse it) or a Refuse (the client shows it); the caller checks `version`.
std::optional<Decoded> decode(std::span<const uint8_t> bytes);

// 64-bit FNV-1a of the session key's text (only the hash travels)
uint64_t sessionKeyHash(const std::string& key);

} // namespace od::net
