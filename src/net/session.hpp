// A netplay session (spec: host-authoritative world, client-owned players; star topology). One player hosts on a UDP
// port, the others join its `ip:port`; clients talk only to the host. The host gives player ids (itself 0, clients
// 1..7), refuses a Hello whose protocol version, build id, game files or session key differ (naming what differs),
// forwards every client's player state unchanged to the other clients and announces joins and leaves.
//
// No threads: poll(nowNs) is called from the main thread once per sim step (between game ticks); it sends the
// resends and pings, reads the socket, updates the per-player stats and times out a silent peer. The game's state
// travels as opaque payloads (the app encodes them).
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
    // joins and leaves since the last call
    std::vector<PeerEvent> takePeerEvents();
    SessionStatus status(uint64_t nowNs) const;

private:
    struct Unacked {
        uint32_t sequence;
        std::vector<uint8_t> bytes;
        uint64_t sentNs;
    };
    // the reliable channel and the stats of one directly connected endpoint
    struct Link {
        Endpoint endpoint;
        uint32_t nextOut = 0;            // the next reliable sequence sent
        std::vector<Unacked> unacked;    // sent, not acknowledged yet
        uint32_t nextIn = 0;             // the next reliable sequence delivered
        std::map<uint32_t, Message> early; // received ahead of nextIn
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
    // the reliable channel: the messages now deliverable in order (acks what arrived)
    std::vector<Message> accept(uint64_t nowNs, Link& link, uint32_t sequence, Message m);
    void handleLinkMessage(uint64_t nowNs, Link& link, const Message& m); // ack / ping / pong
    void serviceLink(uint64_t nowNs, Link& link);                          // resends, pings
    bool acceptState(uint64_t nowNs, const PlayerState& s);                // newest wins; the stats

    void hostReceive(uint64_t nowNs, const Endpoint& from, Decoded d);
    void hostDeliver(uint64_t nowNs, Client& c, const Message& m);
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
    std::vector<PeerEvent> events_;
    uint64_t lastNowNs_ = 0;
};

} // namespace od::net
