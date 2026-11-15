// A netplay session (spec: host-authoritative world, client-owned players; star topology). One player hosts on a UDP
// port, the others join its `ip:port`; clients talk only to the host. The host gives player ids (itself 0, clients
// 1..7), refuses a Hello whose protocol version, build id, game files or session key differ (naming what differs),
// forwards every client's player state unchanged to the other clients and announces joins and leaves.
//
// No threads: poll(nowNs) is called from the main thread (every frame, and around a game tick); it reads the socket,
// sends the resends and pings, updates the per-player stats, times out a silent peer and sends what was handed to
// sendWorld since the last call: one datagram per peer for all of it (net/protocol Bundle), with the acknowledgement of
// what that peer sent. The game's traffic travels as opaque payloads (the app encodes them).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "net/link_conditioner.hpp"
#include "net/protocol.hpp"
#include "net/udp_socket.hpp"

namespace od::net {

enum class Role : uint8_t { Host, Client };

// a peer silent this long is gone (the host drops the client; a client starts over with a new Hello)
constexpr uint64_t kPeerTimeoutNs = 5'000'000'000ull;
// the longest payload sendWorld takes: one datagram holds it with a bundle's framing
constexpr size_t kMaxWorldBytes = kMaxDatagram - kBundleHeadBytes - kBundleItemHeadBytes - kBundleSequenceBytes;

struct SessionConfig {
    Role role = Role::Host;
    uint16_t port = 0;        // host: the port it listens on; client: its own (0 = any free port)
    Endpoint host;            // client: the host's address
    std::string name;         // this player's name
    std::string buildId;      // this OpenDash build
    uint64_t gameFilesHash = 0, exeHash = 0;
    uint64_t sessionKeyHash = 0; // sessionKeyHash(net_key)
    std::optional<LinkSim> sim;  // --net-sim: the link conditioner between the session and the socket
    uint64_t seed = 0;           // the conditioner's and the Hello nonce's random seed
};

// a player state received from another player (the newest of that player so far)
struct ReceivedState {
    uint8_t playerId = 0;
    uint32_t sequence = 0, tick = 0;
    std::vector<uint8_t> payload;
};

// what came for the coordinator or from it (a host: from this client; a client: from the host)
struct ReceivedWorld {
    uint8_t fromId = 0;
    std::vector<uint8_t> payload;
};

struct PeerEvent {
    enum class Kind : uint8_t { Joined, Left } kind = Kind::Joined;
    PeerInfo peer;
};

enum class Connection : uint8_t {
    Hosting,    // host: listening
    Connecting, // client: Hello sent, no answer yet
    Connected,  // client: welcomed
    Refused,    // client: the host refused it (SessionStatus::text says why); nothing more is sent
};
const char* connectionName(Connection c);

struct PlayerStatus {
    uint8_t playerId = 0;
    std::string name;
    std::string address;     // the peer's ip:port where this end talks to it directly ("" = through the host)
    int rttMs = -1;          // round trip of the direct link (-1: not measured / not direct)
    float lossPercent = -1;  // player states lost over the last stats window (-1: none received yet)
    int64_t lastHeardMs = -1; // since the last player state of this player (-1: none yet)
};

struct SessionStatus {
    Role role = Role::Host;
    uint16_t port = 0;
    uint8_t localId = 0;
    Connection connection = Connection::Hosting;
    std::string text; // the refusal (client) or the last thing that went wrong
    std::vector<PlayerStatus> players; // everyone else
};

class Session {
public:
    // binds the socket (throws std::runtime_error when it cannot); a client sends its Hello on the first poll
    explicit Session(const SessionConfig& config);
    ~Session(); // sends Bye (once; a lost Bye is a timeout on the other end)
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void poll(uint64_t nowNs);
    // this player's own state: the host sends it to every client, a connected client to the host
    void sendPlayerState(uint64_t nowNs, uint32_t tick, std::span<const uint8_t> payload);
    // the player states received since the last call (newer than every earlier one of the same player)
    std::vector<ReceivedState> takePlayerStates();
    // the shared world's traffic, opaque here: a host sends to client `toId`, a client to the host. reliable: resent until
    // acknowledged, in order; else the next one replaces it (one of a datagram that arrives after a newer one is dropped).
    // It leaves with the next flush (a payload over kMaxWorldBytes is not sent: the caller splits it)
    void sendWorld(uint8_t toId, bool reliable, std::span<const uint8_t> payload);
    // what sendWorld was handed, and the acknowledgements due: sent now (poll ends with it)
    void flush(uint64_t nowNs);
    std::vector<ReceivedWorld> takeWorld();
    // joins and leaves since the last call
    std::vector<PeerEvent> takePeerEvents();
    SessionStatus status(uint64_t nowNs) const;

private:
    // a reliable message not acknowledged yet: a control message's whole datagram, or a bundle item's payload (sent with
    // the next flush: sentNs 0 before its first)
    struct Unacked {
        uint32_t sequence;
        std::vector<uint8_t> bytes;
        uint64_t sentNs;
        bool item = false;
    };
    // a reliable message as it arrived: a control message, or a bundle item's payload
    struct Incoming {
        std::optional<Message> control;
        std::vector<uint8_t> world;
    };
    // the reliable channel and the stats of one directly connected endpoint
    struct Link {
        Endpoint endpoint;
        uint32_t nextOut = 0;            // the next reliable sequence sent
        std::vector<Unacked> unacked;    // sent, not acknowledged yet
        uint32_t nextIn = 0;             // the next reliable sequence delivered
        std::map<uint32_t, Incoming> early; // received ahead of nextIn
        std::vector<std::vector<uint8_t>> latest; // what the next one replaces, waiting for the flush
        bool ackDue = false;             // a reliable message arrived since the last bundle sent
        uint16_t orderOut = 0;           // the last bundle sent
        uint16_t orderIn = 0;            // the newest bundle received
        bool anyOrder = false;
        uint64_t lastHeardNs = 0;
        uint64_t lastPingNs = 0;
        double rttMs = -1.0;
    };
    // the host's view of one client
    struct Client {
        Link link;
        uint32_t nonce = 0;
        bool accepted = false; // welcomed (refused clients stay until they fall silent, so their resends are acked)
        PeerInfo info;
    };
    // the stats of one player's state stream
    struct StateStats {
        bool any = false;
        uint32_t lastSequence = 0;
        uint64_t lastNs = 0;
        uint32_t windowFirst = 0, windowLast = 0, windowReceived = 0;
        uint64_t windowStartNs = 0;
        float lossPercent = -1;
    };

    void transmit(uint64_t nowNs, const Endpoint& to, std::span<const uint8_t> bytes);
    void sendReliable(uint64_t nowNs, Link& link, const Message& m);
    void sendUnreliable(uint64_t nowNs, const Link& link, const Message& m);
    // the reliable channel: the messages now deliverable in order (an acknowledgement is due)
    std::vector<Incoming> accept(Link& link, uint32_t sequence, Incoming m);
    void handleLinkMessage(uint64_t nowNs, Link& link, const Message& m); // ping / pong
    void serviceLink(uint64_t nowNs, Link& link);                          // the control messages' resends, pings
    void flushLink(uint64_t nowNs, Link& link);                            // a bundle: items due, acknowledgement
    // a bundle's acknowledgement taken, its reliable items through accept; false: an older datagram than the newest seen
    bool openBundle(Link& link, const Bundle& b, std::vector<Incoming>& ready);
    bool acceptState(uint64_t nowNs, const PlayerState& s);                // newest wins; the stats

    void hostReceive(uint64_t nowNs, const Endpoint& from, Decoded d);
    void hostDeliver(uint64_t nowNs, Client& c, const Message& m);
    // what the reliable channel hands on, in order (false: a Bye dropped the client)
    bool hostTake(uint64_t nowNs, const Endpoint& from, std::vector<Incoming> ready);
    void clientTake(uint64_t nowNs, std::vector<Incoming> ready);
    void hostWelcomeOrRefuse(uint64_t nowNs, Client& c, const Hello& h);
    void hostDrop(uint64_t nowNs, const Endpoint& e);
    void clientReceive(uint64_t nowNs, const Endpoint& from, Decoded d);
    void clientDeliver(uint64_t nowNs, const Message& m);
    void clientStart(uint64_t nowNs); // a new connection attempt: fresh channel, new nonce, Hello
    void clientLost(uint64_t nowNs, const char* why);

    SessionConfig config_;
    UdpSocket socket_;
    std::optional<LinkConditioner> conditioner_;
    Connection connection_;
    std::string text_;
    uint8_t localId_ = kHostPlayerId;
    uint32_t stateSequence_ = 0;
    uint64_t nonceState_;
    std::map<Endpoint, Client> clients_; // host
    Link hostLink_;                      // client
    bool started_ = false;               // client: the first Hello sent
    std::map<uint8_t, PeerInfo> players_; // client: everyone else the host announced
    std::map<uint8_t, StateStats> stats_;
    std::vector<ReceivedState> states_;
    std::vector<ReceivedWorld> world_;
    std::vector<PeerEvent> events_;
    uint64_t lastNowNs_ = 0;
};

} // namespace od::net
