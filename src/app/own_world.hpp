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

#include "app/world_host.hpp"
#include "game/player/remote_combat.hpp"
#include "game/world_net_state.hpp"

namespace od::app {

class OwnWorldClient {
public:
    OwnWorldClient(std::filesystem::path exe, std::filesystem::path storySave);

    // the room's files are bound and its spawn has run: the local actors go, this room's world takes over
    void roomBound(int stage, int sub);
    // the turn before a tick: the player's state to the world, what the world sent, the timeline up to now
    void turn(uint64_t nowNs);
    // the part of a frame's body that runs on a client (app/play_session's frame)
    void localActors(const od::port::PlayerEnv& env, od::port::ActorContext& context, bool late);
    // the end of the frame's body: the lock-on and the radar from the world's actors, this tick's shots to the world
    void endFrame();

private:
    static constexpr uint8_t kPlayerId = 1; // in the world; id 0 is the world's own, which leaves on a link

    std::vector<uint8_t> playerState() const;

    WorldHost host_;
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
};

} // namespace od::app
