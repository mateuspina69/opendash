# Netplay Shared World Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The host's world is the only one that simulates. Clients show its monsters as puppets and send it their hits. Players can hit each other (friendly fire, from the F1 menu), drops are shared, cutscenes and room changes follow the host.

**Architecture:** The net layer gets a reliable `Event` message and starts sending `WorldState`. Both carry opaque payloads, which `src/app/netplay_world.*` encodes and decodes. The game side gets small, app-set entry points in two new files:
- `game/net_world.*`: roles, gates, observers, the death policy.
- `game/net_puppets.*`: world capture and puppets.

It also gets the external-hit queue and remote bodies in `game/hit_list.*`. `Netplay` drives one `NetWorld` (host or client logic) between ticks.

**Tech Stack:** C++20, SDL3, Dear ImGui, CMake + vcpkg (VS 2026 preset), Winsock UDP (src/net).

**Spec:** `docs/superpowers/specs/2026-10-25-netplay-shared-world-design.md`. Read the "Corrections found while planning" section too: it overrides the body where they differ.

## Global Constraints

- **Layering** (`tools/check_layers.py`): `src/net` includes no engine/game/app code; `src/game` includes neither `net/` nor `app/`. Only `src/app/netplay*` joins them.
- **No new unit tests** (the user's decision). Every task ends with a full build plus the tests that pass today:
  `ctest --preset debug -R "^(layers|test_lang|test_walker|test_collision|test_stage_files|test_storm_draw|test_weather_draw|test_game_files_hash|test_ghost_buffer|test_net_protocol|test_render_logic)$"`
  The other tests fail on main for lack of `mml2extract` and golden data. That is not this work's to fix.
- **Build:** `cmake --build --preset debug -- -m`. Expected: no errors, and no new warnings in the touched files.
- **Protocol:** `kProtocolVersion = 2`. `Event` is `MessageType` 12, reliable. `WorldState` is 7, unreliable. Datagrams stay ≤ `kMaxDatagram` (1200).
- **Config:** `net_friendly_fire` 0/1 (default 0) and `net_ff_scale` int 25..100 (default 100).
- **Timings:** respawn 3 s after `Downed`; puppet freed after 1 s missing; the host's cutscene wait gives up after 60 s; up to 8 `WorldState` parts per tick.
- **UI text (Portuguese, exact):** "Combate", "fogo amigo", "dano entre jogadores %", "definido pelo host", "só o host muda o cenário", "Mundo", "aguardando %s", "host saiu".
- **Style:** match the surrounding code. Comments say what the exe does or why; no tests or `CHECK`s get added.
- **Unchanged without netplay:** every hook defaults to off/null. With netplay off, or with only the ghost mirror, the game behaves exactly as today.

## Review Focus

1. **Host room change while a client is in a cutscene, or mid-load.** The client must drop the old room's puppets and its pending `ScriptDone`, and must follow. Owned by Task 7 (client `Travel` handling clears puppets and script state).
2. **A puppet's slot reused by the host within one step** (one enemy dies and another spawns in the same slot). The generation must change, so the client frees and respawns the puppet instead of morphing it. Owned by Task 6 (host generation: the routine and the live-edge both count).
3. **The client's pool is full of its own effects or shots.** Puppet creation fails; it is logged once per host ID and retried on later states, never crashing. Owned by Task 4 (`puppetSpawn` returns 0) and Task 6 (log-once set).
4. **The host leaves mid-fight.** Clients free their puppets, turn off every game hook (mirror, gates, death hold) and reload the room locally. A client that was down respawns, because the hold no longer applies. Owned by Task 7.
5. **A friendly-fire toggle during a fight.** Stand-ins vanish the same tick and queued PvP claims are dropped by the host. Owned by Task 6 (the host checks the setting when routing, not when queueing).

---

### Task 1: Protocol and session — `Event`, `WorldState`, version 2

**Files:**
- Modify: `src/net/protocol.hpp`: version, `MessageType::Event = 12`, `struct Event`, `Message` variant (append `Event` after `Ack`), the static_assert, the `WorldState` comment.
- Modify: `src/net/protocol.cpp` (or wherever `encode`/`decode`/`isReliable`/`typeOf` live): `Event` encodes as a length-prefixed payload, like `WorldState`; `isReliable(Event) == true`.
- Modify: `src/net/session.hpp`, `src/net/session.cpp`.
- Modify: `tests/test_net_protocol.cpp`: only what it needs to compile and pass (for example, an exhaustive message list gains `Event`).

**Interfaces:**
- Produces:
  ```cpp
  // protocol.hpp
  struct Event { std::vector<uint8_t> payload; bool operator==(const Event&) const = default; };
  // session.hpp
  constexpr uint8_t kAllPlayers = 0xff;
  struct ReceivedWorld { uint32_t tick = 0; std::vector<uint8_t> payload; };
  struct ReceivedEvent { uint8_t from = 0; std::vector<uint8_t> payload; };
  void Session::sendWorldState(uint64_t nowNs, uint32_t tick, std::span<const uint8_t> payload); // host: to every accepted client; client: no-op
  std::vector<ReceivedWorld> Session::takeWorldStates();   // client: every WorldState with tick >= the newest delivered tick
  void Session::sendEvent(uint64_t nowNs, uint8_t to, std::span<const uint8_t> payload);
      // host: to player `to` (a client id) or kAllPlayers (every accepted client); client: always to the host (`to` ignored)
  std::vector<ReceivedEvent> Session::takeEvents();         // host: from the client's id; client: from = kHostPlayerId
  ```
- The host never forwards `Event` or `WorldState`. A `WorldState` that reaches a host, or an `Event` from a client that isn't accepted, is dropped.

- [ ] **Step 1:** Add `Event` to the protocol (the type, the variant, encode/decode, `isReliable`, `typeOf`) and bump `kProtocolVersion` to 2. In the `WorldState` comment, drop "reserved ... not sent".
- [ ] **Step 2:** Add the four Session methods. Each WorldState gets its own `stateSequence_`-style counter (`worldSequence_`). The client keeps `lastWorldTick_` and an `anyWorld_` flag and drops a `WorldState` whose tick is older. Received events go into a new `gameEvents_` vector in `hostDeliver` / `clientDeliver`.
- [ ] **Step 3:** Fix `tests/test_net_protocol.cpp` so that it compiles: add `Event` wherever every message type is listed, with the round trip `Event{{1,2,3}}`.
- [ ] **Step 4:** Build, then run the passing test set (Global Constraints). Expected: everything passes, including `test_net_protocol`.
- [ ] **Step 5:** Commit: `Net: Event (reliable game events) and WorldState sending; protocol 2`.

### Task 2: Hit list — external hits and remote bodies

**Files:**
- Modify: `src/game/hit_list.hpp`, `src/game/hit_list.cpp`.

**Interfaces:**
- Produces:
  ```cpp
  // a hit decided outside this tick's pair tests (a netplay claim): lands on the box of the entry whose owner slot is
  // targetSlot, as if a player attack at `contact` had touched it
  struct ExternalHit { uint8_t targetSlot = 0; uint32_t attack = 0; int16_t contact[3] = {}; };
  void hitListQueueExternal(const ExternalHit& hit);
  // a body with no actor (a remote player's stand-in): submitted at the start of every hitListPass while set
  struct RemoteBody { uint8_t slot = 0; int16_t pos[3] = {}; uint16_t radius = 0x30; uint32_t attack = 0; };
  void hitListSetRemoteBodies(std::vector<RemoteBody> bodies);           // replaces the set (empty: none)
  uint32_t hitListRemoteBodyResult(size_t index);                       // its box's result after the last pass (0: none)
  const HitBox& hitListRemoteBodyBox(size_t index);                     // ... and its contact
  ```
- In `hitListPass()`:
  1. Before reading `hitCount`, submit each remote body with `hitListSubmit(pos, box_i, hitOwner(slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | kHitContactPoint | hitRadius(radius), attack)`, after zeroing `box_i.result`.
  2. After the pair loop, for every queued `ExternalHit`, find the first entry in `[0, n)` whose `owner >> kHitSlotShift == targetSlot`. Build a synthetic `HitEntry` with pos = contact, radius 0, `attack = hit.attack`, `owner = hitOwner(0xff) | hitGroup(kHitGroupPlayerAttacks)`. Then call `applyHit(entry, synthetic, contactOf(entry), 0, sphereOf(synthetic), sphereOf(synthetic), 0)`.
  3. Clear the queue, including entries that found no target, which are dropped silently.
- Note: `hitRadius`, `kHitContactPoint` and the other shape helpers already exist in hit_list.hpp. Use them as `submitBusterHit` does.

- [ ] **Step 1:** Add the declarations, and the storage in the anonymous namespace (`std::vector<ExternalHit> gExternal; std::vector<RemoteBody> gBodies; std::vector<HitBox> gBodyBoxes;`). `hitListSetRemoteBodies` resizes `gBodyBoxes`.
- [ ] **Step 2:** Implement the two additions to `hitListPass` as above. The remote bodies must be submitted before `const uint32_t n = s.hitCount;` so that `n` includes them.
- [ ] **Step 3:** Build and run the passing test set. Expected: everything passes (`test_walker` and `test_collision` exercise the pass, and with nothing queued their behaviour is unchanged).
- [ ] **Step 4:** Commit: `Hit list: external hits and remote bodies (netplay)`.

### Task 3: Game-side netplay hooks — roles, gates, observers, death policy

**Files:**
- Create: `src/game/net_world.hpp`, `src/game/net_world.cpp` (add them to the game library's source list in `CMakeLists.txt`, next to `hit_list.cpp`).
- Modify:
  - `src/game/actors.cpp` (`updatePool`)
  - `src/game/encounter.cpp` (`encounterTick`)
  - `src/game/script_engine.cpp` (`scriptStart`)
  - `src/game/pickups.cpp` (`spawnDropsAt`, `spawnLaunchedPickup`)
  - `src/game/pickups.hpp`
  - `src/game/breakable_walls.cpp` (`queueBreakableWallHit`, `breakableWallsTick`)
  - `src/game/player/player_update.cpp` (`deadState`)
  - `src/game/player/player_update.hpp`

**Interfaces:**
- Produces (`od::port`, net_world.hpp):
  ```cpp
  enum class NetRole : uint8_t { Local, Puppet };
  NetRole netRole(RoutineId cls);              // the table below; anything else Local
  // what one pickup a drop spawned was (the client spawns the same one)
  struct PickupSpawn { int32_t pos[3] = {}; uint8_t kind = 0, size = 0, variant = 0; uint32_t velocityXY = 0, velocityZSpare = 0; int16_t yaw = 0; };
  void spawnPickup(const PickupSpawn& p);      // pickups.cpp: the actor exactly as recorded (no rng, no drop table)
  struct NetWorldHooks {
      bool mirror = false;     // client: the host's world is shown; local Puppet-role actors are freed, encounters and wall damage held back
      bool holdDeath = false;  // netplay: dead holds (no game over)
      std::function<bool(uint8_t id)> scriptGate;          // client: false = the start is refused
      std::function<void(uint8_t id)> scriptStarted;       // host: a script started
      std::function<void(const PickupSpawn&)> pickupSpawned; // host: a drop's pickup spawned
      std::function<void(uint32_t result, int attack)> wallHit; // client (mirror): a queued wall hit, sent instead of applied
      std::function<void(uint32_t cellWord, uint8_t variant)> wallBroken; // host: a segment broke
      std::function<bool(const ActorHeader&)> isPuppet;    // client: the slot is a puppet (net_puppets)
      std::function<void(const PlayerEnv&, uint32_t actor)> puppetUpdate; // client: a puppet's tick
  };
  NetWorldHooks& netWorldHooks();
  bool playerDown();                           // player().state == Dead (held)
  void playerRespawn();                        // player_update.cpp: playerRestore(), state Init, at transitionRequest().destination (arrivalPlacement)
  ```
- **Role table.** Mark a `RoutineId` as `Puppet` when its class is a combat enemy or an enemy attack. The rule:
  - Every class in `registry()` (actors.cpp, plus content/*/register.cpp) whose update calls `spawnDrops` / `spawnLaunchedDrops` on death or calls `hitPointsAdd`/`hitPointsChange` on itself from its hit result. Start from the callers listed in the spec's research: reaverbot_walker, field_reaverbot, field_hoverbot, guardian_reaverbot, reaverbot_turret, snow_reaverbot, gunship_flight. Then grep for the rest.
  - Plus their attacks: `EnemyShotUpdate`, `LobbedShotUpdate`, `LobbedBlobUpdate`, `BossShotUpdate`, `BossBeamUpdate`, `GuardianBeamUpdate`, `GuardianOrbUpdate`.
  - Exclude every class whose name contains Cast, Scene, Cutscene, Intro, Defeat, Escape, Retreat, Outro, Dissolves, Beaten or Wakes: those are script-driven and stay `Local`.
  - Keep the table as one sorted `static const std::set<RoutineId>` in net_world.cpp, with a one-line comment per group.
- **Gates.** Each is one `if` at the choke point:
  - `updatePool`: after `actorHandler`, if `hooks.mirror`, the actor's role isn't Local and it isn't a puppet → `freeActor(actor)`, then `continue`. If `hooks.isPuppet && hooks.isPuppet(actor)` and its state isn't 0 (Init) → call `hooks.puppetUpdate(env, actor.at)` and `continue`. Puppets in state 0 run their class once, so the model and bones get set up.
  - `encounterTick`: return at once while `mirror`.
  - `scriptStart(id)`: if `scriptGate` is set and returns false → return. After the start succeeds → `scriptStarted(id)` if set.
  - `spawnDropsAt` / `spawnLaunchedPickup`: after each pickup is fully set → `pickupSpawned(...)` with its pos, kind, size, variant, velocity words and `rotation.y`.
  - `queueBreakableWallHit`: if `mirror` and `wallHit` is set → `wallHit(result, attack)`, then return.
  - `breakableWallsTick`: where a segment breaks (`e.flags = next; breakReaction(...)`) → `wallBroken(cellWord, variant)`, where `cellWord` is the word that `switchCellSegmentVariant` takes for that segment.
  - `deadState`: if `holdDeath`, skip the `setMode(GameOver)` block. Push-out and `prevPos` stay as they are.

- [ ] **Step 1:** Write net_world.hpp/.cpp (the hooks, the role table, `playerDown`) and `spawnPickup` + `playerRespawn`.
- [ ] **Step 2:** Add the gates listed above.
- [ ] **Step 3:** Build, run the passing test set, and run `layers`. Expected: everything passes, and the hooks are inert by default.
- [ ] **Step 4:** Commit: `Game: netplay hooks (roles, gates, observers, death hold)`.

### Task 4: Puppets — world capture and the generic puppet update

**Files:**
- Create: `src/game/net_puppets.hpp`, `src/game/net_puppets.cpp` (add to CMakeLists next to net_world.cpp).

**Interfaces:**
- Consumes: `netRole`, `netWorldHooks` (Task 3); `hitListSubmit` (hit_list.hpp); `animSetPose`, `animStepPose` (actor_animation.hpp); `allocActor20`, `allocActor40`, `freeActor`, `actorHandler`.
- Produces (`od::port`):
  ```cpp
  // one shared actor's state (host -> client): the class identity and its generic pose / hit box
  struct PuppetState {
      uint8_t hostSlot = 0;        // the host's slot id (pool class + index)
      uint8_t pool = 0;            // 0x20 animated, 0x40 projectile
      uint32_t typeWord = 0;       // type / subtype / variant / spawnByte (setTypeWord)
      uint32_t paramsWord = 0;
      uint8_t loopFlags = 0;
      int32_t pos[3] = {};         // 16.16
      int16_t rotation[3] = {};
      int16_t hitPoints = 0, maxHitPoints = 0;
      uint8_t state = 0, anim = 0, keyIndex = 0, shownParts = 0;
      uint16_t tint = 0;
      int16_t scale[3] = {};
      uint16_t hitRadius = 0;      // from its last submitted hit entry this tick (0: none)
      int16_t hitPos[3] = {};
      uint32_t hitOwnerFlags = 0;  // the entry's owner word without the slot byte
      uint32_t hitAttack = 0;      // its attack word (the body's touch damage)
  };
  std::vector<PuppetState> captureWorld();      // host: every live Puppet-role actor of pools 0x20 (slots 0..23) and 0x40
  uint32_t puppetSpawn(const PuppetState& s);   // client: allocated in its pool, set from s with state 0, registered as a puppet before it returns (0: pool full)
  void puppetApply(uint32_t actor, const PuppetState& s); // client: the newest (interpolated) state to follow; never writes `state` (the class's Init runs locally, then the puppet update takes over)
  void puppetFree(uint32_t actor);
  bool isPuppetActor(const ActorHeader& a);
  void puppetUpdate(const PlayerEnv& env, uint32_t actor); // installed as netWorldHooks().puppetUpdate
  struct PuppetHit { uint32_t actor = 0; uint32_t result = 0; int16_t contact[3] = {}; };
  std::vector<PuppetHit> takePuppetHits();      // client: puppets a local player attack hit since the last call
  void puppetsReset();                          // every puppet freed (host left / room change)
  ```
- **Hit capture (host).** Record each actor's hit entry as it is submitted: in `submit()` (hit_list.cpp), call a net_puppets hook `noteHitEntry(slot, pos, ownerShape, attack)` when the slot belongs to pool 0x20 or 0x40. The last entry per slot in a tick wins. Declare it in net_puppets.hpp as `void noteHitEntry(uint8_t slot, const int16_t pos[3], uint32_t ownerShape, uint32_t attack);`. `captureWorld()` reads these and clears them.
- **The puppet update.** Each tick:
  1. Copy pos/rotation/scale/tint/shownParts/hitPoints from the applied state, with `prevPos`/`prevRotation` kept for the renderer's blend.
  2. Animated pool only: if `anim != prevAnim` or the local `keyIndex` is more than 2 off → `animSetPose(a, anim, keyIndex)`; otherwise `animStepPose(a)`. Then `prevAnim = anim`.
  3. Submit its hit entry: the puppet's own `HitBox` in a side table keyed by slot, `hitOwner(a.slot) | hitOwnerFlags` and `hitAttack`, at hitPos.
  4. Read last tick's result: if `result & kHitAttackByPlayer`, push a `PuppetHit`.
  5. Projectiles (pool 0x40): steps 1, 3 and 4 only.

- [ ] **Step 1:** Write net_puppets.hpp/.cpp as above, plus the `noteHitEntry` call in hit_list.cpp `submit`.
- [ ] **Step 2:** Build and run the passing test set. Expected: everything passes.
- [ ] **Step 3:** Commit: `Game: world capture and generic puppets (netplay)`.

### Task 5: Payload codec — `netplay_world_codec`

**Files:**
- Create: `src/app/netplay_world_codec.hpp`, `src/app/netplay_world_codec.cpp` (add to the app sources in CMakeLists). The build-id hash covers `src/app/netplay.*` only, so extend `cmake/build_id.cmake`'s glob to `src/app/netplay*` so the new files count.

**Interfaces:**
- Consumes: `PuppetState`, `PickupSpawn` (Tasks 3–4); `GhostRoom` (player_snapshot.hpp).
- Produces (`od::app`):
  ```cpp
  struct WorldPart { od::port::GhostRoom room; uint32_t tick = 0; uint8_t part = 0, parts = 1;
                     std::vector<std::pair<uint8_t /*generation*/, od::port::PuppetState>> puppets; };
  std::vector<std::vector<uint8_t>> encodeWorld(const od::port::GhostRoom& room, uint32_t tick,
      const std::vector<std::pair<uint8_t, od::port::PuppetState>>& puppets, size_t maxBytes); // ≤ 8 parts, each ≤ maxBytes
  std::optional<WorldPart> decodeWorld(std::span<const uint8_t> payload);
  enum class EventKind : uint8_t { HitClaim = 1, PlayerHit, Downed, Respawn, DropSpawned, ObjectChanged,
                                   ScriptStarted, ScriptDone, Travel, Settings };
  enum class ClaimTarget : uint8_t { Puppet = 0, Player = 1, Wall = 2 };
  struct HitClaim { ClaimTarget target; uint8_t hostSlot = 0, generation = 0, playerId = 0; uint32_t wallResult = 0;
                    uint32_t attack = 0; int16_t contact[3] = {}; uint8_t weapon = 0;
                    int16_t origin[3] = {}; int16_t dir[3] = {}; uint32_t tick = 0; };
  struct PlayerHit { uint8_t attacker = 0; uint32_t attack = 0; int16_t contact[3] = {}; };
  struct Travel { int stage = 0, sub = 0; bool transition = false; };
  struct NetCombatSettings { bool friendlyFire = false; int scale = 100; };
  struct NetEvent { EventKind kind; od::port::GhostRoom room;
                    std::variant<HitClaim, PlayerHit, uint8_t /*Downed/Respawn: player id; Script*: id*/,
                                 od::port::PickupSpawn, std::pair<uint32_t,uint8_t> /*ObjectChanged: cellWord, variant*/,
                                 Travel, NetCombatSettings> body; };
  std::vector<uint8_t> encodeEvent(const NetEvent& e);
  std::optional<NetEvent> decodeEvent(std::span<const uint8_t> payload); // nullopt: unknown kind, short or overlong
  ```
- **Wire format:** little-endian fixed fields, in the order declared. Every list has a count byte first. `encodeWorld` packs puppets into parts greedily. When the last part would be the 9th, it drops the puppets farthest from every player passed in (an extra `const std::vector<std::array<int32_t,3>>& players` parameter, sorted by squared distance) and returns `true` through an `bool* trimmed` out-param, so the caller can log it once per room. A puppet entry is 1 (generation) plus the `PuppetState` fields, packed.

- [ ] **Step 1:** Write the codec.
- [ ] **Step 2:** Build. Expected: no errors.
- [ ] **Step 3:** Commit: `Netplay: world and event payloads`.

### Task 6: `NetWorld` — host and client logic, wired into `Netplay`

**Files:**
- Create: `src/app/netplay_world.hpp`, `src/app/netplay_world.cpp` (CMakeLists).
- Modify: `src/app/netplay.hpp`, `src/app/netplay.cpp`: own a `NetWorld` and call it from `afterTick` and `step`. Revise the header comment: netplay now writes the game state, through the game's netplay hooks only, between ticks.

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces:
  ```cpp
  class NetWorld {
  public:
      void begin(bool host, uint8_t localId);  // installs the game hooks (host: observers; client: mirror, gates, puppets)
      void end();                              // every hook back to default, puppetsReset()
      void afterTick(uint32_t tick);           // after every logic tick: host: capture; client: puppet hits -> claims, stand-in results -> claims, downed, script done
      // slotPlayer[slot]: the player id a ghost slot shows (-1: none / the mirror)
      void step(uint64_t nowNs, od::net::Session& s, const GhostView& ghosts,
                const std::array<int, od::ghost::kMaxGhosts>& slotPlayer); // send, receive, apply
      // the app's side
      void setCombat(const NetCombatSettings& c, uint64_t nowNs, od::net::Session* s); // host: store + broadcast; client: ignored
      const NetCombatSettings& combat() const;
      void hostTravelled(int stage, int sub, bool transition);  // host: remembered and broadcast
      std::optional<Travel> takeTravel();                       // client: a room to load
      bool holdTicks() const;                                   // host: waiting for clients' cutscenes
      std::string waitingFor() const;                           // names, for the overlay / F1
      struct Stats { int puppets = 0; int claimsSent = 0, claimsApplied = 0, claimsDropped = 0; };
      Stats stats() const;
  };
  ```
  `Netplay` gains:
  - `NetWorld& world()`;
  - `bool isClient() const` (a session in the Client role);
  - `bool hostLost()`: true once, when a connected client loses its host (Connected → anything else). Netplay then calls `world_.end()`.

- **Host behaviour:**
  - Generation per host slot: it is incremented when a slot's routine (`typeWord`) differs from the last capture, or when the slot was absent from it.
  - Claims:
    - **Puppet:** the generation must match → `hitListQueueExternal({hostSlot, attack | kHitAttackByPlayer, contact})`.
    - **Wall:** → `queueBreakableWallHit(wallResult, attack & kHitDamageMask)`. The `mirror` gate is off on the host, so it applies.
    - **Player:** dropped unless `combat().friendlyFire`. The damage becomes `max(1, dmg * scale / 100)`. Victim 0 (the host itself) → `hitListQueueExternal({player().slot, ...})`; any other victim → `PlayerHit` to them.
  - `Downed` → `Respawn` to that player 3 s later. The host's own death goes through the same path (`playerDown()` → a timer → `playerRespawn()`).
  - `scriptStarted` (not 0, 7 or 8) → broadcast `ScriptStarted`, and remember it as running.
  - When `scriptState().flags == kScriptEnded` for that script → hold. The hold ends once every accepted client has sent `ScriptDone(id)`, or has left, or 60 s have passed.
  - On a join: `Settings` + `Travel(current)` to that client.
  - `pickupSpawned` → `DropSpawned`; `wallBroken` → `ObjectChanged`.
- **Client behaviour:**
  - `WorldState` for another room, or with a different `loadedDataFingerprint`, is ignored.
  - For each puppet: a host-ID → local-address map. A new ID or a new generation → `puppetSpawn`, with failures logged once per (slot, generation). Each puppet also gets a 3-tick delay buffer, interpolated the same way as `GhostBuffer`. Puppets missing for 1 s → `puppetFree`.
  - `PuppetHit` → `HitClaim{Puppet}`. Stand-in results → `HitClaim{Player}`. `wallHit` → `HitClaim{Wall}`.
  - `PlayerHit` → `hitListQueueExternal({player().slot, attack, contact})`.
  - `playerDown()` rising → `Downed`; `Respawn` → `playerRespawn()`.
  - `ScriptStarted(id)` → `scriptRun(id)`, with the gate allowing it for that call. When it ends → `ScriptDone(id)`.
  - `Travel` → queued for the app; `DropSpawned` → `spawnPickup`; `ObjectChanged` → `switchCellSegmentVariant`.
  - Script gate: allow only `id == 8` or the host's id being applied.
- **Both:**
  - While `combat().friendlyFire`, `hitListSetRemoteBodies` gets one body per shown ghost: slot `0x01 + ghostSlot`, pos = the ghost's pos − `kHitHeight` on y (as the player's own entry), attack `kHitAttackSolid`. Otherwise it gets an empty set.
  - `holdDeath = true` for the whole session.

- [ ] **Step 1:** Write NetWorld (host and client), the generation tracking and the buffers.
- [ ] **Step 2:** Wire it into Netplay: `begin()` once the session is Hosting or Connected, `end()` on a stop or a lost host. `afterTick` calls `world_.afterTick(localTick_)`; `step` calls `world_.step(...)` after the ghost buffers are sampled. Netplay builds `slotPlayer` from `sources_` (Peer slots only). Shown ghosts come from what `GhostView` shows: `GhostView` gains `std::optional<od::port::PlayerSnapshot> shown(int slot) const`, which returns the slot's state when the room/fingerprint checks passed.
- [ ] **Step 3:** Build and run the passing test set. Expected: everything passes.
- [ ] **Step 4:** Commit: `Netplay: the shared world (host simulates, clients mirror)`.

### Task 7: App integration — travel, the scenario lock, the cutscene hold, the host leaving, the settings and the F1 menu

**Files:**
- Modify:
  - `src/engine/config.hpp`, `src/engine/config.cpp` (`netFriendlyFire`, `netFfScale`, with loadConfig keys `net_friendly_fire`, `net_ff_scale`, clamped to 25..100)
  - `src/app/app.hpp`, `src/app/app_setup.cpp` (`reload`)
  - `src/app/app_frame.cpp` (`simStep`)
  - `src/app/main.cpp` (the `[` `]` / PgUp / PgDn keys)
  - `src/app/debug_ui.cpp` (the Cenarios and Netplay tabs)

**Interfaces:**
- Consumes: `NetWorld` (Task 6), `Netplay::isClient`, `Netplay::hostLost`.

- **`App::reload`:**
  - On a client, while `netplay.isClient() && !netTravelling_`, refuse: log "netplay: só o host muda o cenário" once, then return.
  - On the host (and only when the session is active), after a successful load: `netplay.world().hostTravelled(newStage, newSub, transition)`. Also once at session start, with the current room.
- **`App::simStep`:**
  - Before the tick loop: `if (auto t = netplay.world().takeTravel())` → `netTravelling_ = true; reload(t->stage, t->sub, t->transition); netTravelling_ = false; netPlaceBesideHost_ = true;` and `puppetsReset()`.
  - After the first tick in field play with `netPlaceBesideHost_`, put the player at the host's newest snapshot pos + 0x80 on x, then clear the flag. This uses the same direct writes as the playscript teleport, between ticks.
  - While `netplay.world().holdTicks()`, skip `playTick` / `afterTick`. Rendering and `netplay.step` still run.
  - After `netplay.step`: `if (netplay.hostLost())` → `reload(stage, sub, true)` locally, so the room's own monsters spawn.
- **`main.cpp` keys:** they call `reload`, so the refusal there covers them. Nothing to change unless they bypass it.
- **F1, Cenarios tab:** while `netplay.isClient()`, wrap both lists in `ImGui::BeginDisabled()` / `EndDisabled()` with `TextDisabled("só o host muda o cenário")`.
- **F1, Netplay tab:**
  - A `SeparatorText("Combate")`: `Checkbox("fogo amigo")` and `SliderInt("dano entre jogadores %", &v, 25, 100)`. On a client they are disabled and show `world().combat()`, with `TextDisabled("definido pelo host")`. On the host or off, a change → `saveConfigValue("net_friendly_fire"|"net_ff_scale", ...)` + `world().setCombat(...)`.
  - A `SeparatorText("Mundo")`: one line with the puppets and claims sent/applied/dropped, plus "aguardando %s" when `holdTicks()`.
  - "host saiu" when the host was lost.
- **Overlay:** while `holdTicks()`, draw "aguardando %s" with `ImGui::GetForegroundDrawList()->AddText` at the top centre.

- [ ] **Step 1:** Config keys, plus `start()` passing them into `world().setCombat` on the host.
- [ ] **Step 2:** `reload` gate and `hostTravelled`; the `simStep` travel, placement, hold and host-lost paths.
- [ ] **Step 3:** The F1 Cenarios lock, the Combate/Mundo blocks and the overlay.
- [ ] **Step 4:** Build and run the passing test set. Expected: everything passes.
- [ ] **Step 5:** Commit: `App: travel follows the host, scenario lock, cutscene hold, friendly fire in F1`.

### Task 8: Docs and manual verification

**Files:**
- Modify: `docs/netplay.md`: replace "A ghost changes nothing in your game ..." with the shared world. Cover the host simulating, clients mirroring, friendly fire and its scale, own-copy drops and chests, party travel, the scenario lock, cutscenes waiting, respawn, and what happens when the host leaves. Add the two ini keys to the settings table.

- [ ] **Step 1:** Update `docs/netplay.md`.
- [ ] **Step 2:** Manual run, two instances on localhost:
  1. Run `opendash --host`, then `opendash --join 127.0.0.1:27460 --net-name b`.
  2. Repeat with `--net-sim 80 20 2` on the client.
  3. Check each spec §6 item. Note any failures in the final report; don't hide them.
- [ ] **Step 3:** Commit: `Docs: netplay's shared world`.
