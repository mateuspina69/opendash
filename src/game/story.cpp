// The story system's dispatch (see story.hpp).
#include "game/story.hpp"

#include "engine/data_source.hpp"
#include "game/content_registry.hpp"
#include "game/script_state.hpp"
#include "game/shop.hpp"
#include "game/stage_state.hpp"
#include "game/travel.hpp"

namespace od::port {

namespace {

// the scripts' handlers (exe table 0x89b6d0, a routine per script id: the named routines of engine/routines.csv; the
// game's own: StageCallbackScript (script 8), ContentCallbackScript (script 0x30), TravelScript; the other ids: the
// content's modules)
constexpr size_t kScriptIds = 0x80;
const DataTable<RoutineTable<kScriptIds>> scriptHandlers{TableId::ScriptHandlers};
// the stage callback script 8 runs (the stage globals store it)
constexpr RoutineId kShopCallback = RoutineId::ShopMenuCallback; // the shop stages' menu (game/shop)

} // namespace

RoutineRef storyScriptHandler(uint8_t id) { return id < kScriptIds ? scriptHandlers[id] : RoutineRef(); }

bool storyScriptPorted(uint8_t id) {
    const StageState& st = stage();
    switch (storyScriptHandler(id).id()) {
    case RoutineId::StageCallbackScript: return st.script8Callback == kShopCallback;
    case RoutineId::TravelScript: return true;
    case RoutineId::ContentCallbackScript:
        return content().script30Callbacks.count(st.script30Callback) != 0;
    // the content's scripts (content/registry.cpp: the opening, the attract, the chapters, the room batches ...)
    default:
        for (const ScriptModule& s : content().scripts)
            if (s.ported(id)) return true;
        return false;
    }
}

bool storyScriptTick(const PlayerEnv& env, uint8_t id) {
    const StageState& st = stage();
    switch (storyScriptHandler(id).id()) {
    case RoutineId::StageCallbackScript:
        if (st.script8Callback != kShopCallback) return false;
        shopTick(scriptState());
        return true;
    case RoutineId::TravelScript: return travelScriptTick(id);
    case RoutineId::ContentCallbackScript: {
        const auto it = content().script30Callbacks.find(st.script30Callback);
        if (it == content().script30Callbacks.end()) return false;
        it->second();
        return true;
    }
    default: // the content's scripts (content/registry.cpp), in their registration order
        for (const ScriptModule& s : content().scripts)
            if (s.tick(env, id)) return true;
        return false;
    }
}

bool storyRoomCase() {
    for (auto roomCase : content().roomCases) // content/registry.cpp: the attract, the opening, the chapters
        if (roomCase()) return true;
    return false;
}

} // namespace od::port
