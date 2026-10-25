// The stage files' state in the exe's layout (game/stage_files): the stages' file ids, the loader's flags, the DAT
// load queue.
#include "game/stage_files.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void filesLayout(LayoutVisitor& v) {
    StageFileTables& t = stageFileTables();
    v.field(0x8d3458, t.geometry);
    v.field(0x8d3514, t.texture);
    v.field(0xa714c4, fileLoads().loading);
    DatLoadQueue& q = datLoadQueue();
    for (size_t i = 0; i < 4; ++i) {
        v.field(0xa79fe0 + uint32_t(i) * 4, q.requests[i].kind);
        v.field(0xa79fe2 + uint32_t(i) * 4, q.requests[i].fileId);
    }
}

} // namespace od::test
