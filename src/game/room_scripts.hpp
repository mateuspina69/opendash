// The room scripts (exe 0x512890, every field tick): the running stage's case -- the room's handler, picked by the
// story and the sub-room or by the sub-room alone, and what the case adds around it, or the case's own code -- and
// their state: the handler's step bytes and the actors the room's records spawned. The stage cases with content of
// their own (the story's, the areas' room handlers) answer through game/content_registry; game/transition runs the
// door test (story flag 0x700 -> script 0) of the cases that have one.
#pragma once

#include <cstdint>
#include <string>

#include "game/state_array.hpp"

namespace od::port {

// The room scripts' state: the handler's word (four bytes each handler uses its own way; [0] its step), the room's
// actors (the room scripts' records keep them at their record's slot) and the mask of those spawned
struct RoomScriptState {
    uint8_t handlerStep = 0;
    uint8_t handlerByte1 = 0;
    uint8_t handlerByte2 = 0;
    uint8_t handlerByte3 = 0;
    StateArray<uint32_t, 32> actors{}; // the actors' addresses (0: none)
    uint32_t actorMask = 0;

    // the handler's word cleared (the stage load's and the room helpers' dword store)
    void clearHandlerWord() { enterHandlerStep(0); }
    // the handler's step s with its second byte b and the bytes after them cleared (the handlers' dword store)
    void enterHandlerStep(uint8_t s, uint8_t b = 0) {
        handlerStep = s;
        handlerByte1 = b;
        handlerByte2 = 0;
        handlerByte3 = 0;
    }
    // the handler's step s with its second byte cleared (the handlers' word store)
    void enterHandlerStepWord(uint8_t s) {
        handlerStep = s;
        handlerByte1 = 0;
    }
};
inline RoomScriptState& roomScriptState() {
    static RoomScriptState s;
    return s;
}

// exe 0x582490: the door test of the room handlers: the armed transition (story flag 0x700) starts script 0; true when
// it did
bool roomDoorScript();
// exe 0x512890's per-stage case for stage cases made of a handler (+ what the case adds; the door test stays in
// game/transition). false when the case has more logic or its handler is not transcribed (nothing done).
bool roomScriptHandler();
// true when exe 0x512890 would call a per-room handler that is not transcribed: game/transition then runs the
// handler's door test itself (the stages whose door test is only inside their handlers, e.g. ST12 / ST13: without it
// the arrival walk-in (script 0) never starts and the player stays in the doorway, outside the room)
bool roomScriptHandlerMissing();
// the room-script part of the setup coverage (game/stage_setup spawnCoverage): the running room's case is transcribed;
// else what is missing is added to `why`
bool roomScriptCovered(std::string& why);

} // namespace od::port
