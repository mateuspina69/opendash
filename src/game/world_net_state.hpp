// The network state of a world's actors (docs/adr/0006-coop-netplay.md, "Network traffic" and "Client mode"): only what a
// client cannot work out alone. Animation is the client's: no bone, pose, frame or draw list is here.
//
//   World side: NetWorldTable holds, tick by tick, each live actor's slot, position, rotation and the class's choice of
//   animation (actor.anim, with the tick it was chosen). One NetStateSender per client turns it into what that client
//   lacks: reliable events (an actor appears, an actor goes) and an update of what changed since the state the client
//   last acknowledged. An update fits one datagram; a lost or stale one is replaced by the next, which is built against
//   the same acknowledged state.
//
//   Client side: NetStateClient puts them on a timeline a little in the past (the interpolation delay) and hands each
//   shown tick to a NetClientActors: an actor appears, goes, or stands at an interpolated position with, when it is due, a
//   new animation that started `steps` ticks ago. PoolNetActors is that on the game's pools: it binds the model from the
//   client's own files and runs animSetPose / animStepPose; no class update runs.
//
// Update payload (little-endian): u16 sequence, u32 tick, u32 events sent so far, then per actor: u8 slot, u8 mask,
//   the fields of the mask's bits: i16 x, i16 y, i16 z, i16 yaw, i16 rotation x + i16 rotation z, u8 animation; with the
//   mask's top bit a second mask: u8 the animation's age, u16 key + u8 ticks left, i16 scale x, y, z, u16 tint, u24 hidden
//   parts, u8 count + the aim offsets (u8 entry | lanes << 5, i16 per lane), u8 the path's age + its steps (i16 x, y, z in
//   1/16 unit a tick, i16 rotation x, y, z a tick).
// Event batch: u32 tick, u32 index of the batch's first event, then per event: u8 kind, Appear: u8 slot, type, subtype,
//   variant, the model bank's entry, shown; i16 x, y, z, rotation x, y, z; with an animation in play u8 animation, u16 its
//   age, u16 key, u8 ticks left, u8 held. Go: u8 slot. What a class changes of the model (scale, tint, hidden parts, aim)
//   goes in the updates, against what the model binds: a client's bind gives the same.
//   Effect: u8 pool, type, subtype, variant, spawn byte, loop flags, flags, parent, 4 parameters; i16 x, y, z; statics: i16 rotation
//   and velocity x, y, z; effects: i16 the short after the position.
//
// A shot or a projectile goes as a path: the client moves it by itself from a point and its steps, and the world sends a new
// path only when the actor leaves the last one (a straight shot: once; a homing one or a falling one: as often as it turns).
// An effect (the effect pool, and the statics of netEffectClass: a blast, a splash, sparks) runs on the client from its event,
// with the client's own random numbers; the world's copy is not sent.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "game/actors.hpp"

namespace od::port {

constexpr size_t kNetSlots = 256;                // a slot id is one byte: the pool's class + the index
constexpr int kNetDefaultUpdateTicks = 2;        // an update every 2 ticks of 30 a second: 15 a second
constexpr size_t kNetMaxPayload = 1160;          // a payload in net/udp_socket's 1200-byte datagram, with the link's framing
constexpr size_t kNetMaxEventBatch = 1100;       // an event batch, likewise
constexpr double kNetInterpolationTicks = 4.0;   // how far in the past a client shows the world: an update's period + jitter
constexpr uint8_t kNetNoModel = 0xff;            // an actor with no entry of the model bank
constexpr uint16_t kNetAnimNotPlaying = 0xffff;  // an Appear's age: no animation has started on the actor
constexpr uint32_t kNetNoAnimTick = 0xffffffffu; // an actor on which no animation has started
constexpr uint8_t kNetNoPlayer = 0xff;

// which actors go to clients: by slot id
using NetSlotFilter = bool (*)(uint8_t slot);
// the animated pool's field slots and the statics: what this slice of the traffic carries
bool netSlotVisible(uint8_t slot);
// ... and the shots and the projectiles
bool netSlotVisibleOrShot(uint8_t slot);
// ... and the effects: all the traffic
bool netSlotNetworked(uint8_t slot);
// a shot or a projectile: it moves along a path
bool netSlotOnPath(uint8_t slot);

constexpr uint8_t kNetNoParent = 0xff;
// What starts an effect on a client
struct NetEffect {
    uint8_t pool = 0, type = 0, subtype = 0, variant = 0, spawnByte = 0, loopFlags = 0, flags = 0;
    uint8_t parent = kNetNoParent; // the slot of the actor it hangs from (a walker's foot dust, a glow, a splash)
    uint8_t bone = 0;              // the point of the player it follows (a splash at a player's bone)
    uint8_t owner = 0;             // the player it belongs to (a blade's swing)
    std::array<uint8_t, 4> params{};
    std::array<int16_t, 3> pos{}, rot{}, velocity{};
    int16_t spare = 0;
    friend bool operator==(const NetEffect&, const NetEffect&) = default;
};

// What an actor's model is bound from on the client
struct NetActorIdentity {
    uint8_t type = 0, subtype = 0, variant = 0;
    uint8_t modelEntry = kNetNoModel; // the entry of the stage's model bank (game/actor_model bindActorModelEntry)
    friend bool operator==(const NetActorIdentity&, const NetActorIdentity&) = default;
};

// What a class adds to a decoded frame's angles before the pose (a head's aim, a turret's yaw, a spin): the difference from
// the frame in the files, by entry
struct NetAimOffset {
    uint8_t entry = 0;
    std::array<int16_t, 3> lanes{};
    friend bool operator==(const NetAimOffset&, const NetAimOffset&) = default;
};

// A lock-on candidate of an actor: its hit-list entry, relative to the actor. The client runs the game's selection over these.
struct NetLockPoint {
    std::array<int16_t, 3> offset{};
    uint16_t shape = 0;
    uint32_t owner = 0;    // the entry's owner word without the slot
    bool harmless = false; // kHitAttackHarmless
    friend bool operator==(const NetLockPoint&, const NetLockPoint&) = default;
};

// A solid an actor submits to the actor collision pass: the client's Mega Man collides with it
struct NetSolid {
    int16_t type = 0, bone = 0;
    uint32_t param = 0;
    friend bool operator==(const NetSolid&, const NetSolid&) = default;
};

// What a class paints on one part of its model (a fade, a flash, a part that grows), different from what the model binds
struct NetPartPaint {
    uint8_t part = 0;
    uint16_t colour = 0;
    std::array<uint8_t, 3> scale{};
    uint8_t semi = 0;
    friend bool operator==(const NetPartPaint&, const NetPartPaint&) = default;
};

// What a class changes of its model
struct NetActorVisual {
    std::array<int16_t, 3> scale{};
    uint16_t tint = 0;
    uint32_t hiddenParts = 0; // bit k: part k carries kPartHidden
    bool boneScale = false;   // kActorBoneScale: the pose takes each part's scale bytes
    std::array<uint8_t, 3> rootScale{0x40, 0x40, 0x40}; // the root's scale bytes / a mesh's
    uint8_t semi = 0;         // the draw's semi-transparency bits
    friend bool operator==(const NetActorVisual&, const NetActorVisual&) = default;
};

// What moves
struct NetActorState {
    std::array<int16_t, 3> pos{}; // the whole part of the 16.16 position: what the game's pose reads
    std::array<int16_t, 3> rot{}; // x / y / z as the game holds them
    uint8_t anim = 0;             // the class's choice
    uint32_t animTick = kNetNoAnimTick; // the tick the animation last did what stepping would not: started, jumped, held
    uint16_t animKey = 0;         // on that tick: the key (of the animation's script) and its ticks left
    uint8_t animTicks = 0;
    bool animKeyed = false;       // ... which are not the animation's start
    bool held = false;            // the class decodes the key without stepping it
    bool shown = false;
    NetActorVisual visual;
    std::vector<NetAimOffset> aim;
    std::vector<NetPartPaint> paint;
    std::vector<NetLockPoint> lock;
    std::vector<NetSolid> solids;
    uint32_t pathTick = 0;        // on a path: pos and rot are the actor's on this tick, and it goes on by these steps
    std::array<int16_t, 3> posStep{}, rotStep{};
    int16_t fallStep = 0;         // ... its y step growing by this each tick (a fall, an arc)
    uint8_t told = 0;             // a client's: which of the visual's fields the world has said (kNetTold...)
    friend bool operator==(const NetActorState&, const NetActorState&) = default;
};
constexpr uint8_t kNetToldScale = 1, kNetToldTint = 2, kNetToldParts = 4, kNetToldLook = 8;

// An actor as the world's table holds it
struct NetWorldActor {
    bool live = false;
    uint32_t generation = 0; // counts this slot's occupants
    NetActorIdentity identity;
    NetActorState state;
    NetActorVisual bound;    // what the model binds: what a client has without being told
    uint32_t model = 0, animTable = 0, animScript = 0; // what tells that the occupant changed or an animation started
    uint32_t lastKeyAt = 0;  // the animation's place on the last tick
    uint8_t lastTicks = 0;
    std::array<int32_t, 3> lastPos{}; // an actor on a path: its 16.16 position and rotation on the last tick
    std::array<int16_t, 3> lastRot{};
    uint8_t owner = 0;       // ActorHeader::ownerPlayer: a shot's is told to the others only
    bool startedByShot = false; // ... and so is what a shot started
    bool effect = false;     // it runs on the client from an event
    bool effectGoes = false; // ... and this one goes (one that follows something only the world holds does not)
    NetEffect effectStart;
};

// A change the game made to the room block: a door's cell opened, a wall broken. The client's copy takes the same bytes.
struct NetRoomPatch {
    uint32_t offset = 0;
    std::vector<uint8_t> bytes;
    friend bool operator==(const NetRoomPatch&, const NetRoomPatch&) = default;
};

class NetWorldTable {
public:
    std::vector<NetRoomPatch> roomPatches; // since the room's files were read
    std::vector<uint8_t> roomShadow;       // the room block as the patches leave it
    // the live room block against the shadow: what differs becomes patches. Only the range of it: the grid's cells and
    // segments, which decide what collides; the rest (a texture's scroll) each machine runs by itself.
    void watchRoomBlock(std::span<const uint8_t> live, size_t from = 0, size_t to = size_t(-1));
    uint32_t tick = 0;
    uint8_t coordinateShift = 0xff; // the stage's (a camera script can change it): every pose scales by it
    std::array<NetWorldActor, kNetSlots> actors{};
    // the actors that started in this table's life
    uint64_t occupants = 0;

    // The tick's value of a live actor: a new model or type is a new occupant; a new animation keeps the tick it was chosen
    struct Seen {
        uint8_t type = 0, subtype = 0, variant = 0, modelEntry = kNetNoModel;
        uint32_t model = 0, animTable = 0, animScript = 0;
        std::array<int16_t, 3> pos{}, rot{};
        uint8_t anim = 0;
        bool shown = false;
        uint16_t key = 0;     // the animation's place
        uint8_t ticksLeft = 0;
        uint32_t keyAt = 0;
        bool atStart = true;  // ... is its first key with all its ticks
        bool stepped = true;  // ... is what one step from the last tick gives
        bool stayed = false;  // ... is the last tick's
        NetActorVisual visual, bound;
        std::vector<NetAimOffset> aim;
        std::vector<NetPartPaint> paint;
        std::vector<NetLockPoint> lock;
        std::vector<NetSolid> solids;
        uint8_t owner = 0;
        bool startedByShot = false;
        bool onPath = false;
        std::array<int32_t, 3> fine{}; // the 16.16 position
        bool effect = false, effectGoes = false;
        NetEffect effectStart;
    };
    void seen(uint8_t slot, const Seen& s);
    void gone(uint8_t slot);
};

// the tick's values of every actor of the pools (the model bank's entry looked up when an occupant's model changes). An
// actor is read, never changed.
void captureNetWorld(NetWorldTable& table, uint32_t tick);
// from here on what a class adds to a frame is noted at each pose: to call before the world's first tick
void watchNetPoses();

// One client's view of the world: what it was told, what it acknowledged
class NetStateSender {
public:
    // ownShotsOf: the player this client plays: their own shots they draw themselves (kNetNoPlayer: none)
    explicit NetStateSender(NetSlotFilter filter = netSlotNetworked, uint8_t ownShotsOf = kNetNoPlayer)
        : filter_(filter), ownShotsOf_(ownShotsOf) {}

    // what must arrive since the last call: the appearances and departures, in batches of at most kNetMaxEventBatch bytes
    std::vector<std::vector<uint8_t>> events(const NetWorldTable& table);
    // what changed since the acknowledged state, at most kNetMaxPayload bytes (what does not fit goes in the next one, the
    // next actor first). Call events() first: an update counts on them.
    std::vector<uint8_t> update(const NetWorldTable& table);
    // the client holds update `sequence`
    void acknowledge(uint16_t sequence);
    uint64_t updatesCut() const { return updatesCut_; } // updates that did not hold every change
    // the bytes of the largest update, by kind of field (measuring)
    struct UpdateBytes {
        uint32_t total = 0, head = 0, position = 0, rotation = 0, animation = 0, visual = 0, aim = 0, paint = 0, solids = 0, lock = 0, path = 0;
    };
    const UpdateBytes& largestUpdate() const { return largest_; }

private:
    struct Known {
        bool told = false;      // the client was told of this occupant
        bool effect = false;    // ... which runs there by itself from its event
        uint32_t generation = 0;
        NetActorState acked;    // the state the client is known to hold
        NetActorState sent;     // the newest state sent: a value back at the acknowledged one still has to replace this
    };
    struct Entry {
        uint8_t slot = 0;
        uint32_t generation = 0;
        NetActorState state;
    };
    struct Sent {
        uint16_t sequence = 0;
        std::vector<Entry> entries;
    };
    NetSlotFilter filter_;
    uint8_t ownShotsOf_;
    std::array<Known, kNetSlots> known_{};
    std::deque<Sent> inFlight_;
    uint8_t shiftTold_ = 0xff;
    size_t patchesTold_ = 0;
    uint16_t nextSequence_ = 1;
    uint32_t eventsSent_ = 0;
    size_t cursor_ = 0;
    uint64_t updatesCut_ = 0;
    UpdateBytes largest_;
};

// What a client does with what it holds, tick by tick of its timeline
class NetClientActors {
public:
    virtual ~NetClientActors() = default;
    // the slot's occupant appears
    virtual void appear(uint8_t slot, const NetActorIdentity& identity, const NetActorState& state) = 0;
    virtual void go(uint8_t slot) = 0;
    // the room block changes
    virtual void roomPatch(const NetRoomPatch& patch) = 0;
    // the stage's coordinate shift changes
    virtual void coordinateShift(uint8_t shift) = 0;
    // an effect starts, in a slot of the client's own
    virtual void effect(const NetEffect& start) = 0;
    struct AnimStart {
        uint8_t anim = 0;
        uint32_t steps = 0; // the ticks it has been in play (0: it starts on this tick)
        uint16_t key = 0;   // keyed: the key it is at, with these ticks left
        uint8_t ticks = 0;
        bool keyed = false;
        bool held = false;  // no step from here on
    };
    // the actor's position and rotation on this tick, and its animation: this one started, or the current one stepped
    virtual void tick(uint8_t slot, const NetActorState& state, const std::optional<AnimStart>& start) = 0;
};

// The render clock: the shown instant, in world ticks, steered toward the newest tick heard minus the delay
class NetRenderClock {
public:
    explicit NetRenderClock(double delayTicks = kNetInterpolationTicks) : delay_(delayTicks) {}
    void heard(uint32_t tick);
    bool running() const { return running_; }
    // the shown instant at nowNs (it never goes back)
    double at(uint64_t nowNs);

private:
    double delay_;
    bool running_ = false;
    bool started_ = false;
    uint32_t newest_ = 0;
    double shown_ = 0.0;
    uint64_t lastNs_ = 0;
};

class NetStateClient {
public:
    explicit NetStateClient(NetClientActors& actors) : actors_(actors) {}

    // a batch of events, in the order sent (false: not a batch)
    bool applyEvents(std::span<const uint8_t> bytes);
    // an update: the sequence to acknowledge once it is applied; none for a stale one, a bad one, or one waiting for events
    // (the latest of those is kept and applied when they arrive)
    std::optional<uint16_t> applyUpdate(std::span<const uint8_t> bytes);
    // an update held back, now that the events it counts on arrived
    std::optional<uint16_t> applyHeld();
    // the newest world tick heard
    uint32_t newestTick() const { return newestTick_; }
    bool heard() const { return heard_; }
    // shows world tick `tick`: what is due of the events, and each actor
    void advance(uint32_t tick);
    // how many actors the timeline shows now
    size_t shownCount() const;

private:
    struct Sample {
        uint32_t tick = 0;
        NetActorState state;
    };
    struct Occupant {
        uint8_t slot = 0;
        uint32_t appearedAtEvent = 0; // an update built before this event says nothing of this occupant
        NetActorIdentity identity;
        NetActorState last;           // the newest state heard
        std::deque<Sample> samples;
        struct Start {
            uint32_t tick = 0;
            NetClientActors::AnimStart start;
        };
        std::deque<Start> starts;
        uint32_t startNoted = kNetNoAnimTick; // the tick of the newest one heard: an update repeats it until acknowledged
    };
    struct Due {
        uint32_t tick = 0;
        bool appear = false;
        std::shared_ptr<Occupant> occupant;
        std::optional<NetEffect> effect;
        std::optional<uint8_t> shift;
        std::optional<NetRoomPatch> patch;
    };
    void noteStart(Occupant& o, const NetActorState& s);
    std::optional<uint16_t> applyParsed(std::span<const uint8_t> bytes, uint32_t eventsThrough);

    NetClientActors& actors_;
    std::array<std::shared_ptr<Occupant>, kNetSlots> heardOf_{}; // the occupant of each slot, by what has arrived
    std::array<std::shared_ptr<Occupant>, kNetSlots> shown_{};   // ... by the timeline
    std::deque<Due> due_;
    std::vector<uint8_t> held_;
    uint32_t eventsArrived_ = 0;
    uint16_t lastSequence_ = 0;
    bool anySequence_ = false;
    bool heard_ = false;
    uint32_t newestTick_ = 0;
};

// The game's pools as a client holds them: the slot taken, the model bound from the client's own files, the animation run
// from them. No class update runs.
class PoolNetActors : public NetClientActors {
public:
    // shotsInOwnSlots: the client's own Mega Man has shots in that pool, so the world's take free slots there
    explicit PoolNetActors(bool shotsInOwnSlots = false) : shotsInOwnSlots_(shotsInOwnSlots) {}
    // the table id on this machine of a world's player id (a client's game holds its own player at 0)
    void setPlayerMap(uint8_t ownPlayerId) { ownPlayerId_ = ownPlayerId; }
    uint8_t tableOfPlayer(uint8_t id) const { return id == ownPlayerId_ ? uint8_t(0) : id == 0 ? ownPlayerId_ : id; }
    void appear(uint8_t slot, const NetActorIdentity& identity, const NetActorState& state) override;
    void go(uint8_t slot) override;
    void effect(const NetEffect& start) override;
    void coordinateShift(uint8_t shift) override;
    void roomPatch(const NetRoomPatch& patch) override;
    void tick(uint8_t slot, const NetActorState& state, const std::optional<AnimStart>& start) override;
    // to run after a timeline's ticks: what went frees its slot
    void endTick();
    // the hit-list entries of the actors held here, for the game's lock-on selection
    void submitLockPoints();
    // the solids of the actors held here, for the actor collision pass
    void submitSolids();
    uint64_t effectsStarted() const { return effectsStarted_; }

private:
    ActorHeader* slotOf(uint8_t slot) const;
    bool shotsInOwnSlots_;
    uint8_t ownPlayerId_ = 0;
    std::array<bool, kNetSlots> animates_{}, held_{};
    std::array<uint32_t, kNetSlots> ownSlot_{}; // a world's shot in a slot of the client's: its address
    std::array<std::vector<NetLockPoint>, kNetSlots> lock_{};
    std::array<std::vector<NetSolid>, kNetSlots> solids_{};
    std::array<HitBoxHead, kHitListCapacity> lockBoxes_{}; // nothing reads their results: the world decides
    uint64_t effectsStarted_ = 0;
};

// the actors a client keeps from its own room setup and runs itself: the door actor and its leaves (a door asks its own player)
bool netClientKeepsActor(const StaticActor& a);
bool netClientKeepsAnyActor(const Actor& a); // of any pool

// One tick of what runs on a client: the effects, and the statics started as effects (the world's actors are held with their
// run flag off, so the pool loops pass them by). A class that throws ends its own actor, not the client; the classes that
// did are named in `failed` once each.
struct NetLocalRunFailures {
    std::vector<std::string> classes;
};
void runNetLocalActors(const PlayerEnv& env, ActorContext& context, bool late, NetLocalRunFailures& failed);

// ---- a test instrument: each actor's pose on a tick, in a local file of each process, compared afterwards (a pose never
// crosses the network) ----------------------------------------------------------------------------------------------------
struct NetPoseRecord {
    uint32_t tick = 0;
    uint8_t slot = 0, type = 0, anim = 0;
    uint16_t key = 0; // of the animation's script
    int8_t frame = 0;
    uint8_t ticksLeft = 0;
    uint16_t tint = 0;
    uint32_t hiddenParts = 0;
    uint32_t paint = 0; // a hash of the parts' colours, scale bytes and semi-transparency
    std::array<int16_t, 3> pos{}, rot{};
    std::vector<int16_t> bones; // per part: the point relative to the root's, then the matrix (12 values)
};
// every live actor the filter takes, on this tick, but the effects: a world's by their class, a client's by their run flag
void writeNetPoses(std::FILE* file, uint32_t tick, NetSlotFilter filter, bool client);
std::vector<NetPoseRecord> readNetPoses(const std::filesystem::path& file);

} // namespace od::port
