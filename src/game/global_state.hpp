// What a session's processes share of the game's state: the story and extra flags, the zenny and the other shared values. Each
// process runs its own copy and watches it; the coordinator holds the merge and sends each change on. Flags are bits, so
// two processes changing different flags of a byte do not undo each other. A flag's class says where it travels.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace od::port {

// Where a flag travels (a property of the flag, not of the code that sets it)
enum class FlagScope : uint8_t {
    Shared, // every world and player
    Stage,  // the worlds and players of its stage: a stage load clears these
    Local,  // this process only: a transition under way, a menu asked for
};
FlagScope flagScope(uint32_t flag);

struct GlobalChange {
    struct Bits {
        uint16_t byte = 0; // of the flag bytes, the story's then the extra block's
        uint8_t set = 0, clear = 0;
        friend bool operator==(const Bits&, const Bits&) = default;
    };
    struct Value {
        uint8_t at = 0, value = 0; // of the shared values' bytes
        friend bool operator==(const Value&, const Value&) = default;
    };
    uint8_t stage = 0; // the stage its stage-scope flags are of
    std::vector<Bits> flags;
    std::vector<Value> values;
    std::optional<int32_t> zennyDelta; // from a process: what it added
    std::optional<int32_t> zennyNow;   // from the coordinator: what the session holds
    bool empty() const { return flags.empty() && values.empty() && !zennyDelta && !zennyNow; }
    friend bool operator==(const GlobalChange&, const GlobalChange&) = default;
};
std::vector<uint8_t> encode(const GlobalChange& c);
std::optional<GlobalChange> decodeGlobalChange(std::span<const uint8_t> bytes);

// This process's copy. What it holds now counts as known
void globalWatchRebase();
// what it changed since the last call (a Local flag never)
GlobalChange globalWatchTake(uint8_t stage);
// a change from the coordinator, into the game's state: the stage-scope flags of another stage are not this one's
void globalWatchApply(const GlobalChange& c);

// A world started in a session takes the session's state after the stage's setup, before the room's actors look at it
void setStartingGlobalState(const GlobalChange& c);
void applyStartingGlobalState();

// The coordinator's merge. What it holds is what a process that starts from the session's save must take to be up to date
class GlobalMaster {
public:
    explicit GlobalMaster(int32_t zenny) : zenny_(zenny) {}
    struct Out {
        GlobalChange shared;     // to every other process
        GlobalChange stage;      // to the other processes of the stage
        std::optional<int32_t> zenny; // to every process, the changer too
    };
    Out merge(const GlobalChange& c);
    // all of it, for a process entering this stage
    GlobalChange all(uint8_t stage) const;
    // the stage has emptied: its flags start again with its next load
    void dropStage(uint8_t stage) { stageBits_.erase(stage); }

private:
    using Bits = std::map<uint16_t, GlobalChange::Bits>;
    Bits shared_;
    std::map<uint8_t, Bits> stageBits_;
    std::map<uint8_t, uint8_t> values_;
    int32_t zenny_ = 0;
};

// the zenny a save holds
int32_t saveZenny(std::span<const uint8_t> save);

} // namespace od::port
