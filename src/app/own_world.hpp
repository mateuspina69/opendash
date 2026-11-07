// The windowed game in a netplay session (co-op, always): the room's actors come from the session's world processes, which
// the host's game runs (app/world_host) from the game the host starts, through game/world_net_state; the game runs only
// its own Mega Man and what runs on a client (the effects). A client follows the host's save and room. Single player
// (net=off) never comes here, except the dev switch --dev-world-alone <save> (this game alone with a world process).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "app/coop_host.hpp"
#include "app/world_host.hpp"
#include "app/ghost_view.hpp"
#include "app/scene_pads.hpp"
#include "game/event_state.hpp"
#include "game/fade.hpp"
#include "game/presentation.hpp"
#include "game/save_menus.hpp"
#include "game/room_scripts.hpp"
#include "game/story_flags.hpp"
#include "game/ghost_buffer.hpp"
#include "game/player/net_player.hpp"
#include "game/player/remote_combat.hpp"
#include "game/world_net_state.hpp"

namespace od::app {

class OwnWorldClient {
public:
    OwnWorldClient(std::filesystem::path exe, std::filesystem::path storySave);
    ~OwnWorldClient();
    OwnWorldClient(const OwnWorldClient&) = delete;
    OwnWorldClient& operator=(const OwnWorldClient&) = delete;
    // the session this game is in: a host's clients come through its coordinator; a client's world is the host's
    void useSession(od::net::Session* session, bool host);
    // the host's save, when it differs from this game's: the game restarts from it (what exists is the host's)
    std::optional<std::vector<uint8_t>> takeHostSave(const std::vector<uint8_t>& current);
    // everyone was down: the save the game goes on from, when its fade is over
    std::optional<std::vector<uint8_t>> takeReload();
    // this game has written a save: on the host, it is what a game over goes back to
    void gameSaved(std::span<const uint8_t> image) { if (coordinator_) coordinator_->hostSaved(image); }
    // on the host: this game's state as a save image, now (none: not played): what the session's state is
    // this game's stage banks (app/stage_banks, encoded) and the taking of a world's
    void setStageBanks(std::function<std::vector<uint8_t>()> held, std::function<void(std::span<const uint8_t>)> take) {
        heldBanks_ = std::move(held);
        takeBanks_ = std::move(take);
    }
    void setLiveState(std::function<std::optional<std::vector<uint8_t>>()> live) {
        if (coordinator_) coordinator_->setLiveState(std::move(live));
    }
    // this game started from this save (a load, a new game): on the host the session starts over from it
    void gameStarted(std::span<const uint8_t> image);
    // a client away from a game (at its title): the host's save once it has come whole
    std::optional<std::vector<uint8_t>> takeHostSaveAway();
    // the host's game: the session's coordinator runs here
    bool hosts() const { return coordinator_ != nullptr; }
    // what went wrong in the session: in the log and a line on the screen
    void notice(const std::string& text);

    // the room's files are bound and its spawn has run: the local actors go, this room's world takes over
    void roomBound(int stage, int sub);
    // the room's logic is this room's world's (else this game runs the room itself: a race of its own)
    // ... and not while a story scene plays: every game runs the scene itself, as the normal game does
    // ... and not on the room's first field tick, whose scripts run here as in the normal game (roomScriptsRan)
    bool inWorld() const { return room_.has_value() && !localScene_ && !entering_; }
    // the room's scripts have run (game/transition roomScripts): on the room's first field tick, a scene they started
    // plays here from that tick, as in the normal game; else the room's world takes over
    void roomScriptsRan();
    // a scene the room started here that waits for everyone (the session's gate): nothing of the field is shown
    bool sceneHeld() const { return localScene_ && heldHere_; }
    // a story scene starts here (the world said so): its script, once (the game frees the world's actors, spawns the
    // room's own and starts the script as the normal game would)
    std::optional<uint8_t> takeSceneStart() { return std::exchange(sceneStart_, std::nullopt); }
    // a script of this game's own started while it was in the room's world (a message's, an arrival's): it plays here as
    // a scene, with the room's own actors again
    bool takeRoomRebuild() { return std::exchange(roomRebuild_, false); }
    // the room as this game entered it, before its first field tick: the story's flags and the room's logic (its
    // handler's steps), which a scene the world starts here plays the room's logic from again (what it spawns there --
    // a cast waiting by a lift -- is the scene's)
    struct RoomEntry {
        WorldRoom room;
        od::port::StoryFlags flags;
        od::port::RoomScriptState logic;
    };
    const RoomEntry* roomEntry() const { return roomEntry_ && room_ && roomEntry_->room == *room_ ? &*roomEntry_ : nullptr; }
    // the turn before a tick: the player's state to the world, what the world sent, the timeline up to now
    void turn(uint64_t nowNs);
    // a turn away from a game (the title, a load): the session's save and what it says; nothing of a room
    void awayTurn(uint64_t nowNs);
    // the part of a frame's body that runs on a client (app/play_session's frame)
    void localActors(const od::port::PlayerEnv& env, od::port::ActorContext& context, bool late);
    // the end of the frame's body: the lock-on and the radar from the world's actors, this tick's shots to the world
    void endFrame();
    // after the ticks of a frame: the room's other players, for the ghost slots
    void showPlayers(GhostView& ghosts, uint64_t localFingerprint, uint64_t nowNs);
    // test instrument: a second player in the world who follows this one at an offset (game units)
    void addGuest(int dx, int dz);
    // what the session has to say to this player, over the picture (between ImGui's frame calls)
    void drawNotice() const;
    // a scene that another player owns: this game's copy runs on the owner's pad
    void overridePad(uint16_t& buttons);
    void setFade(od::port::FadeTask* fade) { fade_ = fade; }
    // test instrument: on joining, stay where this game is
    void setGateTeleport(bool teleport) { if (coordinator_) coordinator_->setGateTeleport(teleport); }
    void testForceAt(uint32_t tick) { testForceTick_ = tick; }
    void testBossFight() { testBossFight_ = true; }
    void testStory(uint8_t story) { if (coordinator_) coordinator_->testStory(story); }
    void testClearFlag(uint32_t flag) { if (coordinator_) coordinator_->testClearFlag(flag); }
    // test instrument: at this local tick the session sets or clears this flag (what a room's logic does, as the session
    // shares it)
    void testFlagAt(uint32_t tick, uint32_t flag, bool set) { testFlags_.push_back({tick, flag, set}); }
    // a flag the game starts with (--flag, --clear-flag): the session's from the start, so a world started now has it
    void startFlag(uint32_t flag, bool set, uint8_t stage) { if (coordinator_) coordinator_->testFlagNow(flag, set, stage); }
    void stayOnJoin() { if (pipe_) pipe_->setStays(true); stays_ = true; }
    // test instrument: at this local tick the player's life is this
    void testLifeAt(uint32_t tick, int16_t life) { testLifeTick_ = tick; testLife_ = life; }
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
        std::vector<uint8_t> equipment; // what they wear (the world's NetEquip of them): their shots here are theirs
    };
    size_t gaugesHeard_ = 0; // the enemy gauges the world last showed (logged when it changes)
    // the ghost slot each other player of this machine's table is drawn in (-1: none), and the ghosts: their files are
    // what those players pose on here
    std::array<int, od::port::kPlayerSlots> ghostSlotOfTable_{};
    GhostView* ghosts_ = nullptr;
    od::LoadedFileData* filesOfTable(size_t table);
    void otherPlayerRules(uint8_t id, Other& o, const od::port::PlayerSnapshot& shown);
    void downedTurn();
    bool reloading_ = false;
    std::vector<uint8_t> reloadSave_;
    void gateTurn();
    std::optional<std::array<int16_t, 3>> gateGlow_;
    int gateGlowTicks_ = 0;
    bool bossFight_ = false, testBossFight_ = false;
    std::string notice_;
    int noticeTicks_ = 0;
    bool saveEnded_ = false;   // the last save that came is whole: a chunk now starts another
    std::string alert_;        // what went wrong (notice), shown until alertUntilNs_
    uint64_t alertUntilNs_ = 0;
    bool gateAsked_ = false;
    bool gateAtDoor_ = false; // ... at a door actor: when the gate opens its own script plays here (doorPass_: let it by)
    bool doorPass_ = false;
    std::optional<std::array<int16_t, 5>> doorArrival_; // stage, sub, x, y, z the coordinator gave this player: the spot
                                                         // beside the others where the door's script arrives
    od::port::Fixed16Vec3 gatePlace_{};
    uint32_t testForceTick_ = 0;
    struct TestFlag {
        uint32_t tick = 0, flag = 0;
        bool set = false;
    };
    std::vector<TestFlag> testFlags_;
    void sceneTurn();
    void sceneEnd(const std::vector<uint8_t>& at);
    void movedTo(const std::vector<uint8_t>& to);
    bool stays_ = false, warping_ = false;
    od::port::FadeTask* fade_ = nullptr;
    bool scene_ = false;
    uint8_t sceneOwner_ = 0;
    ScenePads scenePads_;      // the owner's pad, as this game's copy of the scene plays it
    uint16_t sentPad_ = 0;     // the owner: its pad as last sent
    uint32_t sceneTick_ = 0;   // the owner: the scene's ticks run (the stamp of what it sends)
    // a story scene this game runs itself: from the world's start to its end and the script's here (then the world again)
    bool localScene_ = false;
    std::optional<uint8_t> sceneStart_;   // the script to start, for the game to take
    std::optional<WorldRoom> sceneRoom_;  // the room it started in
    uint8_t sceneScript_ = 0;             // its script
    bool roomRebuild_ = false;
    bool sceneRan_ = false;               // its script has run here (its end is the script's end, not its start)
    bool sceneMoved_ = false;             // its script went to another room, whose first field tick has not run yet
    // the scene this game played to its end before its room's world did (that world's copy of it may still run)
    struct PlayedScene {
        WorldRoom room;
        uint8_t script = 0;
    };
    std::optional<PlayedScene> playedScene_;
    std::optional<RoomEntry> roomEntry_;
    // the world's event this game plays: its talker's menus (game/event kEventMenus*), their state here
    uint8_t eventMenus_ = 0, eventHints_ = 0;
    od::port::SaveMenuBlock eventSaveMenu_;
    std::function<std::vector<uint8_t>()> heldBanks_;
    std::function<void(std::span<const uint8_t>)> takeBanks_;
    // the world's actor this Mega Man lifted (its slot there, its type)
    struct Lifted {
        uint8_t slot = 0, type = 0;
        std::optional<std::array<int32_t, 3>> place; // how its class places it at the hands: x, the height word, z
    };
    std::optional<Lifted> lifted_;
    static constexpr uint8_t kLiftRefused = 1; // NetLift's byte: this game could not lift what the world's class took
    void liftTurn();
    void storyMoveTurn();
    bool storyMoveSent_ = false; // the room change of this player's own story was told
    void liftPlace();
    std::optional<std::vector<uint8_t>> sceneEndAt_; // the world's end: where its Mega Man stands, its owner
    bool sceneHeard_ = false; // the world has started it too (a scene started here: the world's start confirms it)
    bool heldHere_ = false;   // started here, held until the world starts it (it waits for everyone)
    bool entering_ = false;   // the room is bound, its first field tick has not run its scripts yet
    void localSceneTurn();
    void startedHere(uint8_t script);
    void freeLocalActors();
    void bindWorld(int stage, int sub);
    std::vector<uint8_t> pendingMove_;
    uint32_t testLifeTick_ = 0;
    int16_t testLife_ = 0;
    int downTicks_ = 0;                  // this player has been down this long
    std::optional<uint8_t> reviving_;    // this player holds Use by this teammate
    int reviveTicks_ = 0;
    bool ownEvent_ = false; // an event the world asked of this player runs here
    od::port::Fixed16Vec3 eventPoint_{};
    uint32_t testLevelTick_ = 0;
    int8_t testLevel_ = 0;
    int testWeapon_ = -1;
    std::array<Other, 8> others_{};
};

} // namespace od::app
