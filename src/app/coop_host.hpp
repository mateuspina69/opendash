// The way from a player's game to their world. On the host: its worlds, directly (and the clients' messages routed to them).
// On a client: the session to the host, which routes them.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "app/world_host.hpp"
#include "app/world_link.hpp"
#include "net/session.hpp"

namespace od::app {

class WorldPipe {
public:
    virtual ~WorldPipe() = default;
    // the session has given this game its player id (a host or a game with no session: at once)
    virtual std::optional<uint8_t> playerId() const = 0;
    // the player enters the room in this state; its world starts if it has none
    virtual void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state) = 0;
    // what must arrive
    virtual void sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) = 0;
    // what the next one replaces
    virtual void sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) = 0;
    virtual void poll(uint64_t nowNs) = 0;
    // what came for this player since the last call
    virtual std::vector<WorldMessage> take(uint8_t playerId) = 0;
    // test instrument: this player, on joining, stays where they are instead of coming next to the host
    void setStays(bool stays) { stays_ = stays; }

protected:
    bool stays_ = false;
};

// The coordinator in the host's game: the worlds, this game's player directly, the session's clients routed
class CoopHost : public WorldPipe {
public:
    CoopHost(std::filesystem::path exe, std::filesystem::path storySave);
    // the session whose clients come through here (none: a game on its own)
    void useSession(od::net::Session* session) { session_ = session; }
    void testClearFlag(uint32_t flag) { worlds_.testClearFlag(flag); }
    void testFlagNow(uint32_t flag, bool set, uint8_t stage) { worlds_.testFlagNow(flag, set, stage); }
    void testStory(uint8_t story) { worlds_.testStory(story); }
    void setGateTeleport(bool teleport) { worlds_.setGateTeleport(teleport); }
    // the host's game has written a save: a game over goes back to it
    void hostSaved(std::span<const uint8_t> save);
    // the host's game started from this save (a load, a new game): the session's worlds and clients start over from it
    void hostLoaded(std::span<const uint8_t> save, uint64_t nowNs);
    // everyone was down: the host's game goes on from this save
    std::optional<std::vector<uint8_t>> takeReload();
    // the host's game state as a save image, now (none: no game is played): what the session is, never a file it began with
    void setLiveState(std::function<std::optional<std::vector<uint8_t>>()> live) { live_ = std::move(live); }
    // the host's game is in this room, its first field tick still to run: a world of the room starts from the state now
    // (what that tick changes -- a room's script, its flags -- the world's own first tick does as the game does)
    void hostBound(WorldRoom room);

    std::optional<uint8_t> playerId() const override { return uint8_t(0); }
    void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state) override;
    void sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void poll(uint64_t nowNs) override;
    std::vector<WorldMessage> take(uint8_t playerId) override;

private:
    void fromClient(uint8_t id, const WorldMessage& m, uint64_t nowNs);
    void sendSave(uint8_t id, uint64_t nowNs);
    // the host's live state (else the last one known: the game is not played now), and the file a world starts from
    const std::vector<uint8_t>& liveSave();
    // ... the file a world of this room starts from (the state the host's game entered it with, when it is there)
    void liveSaveFile(WorldRoom room);
    struct Bound {
        WorldRoom room;
        std::vector<uint8_t> save;
    };
    std::optional<Bound> bound_;

    WorldHost worlds_;
    std::vector<uint8_t> save_;    // the host's state as last known (its live state while it plays)
    std::vector<uint8_t> written_; // the host's last written save (or the one its game started from): a game over's
    std::function<std::optional<std::vector<uint8_t>>()> live_;
    od::net::Session* session_ = nullptr;
    std::set<uint8_t> clients_;
    bool hostPlaying_ = false;    // the host's game has started (until then a client waits in its lobby)
    // the clients told a save they have not started from yet: what they send is of a game the session no longer has,
    // dropped until they enter the save's own room
    std::set<uint8_t> stale_;
    std::set<uint8_t> told_;      // the players told the session's save (on connecting, at the title or in a game)
    std::set<uint8_t> placeNext_; // ... who come next to the host when they enter (the session started over)
    std::filesystem::path savePath_, lastSavePath_;
    std::optional<std::vector<uint8_t>> reload_;
};

// A client's way: the session to the host
class SessionPipe : public WorldPipe {
public:
    explicit SessionPipe(od::net::Session& session) : session_(session) {}

    std::optional<uint8_t> playerId() const override;
    void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state) override;
    void sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void poll(uint64_t nowNs) override;
    std::vector<WorldMessage> take(uint8_t playerId) override;

private:
    od::net::Session& session_;
    uint64_t nowNs_ = 0;
    std::vector<WorldMessage> inbox_;
};

} // namespace od::app
