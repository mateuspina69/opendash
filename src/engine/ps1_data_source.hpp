// The PS1 data source: the game's files and tables read off the user's USA disc image (engine/disc_image), no byte of
// dash2.exe. Files: DAT/<STEM>.BIN / COMMON/<STEM>.BIN as engine/game_file parts. Tables: the PS1 copies of the tables
// the game reads, in the stage overlays and SLUS_011.40, at the addresses of ps1_tables.inc (address-only); their
// layout is the PC's, but the pointers:
//   * a function pointer slot (table_slots.inc kTableRoutineSlots) reads as the routine's value in this source's
//     encoding: kRoutineTag | RoutineId (the PS1 has MIPS addresses there, which name no C++ routine);
//   * a data pointer slot (kTableRegions) reads as a handle (kDataTag | n) to the PS1 data the PS1 pointer points at,
//     whose own slots are translated the same way when it is followed.
// A table the disc has no copy of (not found whole, or changed by the PC: the room parameters' fog floats ...) is no
// table of this source: hasTable false, table() throws DataSourceError naming it.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/data_source.hpp"
#include "engine/disc_image.hpp"

namespace od {

class Ps1DataSource final : public DataSource {
public:
    static constexpr uint32_t kRoutineTag = 0x40000000u; // | RoutineId
    static constexpr uint32_t kDataTag = 0x60000000u;    // | handle

    // throws std::runtime_error when the disc is not the USA release
    explicit Ps1DataSource(std::shared_ptr<const DiscImage> disc);

    bool hasFile(const std::string& name) const override;
    GameFile gameFile(const std::string& name) const override;

    bool hasTable(TableKey key) const override;
    TableReader table(TableKey key, uint32_t bytes) const override;
    uint32_t stateAddress(TableKey key) const override;
    uint32_t routineStateValue(RoutineId id) const override;

protected:
    TableReader pointee(uint32_t pointer, uint32_t n) const override;
    std::string pointeeString(uint32_t pointer, size_t maxLength) const override;
    RoutineRef routineOf(uint32_t pointer) const override;

private:
    // a block of PS1 memory: SLUS_011.40's text or a type-1 block of a disc file, at its load address
    struct Part {
        std::string file;
        uint32_t base = 0;
        std::shared_ptr<const std::vector<uint8_t>> data;
    };
    // the PS1 data a handle names: which table load's region, where on the PS1
    struct Handle {
        TableKey key;
        int region = 0;
        size_t part = 0;
        uint32_t address = 0;
        bool operator<(const Handle& o) const {
            return std::tie(key, region, part, address) < std::tie(o.key, o.region, o.part, o.address);
        }
    };

    std::string discPath(const std::string& name) const; // "" when the disc has no such file
    const std::vector<size_t>& partsOf(const std::string& file) const; // the file's type-1 blocks, loaded on first use
    // the part holding [address, address + n): `near` first, then near's file's other blocks, then SLUS_011.40
    std::optional<size_t> partFor(size_t near, uint32_t address, uint32_t n) const;
    // n bytes of the part from address (fewer at its end), with the region's pointer slots translated
    TableReader regionBytes(const Handle& h, uint32_t n) const;
    uint32_t handleOf(const Handle& h) const;

    std::shared_ptr<const DiscImage> disc_;
    mutable std::vector<Part> parts_;
    mutable std::map<std::string, std::vector<size_t>> partsByFile_;
    size_t slus_ = 0;
    struct Located {
        std::string file;
        uint32_t address = 0;
    };
    std::map<TableKey, Located> tables_;
    // the slots by (table, region): the function pointers' offsets / routines, the data pointers' offsets / child regions
    std::map<std::pair<TableKey, int>, std::vector<std::pair<uint32_t, RoutineId>>> routineSlots_;
    std::map<std::pair<TableKey, int>, std::vector<std::pair<uint32_t, int>>> dataSlots_;
    mutable std::vector<Handle> handles_;
    mutable std::map<Handle, uint32_t> handleIndex_;
    mutable std::recursive_mutex lock_;
};

} // namespace od
