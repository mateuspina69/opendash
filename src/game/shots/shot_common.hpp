// What the player's shot classes (pool 0x10, game/shots/*) share: the shot slot's fields past the mesh layout (the hit
// box, the aim the arm hands it, the locked target), the stat block every class keeps (range, attack, level), and the
// routines several classes call -- the spawn at a bone point, the homing on the target (exe 0x5732f0), the wall test
// with its breakable-wall hit, the hit entries, the hit spark (exe 0x5704f0) and the blast (exe 0x56e5d0) at the shot.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

struct Player;

// The shot's fields past the mesh layout (the same for every class; written by the arm, game/player/player_weapons)
struct PlayerShotFields {
    explicit PlayerShotFields(const ShotActor& actor) : PlayerShotFields(const_cast<ShotActor&>(actor)) {}
    explicit PlayerShotFields(ShotActor& actor)
        : hitBox(actor.hitBox), aimPoint(actor.aimPoint), aimOffset(actor.aimOffset), target(actor.target) {}
    HitBox& hitBox;                 // its hit entry's result (contact: where a deflecting body was hit)
    ShortVec4& aimPoint;            // the point the arm aimed it at (the exe copies it as two dwords)
    ShortVec3& aimOffset;           // the aim point relative to the target (the homing aims there)
    ActorRef<ActorHeader>& target;  // the target actor (any pool; null: none)
};

// The stat block of a shot class (+0xbc for most, +0x74 / +0x64 for the held weapons): the weapon's stats at the shot
struct ShotStats {
    uint16_t range = 0;  // the ticks left (or the beam's steps)
    uint16_t attack = 0; // the hit entry's damage
    int16_t level = 0;   // the weapon's attack level (the shot's size)
};

// the stat block filled from the special weapon in use: its range, its attack, its attack level
void fillShotStats(ShotStats& stats);
// the shot at one of the player's bone points (<< (16 - the stage shift)) in its position and previous position, dy
// added to the height first
void placeAtBone(ShotActor& shot, uint32_t bone, int dy);
// the shot's rotation = the player's aim (pitch, yaw, roll 0)
void aimAlongPlayer(ShotActor& shot);
// the shot stopped halfway between its last two positions (it hit a body)
void stopHalfway(ShotActor& shot);
// the aim point relative to the shot's target, when it has one
void aimRelativeToTarget(ShotActor& shot);
// the target dropped when its slot was freed (the dispatchers' first check)
void dropDeadTarget(ShotActor& shot);
// the last move of the platform the player stands on, turned into the player's facing frame (x / z)
void platformMove(int& x, int& z);
// the target's point the shot aims at: its position + the aim offset (16.16)
void targetAimPoint(ShotActor& shot, uint32_t target, int32_t out[3]);
// exe 0x5732f0(shot, target, step, flags): the shot turned toward the target's aim point -- the pitch by step <<
// (variant), the yaw by twice that; an angle off by 0x200 or more drops the target unless flags & kHomingKeepTarget,
// flags & kHomingAlwaysTurn turns anyway; true = pitch and yaw reached
constexpr uint8_t kHomingKeepTarget = 1;
constexpr uint8_t kHomingAlwaysTurn = 2;
bool homeOnTarget(ShotActor& shot, uint32_t target, int step, uint8_t flags);
// exe 0x40f570 < 0: the shot's move ended in a wall -- the breakable-wall hit queued with its attack, the push-out
// (when asked); true = blocked
bool shotHitWall(const PlayerEnv& env, ShotActor& shot, uint16_t attack, bool pushOut = true);
// exe 0x5e3810 on the shot: its position moved along its rotation
void moveShot(ShotActor& shot, int x, int y, int z);
// the shot drawn this tick: airborne, off any platform
void markFlying(ShotActor& shot);
// the hit box cleared and its previous x / z set after a submit
void clearHitBox(ShotActor& shot);
// exe 0x5e4120 at the shot's position (its slot as the owner)
void submitShotHit(ShotActor& shot, uint32_t shape, uint32_t attack);
// exe 0x5e4190 from the shot's position to its previous one, radius r at both ends (offset by `offset` when given)
void submitShotCapsule(ShotActor& shot, uint16_t r, uint32_t shape, uint32_t attack, const int16_t* offset = nullptr);
// the shot's position as the draw commands' xy word (y << 16 | x)
uint32_t drawXY(const ShotActor& shot);
// exe 0x5704f0 / 0x56ed50 / 0x56fe40(shot, stats, variant): the hit spark (pool 0xa0 global type 0) at the shot (+
// offset), its size by the level (the hunter seeker's crystal, a static, calls it too)
void spawnShotSpark(const Actor& shot, int16_t level, uint8_t variant, const int16_t* offset = nullptr);
// exe 0x56b880(shot, stats, variant): the burst (pool 0xa0 global type 1) at the shot, its size by the level (the
// buster's shot and the hunter seeker's crystal)
void spawnShotBurst(const Actor& shot, const ShotStats& stats, uint8_t variant);
// exe 0x56e5d0(shot, stats): a blast (static global type 7, variant 1) at the shot, its size / attack by the level
void spawnShotBlast(const ShotActor& shot, int16_t level);

} // namespace od::port
