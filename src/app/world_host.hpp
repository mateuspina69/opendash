// The coordinator's worlds: one process per occupied room, started when the first player
// enters it and stopped when the last one leaves. Runs between frames in the host's game.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "app/world_link.hpp"
#include "game/global_state.hpp"
#include "game/stage_record.hpp"

struct SDL_Process;

namespace od::app {

// A room: a stage and a sub-room
struct WorldRoom {
    int stage = 0, sub = 0;
    friend auto operator<=>(const WorldRoom&, const WorldRoom&) = default;
};

// What a world last said of itself
struct WorldReport {
    uint64_t tick = 0;
    uint32_t liveActors = 0;
    uint8_t players = 0;
    uint64_t digest = 0;
};

class WorldHost {
public:
    static constexpr size_t kPlayers = 8;

    // exe: the game's executable; storySave: the save image the worlds start from (empty: a new game's state)
    WorldHost(std::filesystem::path exe, std::filesystem::path storySave, int32_t zenny = 0);
    ~WorldHost(); // every world stopped

    // the player enters the room in this state (from another room, if they were in one): its world starts if it has none
    void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& playerState);
    // the player's state of this tick, to their world
    void state(uint8_t playerId, const std::vector<uint8_t>& playerState);
    // the player is out of the session: the world they were in stops if they were its last
    void leave(uint8_t playerId);
    // the network state of the world the client's player is in (game/world_net_state): the client wants it, what came for it
    // since the last call, what it holds
    void netJoin(uint8_t client);
    std::vector<WorldMessage> takeNet(uint8_t client);
    void netAck(uint8_t client, uint16_t sequence);
    // what must arrive, to the world the client's player is in (a fire, a report)
    void netSend(uint8_t client, WorldMessageType type, std::vector<uint8_t> bytes);
    // ... what the next one replaces (a player's state, its acknowledgement)
    void netSendLatest(uint8_t client, WorldMessageType type, std::vector<uint8_t> bytes);
    // what the session shares changed in this player's game
    void globalFromPlayer(uint8_t playerId, std::span<const uint8_t> bytes);
    // one who is down asks to come back next to a standing teammate: told where, unless nobody stands or a story boss is
    // being fought where they are
    void respawnAsk(uint8_t playerId);
    // a player who has just joined the session: told to come next to the host's Mega Man
    void placeNextToHost(uint8_t playerId);
    // test instrument: this flag is clear from the session's start, whatever the save says
    void testClearFlag(uint32_t flag);
    // The gates. Teleport: those far away are brought at once; else they are waited for
    void setGateTeleport(bool teleport) { gateTeleport_ = teleport; }
    void setPlayerName(uint8_t playerId, const std::string& name) { names_.at(playerId) = name; }
    void gateAsk(uint8_t playerId, const std::vector<uint8_t>& move);
    void gateLeave(uint8_t playerId);
    // the host starts it without those missing, after a visible countdown
    void gateForce(uint64_t nowNs);
    // the turn between frames: the links, the worlds' reports, the keep-alives; a world that ended on its own is forgotten
    void poll(uint64_t nowNs);

    size_t worldCount() const { return worlds_.size(); }
    std::optional<WorldRoom> roomOf(uint8_t playerId) const { return playerRoom_.at(playerId); }
    // the room's world's last report (none before its first, or with no world)
    std::optional<WorldReport> report(WorldRoom room) const;
    // the stage's record while someone is in the stage (none once it has emptied: its enemies are back)
    std::optional<od::port::StageRecord> stageRecord(int stage) const;

private:
    struct World {
        WorldLink link;
        SDL_Process* process = nullptr;
        std::optional<WorldReport> report;
        std::vector<uint8_t> players;
        uint64_t lastKeepAliveNs = 0;
        ~World();
    };
    World& worldOf(WorldRoom room);
    void removeFromWorld(uint8_t playerId);
    void dropEmptiedStage(int stage);
    void globalChanged(const od::port::GlobalChange& change, const World* fromWorld, std::optional<uint8_t> fromPlayer);

    std::filesystem::path exe_, storySave_;
    std::map<WorldRoom, std::unique_ptr<World>> worlds_;
    std::vector<std::unique_ptr<World>> stopping_; // told to stop, waiting for the process to end
    std::array<std::optional<WorldRoom>, kPlayers> playerRoom_{};
    std::map<int, od::port::StageRecord> stageRecords_;
    std::array<std::vector<WorldMessage>, kPlayers> netInbox_{};
    od::port::GlobalMaster global_;
    std::array<std::optional<WorldBrief::Entry>, kPlayers> briefs_{};
    std::map<WorldRoom, bool> bossFight_;
    bool gameOverSent_ = false;
    std::array<bool, kPlayers> joining_{}; // to be placed next to the host, as soon as the host's place is known
    void gameOverTurn();
    struct Gate {
        bool scene = false;       // a scene waits in `room`; else a door, to `move`
        bool mission = false;     // ... a mission's: everyone walks there, nobody is brought
        WorldRoom room{};
        std::vector<uint8_t> move;
        std::set<uint8_t> waiting;
        uint8_t first = 0;        // who got there first
        std::set<uint8_t> brought;
        uint64_t forceAtNs = 0, nextNoticeNs = 0;
    };
    std::optional<Gate> gate_;
    bool gateTeleport_ = false;
    std::array<std::string, kPlayers> names_{};
    uint64_t lastNowNs_ = 0;
    void gateTurn(uint64_t nowNs);
    void gateNotice(const std::string& text);
    void moveTo(uint8_t playerId, const std::vector<uint8_t>& move);
};

} // namespace od::app
