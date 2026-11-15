// The way from a player's game to their world. On the host: its worlds, directly (and the clients' messages routed to them).
// On a client: the session to the host, which routes them.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <thread>
#include <utility>
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
    // the player enters the room in this state, its game holding these stage banks (app/stage_banks, encoded); the
    // room's world starts (with those banks) if it has none
    virtual void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state, const std::vector<uint8_t>& banks) = 0;
    // what must arrive
    virtual void sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) = 0;
    // what the next one replaces
    virtual void sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) = 0;
    virtual void poll(uint64_t nowNs) = 0;
    // what was sent since the last flush leaves now: once at the end of a turn, so that all of it is one datagram
    virtual void flush(uint64_t nowNs) = 0;
    // what came for this player since the last call
    virtual std::vector<WorldMessage> take(uint8_t playerId) = 0;
    // the session's datagrams so far (none: a game with no session)
    virtual od::net::Session::Traffic traffic() const = 0;
    // test instrument: this player, on joining, stays where they are instead of coming next to the host
    void setStays(bool stays) { stays_ = stays; }

protected:
    bool stays_ = false;
    // each player's entries into a room, counted by enter: a world's message of its room for an entry before the player's
    // last is of a room this game has left (or bound anew) -- held still on the way when it did, it is not played
    std::array<uint8_t, 256> entries_{};
    uint8_t nextEntry(uint8_t playerId) { return ++entries_[playerId]; }
    bool stale(const WorldMessage& m) const { return worldRoomMessage(m.type) && m.entry != entries_[m.playerId]; }
    // ... those taken out of what came
    void dropStale(std::vector<WorldMessage>& in);
};

// The coordinator in the host's game: the worlds, this game's player directly, the session's clients routed.
// What goes between the clients and their worlds is passed on by a thread of its own, as it arrives: the host's game may
// be loading a room, or between two frames. Only what needs the game's state (a client's entry into a room, the save a
// new player is told, a game over's way back) waits for the game's thread, in poll. Every call holds the one lock.
class CoopHost : public WorldPipe {
public:
    CoopHost(std::filesystem::path exe, std::filesystem::path storySave);
    ~CoopHost() override; // the relay thread ended
    // the session whose clients come through here (none: a game on its own, or the session is about to end)
    void useSession(od::net::Session* session) {
        const std::lock_guard lock(mutex_);
        session_ = session;
    }
    void testClearFlag(uint32_t flag) {
        const std::lock_guard lock(mutex_);
        worlds_.testClearFlag(flag);
    }
    void testFlagNow(uint32_t flag, bool set, uint8_t stage) {
        const std::lock_guard lock(mutex_);
        worlds_.testFlagNow(flag, set, stage);
    }
    void testStory(uint8_t story) {
        const std::lock_guard lock(mutex_);
        worlds_.testStory(story);
    }
    void testEnemyLife(int life) {
        const std::lock_guard lock(mutex_);
        worlds_.testEnemyLife(life);
    }
    void setGateTeleport(bool teleport) {
        const std::lock_guard lock(mutex_);
        worlds_.setGateTeleport(teleport);
    }
    // the host's game has written a save: a game over goes back to it
    void hostSaved(std::span<const uint8_t> save);
    // the host's game started from this save (a load, a new game): the session's worlds and clients start over from it
    void hostLoaded(std::span<const uint8_t> save, uint64_t nowNs);
    // everyone was down: the host's game goes on from this save
    std::optional<std::vector<uint8_t>> takeReload();
    // the host's game state as a save image, now (none: no game is played): what the session is, never a file it began with
    // (asked on the game's thread only: in poll and in what the game calls here)
    void setLiveState(std::function<std::optional<std::vector<uint8_t>>()> live) {
        const std::lock_guard lock(mutex_);
        live_ = std::move(live);
    }
    // the host's game is in this room, its first field tick still to run: a world of the room starts from the state now
    // (what that tick changes -- a room's script, its flags -- the world's own first tick does as the game does)
    void hostBound(WorldRoom room);

    std::optional<uint8_t> playerId() const override { return uint8_t(0); }
    void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state, const std::vector<uint8_t>& banks) override;
    void sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    // the game's thread's turn: a relay turn, then what waited for the game's state
    void poll(uint64_t nowNs) override;
    void flush(uint64_t) override {} // (a relay turn passes on what the worlds and the clients sent, and sends it)
    std::vector<WorldMessage> take(uint8_t playerId) override;
    od::net::Session::Traffic traffic() const override {
        const std::lock_guard lock(mutex_);
        return session_ ? session_->traffic() : od::net::Session::Traffic{};
    }
    // the longest time between two relay turns since the last call
    uint64_t takeRelayGapNs() {
        const std::lock_guard lock(mutex_);
        return std::exchange(relayGapMaxNs_, 0);
    }

private:
    // the relay thread: waits for a datagram of the session or of a world, then a relay turn
    void relayLoop();
    // the session and the worlds read, the clients' messages to their worlds, the worlds' to their clients
    void relayTurn(uint64_t nowNs);
    // what the relay left for the game's thread
    void gameTurn(uint64_t nowNs);
    // a client's message: to its world now, or held for the game's thread (a room's entry, and all of that client's
    // after one that is held: in order)
    void fromClientOrHold(uint8_t id, WorldMessage m, uint64_t nowNs);
    void fromClient(uint8_t id, const WorldMessage& m, uint64_t nowNs);
    void sendSave(uint8_t id, uint64_t nowNs);
    // the host's live state (else the last one known: the game is not played now)
    const std::vector<uint8_t>& liveSave();
    // ... what a world of this room starts from (the state the host's game entered it with, when it is there)
    void liveSaveForWorlds(WorldRoom room);
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
    std::vector<od::net::PeerInfo> peers_; // everyone else in the session, as of membership peersAt_
    uint32_t peersAt_ = 0;
    bool peersKnown_ = false;
    bool hostPlaying_ = false;    // the host's game has started (until then a client waits in its lobby)
    // the clients told a save they have not started from yet: what they send is of a game the session no longer has,
    // dropped until they enter the save's own room
    std::set<uint8_t> stale_;
    std::map<uint8_t, std::pair<uint8_t, uint8_t>> sentRoom_; // the stage and room of the save each client was last sent
    std::set<uint8_t> told_;      // the players told the session's save (on connecting, at the title or in a game)
    std::set<uint8_t> placeNext_; // ... who come next to the host when they enter (the session started over)
    std::array<WorldParts, od::net::kMaxPlayers> parts_{}; // each client's message on its way in parts
    std::array<std::vector<WorldMessage>, od::net::kMaxPlayers> held_{}; // each client's messages waiting for the game's thread
    std::optional<std::vector<uint8_t>> reload_;
    uint64_t lastRelayNs_ = 0, relayGapMaxNs_ = 0;

    mutable std::mutex mutex_; // every public call and the relay's turn hold it
    std::atomic<bool> stop_{false};
    std::thread relay_; // (last: it runs on all of the above)
};

// A client's way: the session to the host
class SessionPipe : public WorldPipe {
public:
    explicit SessionPipe(od::net::Session& session) : session_(session) {}

    std::optional<uint8_t> playerId() const override;
    void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state, const std::vector<uint8_t>& banks) override;
    void sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void poll(uint64_t nowNs) override;
    void flush(uint64_t nowNs) override { session_.flush(nowNs); }
    std::vector<WorldMessage> take(uint8_t playerId) override;
    od::net::Session::Traffic traffic() const override { return session_.traffic(); }

private:
    od::net::Session& session_;
    uint64_t nowNs_ = 0;
    std::vector<WorldMessage> inbox_;
    WorldParts parts_; // the host's message on its way in parts
};

} // namespace od::app
