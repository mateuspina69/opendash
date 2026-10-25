// This OpenDash build's id (generated on every build by cmake/build_id.cmake: a content hash of the game-state, content,
// data-loading and wire sources). Netplay's handshake refuses a player whose build differs from the host's.
#pragma once

namespace od::app {

const char* buildId();

} // namespace od::app
