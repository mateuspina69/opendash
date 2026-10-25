# Netplay: a shared world (monsters, drops, PvP, cutscenes)

Date: 2026-10-25. Status: approved design, not yet planned.

## Goal

Today netplay shows the other players as ghosts and nothing else (docs/netplay.md: no collision, no damage, no
shared enemies or story). This design makes the players share one world: the same monsters in the same places, the
same drops, hits on monsters from every player, hits between players (friendly fire, configurable in the F1 menu),
cutscenes that everyone sees, and room changes that take everyone along.

**Only one world simulates: the host's.** The clients receive the monsters' state the way they receive the players'
today, and send the host what their own player does to it.

## Decisions

| Topic | Now | Must stay open for |
|---|---|---|
| Rooms | Party travel: every room change by the host takes the clients along; a client never changes room on its own | Each player in any room, the host running several rooms (every message carries a room key) |
| Death | A downed player respawns at the room's entry after 3 s; no game over in netplay | Down until every player is down, then a wipe (the host owns the rule) |
| Hit authority | The attacker's machine detects the hit and sends a claim; the host checks it and applies it | The host re-simulates every hit from the claim's shot data (no cheating) |
| Friendly fire | The host's checkbox and damage scale (25-100 %) in the F1 Netplay tab, saved in the ini, off by default | — |
| Drops | Rolled by the host; every player picks up their own copy | — |
| Shared objects | Monsters (with their shots), drops, breakables, chests | Other kinds through the same object message |
| Cutscenes | The host's start runs the same script on each client (own pace, own skip); the host waits until everyone has finished | — |
| Scenario menu | Only the host can change the stage or room (F1 Cenarios, PgUp/PgDn, `[` `]`); the clients follow | — |
| Client puppets | Generic puppets: one generic state per actor and a generic puppet update, with an optional extra per type | — |

## Layering

Unchanged rules (tools/check_layers.py): `src/net` carries opaque payloads and knows nothing of the game;
`src/game` includes neither `net/` nor `app/`; `src/app/netplay.*` is the only glue. The new game-side functions
(world capture, puppets, the external hit queue, the death policy, the role table) are plain game APIs the app
calls or sets, the same way it uses `capturePlayerSnapshot` and the `--god` hook today.

## 1. Protocol

`kProtocolVersion` 1 → 2. Two message types (src/net/protocol.hpp; the `Message` variant and `MessageType` stay in
the same order, as the static_assert requires):

- **`WorldState` (7)**, unreliable, newest wins, host → clients once per sim step. It was reserved and is now sent.
  Its payload holds the room key `{stage, sub, fieldPlay}`, the host's tick, a part index and count (one tick in up
  to 8 parts of at most 1200 bytes), and the puppet entries `{hostSlot, generation, actorType, pos, rotation, hp,
  animId/step/frame, drawFlags, hit box radius/height/attack word, per-type extra bytes}`. The generation counts up
  each time the host reuses a slot, so a client never mixes a dead monster with the next one.
- **`Event` (new, 12)**, reliable and in order. Its payload is `{kind, roomKey, body}`:

| Kind | Direction | Body |
|---|---|---|
| `HitClaim` | client → host | target (host slot + generation, or player id), damage, attack flags, weapon, shot origin + direction, tick |
| `PlayerHit` | host → victim | attacker id, damage (already scaled), attack flags, contact point |
| `Downed` | client → host | player id |
| `Respawn` | host → client | respawn point |
| `DropSpawned` | host → all | drop id, kind, amount, position |
| `ObjectChanged` | host → all | host slot + generation, new state (broken / opened) |
| `ScriptStarted` | host → all | script id |
| `ScriptDone` | client → host | script id |
| `Travel` | host → all (or one, on join) | stage, sub, transition, entry point |
| `Settings` | host → all (on join and on change) | friendly fire on/off, damage scale |

Session (src/net/session.*) gains `sendWorldState` / `takeWorldStates` and `sendEvent(to, payload)` / `takeEvents`
on the existing reliable channel. The host forwards nothing: it reads the events itself and sends what follows.
The payloads are encoded and decoded in src/app (a new `netplay_world.*`, next to netplay.cpp).

**Joining late:** the client receives `Settings`, then `Travel` to the host's room, then `WorldState` as usual. No
separate catch-up message.

## 2. Host

- **Roles.** A game-side table `netRole(actorType)` → `Puppet` (monsters, enemy shots), `Object` (breakables,
  chests), `Drop`, `Local` (everything else: effects, the player's own shots). A type that isn't in the table counts
  as `Local` and is logged once.
- **Capture.** After the last tick of each sim step (the `Netplay::afterTick` point), `od::port::captureWorld()`
  walks the pools and returns the `Puppet` and `Object` actors as puppet entries; netplay encodes them into
  `WorldState`.
- **Hit claims through the game's own hit code.** An incoming `HitClaim` goes into a game-side external hit queue.
  In `hitListPass()`, after the pair tests, every queued hit finds its target's own hit entry (by owner slot,
  checked against the generation) and calls the existing `applyHit` with a synthetic attacker built from the claim
  (damage, flags | `kHitAttackByPlayer`, contact point). On its next update, the target's own `hurt()` takes it as a
  player's hit: flinch, hp, death, the defeat count and the drop roll are the game's own code. A claim whose
  target's generation has changed, or whose room isn't the host's, is dropped and counted. *Later (host-decided
  hits):* the synthetic attacker becomes a re-simulation of the claim's shot (origin, direction, tick); the message
  doesn't change.
- **PvP.** A claim on a player: dropped while friendly fire is off. Otherwise the host scales the damage and sends
  `PlayerHit` to the victim, or queues it for its own player when the host is the victim.
- **Drops and objects.** Hooks on the drop spawn and on the breakables' and chests' state change send `DropSpawned` /
  `ObjectChanged`.
- **Settings.** The host keeps `{friendlyFire, scale}` and sends `Settings` on every join and every change.
- **Death.** `Downed` → `Respawn` (the room's entry, full hp) 3 s later. This rule is one host function (the later
  wipe goes there).
- **Cutscenes.** An observer on `scriptStart(id)`: a story script (id ≠ 0) sends `ScriptStarted(id)`. When the
  host's script ends, the host waits: the sim step runs no game ticks, the frame keeps drawing, and an overlay reads
  "aguardando <nomes>". It resumes once every client in the room has sent `ScriptDone`, has left, or 60 s have
  passed.
- **Travel.** Every successful `App::reload` on the host sends `Travel`. That covers the F1 Cenarios tab,
  PgUp/PgDn, `[` `]` and the game's own door transitions (`playerView.loadRoom`). A door carries its entry point; a
  menu or key jump carries the host's position after the load. On a join, the host sends `Travel` to that client
  through the same function.

## 3. Client

- **Holding back its own world.** While in a shared room, the room spawns and `encounterTick` create no actor
  whose role isn't `Local`. The client starts no script 0 on its own, and no story script either, with one
  exception: a script started by the player's own talk interaction (NPCs, shops, dialogue) runs locally, as they
  stay `Local`. Every other start (room and area triggers, encounters) is held back; the client gets those only
  through the host's `ScriptStarted`. The host likewise sends no `ScriptStarted` for a script its talk interaction
  started.
- **Scenario.** While a session is connected, `App::reload` refuses every caller except the `Travel` handler. In
  F1 Cenarios, the stage and room lists are disabled ("só o host muda o cenário"); PgUp/PgDn and `[` `]` do nothing
  (logged once).
- **Puppets.** A map host ID (slot + generation) → local slot. A puppet is created when its ID first appears, and
  freed when its generation changes or after 1 s missing. It is allocated wherever the pool has room, so it never
  collides with the client's own actors. The type's own update is replaced by one generic `puppetUpdate`:
  1. pose (pos, rotation, animation) interpolated from a buffer with a 3-tick delay, which is the ghost buffer
     generalised beyond `PlayerSnapshot`;
  2. a hit entry with the host's hit box and the host's body attack word: a touch hurts the client's player
     through the normal `applyHits` → `takeHit` path (the victim's own machine decides enemy damage to it);
  3. its own hit result: a hit from a local player attack sends `HitClaim` and draws the local spark; the hp never
     changes locally.
- **Enemy shots** are puppets, so they can hit the client (about 100 ms later than on the host, the interpolation's
  delay).
- **PvP stand-ins.** While friendly fire is on, every shown ghost (the `GhostView` checks passed) submits a group-2
  hit entry (the group that pairs with player attacks) with no contact damage. A local shot that hits one sends
  `HitClaim` naming that player. An incoming `PlayerHit` goes into the external hit queue and lands on the player's
  own entry in `hitListPass()`: the normal `takeHit` (armour, knockback, knockdown, spark). While friendly fire is
  off, nothing is submitted, so shots pass through.
- **Drops.** `DropSpawned` spawns the drop locally as an ordinary actor: the game's own pickup, so each player gets
  their own copy.
- **Objects.** `ObjectChanged` puts the object in its broken or opened state through its own routine. A hit on a
  breakable sends `HitClaim`, the same as a hit on a monster.
- **Death.** A death-policy flag (set by the app, off in single-player): `deadState` doesn't switch to game over;
  the player stays down and sends `Downed`. `Respawn` refills the hp and places the player.
- **Cutscenes.** `ScriptStarted(id)` → `scriptRun(id)` locally, at its own pace and with its own skip. When the
  script ends → `ScriptDone(id)`.
- **Travel.** `Travel` → the game's own room change (`reload`) to the host's room and entry point.

## 4. Settings and the F1 menu

- `od::Config`: `net_friendly_fire` (0/1, default 0) and `net_ff_scale` (25-100, default 100), read by
  `loadConfig()` and written by `saveConfigValue()` when changed. No command-line flag.
- F1 → Netplay, "Combate" block: `Checkbox("fogo amigo")` and `SliderInt("dano entre jogadores %", 25, 100)`.
  - **Host, or netplay off:** they can be edited; a change is saved and, during a session, sent as `Settings`.
  - **Client:** greyed out with the host's values and "definido pelo host"; the client's own ini values are ignored
    while connected.
- A "Mundo" status line: puppets shown, claims sent / applied / dropped, the wait for cutscenes and for whom.
- The game sees only the "submit PvP stand-ins" flag and the death policy, both set by the app.

## 5. Errors

- **Version:** protocol 2 and a new build id: old and new builds refuse each other with the existing `Refuse`.
- **Room key:** a `WorldState` or `Event` for a room other than the client's current one is ignored (for example
  during a `Travel` load). Puppets appear only when the room and the loaded-data fingerprint match, as for ghosts.
- **Loss:** `WorldState` newest wins; a lost part holds those puppets for a tick; a puppet missing for 1 s is freed.
  Events are resent by the reliable channel. Stale claims are dropped and counted, not logged per hit.
- **A client leaves:** its ghost and stand-in go away, its queued claims are dropped, and the host stops waiting for
  it in cutscenes.
- **The host leaves, or a timeout:** the client goes back to single-player. Its puppets are freed and its current
  room is reloaded locally (its own monsters spawn again); the player keeps their hp, inventory and position. F1
  Netplay: "host saiu".
- **A room that doesn't fit in 8 parts:** each step the host sends the puppets nearest to a player and rotates the
  others in; logged once per room.
- **A full pool on the client:** that puppet isn't shown, logged once per host ID.
- **An unknown type:** `Local`, logged once. **A hung cutscene:** the host resumes after 60 s.

## 6. Verification

No new unit tests for now (the user's decision). Required:

- The whole tree builds; every test that passes today on main keeps passing. `test_net_protocol` is updated only
  as far as needed to compile against the new message.
- Manually, two instances on localhost (`--host` and a join), then again with `--net-sim` (latency, jitter, loss):
  the same monsters on both; hits from either side; PvP with friendly fire on and off and with the scale; drops on
  both and picked up separately; an F1 stage change (and a door) on the host takes the client along; the client's
  Cenarios tab and keys are blocked; a cutscene on the host plays on the client and the host waits; death and
  respawn; the host leaving puts the client back in single-player.
- docs/netplay.md is updated: the shared world, the new settings, and the client's limits.

## Out of scope

Players in different rooms; host-decided hits; the wipe when everyone is down; shared story beyond what the
cutscene scripts themselves set; NPCs, shops and dialogue (they stay `Local`); host migration.
