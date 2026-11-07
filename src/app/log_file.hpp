// The run's log file: every SDL_Log line also written to opendash.log next to the executable (created anew each run), so a
// run started from the desktop keeps its errors as one started from a console does.
#pragma once

namespace od::app {

// installs the log output (the console's as before, and the file); call once, before anything logs
void startLogFile(const char* fileName = "opendash.log");
// the log goes on under this name (in the same folder; one there is replaced)
void renameLogFile(const char* fileName);
// the log was of nothing worth keeping (a world process that never had a room): its file goes
void discardLogFile();

} // namespace od::app
