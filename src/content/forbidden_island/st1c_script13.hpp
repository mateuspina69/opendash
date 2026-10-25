// ST1C after the elephant (save 00 path, story 1 -> 2): script 0x13 (exe 0x51bb10), the long cutscene in ST1C subs
// 0..5 that ends with the request to ST3E sub 3, its room case (the stage case of ST1C starts it), and the bytes it
// hands its cast every tick (content/forbidden_island/st1c_cast keys its animations and track keys on them).
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/script_state.hpp"

namespace od::port {

// Script 0x13's own bytes of the script block (game/script_state's scriptBytes): the copies of the current step's tick,
// id and sub it makes every tick for its cast, the player's face-key cursors and the effect its steps spawn
struct St1cScriptBytes : ScriptPart<St1cScriptBytes> {
    StateArray<uint8_t, 2> faceKeyCursors{}; // +0x12: the player's face keys done (eyes, mouth)
    ActorRef<EffectActor> effect; // +0x14: the pool-0xa0 type-0x19 effect of the steps (exe 0x51d0d0)
    int32_t waitTicks = 0; // +0x1c: the last step's wait after its fade
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        faceKeyCursors = {};
        effect = {};
    }
    // both face-key cursors cleared (the exe's word store)
    void clearFaceKeyCursors() { faceKeyCursors = {}; }
};
inline St1cScriptBytes& st1cScriptBytes() {
    return scriptState().part<St1cScriptBytes>();
}
// The exe's pointer to the script's bytes (the global 0xa59f50, kept written every tick): a value (no reader in the port)
struct St1cScriptGlobals {
    uint32_t bytesAddress = 0;
};
inline St1cScriptGlobals& st1cScriptGlobals() {
    static St1cScriptGlobals s;
    return s;
}

// A timed key of a face / texture track (exe data, 4 bytes): at the step tick `tick` the track plays sequence
// `sequence` from key `key`; a list ends with kCueKeysEnd
struct CueKey : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint16_t tick = 0; // +0x0
    uint8_t sequence = 0; // +0x2
    uint8_t key = 0; // +0x3
    template <class R> static CueKey load(const R& r) {
        CueKey v;
        v.tick = r.template read<uint16_t>(0x0);
        v.sequence = r.template read<uint8_t>(0x2);
        v.key = r.template read<uint8_t>(0x3);
        return v;
    }
};
constexpr uint16_t kCueKeysEnd = 0xffff;
constexpr uint32_t kCueKeySize = 4;

// script 0x13 by its handler (the story's dispatch, content/registry)
bool st1cScriptPorted(RoutineId handler);
bool st1cScriptTick(const PlayerEnv& env, RoutineId handler);
// the stage case of ST1C in the room scripts (exe 0x580370: sub 0, the visit's first tick -> script 0x13)
bool st1cRoomCaseKnown(uint8_t stage);
bool st1cRoomCase();

} // namespace od::port
