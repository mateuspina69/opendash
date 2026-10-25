// The PC data source: the game's files from DAT/, its tables from the user's dash2.exe read as a FILE (engine/
// pe_image: a table's known virtual address -> its file offset through the PE section headers; the exe is never
// mapped as memory). The tables' addresses (pc_tables.inc, pc_room_tables.inc) and the function map are address-only
// files: no byte of the exe is in the repo.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/data_source.hpp"
#include "engine/pe_image.hpp"

namespace od {

class PcDataSource final : public DataSource {
public:
    // throws std::runtime_error when the exe is missing or not a PE32 image
    PcDataSource(std::filesystem::path datDir, const std::filesystem::path& exe);

    bool hasFile(const std::string& name) const override;
    GameFile gameFile(const std::string& name) const override;

    bool hasTable(TableKey key) const override;
    TableReader table(TableKey key, uint32_t bytes) const override;
    uint32_t stateAddress(TableKey key) const override;
    uint32_t routineStateValue(RoutineId id) const override;

    const std::filesystem::path& datDir() const { return datDir_; }

protected:
    TableReader pointee(uint32_t pointer, uint32_t n) const override;
    std::string pointeeString(uint32_t pointer, size_t maxLength) const override;
    RoutineRef routineOf(uint32_t pointer) const override;

private:
    // the table's virtual address (0: the source has no such table)
    uint32_t locate(TableKey key) const;
    // the bytes [va, va + n) (a section's zero tail included); throws when no section holds them
    TableReader bytesAt(uint32_t va, uint32_t n) const;

    std::filesystem::path datDir_;
    PeImage exe_;
    // the sections' bytes (their zero tails included), made once each: the readers' blocks
    mutable std::map<uint32_t, std::shared_ptr<const std::vector<uint8_t>>> sectionBlocks_;
};

} // namespace od
