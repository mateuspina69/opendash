// The CLUT animation (see clut_animation.hpp).
#include "game/clut_animation.hpp"
#include "game/clut_animation_state.hpp"

#include "game/actor_pools.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace clut_animation_state;

namespace {

constexpr const char* kClutArea = "clut animation"; // the area of its port-gap errors

// One CLUT sequence of the stage (8 bytes): its keys and the base of its own tables
struct ClutSequence {
    explicit ClutSequence(uint32_t pointer) {
        const DataReader r = dataAt(pointer, 8);
        keys = r.u32(0);
        ownTables = r.u32(4);
    }
    uint32_t keys = 0;
    uint32_t ownTables = 0;
};
constexpr uint32_t kClutSequenceSize = 8;
// One key (4 bytes): the table it blends toward (kClutOwnTable: from the sequence's own tables), its ticks, the next
// key (kClutEnd: the sequence ends; kClutJump | n: back to key n; else the one after)
struct ClutKey {
    explicit ClutKey(uint32_t pointer) {
        const DataReader r = dataAt(pointer, 4);
        table = r.u8(0);
        flags = r.u8(1);
        ticks = r.u8(2);
        next = r.u8(3);
    }
    uint8_t table = 0;
    uint8_t flags = 0; // kClutOwnTable; the first key's also the table size (& kClutSizeMask)
    uint8_t ticks = 0;
    uint8_t next = 0;
    // its table and flags as the exe reads them, one word
    uint16_t tableWord() const { return uint16_t(table | uint32_t(flags) << 8); }
};
constexpr uint32_t kClutKeySize = 4;
constexpr uint8_t kClutEnd = 0xff, kClutJump = 0x80;

enum class ClutAnimationState : uint8_t { Start = 0, Blend = 1, Restore = 2 }; // exe 0x55da70 / 0x55dae0 / 0x55dc50
constexpr uint8_t kClutStartFailed = 2; // the step byte the start leaves without a sequence
constexpr uint8_t kRestoringVariant = 1;


// the words a table holds by the size bits
uint32_t clutWords(uint8_t size) {
    switch (size & kClutSizeMask) {
    case kClutSize64: return 0x40;
    case kClutSize128: return 0x80;
    default: return 8;
    }
}
// word i of a table of the stage's (the stage tables) or of the sequence's own (its data)
uint32_t clutWord(const ClutAnimationFields& f, uint8_t table, uint8_t flags, uint32_t i) {
    const uint32_t at = uint32_t(table) * kClutTableSize + i * 4;
    if (flags & kClutOwnTable) return dataScalar<uint32_t>(ClutSequence(f.sequence).ownTables + at);
    return clutTables().stage[at / 4];
}

// exe 0x55dae0: the key's tick -- a new key loaded (its destination, its ticks), the live table = the source blended
// toward the destination by the ticks left; at the key's end the next key (its source the old destination)
void blendStep(EffectActor& e, ClutAnimationFields& f) {
    if (e.subState == 0) {
        e.subState = 1;
        const uint32_t keyAt = ClutSequence(f.sequence).keys + uint32_t(int32_t(f.keyIndex)) * kClutKeySize;
        f.key = keyAt;
        const ClutKey k(keyAt);
        f.ticksLeft = uint8_t(k.ticks - 1);
        f.setDestination(k.tableWord());
    }
    const ClutKey k(f.key);
    const uint32_t out = uint32_t(f.live) * kClutTableSize / 4; // the live table's first word
    const uint32_t n = clutWords(f.size);
    const uint32_t ticks = k.ticks;
    if (ticks == 0) notPorted(kClutArea, "exe 0x55dae0: a key of 0 ticks (the exe divides by zero)");
    const uint32_t left = f.ticksLeft;
    const uint32_t t = ((((left << 20) - left) + ticks) << 12) / ticks;
    for (uint32_t i = 0; i < n; ++i)
        clutTables().live[out + i] = blendColourPair(clutWord(f, f.source, f.sourceFlags, i),
                                                     clutWord(f, f.destination, f.destinationFlags, i), t);
    const uint8_t was = f.ticksLeft;
    f.ticksLeft = uint8_t(was - 1);
    if (was != 0) return;
    const uint8_t next = k.next;
    if (next == kClutEnd) {
        e.state = uint8_t(ClutAnimationState::Restore);
        return;
    }
    e.subState = 0;
    f.sourceFromDestination();
    if (next & kClutJump) f.keyIndex = int8_t(next & ~kClutJump);
    else f.keyIndex = int8_t(f.keyIndex + 1);
}

// the 5-bit channel at `shift` of a 15-bit colour, widened to 8 bits
uint32_t widenChannel(uint32_t colour, int shift) {
    const uint32_t c = (colour >> shift) & 0x1f;
    return (c << 3) | (c >> 2);
}

constexpr uint32_t kRoomFlagClutCount = 0x10;
constexpr uint8_t kRoomFlagClutVariant = 1; // the animations it spawns restore their table at the end
enum class RoomFlagClutState : uint8_t { Init = 0, Watch = 1 };

void roomFlagClutAnimations(ControllerActor& c) { // exe 0x425140
    RoomFlagClutFields& f = classState<RoomFlagClutFields>(c);
    if (RoomFlagClutState(c.state) == RoomFlagClutState::Init) {
        for (int k = int(kRoomFlagClutCount) - 1; k >= 0; --k) f.animationSlots[size_t(k)] = 0;
        ++c.state;
    }
    for (int k = int(kRoomFlagClutCount) - 1; k >= 0; --k) {
        auto&& slotOf = f.animationSlots[size_t(k)];
        const bool on = storyFlag(kFlagLocked + uint32_t(k));
        const uint8_t slot = slotOf;
        if (on) {
            if (slot != 0) continue;
            const uint32_t address = allocActorA0(0); // exe 0x5e13b0
            if (!address) continue;
            EffectActor& e = effectActorAt(address);
            const uint8_t effectSlot = e.slot;
            e.flags = kActorLive;
            e.type = typeByte(GlobalEffectType::ClutAnimation);
            e.variant = kRoomFlagClutVariant;
            e.params[1] = uint8_t(k);
            e.params[0] = uint8_t(f.firstSequence + uint8_t(k));
            slotOf = effectSlot;
        } else if (slot != 0) {
            actorHeaderAt(actorBySlot(slot)).state = uint8_t(ClutAnimationState::Restore); // exe 0x5e0fa0
            slotOf = 0;
        }
    }
}

} // namespace

uint32_t blendColourPair(uint32_t a, uint32_t b, uint32_t t) {
    static constexpr int kChannels[6] = {0, 5, 10, 16, 21, 26}; // r, g, b of the low colour, then of the high one
    uint32_t out = (a | b) & 0x80008000u;                       // the semi-transparency bits
    for (const int shift : kChannels) {
        const uint32_t from = widenChannel(a, shift), to = widenChannel(b, shift);
        const uint32_t blended = from - (((from - to) * t + 0x800u) >> 12);
        out |= ((blended & 0xf8) >> 3) << shift;
    }
    return out;
}

void clutAnimationUpdate(const PlayerEnv&, uint32_t actor) {
    EffectActor& e = effectActorAt(actor);
    ClutAnimationFields& f = classState<ClutAnimationFields>(e);
    switch (ClutAnimationState(e.state)) {
    case ClutAnimationState::Start: {
        const uint32_t sequences = stage().clutAnimations;
        if (sequences == 0) {
            e.step = kClutStartFailed;
            return;
        }
        const uint32_t sequence = sequences + uint32_t(int32_t(int8_t(e.params[0]))) * kClutSequenceSize;
        f.sequence = sequence;
        const uint32_t first = ClutSequence(sequence).keys;
        f.key = first;
        if (first == 0) {
            e.step = kClutStartFailed;
            return;
        }
        f.keyIndex = 1;
        const ClutKey k(first);
        f.setLive(k.tableWord());
        f.source = k.table;
        f.sourceFlags = uint8_t(k.flags & kClutOwnTable);
        e.state = uint8_t(ClutAnimationState::Blend);
        e.subState = 0;
        if (k.ticks == 1) blendStep(e, f);
        return;
    }
    case ClutAnimationState::Blend: blendStep(e, f); return;
    case ClutAnimationState::Restore: {
        if (e.variant == kRestoringVariant) {
            const uint32_t at = uint32_t(f.live) * kClutTableSize / 4;
            const uint32_t n = clutWords(f.size);
            for (uint32_t i = 0; i < n; ++i) clutTables().live[at + i] = clutTables().stage[at + i];
        }
        freeActor(e); // exe 0x5e1580
        return;
    }
    default: notPortedAt(kClutArea, "exe 0x55da50 state", e.state);
    }
}

void roomFlagClutAnimationsUpdate(const PlayerEnv&, uint32_t actor) { roomFlagClutAnimations(controllerActorAt(actor)); }

} // namespace od::port
