// The windowed game as a client of a world of its own, on one machine (--own-world <save>): the room's actors come from a world
// process through app/world_host and game/world_net_state, as a remote client's would; the game runs only its own Mega Man
// and what runs on a client (the effects). Off without the flag: single player never comes here.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "app/coop_host.hpp"
#include "app/world_host.hpp"
#include "app/ghost_view.hpp"
#include "game/event_state.hpp"
#include "game/ghost_buffer.hpp"
#include "game/player/net_player.hpp"
#include "game/player/remote_combat.hpp"
#include "game/world_net_state.hpp"

namespace od::app {

class OwnWorldClient {
public:
    OwnWorldClient(std::filesystem::path exe, std::filesystem::path storySave);
    // the session this game is in: a host's clients come through its coordinator; a client's world is the host's
    void useSession(od::net::Session* session, bool host);
    // the host's save, when it differs from this game's: the game restarts from it (what exists is the host's)
    std::optional<std::vector<uint8_t>> takeHostSave(const std::vector<uint8_t>& current);

    // the room's files are bound and its spawn has run: the local actors go, this room's world takes over
    void roomBound(int stage, int sub);
    // the turn before a tick: the player's state to the world, what the world sent, the timeline up to now
    void turn(uint64_t nowNs);
    // the part of a frame's body that runs on a client (app/play_session's frame)
    void localActors(const od::port::PlayerEnv& env, od::port::ActorContext& context, bool late);
    // the end of the frame's body: the lock-on and the radar from the world's actors, this tick's shots to the world
    void endFrame();
    // after the ticks of a frame: the room's other players, for the ghost slots
    void showPlayers(GhostView& ghosts, uint64_t localFingerprint, uint64_t nowNs);
    // test instrument: a second player in the world who follows this one at an offset (game units)
    void addGuest(int dx, int dz);
    // test instruments: at this local tick the buster's attack level is this (an equipment change the world must wear); a
    // special weapon equipped at the first tick
    void testLevelAt(uint32_t tick, int8_t level) { testLevelTick_ = tick; testLevel_ = level; }
    void testWeapon(int weapon) { testWeapon_ = weapon; }
    int takeTestWeapon() { const int w = testWeapon_; testWeapon_ = -1; return w; }

private:
    std::vector<uint8_t> playerState() const;
    void joinRoom();
    void handle(const WorldMessage& m, uint64_t nowNs);

    std::unique_ptr<WorldPipe> pipe_;
    CoopHost* coordinator_ = nullptr; // pipe_, on the host
    uint8_t playerId_ = 0;
    bool joined_ = false;
    std::vector<uint8_t> hostSave_, lastEquipment_;
    bool hostSaveWhole_ = false;
    std::optional<WorldRoom> room_;
    std::unique_ptr<od::port::PoolNetActors> actors_;
    std::unique_ptr<od::port::NetStateClient> state_;
    std::unique_ptr<od::port::NetRenderClock> clock_;
    od::port::NetLocalRunFailures failures_;
    size_t failuresLogged_ = 0;
    struct OwnShot {
        bool live = false;
        uint16_t serial = 0;
    };
    std::array<OwnShot, 16> ownShots_{};
    uint16_t nextSerial_ = 1;
    od::port::NetPlayerReport reported_{};
    bool shownAny_ = false;
    uint32_t shown_ = 0;
    uint64_t nextTickNs_ = 0;
    uint32_t localTick_ = 0;
    od::port::NetPlayerWatch watch_;
    od::port::NetPlayerSender uplink_, guestUplink_;
    std::optional<std::pair<int, int>> guest_;
    static constexpr uint8_t kGuestId = 7;
    struct Other {
        od::port::NetPlayerReceiver from;
        od::ghost::GhostBuffer buffer;
        std::array<od::port::NetPlayerTrack, 2> tracks{};
        int64_t stepped = -1;
        bool joined = false;
    };
    void otherPlayerRules(uint8_t id, Other& o, const od::port::PlayerSnapshot& shown);
    bool ownEvent_ = false; // an event the world asked of this player runs here
    od::port::Fixed16Vec3 eventPoint_{};
    uint32_t testLevelTick_ = 0;
    int8_t testLevel_ = 0;
    int testWeapon_ = -1;
    std::array<Other, 8> others_{};
};

} // namespace od::app
