// A world process: one room's simulation with no window, no renderer and no sound device, the same game code the
// windowed game runs.
//   opendash --world <stage, hex> <sub> [--story-save <file>] [--world-ticks N] [--world-unpaced]
#pragma once

namespace od::app {

bool worldProcessRequested(int argc, char** argv);
int runWorldProcess(int argc, char** argv);

} // namespace od::app
