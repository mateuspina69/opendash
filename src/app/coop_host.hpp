// The way from a player's game to their world. On the host: its worlds, directly (and the clients' messages routed to them).
// On a client: the session to the host, which routes them.
#pragma once

#include <cstdint>
#include <filesystem>
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
    void testStory(uint8_t story) { worlds_.testStory(story); }
    void setGateTeleport(bool teleport) { worlds_.setGateTeleport(teleport); }
    // the host's game has written a save: a game over goes back to it
    void hostSaved(std::span<const uint8_t> save);
    // everyone was down: the host's game goes on from this save
    std::optional<std::vector<uint8_t>> takeReload();

    std::optional<uint8_t> playerId() const override { return uint8_t(0); }
    void enter(uint8_t playerId, WorldRoom room, const std::vector<uint8_t>& state) override;
    void sendReliable(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void sendLatest(uint8_t playerId, WorldMessageType type, std::vector<uint8_t> bytes) override;
    void poll(uint64_t nowNs) override;
    std::vector<WorldMessage> take(uint8_t playerId) override;

private:
    void fromClient(uint8_t id, const WorldMessage& m, uint64_t nowNs);
    void sendSave(uint8_t id, uint64_t nowNs);

    WorldHost worlds_;
    std::vector<uint8_t> save_;
    od::net::Session* session_ = nullptr;
    std::set<uint8_t> clients_;
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
