# ADR 0006 — Co-op netplay: one world on the host, everyone in its room

Date: 2026-10-25. Status: accepted.

## Context

Netplay milestone 1 (`docs/netplay.md`, ADR 0005) shows remote players as ghosts: no collision, no damage, no shared
world. Milestone 2 is a true co-op for up to 8 players. The user's requirements:

- One shared world. Players stay together: free roaming and per-room authority are rejected.
- The room changes only when every living player stands in the exit zone. No confirmation dialog. Players waiting at an
  exit trigger a notice on everyone's screen ("Rui is at the exit (1/3)"), a glow on that exit and a marker on the
  game's minimap (the minimap is not ported yet; the enemy radar is not used for this).
- A player at 0 life is downed and a teammate revives them (Borderlands style). Game over only when every player is
  down.
- The host's game decides what exists; each player chooses their own equipment from it. The parts, special weapons,
  upgrade levels and costumes are the host's game's, not the client's save. Each player equips what they like in
  their own status menu, and two players may equip the same weapon.
- The story works like Dying Light's co-op: everyone is Mega Man, and canonically there is only one, so the story and
  its scenes play once, for everyone together, with one Mega Man in them.
- A player can join a session at any moment and comes in at once.
- No friendly fire.
- Drops and chests are shared. Per-player loot may come later, so a flag's ownership (shared / per player) must be a
  property of the flag, not of the code that sets it.

What the code imposes (survey of 2026-10-25):

- `player()` (`src/game/player/player_state.cpp`) is one static `Player` with the fake exe address `kPlayer`
  (0xa71d20). About 1400 call sites in about 300 files (432 bind `Player& p = player()`). The actor pools resolve
  `kPlayer` specially and slot class 0 means the player. Actors store their target as `kPlayer`.
  - Player-internal code (~130 sites) already takes `Player&`.
  - Local view (camera, HUD, listener; ~90) stays the local player.
  - World code that looks at "the player" (enemy AI, pickups, carry, triggers; ~650–700) needs "any / nearest /
    each player". About 20 per-file copies of the exe's distance / angle-to-player helpers cover a large share.
  - Scripts and cutscenes (~430) move, freeze or hide the player.
- Hits are already per actor: every actor submits boxes to the hit list and reads its own result. Damage to N players
  is cheap. `kHitAttackByPlayer` marks any player's attack.
- One global RNG (about 1100 calls in 177 files) serves enemies, drops and the player's effects. Two machines that
  both simulate the world drift apart, so clients must not simulate it.
- The world state is spread across about 126 static accessors and 28 globals. Saving and restoring all of it
  (rollback) is out of reach.
- The renderer reads a small set of actor fields: header, transform, model, parts, posed bone blocks. A
  class-independent replica of every model-bearing actor is possible. Effects (pickups, sparks, explosions) are drawn
  from class state by `od::fx::collect`; their output primitives are what replicates.

## Decision

- **Host-authoritative world.** Only the host simulates the room: actors, AI, RNG, scripts, flags, drops, chests,
  transitions, music. Rollback and lockstep are rejected: no full-state save/restore, and 250 ms of input delay on
  the Curitiba – Lisbon link.
- **Each player simulates their own Mega Man** (input, movement, animation, their own shots' visuals), so control
  responds at once. The host holds a **remote player** for each client: a real `Player` in its world, driven by that
  client's state each tick instead of by a pad. It submits hit boxes, enemies target it, it collects pickups and stands
  in exit zones.
- **N players in the simulation:** the `Player` singleton becomes a fixed table of players (index 0 local) with stable
  ids, `kPlayer` and slot class 0 become a player range, and each call site moves to one of: the player passed in, the
  local player, nearest / each player, or a script's explicit choice.
- **Damage:** the host's hit pass decides hits on any player and sends the hit to that player's owner, who applies it
  (`takeHit`, knock-down) and reports the result. A client's shots are fired in the host's world from the client's
  fire events; the client draws its own shot at once. No friendly fire: players' attacks are not tested against other
  players.
- **Equipment from the host's pool, chosen by each player:** the host's game owns what is unlocked (parts, special
  weapons and their upgrade levels, costumes) and replicates it. Each player opens their own status menu and equips
  from that pool; their choice travels to the host, whose remote player wears it (the look already travels in the
  player state). Equipping is not exclusive. Life, maximum life and weapon energy follow each player's own equipment.
  Zenny, items and the inventory are the host's, shared; a purchase or a development is the host's game's.
- **Client replica mode:** a client loads the same room but runs no world update. It shows the host's actors from a
  class-independent replica (header, transform, model, parts, bone blocks; `ActorRef`s as slot ids; `DataPtr`s as
  they are, checked by the loaded-data fingerprint) and the host's effect primitives. Its Mega Man collides with the
  room geometry and the replicated solids.
- **Presentation from the host:** camera of scripted scenes, message windows, fades, sound and music events, screen
  effects.
- **One Mega Man in the story (Dying Light rule):** scenes and scripts run on the host with the host's Mega Man as the
  only Mega Man, and every client watches the same scene. While a scene runs, the other players are frozen and hidden.
  When it ends, they are placed next to the host's Mega Man. Scripts need no per-scene policy for the others.
- **Joining at any moment:** the joining client loads the host's current room and appears next to the host's Mega Man.
  During a scene it watches the scene and appears when the scene ends.
- **Exits:** the trigger arms instead of firing; it fires when every living player is in the zone. A downed player is
  carried along. Clients follow the host's transition request (`TransitionRequest` and the game state's stage, room,
  mode and step).
- **Downed and revive:** the hp < 0 path of the knock-down ends in a downed state instead of `Dead`. A teammate holding
  Use next to them for about 3 s revives them with part of their life. No bleed-out timer. `GameMode::GameOver` starts
  only when every player is down.
- **Global state** (story and extra flags, zenny, chests) lives on the host and replicates to clients. Each flag
  carries an ownership class, all shared for now.

## Plan (estimates in weeks of sessions)

1. Player table and identity: `Player` instances, ids, pool resolution, single-player identical (golden tests). 1
2. Call sites in world code: nearest / each player helpers, pickups, carry, triggers, lock-on, hit list. 1.5–2
3. Client replica mode and actor / effect replication; the client's Mega Man against replicated solids. 2
4. Remote players on the host, damage and shot events, no friendly fire. 1
4b. Per-player equipment: each player's status menu over the host's pool, the choice sent to the host. 0.5–1
5. Presentation and scripts: camera, windows, sound, fades, the others frozen and hidden during scenes, joining
   at any moment. 1.5–2
6. Global state: flags with ownership, zenny, chests, drops. 0.5–1
7. Downed, revive, everyone-down game over. 1
8. Exit gathering: arm / fire, notice, door glow. Minimap marker once the minimap is ported (its own task). 0.5–1

Total about 9.5–12 weeks. Single player stays exactly the game after every step.
