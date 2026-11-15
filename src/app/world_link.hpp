// The link between the coordinator (the host's game) and a world process over loopback UDP:
// what must arrive (a player joins or leaves, stop) is resent until acknowledged, what the next tick replaces (a player's
// state, the world's report) is not. Each side goes silent only by ending: a world whose coordinator goes silent stops by
// itself, so no process outlives the session.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "net/udp_socket.hpp"

namespace od::app {

// Coordinator -> world: Join {id, player state}, State {id, player state}, Leave {id}, Stop, KeepAlive.
// World -> coordinator: Hello, Report {tick, players in the world}.
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
    // between a client and the host's coordinator: this player enters a room (stage, sub, state), the host's save,
    // what the player wears
    NetEnter = 20, NetSave = 21, NetEquip = 22,
    // what the session shares (game/global_state), both ways; what a pickup or a chest gave a player; a player pressed Use by
    // nothing of their own; an event that runs on its player's machine, its end and the speaker's animation it asks
    GlobalChange = 23, NetGift = 24, NetUse = 25, NetEventStart = 26, NetEventEnd = 27, NetSpeaker = 28,
    // where the room's players stand, from each world to the coordinator (WorldBrief); one who is down asks to come back and
    // is told where; a teammate has revived them; everyone is down
    PlayerBrief = 29, NetRespawnAsk = 30, NetRespawnAt = 31, NetRevive = 32, NetRevived = 33, NetGameOver = 34,
    // what a world shows (game/presentation); a scene starts with its owner and its script, its owner's pad, its end with where
    // its Mega Man stands; a transition asked in a world; a player is moved there
    NetPresent = 35, NetSceneStart = 36, NetScenePad = 38, NetSceneEnd = 39, NetWorldExit = 40, NetMoveTo = 41,
    // the box carrier holds a player, or lets go (ST16's catch): carrier's slot, grip pose, holds
    NetHold = 42,
    // the gates: a player stands at a gated exit (where it leads, whether it is a mission's), or has walked off it; the host
    // forces the start; what the session says to everyone; a story boss is being fought in the room; a scene asks for its room,
    // and may start
    NetGateAsk = 43, NetGateLeave = 44, NetGateForce = 45, NetNotice = 46, NetBossFight = 47, NetSceneGate = 48, NetSceneGo = 49,
    // a warp inside the room's stage; what a world's controllers draw from; where a gate's door glows
    NetWarp = 50, NetScreen = 51, NetGateGlow = 52,
    // everyone is down: the host's last save is what the session goes on from, here its stage and room
    NetReload = 53,
    // a room's logic set this on its players (game/player/room_player_effects): field, value
    NetRoomPlayer = 54,
    // what went wrong in the session (a world that stopped): a line on every player's screen
    NetAlert = 55,
    // the enemy gauges the world shows and their actors' life (game/hud hudNetGaugesCapture): what the next one replaces
    NetGauges = 56,
    // a player pressed the lift by nothing of their own; a world's class lifted onto a player: the actor's slot, the lift's
    // mode
    NetLift = 57, NetLifted = 58,
    // the stage's alternate model banks the world holds (app/stage_banks): its players' games hold the same
    NetStageBanks = 59,
    // a player's own event or scene (its message's warp, a script only their game plays) changes the room: where it goes
    // (as a gate's ask): the others go there too
    NetStoryMove = 60,
    // an event the room or everyone sees (game/event EventReach): its owner's pad (app/scene_pads, on the event's ticks), to
    // the world and on to the others' games, whose copies play it; its end (NetEventEnd) carries the owner's last tick
    NetEventPad = 61,
    // coordinator -> a world process waiting with the game's files loaded (--world-standby): its room, as the command line
    // of a room's world gives it (each argument, then a zero byte)
    Assign = 62,
    // a piece of a message that one datagram does not hold (encodeWorldMessageParts): its bytes, and whether more follow
    Part = 63,
    // what a scene's script left in a room it went to (game/spawn SpawnNote), from its owner's game to that room's world,
    // which played none of it and makes them
    NetSceneLeft = 64
};
// the last type: what a decoder takes (a type added after it is the new last)
constexpr WorldMessageType kLastWorldMessageType = WorldMessageType::NetSceneLeft;

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
    uint8_t players = 0;
    uint16_t netSequence = 0;         // NetAck
    std::vector<uint8_t> netState;    // NetEvents, NetUpdate
    // a Net message's: the player's entry into a room it belongs to (WorldPipe::enter counts them; a NetEnter carries the
    // new one, the coordinator gives what it passes on the one now): what a world of a room left says is not played
    uint8_t entry = 0;
};
// a world's message of its room, to one of its players: of their entry into it only (a room left, entered again since)
bool worldRoomMessage(WorldMessageType type);

std::vector<uint8_t> encodeWorldMessage(const WorldMessage& m);
std::optional<WorldMessage> decodeWorldMessage(std::span<const uint8_t> bytes);

// The bytes of an encoded message one datagram holds, on the link and in a session (net/udp_socket's datagram less
// their framing)
constexpr size_t kWorldMessageBytes = od::net::kMaxDatagram - 20;
// A message as the datagrams that carry it where what is sent arrives in order: itself when one holds it, else Part
// messages with its bytes in order
std::vector<std::vector<uint8_t>> encodeWorldMessageParts(const WorldMessage& m);
// The receiving end of one such way: a whole message is given back as it is, a Part is kept until its last one came
class WorldParts {
public:
    std::optional<WorldMessage> take(WorldMessage m);

private:
    std::vector<uint8_t> bytes_;
};

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
    // what must arrive: sent now, each resent kResendNs after its last sending until acknowledged, delivered in order (of
    // any length a message has)
    void sendReliable(const WorldMessage& m);
    // what the next one replaces (nothing happens before the peer is known): one datagram, a longer one is not sent
    void sendLatest(const WorldMessage& m);
    // this side's turn: what arrived, in order for what must arrive, and the resends due at time nowNs
    std::vector<WorldMessage> poll(uint64_t nowNs);
    // how long ago the peer was heard, or this link started
    uint64_t quietNs(uint64_t nowNs) const;
    // ... longer ago than kSilenceNs
    bool silent(uint64_t nowNs) const;
    // nothing of what must arrive is still unacknowledged
    bool settled() const { return unacked_.empty(); }
    // waits until a datagram is there to poll, at most timeoutNs (a process with nothing else to do between two polls)
    void wait(uint64_t timeoutNs) { socket_.waitReadable(timeoutNs); }
    // from here on what this side sends leaves at its flush(), all at once: a world's tick is one burst, which the other
    // side reads in one turn (and passes on as one datagram a player)
    void defer() { deferred_ = true; }
    void flush();

private:
    void sendRaw(std::span<const uint8_t> bytes);

    od::net::UdpSocket socket_;
    std::optional<od::net::Endpoint> peer_;
    struct Unacked {
        uint32_t sequence = 0;
        std::vector<uint8_t> bytes;
        uint64_t sentNs = 0; // its last sending (0: since the last poll)
    };
    std::vector<Unacked> unacked_;
    std::map<uint32_t, std::vector<uint8_t>> early_; // what came ahead of the next one due, by sequence
    static constexpr size_t kMaxEarly = 256;
    WorldParts parts_;
    bool deferred_ = false;
    std::vector<std::vector<uint8_t>> outbox_; // deferred: the datagrams waiting for the flush
    uint32_t nextSend_ = 1, nextReceive_ = 1;
    uint64_t lastHeardNs_ = 0;
};

} // namespace od::app
