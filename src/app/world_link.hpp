// The link between the coordinator (the host's game) and a world process over loopback UDP:
// what must arrive (a player joins or leaves, stop) is resent until acknowledged, what the next tick replaces (a player's
// state, the world's report) is not. Each side goes silent only by ending: a world whose coordinator goes silent stops by
// itself, so no process outlives the session.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "net/udp_socket.hpp"

namespace od::app {

// Coordinator -> world: Join {id, player state}, State {id, player state}, Leave {id}, Stop, KeepAlive.
// World -> coordinator: Hello, Report {tick, live actors, players in the world, digest}.
// Both ways: StageState {the stage's record: game/stage_record}.
// The network state (game/world_net_state), per client: NetJoin {client} and NetAck {client, sequence} toward the world,
// NetEvents {client, batch} (what must arrive) and NetUpdate {client, update} (what the next one replaces) from it.
enum class WorldMessageType : uint8_t {
    Hello = 1, Join = 2, State = 3, Leave = 4, Stop = 5, KeepAlive = 6, Report = 7, StageState = 8,
    NetJoin = 9, NetLeave = 10, NetAck = 11, NetEvents = 12, NetUpdate = 13,
    // the fight (game/player/remote_combat): NetFire, NetReport toward the world; NetHit, NetShotHit from it
    NetFire = 14, NetReport = 15, NetHit = 16, NetShotHit = 17,
    // a player's state (game/player/net_player): both ways, for what the next one replaces; its acknowledgement: id, sequence
    NetPlayer = 18, NetPlayerAck = 19,
    // between a client and the host's coordinator: this player enters a room (stage, sub, state), the host's save in chunks,
    // what the player wears
    NetEnter = 20, NetSave = 21, NetEquip = 22,
    // what the session shares (game/global_state), both ways; what a pickup or a chest gave a player; a player pressed Use by
    // nothing of their own; an event that runs on its player's machine, its end and the speaker's animation it asks
    GlobalChange = 23, NetGift = 24, NetUse = 25, NetEventStart = 26, NetEventEnd = 27, NetSpeaker = 28,
    // where the room's players stand, from each world to the coordinator (WorldBrief); one who is down asks to come back and
    // is told where; a teammate has revived them; everyone is down
    PlayerBrief = 29, NetRespawnAsk = 30, NetRespawnAt = 31, NetRevive = 32, NetRevived = 33, NetGameOver = 34
};

// What a world says of its players to the coordinator
struct WorldBrief {
    struct Entry {
        uint8_t id = 0;
        int16_t x = 0, y = 0, z = 0, yaw = 0, life = 0;
        bool downed = false;
    };
    bool bossFight = false; // a story boss is being fought in this room
    std::vector<Entry> players;
};
std::vector<uint8_t> encodeBrief(const WorldBrief& b);
std::optional<WorldBrief> decodeBrief(std::span<const uint8_t> bytes);

struct WorldMessage {
    WorldMessageType type = WorldMessageType::KeepAlive;
    uint8_t playerId = 0;             // the player, or the client of a Net message
    std::vector<uint8_t> playerState; // Join, State: game/player/player_snapshot's encodePlayerState
    std::vector<uint8_t> stageRecord; // StageState: game/stage_record's encodeStageRecord
    uint64_t tick = 0;                // Report
    uint32_t liveActors = 0;
    uint8_t players = 0;
    uint64_t digest = 0;
    uint16_t netSequence = 0;         // NetAck
    std::vector<uint8_t> netState;    // NetEvents, NetUpdate
};

std::vector<uint8_t> encodeWorldMessage(const WorldMessage& m);
std::optional<WorldMessage> decodeWorldMessage(std::span<const uint8_t> bytes);

// One side of a link. The coordinator binds (port 0: any free one) and learns its peer from the first datagram; the world
// is told the coordinator's port.
class WorldLink {
public:
    static constexpr uint64_t kResendNs = 100'000'000;
    static constexpr uint64_t kSilenceNs = 5'000'000'000;

    WorldLink();                     // the coordinator's side
    explicit WorldLink(uint16_t to); // the world's side

    uint16_t port() const { return socket_.port(); }
    bool hasPeer() const { return peer_.has_value(); }
    // what must arrive: sent now, resent every kResendNs until acknowledged, in order
    void sendReliable(const WorldMessage& m);
    // what the next one replaces (nothing happens before the peer is known)
    void sendLatest(const WorldMessage& m);
    // this side's turn: what arrived, in order for what must arrive, and the resends due at time nowNs
    std::vector<WorldMessage> poll(uint64_t nowNs);
    // the peer has been heard, or this link started, longer ago than kSilenceNs
    bool silent(uint64_t nowNs) const;
    // nothing of what must arrive is still unacknowledged
    bool settled() const { return unacked_.empty(); }

private:
    void sendRaw(std::span<const uint8_t> bytes);

    od::net::UdpSocket socket_;
    std::optional<od::net::Endpoint> peer_;
    struct Unacked {
        uint32_t sequence = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<Unacked> unacked_;
    uint32_t nextSend_ = 1, nextReceive_ = 1;
    uint64_t lastSendNs_ = 0, lastHeardNs_ = 0;
};

} // namespace od::app
