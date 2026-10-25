// The game's side of netplay's shared world (app/netplay_world drives it; none of this is the exe's): the app sets
// these hooks between ticks, the game calls them at its choke points. Every hook is off by default: without netplay the
// game is the exe's.
//
// One world simulates, the host's. Its shared actors -- the combat actors of pools 0x20 and 0x40: those that submit a
// body with a touch damage or an enemy attack (game/net_puppets) -- are sent to the clients, whose own copies of them
// are freed (`mirror`): a client shows the host's as puppets. The cutscene cast is never shared (each machine runs the
// cutscene on its own).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include "engine/routine_ids.hpp"
#include "game/story_flags.hpp"

namespace od::port {

struct ActorHeader;
struct PlayerEnv;

// the classes that stay each machine's own whatever they submit (the cutscene cast: script-driven)
bool netLocalOnly(RoutineId cls);
// an entry that makes its actor shared: a body (group 2) with a touch damage, or an enemy attack (group 3)
bool netSharedEntry(uint32_t ownerShape, uint32_t attack);

// one pickup a drop spawned, as the client spawns the same one
struct PickupSpawn {
    int32_t pos[3] = {}; // 16.16
    uint8_t kind = 0, size = 0, variant = 0;
    uint32_t velocityXY = 0, velocityZSpare = 0;
    int16_t yaw = 0;
};
// game/pickups: the pickup exactly as recorded (no drop table, no random draw)
void spawnPickup(const PickupSpawn& p);

// one breakable wall segment's new step (game/breakable_walls)
struct WallStep {
    uint16_t segment = 0;
    uint8_t flags = 0;
    int16_t hitPoints = 0;
};
// game/breakable_walls: the step set as the host's tick set it (its reaction: the debris, the crumble)
void breakableWallApplyStep(const WallStep& w);

// one fire's strength (ST1E's fires, content/intro/flutter_fire), the fire known by its place
struct FireStep {
    int16_t x = 0, z = 0;
    int32_t strength = 0;
};
// client: the host's strength, taken by the fire's next tick (its own code puts it out)
void fireApplyStep(const FireStep& s);
std::optional<int32_t> takeFireStep(int16_t x, int16_t z);
// host: a client's hit on the fire there, added by the fire's next tick
void fireQueueHit(int16_t x, int16_t z, int damage);
int takeFireHits(int16_t x, int16_t z);
// a new room: none pending
void firesReset();

struct NetWorldHooks {
    bool mirror = false;    // client: the host's world shown; local shared actors freed, encounters and wall damage held
    bool holdDeath = false; // netplay: the dead player holds (no game over); the app respawns it
    std::function<bool(uint8_t id)> scriptGate;              // client: false = the start is refused
    std::function<void(uint8_t id)> scriptStarted;           // host: a script started
    std::function<void(const PickupSpawn&)> pickupSpawned;   // host: a drop's pickup spawned
    std::function<void(uint32_t result, int attack)> wallHit; // client: a wall hit, sent to the host instead of queued
    std::function<void(const WallStep&)> wallBroken;         // host: a segment took its next step
    std::function<void(int16_t x, int16_t z, int damage)> fireHit; // client: a hit on a fire, sent to the host instead
    std::function<void(const FireStep&)> fireChanged;               // host: a fire's strength after a hit
};
NetWorldHooks& netWorldHooks();

// the player is dead and held (holdDeath)
bool playerDown();
// game/player/player_update: the held player back: hit points full, the status cleared, at `pos` (16.16) facing `yaw`,
// in its init state
void playerRespawn(const int32_t pos[3], int16_t yaw);

// a PvP hit's attack on its victim: the damage scaled (25..100 %, at least 1), made solid -- the player is hurt only
// by a solid or strong attack (game/player/player_damage); a shot's own kind is the one the enemies take damage from
uint32_t netPlayerHitAttack(uint32_t attack, int scalePercent);

// The story a room loads with (its stage files, its actors, its scripts: game/stage_files, the room setups)
struct NetStory {
    uint8_t story = 0, nextStory = 0;
    StoryFlags flags;
};
NetStory liveStory();
// the live story set to s, the transient system flags kept (the transition's, the story menu's)
void showStory(const NetStory& s);
// netplay client: the host's story, shown in place of the player's own while it is set (the rooms load as the host's);
// the player's own is kept aside (its saves write it; the host leaving brings it back)
std::optional<NetStory>& netHostStory();
std::optional<NetStory>& netOwnStory();
// game/game_init, a game's start (a new game, a load, F2): the story just loaded is the player's own; the host's shown
void netStoryLoaded();

} // namespace od::port
