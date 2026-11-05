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
//   the fields of the mask's bits: i16 x, i16 y, i16 z, i16 yaw, i16 rotation x + i16 rotation z, u8 animation, u8 its age.
// Event batch: u32 tick, u32 index of the batch's first event, then per event: u8 kind, Appear: u8 slot, type, subtype,
//   variant, the model bank's entry, shown; i16 x, y, z, rotation x, y, z; u8 animation, u16 its age. Go: u8 slot.
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

namespace od::port {

constexpr size_t kNetSlots = 256;                // a slot id is one byte: the pool's class + the index
constexpr int kNetDefaultUpdateTicks = 2;        // an update every 2 ticks of 30 a second: 15 a second
constexpr size_t kNetMaxPayload = 1190;          // a payload in net/udp_socket's 1200-byte datagram, with the link's framing
constexpr size_t kNetMaxEventBatch = 1100;       // an event batch, likewise
constexpr double kNetInterpolationTicks = 4.0;   // how far in the past a client shows the world: an update's period + jitter
constexpr uint8_t kNetNoModel = 0xff;            // an actor with no entry of the model bank
constexpr uint16_t kNetAnimNotPlaying = 0xffff;  // an Appear's age: no animation has started on the actor
constexpr uint32_t kNetNoAnimTick = 0xffffffffu; // an actor on which no animation has started

// which actors go to clients: by slot id
using NetSlotFilter = bool (*)(uint8_t slot);
// the animated pool's field slots and the statics: what this slice of the traffic carries
bool netSlotVisible(uint8_t slot);
// ... and the shots and the projectiles (measured against it: ADR 0006 sends those as events, not as state)
bool netSlotVisibleOrShot(uint8_t slot);

// What an actor's model is bound from on the client
struct NetActorIdentity {
    uint8_t type = 0, subtype = 0, variant = 0;
    uint8_t modelEntry = kNetNoModel; // the entry of the stage's model bank (game/actor_model bindActorModelEntry)
    friend bool operator==(const NetActorIdentity&, const NetActorIdentity&) = default;
};

// What moves
struct NetActorState {
    std::array<int16_t, 3> pos{}; // the whole part of the 16.16 position: what the game's pose reads
    std::array<int16_t, 3> rot{}; // x / y / z as the game holds them
    uint8_t anim = 0;             // the class's choice
    uint32_t animTick = kNetNoAnimTick; // the tick it was chosen
    bool shown = false;
    friend bool operator==(const NetActorState&, const NetActorState&) = default;
};

// An actor as the world's table holds it
struct NetWorldActor {
    bool live = false;
    uint32_t generation = 0; // counts this slot's occupants
    NetActorIdentity identity;
    NetActorState state;
    uint32_t model = 0, animTable = 0, animScript = 0; // what tells that the occupant changed or an animation started
};

class NetWorldTable {
public:
    uint32_t tick = 0;
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
    };
    void seen(uint8_t slot, const Seen& s);
    void gone(uint8_t slot);
};

// the tick's values of every actor of the pools (the model bank's entry looked up when an occupant's model changes). An
// actor is read, never changed.
void captureNetWorld(NetWorldTable& table, uint32_t tick);

// One client's view of the world: what it was told, what it acknowledged
class NetStateSender {
public:
    explicit NetStateSender(NetSlotFilter filter = netSlotVisible) : filter_(filter) {}

    // what must arrive since the last call: the appearances and departures, in batches of at most kNetMaxEventBatch bytes
    std::vector<std::vector<uint8_t>> events(const NetWorldTable& table);
    // what changed since the acknowledged state, at most kNetMaxPayload bytes (what does not fit goes in the next one, the
    // next actor first). Call events() first: an update counts on them.
    std::vector<uint8_t> update(const NetWorldTable& table);
    // the client holds update `sequence`
    void acknowledge(uint16_t sequence);
    uint64_t updatesCut() const { return updatesCut_; } // updates that did not hold every change

private:
    struct Known {
        bool told = false;      // the client was told of this occupant
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
    std::array<Known, kNetSlots> known_{};
    std::deque<Sent> inFlight_;
    uint16_t nextSequence_ = 1;
    uint32_t eventsSent_ = 0;
    size_t cursor_ = 0;
    uint64_t updatesCut_ = 0;
};

// What a client does with what it holds, tick by tick of its timeline
class NetClientActors {
public:
    virtual ~NetClientActors() = default;
    // the slot's occupant appears
    virtual void appear(uint8_t slot, const NetActorIdentity& identity, const NetActorState& state) = 0;
    virtual void go(uint8_t slot) = 0;
    struct AnimStart {
        uint8_t anim = 0;
        uint32_t steps = 0; // the ticks it has been in play (0: it starts on this tick)
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
            uint8_t anim = 0;
        };
        std::deque<Start> starts;
    };
    struct Due {
        uint32_t tick = 0;
        bool appear = false;
        std::shared_ptr<Occupant> occupant;
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
    void appear(uint8_t slot, const NetActorIdentity& identity, const NetActorState& state) override;
    void go(uint8_t slot) override;
    void tick(uint8_t slot, const NetActorState& state, const std::optional<AnimStart>& start) override;
    // to run after a timeline's ticks: what went frees its slot
    void endTick();

private:
    std::array<bool, kNetSlots> animates_{};
};

// ---- a test instrument: each actor's pose on a tick, in a local file of each process, compared afterwards (a pose never
// crosses the network) ----------------------------------------------------------------------------------------------------
struct NetPoseRecord {
    uint32_t tick = 0;
    uint8_t slot = 0, type = 0, anim = 0, keyIndex = 0;
    int8_t frame = 0;
    uint8_t ticksLeft = 0;
    std::array<int16_t, 3> pos{}, rot{};
    std::vector<int16_t> bones; // per part: the point relative to the root's, then the matrix (12 values)
};
// every live actor the filter takes, on this tick
void writeNetPoses(std::FILE* file, uint32_t tick, NetSlotFilter filter);
std::vector<NetPoseRecord> readNetPoses(const std::filesystem::path& file);

} // namespace od::port
