// A world without a screen (see world_view.hpp).
#include "game/world_view.hpp"

#include <array>

#include "game/camera_state.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {
bool g_noView = false;
} // namespace

void setWorldHasNoView(bool noView) { g_noView = noView; }
bool worldHasNoView() { return g_noView; }
namespace {
bool g_elsewhere = false;
} // namespace
void setWorldIsElsewhere(bool elsewhere) { g_elsewhere = elsewhere; }
bool worldIsElsewhere() { return g_elsewhere; }
int16_t viewYawNear(const Player& p) { return g_noView ? p.rotation.y : cameraState().viewAngles.y; }

namespace {
std::array<PlayerCamera, kPlayerSlots> g_cameras{};
} // namespace
void setWorldPlayerCamera(size_t id, const PlayerCamera& camera) {
    if (id < g_cameras.size()) g_cameras[id] = camera;
}
const PlayerCamera& worldPlayerCamera(size_t id) {
    static const PlayerCamera none;
    return id < g_cameras.size() ? g_cameras[id] : none;
}

} // namespace od::port
