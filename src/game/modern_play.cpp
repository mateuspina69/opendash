// OpenDash's gameplay changes to the exe (see modern_play.hpp).
#include "game/modern_play.hpp"

namespace od::port {

namespace {

bool g_modernPlay = true;

} // namespace

bool modernPlay() { return g_modernPlay; }

void setModernPlay(bool on) { g_modernPlay = on; }

} // namespace od::port
