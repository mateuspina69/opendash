// Every table the game's handles name (engine/data_source tableUses: DataTable and TableFamily, from the whole game
// and content libraries) loads from the PC data source: a DataTable's table exists and its typed loader reads it; a
// TableFamily's tables load for every index the source has (the stages, every stage's rooms: its ST##.DAT's grids).
#include <array>
#include <cstdio>
#include <filesystem>
#include <set>

#include "check.hpp"
#include "content/registry.hpp"
#include "data_source_binding.hpp"
#include "engine/dat.hpp"
#include "engine/data_source.hpp"
#include "engine/room.hpp"

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    od::content::registerAll();
    const od::DataSource& source = od::bindTestDataSource(argv[1]);
    std::array<int, od::kStageCount> rooms{}; // the sub-rooms of each stage (its ST##.DAT's grids)
    for (int stage = 0; stage < od::kStageCount; ++stage) {
        char name[16];
        std::snprintf(name, sizeof name, "ST%02X.DAT", stage);
        const std::filesystem::path f = std::filesystem::path(argv[2]) / name;
        std::error_code ec;
        if (!std::filesystem::exists(f, ec)) continue;
        const od::GameFile st = od::pcGameFile(name, od::readFile(f));
        if (st.has(od::FilePart::Kind::Geometry)) rooms[size_t(stage)] = int(od::roomGrids(od::roomBlock(st)).size());
    }
    size_t tables = 0, familyTables = 0, failed = 0, missing = 0;
    std::set<od::TableKey> seen;
    auto load = [&](const od::TableUse& u, od::TableKey key) {
        try {
            u.load(key);
            return true;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", od::describe(key).c_str(), e.what());
            ++failed;
            return false;
        }
    };
    for (const od::TableUse& u : od::tableUses()) {
        if (!u.family) {
            const od::TableKey key{u.id, u.index};
            if (!seen.insert(key).second) continue;
            if (!source.hasTable(key)) {
                std::fprintf(stderr, "%s: the PC source has no such table\n", od::describe(key).c_str());
                ++missing;
                continue;
            }
            tables += load(u, key);
            continue;
        }
        size_t found = 0; // a family's index is the stage (FloorUvs) or roomIndex(stage, room)
        for (int stage = 0; stage < od::kStageCount; ++stage) {
            const od::TableKey byStage{u.id, uint32_t(stage)};
            if (source.hasTable(byStage) && seen.insert(byStage).second) found += load(u, byStage);
            for (int room = 0; room < rooms[size_t(stage)]; ++room) {
                const od::TableKey byRoom{u.id, od::roomIndex(stage, room)};
                if (byRoom.index == byStage.index) continue;
                if (source.hasTable(byRoom) && seen.insert(byRoom).second) found += load(u, byRoom);
            }
        }
        if (found == 0) {
            std::fprintf(stderr, "family %s: the PC source has none\n", od::describe({u.id, 0}).c_str());
            ++missing;
        }
        familyTables += found;
    }
    std::printf("%zu handles: %zu tables and %zu family tables loaded, %zu failed, %zu missing\n",
                od::tableUses().size(), tables, familyTables, failed, missing);
    CHECK(failed == 0 && missing == 0);
    CHECK(tables > 900);
    return testResult("test_tables");
}
