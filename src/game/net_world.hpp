// The game's side of netplay's shared world (app/netplay_world drives it; none of this is the exe's): the app sets
// these hooks between ticks, the game calls them at its choke points. Every hook is off by default: without netplay the
// game is the exe's.
//
// One world simulates, the host's. Its shared actors -- the combat actors of pools 0x20 and 0x40: those that submit a
// body with a touch damage or an enemy attack (game/net_puppets) -- are sent to the clients, whose own copies of them
// are freed (`mirror`): a client shows the host's as puppets. The cutscene cast is never shared (each machine runs the
// cutscene on its own).
#pragma once

#include <cstdint>
#include <functional>

#include "engine/routine_ids.hpp"

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

struct NetWorldHooks {
    bool mirror = false;    // client: the host's world shown; local shared actors freed, encounters and wall damage held
    bool holdDeath = false; // netplay: the dead player holds (no game over); the app respawns it
    std::function<bool(uint8_t id)> scriptGate;              // client: false = the start is refused
    std::function<void(uint8_t id)> scriptStarted;           // host: a script started
    std::function<void(const PickupSpawn&)> pickupSpawned;   // host: a drop's pickup spawned
    std::function<void(uint32_t result, int attack)> wallHit; // client: a wall hit, sent to the host instead of queued
    std::function<void(const WallStep&)> wallBroken;         // host: a segment took its next step
};
NetWorldHooks& netWorldHooks();

// the player is dead and held (holdDeath)
bool playerDown();
// game/player/player_update: the held player back: hit points full, the status cleared, at its room's arrival point
// (the transition request's destination), in its init state
void playerRespawn();

} // namespace od::port
