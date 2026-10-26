// The PS1 data source: the game's files and tables read off the user's USA disc image (engine/disc_image), no byte of
// dash2.exe. Files: DAT/<STEM>.BIN / COMMON/<STEM>.BIN as engine/game_file parts. Tables: the PS1 copies of the tables
// the game reads, in the stage overlays and SLUS_011.40, at the addresses of ps1_tables.inc (address-only); their
// layout is the PC's, but the pointers:
//   * a function pointer slot (table_slots.inc kTableRoutineSlots) reads as the routine's value in this source's
//     encoding: kRoutineTag | RoutineId (the PS1 has MIPS addresses there, which name no C++ routine);
//   * a data pointer slot (kTableRegions) reads as an address in this source's address space -- every block of PS1
//     memory placed at its own virtual base from kVirtualBase, so the game's pointer arithmetic holds -- whose data has
//     its own slots translated the same way when it is followed.
// Tables the PS1 keeps in another form, made from the disc: the DAT names of the load indices (the file table of
// SLUS_011.40, its entries named by the ISO directory), the stages' geometry / texture files (ST%02X / ST%02XT), the
// banks the PC keeps in dash2.exe (engine/ps1_exe_banks), the voice filenames and eight-byte XA descriptors as the
// engine's four-byte {file, track} records (ps1_voice_tables.inc, engine/ps1_xa), including the five built-in voices.
// The tables the game reads by address (ps1_labels.inc: scripts, spawn records, cameras ...): at their PS1 address, the
// pointer slots in their extent translated the same way on every read that meets them.
// The room parameters: their PS1 records, 0x24 bytes (the PS1's colour; the PC's D3D fog range past them reads as
// zero: no fog range). A table the disc has no copy of (not found whole, or changed by the PC) is no table of this
// source: hasTable false, table() throws DataSourceError naming it.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "engine/data_source.hpp"
#include "engine/disc_image.hpp"
#include "engine/ps1_xa.hpp"

namespace od {

class Ps1DataSource final : public DataSource {
public:
    static constexpr uint32_t kRoutineTag = 0x40000000u;  // | RoutineId
    static constexpr uint32_t kVirtualBase = 0x10000000u; // the data address space [kVirtualBase, kRoutineTag)
    // a pointer slot of a table the game reads by address: to a function (this source's routine value), to data (this
    // source's address of it), into the loaded files' memory (the engine's address of that PS1 RAM), to the state the
    // game builds (the engine's address of it)
    enum class SlotKind : uint8_t { Function, Data, Ram, State };

    // throws std::runtime_error when the disc is not the USA release
    explicit Ps1DataSource(std::shared_ptr<const DiscImage> disc);

    bool hasFile(const std::string& name) const override;
    bool isPs1() const override { return true; }
    GameFile gameFile(const std::string& name) const override;

    bool hasTable(TableKey key) const override;
    TableReader table(TableKey key, uint32_t bytes) const override;
    uint32_t stateAddress(TableKey key) const override;
    uint32_t routineStateValue(RoutineId id) const override;

    // the tables the game reads by address this source has (ps1_labels.inc): the table, its bytes equal to the PC's
    struct AddressTable {
        TableKey key;
        uint32_t bytes;
        bool own; // the PS1's own: its bytes not the PC's (the PC port changed it)
    };
    static std::vector<AddressTable> addressTables();
    // the bytes from a pointer of this source to the end of the address-read table it lies in (0: none; a PS1 table the
    // PC keeps several copies of: the bytes all of them have)
    uint32_t tableBytesFrom(uint32_t pointer) const;

protected:
    TableReader pointee(uint32_t pointer, uint32_t n) const override;
    std::string pointeeString(uint32_t pointer, size_t maxLength) const override;
    RoutineRef routineOf(uint32_t pointer) const override;

private:
    // a block of PS1 memory (SLUS_011.40's text, a type-1 block of a disc file, at its load address) or of data made
    // from the disc (a made table, its strings), at its own virtual base
    struct Part {
        std::string file;
        uint32_t base = 0;  // its PS1 address (made data: 0)
        uint32_t vbase = 0; // its address in this source's space
        std::shared_ptr<const std::vector<uint8_t>> data;
    };
    // which table load's region a PS1 address holds
    struct Region {
        TableKey key;
        int region = 0;
    };

    std::string discPath(const std::string& name) const; // "" when the disc has no such file
    size_t addPart(std::string file, uint32_t base, std::vector<uint8_t> data) const;
    const std::vector<size_t>& partsOf(const std::string& file) const; // the file's type-1 blocks, loaded on first use
    // the part holding [address, address + n): `near` first, then near's file's other blocks, then SLUS_011.40
    std::optional<size_t> partFor(size_t near, uint32_t address, uint32_t n) const;
    // the address in this source's space of (part, PS1 address); records the region it starts
    uint32_t virtualOf(size_t part, uint32_t address, std::optional<Region> region) const;
    // n bytes of the part from address (fewer at its end), the region's pointer slots translated
    TableReader regionBytes(size_t part, uint32_t address, uint32_t n, std::optional<Region> region) const;
    void makeTables();

    std::shared_ptr<const DiscImage> disc_;
    mutable std::vector<Part> parts_;
    mutable std::map<uint32_t, size_t> partByVbase_;
    mutable uint32_t nextVbase_ = kVirtualBase;
    mutable std::map<std::string, std::vector<size_t>> partsByFile_;
    size_t slus_ = 0;
    struct Located {
        size_t part = 0; // made tables: their part; disc tables: resolved on first use (npos)
        std::string file;
        uint32_t address = 0;
        bool made = false;
        uint32_t state = 0; // a table in the program's state (built at run time, initially zero): the engine's address
        // a table the PS1 keeps a copy of in several files (the overlays of the stages that use it): the others; the
        // one read is in the file loaded last (the overlay in PS1 RAM), the first before any of them is loaded
        std::vector<std::pair<std::string, uint32_t>> copies;
    };
    size_t voicePart(TableKey key, const Located& table) const;
    std::map<TableKey, Located> tables_;
    // Embedded native pointers to message banks must resolve to the same bank handles as the named tables.
    std::map<std::pair<std::string, uint32_t>, TableId> bankAliases_;
    std::vector<std::string> voiceFiles_; // logical voice file -> ISO XA path, from SLUS's load-index table
    std::map<TableKey, uint32_t> voiceBytes_; // converted four-byte record extent
    std::map<TableKey, uint32_t> voiceCounts_; // complete PS1 descriptor list lengths (general labels may overrun)
    mutable std::map<std::pair<std::string, uint32_t>, size_t> voiceParts_;
    mutable std::map<std::string, std::vector<Ps1XaClip>> voiceClips_;
    // the file and address of a disc table: its copy in the file loaded last
    std::pair<std::string, uint32_t> placeOf(const Located& t) const;
    mutable std::map<std::string, uint64_t> loadedAt_; // a disc file: when gameFile last loaded it
    mutable uint64_t loads_ = 0;
    // the slots by (table, region): the function pointers' offsets / routines, the data pointers' offsets / child regions
    std::map<std::pair<TableKey, int>, std::vector<std::pair<uint32_t, RoutineId>>> routineSlots_;
    std::map<std::pair<TableKey, int>, std::vector<std::pair<uint32_t, int>>> dataSlots_;
    mutable std::map<uint32_t, Region> regionAt_; // by virtual address
    // the tables the game reads by address (ps1_labels.inc), by file and PS1 address: their extent and slots, applied to
    // every read that meets them
    struct Label {
        struct Slot {
            uint32_t offset;
            SlotKind kind;
            RoutineId routine; // a function's (None: no C++ routine)
            uint32_t state;    // a state pointer's: the engine's address of that state
        };
        uint32_t bytes = 0;  // the longest of the PC tables placed here (the slots cover it)
        uint32_t common = 0; // the shortest: the bytes all of them have
        std::vector<Slot> slots;
    };
    static constexpr uint32_t kMaxLabelBytes = 0x1000;
    std::map<std::string, std::map<uint32_t, Label>> labels_;
    mutable std::recursive_mutex lock_;
};

} // namespace od
