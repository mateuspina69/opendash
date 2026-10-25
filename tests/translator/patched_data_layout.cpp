// The records of the program's data the game rewrites in place (game/patched_data): each copy at its exe address.
#include <vector>

#include "content/nino/st19_state.hpp"
#include "game/encounter.hpp"
#include "game/patched_data.hpp"
#include "translator/layout.hpp"

namespace od::test {

void patchedDataLayout(LayoutVisitor& v) {
    // the current stage's field encounter table (a stage's spawner rewrites its formation picks): its copy, from the
    // image on a load (the encounter state's pointer is loaded before)
    const uint32_t fieldTable = od::port::encounterState().fieldTable;
    if (fieldTable != 0) od::port::patchedRecordFor<od::port::FieldEncounterTable>(fieldTable);
    // the stage's enemy-group table (the tick and the classes write its marks): each entry up to the end mark
    if (const uint32_t groups = od::port::encounterState().enemyGroups)
        for (uint32_t at = groups; od::port::dataRecord<od::port::EnemyGroupEntry>(at, od::port::EnemyGroupEntry::kBytes).tag() >= 0;
             at += od::port::kEnemyGroupEntrySize)
            od::port::patchedRecordFor<od::port::EnemyGroupEntry>(at);
    // ST19's spark offsets (shifted in place every time they are read)
    for (const od::TableId p : od::port::st19_state::kSparkPoints) od::port::patchedRecordFor<od::port::PatchedPoint>(od::stateAddress(p));
    for (od::port::PatchedData* d : od::port::patchedRecords()) {
        const uint32_t a = d->address();
        std::vector<uint8_t> b(d->bytes());
        if (!v.interprets(a, b.size())) continue;
        if (!v.loading()) d->save(b);
        v.bytes(a, b.data(), b.size());
        if (v.loading()) d->load(b);
        v.object(a, d->object(), d->type());
    }
}

} // namespace od::test
