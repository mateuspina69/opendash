// The coordinator's worlds: one process per occupied room, started when the first player
// enters it and stopped when the last one leaves. Runs between frames in the host's game.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "app/world_link.hpp"

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
    WorldHost(std::filesystem::path exe, std::filesystem::path storySave);
    ~WorldHost(); // every world stopped

    // the player enters the room in this state (from another room, if they were in one): its world starts if it has none
    void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& playerState);
    // the player's state of this tick, to their world
    void state(uint8_t playerId, const std::vector<uint8_t>& playerState);
    // the player is out of the session: the world they were in stops if they were its last
    void leave(uint8_t playerId);
    // the turn between frames: the links, the worlds' reports, the keep-alives; a world that ended on its own is forgotten
    void poll(uint64_t nowNs);

    size_t worldCount() const { return worlds_.size(); }
    std::optional<WorldRoom> roomOf(uint8_t playerId) const { return playerRoom_.at(playerId); }
    // the room's world's last report (none before its first, or with no world)
    std::optional<WorldReport> report(WorldRoom room) const;

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

    std::filesystem::path exe_, storySave_;
    std::map<WorldRoom, std::unique_ptr<World>> worlds_;
    std::vector<std::unique_ptr<World>> stopping_; // told to stop, waiting for the process to end
    std::array<std::optional<WorldRoom>, kPlayers> playerRoom_{};
};

} // namespace od::app
