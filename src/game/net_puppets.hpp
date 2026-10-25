// Netplay's shared actors (none of this is the exe's; app/netplay_world drives it). The host simulates every actor of
// the field's slots of pool 0x20 (the enemies, the NPCs, the critters; not the cutscene cast, game/net_world
// netLocalOnly) and the projectiles of pool 0x40 whose class submitted a shared entry (game/net_world netSharedEntry:
// the enemies' shots; the classes learned stay learned).
//
// The host captures them after each tick (captureWorld). A client shows them as puppets: the same class spawned into a
// free slot of its own, its first tick run by the class (the init: the model, the bones), then the puppet's own tick instead of the
// class's -- the host's pose followed, the animation stepped toward the host's key, the host's hit entry submitted (its
// touch hurts the client's player through the normal hit pass), a local player attack's hit reported
// (takePuppetHits) and never applied. A client's own copies of a shared class are freed (netWorldHooks().mirror).
// A talk with the local player runs on the class's own tick, from its start to the event's end (the message, the turn
// to the player); a puppet cannot be lifted (its lifted states are the class's).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/routine_ids.hpp"

namespace od::port {

struct ActorHeader;
struct PlayerEnv;
enum class ActorPool : uint8_t;

// one hit entry of a shared actor's tick: a sphere (a, its shape's radius) or a capsule (a / ra, b / rb)
struct PuppetHitEntry {
    bool capsule = false;
    int16_t a[3] = {}, b[3] = {};
    uint16_t ra = 0, rb = 0;     // a capsule's radii
    uint32_t ownerShape = 0;     // the owner word and the shape without the slot byte
    uint32_t attack = 0;
};
constexpr int kPuppetHitEntries = 4; // per actor and tick (the Guardian: its body and two fists)

// one shared actor's state (host -> client)
struct PuppetState {
    uint8_t hostSlot = 0;   // the host's slot id (pool class + index)
    uint8_t pool = 0;       // 0x20 animated, 0x40 projectile
    uint16_t routine = 0;   // its class (RoutineId)
    uint32_t typeWord = 0;  // type / subtype / variant / spawn byte
    uint32_t paramsWord = 0;
    uint8_t loopFlags = 0;
    uint8_t flags = 0, runFlags = 0; // +0 (drawn, the model's bits) / +1 (shown): what the draw pass tests
    int32_t pos[3] = {};    // 16.16
    int16_t rotation[3] = {};
    int16_t hitPoints = 0, maxHitPoints = 0;
    uint8_t anim = 0, keyIndex = 0, shownParts = 0; // animated only
    uint16_t tint = 0;
    int16_t scale[3] = {};
    uint8_t hitCount = 0;       // its hit entries of the last tick
    PuppetHitEntry hits[kPuppetHitEntries];
};

// game/hit_list's submits: an entry of `slot` this tick, a sphere (b null) or a capsule (the shared entries teach their
// class)
void noteHitEntry(uint8_t slot, const int16_t a[3], const int16_t b[3], uint16_t ra, uint16_t rb, uint32_t ownerShape,
                  uint32_t attack);
// every puppet forgotten without a free (a new room's init has reused their slots)
void puppetsForget();
// game/hit_list's pass, at its end: the tick's entries are the last tick's
void noteHitPassEnded();
// a shared class: every field actor's (pool 0x20) but the cutscene cast's; a projectile's (pool 0x40) once one of its
// actors submitted a shared entry
bool netSharedClass(RoutineId cls, ActorPool pool);

// host: every live shared actor, its hit entry of the last tick
std::vector<PuppetState> captureWorld();

// client: a puppet of s spawned into a free slot of its pool (0: the pool is full); its class's init runs first
uint32_t puppetSpawn(const PuppetState& s);
// client: the state the puppet follows from its next tick (its class's state byte is never written)
void puppetApply(uint32_t actor, const PuppetState& s);
void puppetFree(uint32_t actor);
// every puppet freed (the host left, a new room)
void puppetsReset();
bool isPuppetActor(const ActorHeader& a);

// a puppet a local player attack hit (its box's result, the contact)
struct PuppetHit {
    uint32_t actor = 0;
    uint32_t result = 0;
    int16_t contact[3] = {};
};
std::vector<PuppetHit> takePuppetHits();

// game/actors updatePool's netplay steps: before the class (true: handled, the class does not run) / after it
bool netPoolBefore(const PlayerEnv& env, ActorHeader& actor, ActorPool pool, RoutineId cls);
void netPoolAfter(ActorHeader& actor, ActorPool pool, RoutineId cls);

} // namespace od::port
