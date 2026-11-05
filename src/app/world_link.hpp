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
enum class WorldMessageType : uint8_t {
    Hello = 1, Join = 2, State = 3, Leave = 4, Stop = 5, KeepAlive = 6, Report = 7, StageState = 8
};

struct WorldMessage {
    WorldMessageType type = WorldMessageType::KeepAlive;
    uint8_t playerId = 0;
    std::vector<uint8_t> playerState; // Join, State: game/player/player_snapshot's encodePlayerState
    std::vector<uint8_t> stageRecord; // StageState: game/stage_record's encodeStageRecord
    uint64_t tick = 0;                // Report
    uint32_t liveActors = 0;
    uint8_t players = 0;
    uint64_t digest = 0;
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
