// What a session's world shows and plays: it has no screen or speakers, so its windows, fades and sounds go to the room's players
// as they start, and each machine runs them from its own files. Nothing here in single player: no sink.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace od::port {

struct SoundRequest;
struct FadeTask;

constexpr uint8_t kNoSoundOwner = 0xff;

class PresentationBatch {
public:
    void windowOpened(int8_t win, uint32_t bank, int16_t id);
    void windowClosed(int8_t win, int how);
    void fadeStarted(uint32_t request, uint8_t param);
    void sound(const SoundRequest& r, uint8_t owner); // owner: the player whose shot made it, who has played it already
    void pad(uint16_t bits);
    std::vector<uint8_t> take();
    bool empty() const { return bytes_.empty(); }

private:
    std::vector<uint8_t> bytes_;
};
// The world's batch (none: not a world)
void setPresentationBatch(PresentationBatch* batch);
PresentationBatch* presentationBatch();

// What a player's game does of a batch: the windows and fades only while a scene runs; the sounds always, but those of its own
// shots. The last pad bits of the scene's owner come back
void applyPresentation(std::span<const uint8_t> bytes, FadeTask& fade, uint8_t me, uint16_t& scenePad);

// This shot's owner makes the next sounds (kNoSoundOwner: nobody's)
void setSoundOwner(uint8_t owner);
// A player's game: what its effect classes play comes from the world instead
void setLocalEffectsMuted(bool muted);

// The view a scene is watched through
struct SceneView {
    int32_t x = 0, y = 0, z = 0;
    int16_t distance = 0, yaw = 0, pitch = 0, roll = 0;
    friend bool operator==(const SceneView&, const SceneView&) = default;
};
SceneView sceneViewNow();
void sceneViewShow(const SceneView& v);
std::vector<uint8_t> encode(const SceneView& v);
bool decodeSceneView(std::span<const uint8_t> bytes, SceneView& v);

} // namespace od::port
