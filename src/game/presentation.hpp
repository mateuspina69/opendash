// What a session's world shows and plays: it has no screen or speakers, so its windows, fades and sounds go to the room's players
// as they start, and each machine runs them from its own files. Nothing here in single player: no sink.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/routine_ids.hpp"

namespace od::port {

struct SoundRequest;
struct FadeTask;
struct ControllerActor;

constexpr uint8_t kNoSoundOwner = 0xff;

class PresentationBatch {
public:
    void windowOpened(int8_t win, uint32_t bank, int16_t id);
    void windowClosed(int8_t win, int how);
    void fadeStarted(uint32_t request, uint8_t param);
    void sound(const SoundRequest& r, uint8_t owner); // owner: the player whose shot made it, who has played it already
    void loopStart(uint32_t handle, uint16_t id, bool hasPoint, int16_t x, int16_t y, int16_t z, uint16_t param);
    void loopMove(uint32_t handle, bool hasPoint, int16_t x, int16_t y, int16_t z, uint16_t param);
    void loopStop(uint32_t handle);
    void shake(uint32_t amount, uint16_t decay, bool force);
    void banner(uint16_t x, uint16_t y, uint8_t style);
    void white(bool on);
    // a fade of the music channels (audio_logic soundFadeOut / In / Dip / Restore): the stage music cut for the battle
    // music, and back
    enum class MusicFade : uint8_t { Out = 0, In = 1, Dip = 2, Restore = 3 };
    void musicFade(MusicFade kind, uint32_t mask, int16_t step, int16_t target, int16_t delay);
    std::vector<uint8_t> take();
    bool empty() const { return bytes_.empty(); }

private:
    std::vector<uint8_t> bytes_;
};
// The world's batch (none: not a world)
void setPresentationBatch(PresentationBatch* batch);
PresentationBatch* presentationBatch();

// What a player's game does of a batch: the windows and fades only while a scene runs; the sounds always, but those of its own
// shots
void applyPresentation(std::span<const uint8_t> bytes, FadeTask& fade, uint8_t me);

// A player's game, each tick: the world's loops kept going; what it had of a world it has left dropped
void presentationLoopsTick();
void presentationLoopsClear();
// the world's script draws its white screen (a player's game does not run the script)
bool presentationWhiteWanted();

// The game state a world's controllers draw from, for a client to show: each class says what its controller holds. A client keeps a
// controller of the class in the same state: it is not run, the class's draw reads it
struct NetScreenClass {
    RoutineId cls;
    size_t bytes;
    void (*read)(ControllerActor& c, uint8_t* out);
    void (*write)(ControllerActor& c, const uint8_t* in);
};
void registerNetScreenClass(const NetScreenClass& c);
std::vector<uint8_t> netScreenCapture();
void netScreenApply(std::span<const uint8_t> bytes);

// This shot's owner makes the next sounds (kNoSoundOwner: nobody's)
void setSoundOwner(uint8_t owner);
// A player's game: what its effect classes play comes from the world instead
void setLocalEffectsMuted(bool muted);

} // namespace od::port
