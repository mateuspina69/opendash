// The run's log file: every SDL_Log line also written to opendash.log next to the executable (created anew each run), so a
// run started from the desktop keeps its errors as one started from a console does.
#pragma once

#include <filesystem>

namespace od::app {

// installs the log output (the console's as before, and the file: `path`, or opendash.log next to the executable when
// empty); call once, before anything logs
void startLogFile(const std::filesystem::path& path = {});

} // namespace od::app
