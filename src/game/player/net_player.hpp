// A player's state on the network, by the same rules as the actors' (game/world_net_state): only what changed since what the
// receiver acknowledged, the animation's choice when it changes, and each machine plays the animation from its own files. It
// goes from a player's owner to their world, and from the world to the room's other players. No frame or bone goes.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "game/player/player_snapshot.hpp"
#include "game/world_view.hpp"

namespace od::port {

struct Player;

constexpr uint32_t kNetPlayerNoTick = 0xffffffffu;

// One animation channel: what its class chose, and where it was when it stopped doing what stepping does
struct NetPlayerChannel {
    uint8_t anim = 0;
    uint32_t tick = kNetPlayerNoTick; // the owner's tick it last started, jumped or held
    uint8_t key = 0, ticks = 0;
    bool keyed = false, held = false;
    friend bool operator==(const NetPlayerChannel&, const NetPlayerChannel&) = default;
};

struct NetPlayerFields {
    std::array<int16_t, 3> pos{};
    int16_t pitch = 0, yaw = 0, yawOfs = 0, lean = 0, lean2 = 0;
    std::array<uint8_t, 2> drawFlags{};
    int8_t channelMix = 0;
    uint32_t light = 0, stageAnims = 0;
    uint8_t aimFlags = 0, armed = 0;
    int8_t weaponSlot = 0, busterSlot = 0;
    std::array<int8_t, 2> face{};
    PlayerLook look;
    std::array<NetPlayerChannel, 2> channels{};
    uint8_t status = 0, charge = 0, specialFiring = 0; // what a client's rules for their markers and arms read
    uint8_t downed = 0; // down, for a teammate to revive
    uint8_t specialHeld = 0; // the special button held with the arm out: a swing, a beam, a suction goes on
    PlayerCamera camera;     // the owner's camera, for what their world counts as seen (it goes no further)
    // the owner's free aim (game/modern_aim): the point their crosshair is on, which their shots aim at on every machine
    // (a held beam follows it); aimSet 0: no free aim now (a lock-on, the classic aim)
    uint8_t aimSet = 0;
    std::array<int16_t, 3> aimPoint{};
    friend bool operator==(const NetPlayerFields&, const NetPlayerFields&) = default;
};

// The owner's side: the fields of a tick, with the animation channels watched from tick to tick
class NetPlayerWatch {
public:
    NetPlayerFields see(const Player& p, uint32_t tick);

private:
    struct Place {
        uint32_t script = 0, keyAt = 0;
        uint8_t ticks = 0, anim = 0;
    };
    std::array<Place, 2> last_{};
    NetPlayerFields fields_;
};

// Toward one receiver: what changed since what it acknowledged
// What a shot running from its event reads of its owner, on the world and on the others' machines
void applyNetArm(Player& p, const NetPlayerFields& f);

class NetPlayerSender {
public:
    std::vector<uint8_t> update(uint8_t playerId, uint32_t tick, const NetPlayerFields& now);
    void acknowledge(uint16_t sequence);
    // the receiver starts over (it joined anew): the numbering goes on -- an update of before is older than every one
    // after -- and every field goes until the receiver acknowledges one
    void restart() {
        acked_ = sent_ = NetPlayerFields{};
        inFlight_.clear();
        full_ = true;
    }

private:
    struct Sent {
        uint16_t sequence = 0;
        NetPlayerFields fields;
    };
    NetPlayerFields acked_, sent_;
    std::deque<Sent> inFlight_;
    uint16_t next_ = 1;
    bool full_ = true;
};

// From one sender
struct NetPlayerHeard {
    uint8_t playerId = 0;
    uint16_t sequence = 0;
    uint32_t tick = 0;
};
class NetPlayerReceiver {
public:
    // an update: its head, or nothing for a stale or a bad one
    std::optional<NetPlayerHeard> apply(std::span<const uint8_t> bytes);
    const NetPlayerFields& fields() const { return fields_; }
    // the sender may have started its numbering over (its game bound a room): the next update counts whatever its number,
    // onto the fields held (a sender that did not start over goes on with what changed since them)
    void acceptRestart() { any_ = false; }

private:
    NetPlayerFields fields_;
    uint16_t last_ = 0;
    bool any_ = false;
};
// the player an update speaks of (no such byte: nullopt)
std::optional<uint8_t> netPlayerIdOf(std::span<const uint8_t> bytes);

// A channel played on this machine, from the files bound now (a ghost's, or a world's)
class NetPlayerTrack {
public:
    // the channel's state of `fields` at owner tick `tick`: what is due started, else one step
    void step(const NetPlayerChannel& channel, uint32_t stageAnims, uint32_t tick);
    uint8_t anim() const { return anim_; }
    uint8_t frameData() const { return frameData_; }
    int8_t frame() const { return frame_; }
    int8_t blendFrame() const { return blendFrame_; }
    uint8_t blendSteps() const { return blendSteps_; }

private:
    void stepOnce();
    void enterKey(uint32_t key);
    uint32_t started_ = kNetPlayerNoTick, script_ = 0, keyAt_ = 0;
    uint8_t anim_ = 0, frameData_ = 0, ticks_ = 0, blendSteps_ = 0;
    int8_t frame_ = 0, blendFrame_ = 0;
    bool held_ = false;
};

// what a pose reads, from the fields and the two tracks
PlayerSnapshot netPlayerSnapshot(const NetPlayerFields& fields, const std::array<NetPlayerTrack, 2>& tracks);

} // namespace od::port
