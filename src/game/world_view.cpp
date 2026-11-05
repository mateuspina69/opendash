// A world without a screen (see world_view.hpp).
#include "game/world_view.hpp"

#include "game/camera_state.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {
bool g_noView = false;
} // namespace

void setWorldHasNoView(bool noView) { g_noView = noView; }
bool worldHasNoView() { return g_noView; }
int16_t viewYawNear(const Player& p) { return g_noView ? p.rotation.y : cameraState().viewAngles.y; }

} // namespace od::port
