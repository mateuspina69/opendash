// The exe globals the content's scripts and classes keep (values the exe holds: the addresses of their blocks, the
// flip-book's cue) in the exe's layout.
#include "content/forbidden_island/st1c_script13.hpp"
#include "content/intro/fire_mission.hpp"
#include "content/intro/mission_result_state.hpp"
#include "content/late_game/st59_state.hpp"
#include "game/vram_moves.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void contentGlobalsLayout(LayoutVisitor& v) {
    FireMissionPointers& f = fireMissionPointers();
    v.field(0xa5a6c4, f.roomBlock);
    v.field(0xa59e7c, f.missionBlock);
    v.field(0xa59e80, f.dataBlock);
    v.field(0xa59e84, f.sceneDataBlock);

    st59_state::RaceClassGlobals& r = st59_state::raceGlobals();
    v.field(0xa59de0, r.raceBlock);
    v.field(0xa59dd4, r.classBytesA);
    v.field(0xa59dd8, r.classBytesB);
    v.field(0xa59ddc, r.classBytesC);

    v.field(0xa59f4c, mission_result_state::resultGlobals().countdown);
    v.field(0xa59f50, st1cScriptGlobals().bytesAddress);
    v.field(0xa6d2d8, flipBookCue());
}

} // namespace od::test
