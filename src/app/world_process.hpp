// A world process: one room's simulation with no window, no renderer and no sound device, the same game code the
// windowed game runs.
//   opendash --world <stage, hex> <sub> [--story-save <file>] [--world-ticks N] [--world-unpaced]
//     [--world-pad "<ticks> <hex PS1 pad bits>; ..."] [--world-guest <id> <dx> <dz>]... [--world-host-away]
//     [--world-link <the coordinator's port>] [--stage-record <hex>] [--world-mark-defeated <enemy id>]
#pragma once

namespace od::app {

bool worldProcessRequested(int argc, char** argv);
int runWorldProcess(int argc, char** argv);

} // namespace od::app
