// The game's data, wherever it comes from: the game's files (the DAT archive) and the tables the game program carries
// (stats, per-stage parameters, file names, handler tables ...). The game asks for a table by WHAT it is -- a TableKey
// (engine/table_ids) and a typed struct with a loader (DataTable<T>, ScalarTable, RecordTable, RoutineTable) -- never
// by where it is: each source knows where its copy lives and how its pointers read.
//
//   * The PC source (engine/pc_data_source) reads DAT/ and the user's dash2.exe as a FILE (a table's known virtual
//     address turned into a file offset through the PE section headers); it never maps the exe as memory.
//   * A PS1 source (not written yet) implements the same interface over the disc: its own table locations (the PC ->
//     PS1 map, as an address-only file), its own pointer encoding (MIPS) and its own function map.
//
// Pointers inside a table go through the source's reader (TableReader): a data pointer becomes the reader of the data
// it points at (follow), a function pointer becomes the named C++ routine (RoutineRef: a RoutineId, engine/
// routine_ids) through the source's generated, address-only function map. A pointer with no C++ routine stays an
// explicit gap that names the pointer (RoutineRef::require / the caller's port gap).
//
// The bound source: the host (app, each test) creates one and binds it (bindDataSource / ScopedDataSource) before the
// game runs, as it binds the loaded files (engine/file_data).
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <vector>

#include "engine/game_file.hpp"
#include "engine/routine_ids.hpp"
#include "engine/table_ids.hpp"

namespace od {

// Which table: its id and, for a table the game keeps one of per stage / per room, which one (index)
struct TableKey {
    TableId id{};
    uint32_t index = 0;
    friend auto operator<=>(const TableKey&, const TableKey&) = default;
};
// the game's stages (ST00 .. ST5C: the per-stage tables' rows, the stage and room function tables 0x8d3808 / 0x8d397c)
inline constexpr int kStageCount = 93;
// the per-room tables' index: the stage and the room
constexpr uint32_t roomIndex(int stage, int room) { return uint32_t(stage) << 8 | uint32_t(room & 0xff); }
// "<table name>[index]" for the errors
std::string describe(TableKey key);

// A data-source error (a table the source does not have, a read past a table's end, an unbound source): a refactor or
// installation error, never a port gap -- it derives from std::runtime_error so the update loops' port-gap handler
// (std::logic_error) does not swallow it.
class DataSourceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A function pointer of a table, as the source's reader translated it
class RoutineRef {
public:
    RoutineRef() = default;
    RoutineRef(RoutineId id, uint32_t pointer) : id_(id), pointer_(pointer) {}

    // the named routine; RoutineId::None for a null pointer and for a routine with no C++ routine (mapped() false)
    RoutineId id() const { return id_; }
    bool null() const { return pointer_ == 0; }
    // a pointer is set (mapped or not)
    explicit operator bool() const { return pointer_ != 0; }
    bool mapped() const { return id_ != RoutineId::None; }
    bool is(RoutineId r) const { return id_ == r; }
    // the named routine; a pointer with no C++ routine: std::logic_error "<area>: not ported: <what> 0x<pointer>"
    // (the port gap); RoutineId::None for a null pointer
    RoutineId require(const char* area, const char* what) const;
    // the pointer's value in the source's own encoding (an x86 address for the PC source): for the messages of a
    // gap only (and the value the game state stores: a routine's identity), never to dispatch on
    uint32_t pointerValue() const { return pointer_; }

private:
    RoutineId id_ = RoutineId::None;
    uint32_t pointer_ = 0;
};

class DataSource;

// One table's bytes in a source, read little-endian; its pointers translated by the source
class TableReader {
public:
    TableReader() = default;
    TableReader(const DataSource& source, std::vector<uint8_t> bytes)
        : source_(&source), bytes_(std::make_shared<const std::vector<uint8_t>>(std::move(bytes))), size_(bytes_->size()) {}
    // n bytes from `offset` of a block the source keeps (no copy)
    TableReader(const DataSource& source, std::shared_ptr<const std::vector<uint8_t>> block, size_t offset, size_t n)
        : source_(&source), bytes_(std::move(block)), offset_(offset), size_(n) {}

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    template <class T> T read(size_t o) const {
        static_assert(std::is_arithmetic_v<T>, "TableReader::read: a scalar");
        T v;
        std::memcpy(&v, at(o, sizeof(T)), sizeof(T));
        return v;
    }
    uint8_t u8(size_t o) const { return read<uint8_t>(o); }
    int8_t s8(size_t o) const { return read<int8_t>(o); }
    uint16_t u16(size_t o) const { return read<uint16_t>(o); }
    int16_t s16(size_t o) const { return read<int16_t>(o); }
    uint32_t u32(size_t o) const { return read<uint32_t>(o); }
    int32_t s32(size_t o) const { return read<int32_t>(o); }
    float f32(size_t o) const { return read<float>(o); }
    double f64(size_t o) const { return read<double>(o); }
    std::span<const uint8_t> bytes(size_t o, size_t n) const { return {at(o, n), n}; }
    // the n bytes from o as a reader of their own (a record of the table)
    TableReader sub(size_t o, size_t n) const;

    // a data pointer at o: the reader of the n bytes it points at (an empty reader for a null pointer)
    TableReader follow(size_t o, uint32_t n) const;
    // a data pointer at o to a C string: the string (at most maxLength characters; "" for a null pointer)
    std::string followString(size_t o, size_t maxLength) const;
    // a function pointer at o: the named routine
    RoutineRef routine(size_t o) const;

private:
    const uint8_t* at(size_t o, size_t n) const;

    const DataSource* source_ = nullptr;
    std::shared_ptr<const std::vector<uint8_t>> bytes_;
    size_t offset_ = 0, size_ = 0;
};

// A source of the game's data (engine/pc_data_source: the PC one)
class DataSource {
public:
    DataSource();
    virtual ~DataSource() = default;
    DataSource(const DataSource&) = delete;
    DataSource& operator=(const DataSource&) = delete;

    // ---- the game's files, by the game's names ("ST09.DAT", "PL00P000.DAT" ...)
    virtual bool hasFile(const std::string& name) const = 0;
    // whether this source reads the PS1 disc (a few stage cases keep the PS1's table shape, not the PC's)
    virtual bool isPs1() const { return false; }
    // the file's parts (engine/game_file), in the source's own formats; throws DataSourceError when the source has
    // no such file
    virtual GameFile gameFile(const std::string& name) const = 0;

    // ---- the tables
    virtual bool hasTable(TableKey key) const = 0;
    // the table's first `bytes` bytes; throws DataSourceError when the source has no such table
    virtual TableReader table(TableKey key, uint32_t bytes) const = 0;
    // the value the game state stores for a reference to this table (an actor's box pointer, the room's parameter
    // pointer ...): the table's identity in the source's encoding (the data reads resolve it: game/loaded_data)
    virtual uint32_t stateAddress(TableKey key) const = 0;

    // ---- the routines the game state names (the state keeps a function pointer in the source's encoding as the
    // routine's identity -- an actor's class, a script's step, a stage callback)
    // the routine a function pointer the game state holds names (0: RoutineRef(); a pointer with no C++ routine:
    // not mapped, its value kept for the port gap's message)
    RoutineRef stateRoutine(uint32_t pointer) const { return pointer == 0 ? RoutineRef() : routineOf(pointer); }
    // the program data a data pointer the game holds names, in the source's encoding (a record's pointer an actor or
    // a script keeps, a pointer of a table): its first n bytes (fewer at the end of the source's data; none for a
    // pointer outside it)
    TableReader stateData(uint32_t pointer, uint32_t n) const { return pointee(pointer, n); }
    // the value the game state stores for a reference to the routine (RoutineId::None: 0); throws DataSourceError
    // when the source has no pointer for it
    virtual uint32_t routineStateValue(RoutineId id) const = 0;

    // the typed table T of `key` (T::kBytes bytes, T::load(const TableReader&)), loaded once per source
    template <class T> const T& load(TableKey key) const {
        Cached& slot = cache_[key];
        if (!slot.value) {
            slot.value = std::make_shared<const T>(T::load(table(key, T::kBytes)));
            slot.type = std::type_index(typeid(T));
        } else if (slot.type != std::type_index(typeid(T))) {
            throw DataSourceError("data source: " + describe(key) + " loaded as two types");
        }
        return *static_cast<const T*>(slot.value.get());
    }
    // a number no other source of the process has (the table handles' cache key)
    uint64_t serial() const { return serial_; }

protected:
    friend class TableReader;
    // the reader's pointer translation: the n bytes a data pointer of this source's encoding points at, the C string
    // it points at, the routine a function pointer names
    virtual TableReader pointee(uint32_t pointer, uint32_t n) const = 0;
    virtual std::string pointeeString(uint32_t pointer, size_t maxLength) const = 0;
    virtual RoutineRef routineOf(uint32_t pointer) const = 0;

private:
    struct Cached {
        std::shared_ptr<const void> value;
        std::type_index type = std::type_index(typeid(void));
    };
    mutable std::map<TableKey, Cached> cache_;
    uint64_t serial_;
};

// ---- the bound source --------------------------------------------------------------------------------------------

// the bound source; throws DataSourceError when none is bound
const DataSource& dataSource();
bool dataSourceBound();
// binds `s` as the data source (nullptr unbinds)
void bindDataSource(const DataSource* s);

// the value the game state stores for a reference to the table `id` of the bound source (DataSource::stateAddress):
// a table the game hands on by pointer (the box an actor points at, a script's steps, a spawn list ...)
inline uint32_t stateAddress(TableId id) { return dataSource().stateAddress({id, 0}); }

// the bound source's DataSource::stateRoutine / routineStateValue
inline RoutineRef stateRoutine(uint32_t pointer) { return dataSource().stateRoutine(pointer); }
inline uint32_t routineStateValue(RoutineId id) { return dataSource().routineStateValue(id); }

// Binds a source for a scope and restores the previously bound one (also when the scope throws)
class ScopedDataSource {
public:
    explicit ScopedDataSource(const DataSource& s);
    ~ScopedDataSource();
    ScopedDataSource(const ScopedDataSource&) = delete;
    ScopedDataSource& operator=(const ScopedDataSource&) = delete;

private:
    const DataSource* previous_;
};

// ---- records ----------------------------------------------------------------------------------------------------

// The base of a record of the data (a typed struct its static load reads from a reader): a record is made by its
// loader (dataRecord, DataTable, the record's own reader); an address or any integer is no record, so a construction
// from one -- `Record r(address)`, the aggregate's paren / brace initialization of its first member -- does not compile
struct DataRecord {
    DataRecord() = default;
    template <class I>
        requires std::is_integral_v<I> || std::is_enum_v<I>
    DataRecord(I) = delete;
};

// ---- typed tables ------------------------------------------------------------------------------------------------

// a table index past the table's end (the exe would read the next table's bytes: an explicit error instead)
[[noreturn]] void tableIndexError(size_t index, size_t count);
// ... of the named table
[[noreturn]] void tableIndexError(const std::string& table, size_t index, size_t count);

// N scalars of type T, one after the other
template <class T, size_t N> struct ScalarTable : DataRecord {
    static constexpr uint32_t kBytes = uint32_t(sizeof(T) * N);
    std::array<T, N> values{};

    T operator[](size_t i) const {
        if (i >= N) tableIndexError(i, N);
        return values[i];
    }
    static constexpr size_t size() { return N; }
    static ScalarTable load(const TableReader& r) {
        ScalarTable t;
        for (size_t i = 0; i < N; ++i) t.values[i] = r.read<T>(i * sizeof(T));
        return t;
    }
};

// N scalars of type T, Stride bytes apart (a column of a table of wider records)
template <class T, size_t N, uint32_t Stride> struct StridedScalarTable : DataRecord {
    static constexpr uint32_t kBytes = uint32_t(Stride * (N - 1) + sizeof(T));
    std::array<T, N> values{};

    T operator[](size_t i) const {
        if (i >= N) tableIndexError(i, N);
        return values[i];
    }
    static constexpr size_t size() { return N; }
    static StridedScalarTable load(const TableReader& r) {
        StridedScalarTable t;
        for (size_t i = 0; i < N; ++i) t.values[i] = r.read<T>(i * Stride);
        return t;
    }
};

// N records of R, Stride bytes apart (R::load(const TableReader&) reads one record from its first byte)
template <class R, size_t N, uint32_t Stride> struct RecordTable : DataRecord {
    static constexpr uint32_t kBytes = uint32_t(Stride * N);
    std::array<R, N> records{};

    const R& operator[](size_t i) const {
        if (i >= N) tableIndexError(i, N);
        return records[i];
    }
    static constexpr size_t size() { return N; }
    static RecordTable load(const TableReader& r) {
        RecordTable t;
        for (size_t i = 0; i < N; ++i) t.records[i] = R::load(r.sub(i * Stride, Stride));
        return t;
    }
};

// N data pointers to one record of R each (R::load reads it from the RecordBytes bytes the pointer names); a null
// pointer (or one past the source's data) is an absent record
template <class R, size_t N, uint32_t RecordBytes> struct RecordPointerTable : DataRecord {
    static constexpr uint32_t kBytes = uint32_t(4 * N);
    std::array<R, N> records{};
    std::array<bool, N> present{};

    const R& operator[](size_t i) const {
        if (i >= N) tableIndexError(i, N);
        return records[i];
    }
    bool has(size_t i) const { return i < N && present[i]; }
    static constexpr size_t size() { return N; }
    static RecordPointerTable load(const TableReader& r) {
        RecordPointerTable t;
        for (size_t i = 0; i < N; ++i) {
            const TableReader p = r.follow(i * 4, RecordBytes);
            t.present[i] = p.size() == RecordBytes;
            if (t.present[i]) t.records[i] = R::load(p);
        }
        return t;
    }
};

// N function pointers (a handler table), as named routines
template <size_t N> struct RoutineTable : DataRecord {
    static constexpr uint32_t kBytes = uint32_t(4 * N);
    std::array<RoutineRef, N> routines{};

    const RoutineRef& operator[](size_t i) const {
        if (i >= N) tableIndexError(i, N);
        return routines[i];
    }
    // the routine of entry i; an index past the table: None (a dispatch's port gap -- the exe would call whatever word
    // follows the table)
    RoutineId idAt(size_t i) const { return i < N ? routines[i].id() : RoutineId::None; }
    // entry i's pointer in the source's encoding, for a port gap's message (past the table: 0)
    uint32_t pointerAt(size_t i) const { return i < N ? routines[i].pointerValue() : 0; }
    static constexpr size_t size() { return N; }
    static RoutineTable load(const TableReader& r) {
        RoutineTable t;
        for (size_t i = 0; i < N; ++i) t.routines[i] = r.routine(i * 4);
        return t;
    }
};

// ---- the tables the game reads: every handle (DataTable, TableFamily) registers its table, its byte count and its
// typed loader when it is constructed, so a test or a generator can load every table the game names
struct TableUse {
    TableId id{};
    uint32_t index = 0;   // the table's (a DataTable's); unused for a family
    bool family = false;  // a TableFamily: one table per index (a stage's, a room's: roomIndex)
    uint32_t bytes = 0;   // the bytes the typed loader reads
    void (*load)(TableKey) = nullptr; // loads the table of `key` through the bound source as the handle's type
};
const std::vector<TableUse>& tableUses();
void registerTableUse(const TableUse& use);

// A typed table of the bound source as a namespace-scope handle, loaded on first use:
//     const DataTable<ScalarTable<uint8_t, 8>> puffU{TableId::PuffU};
//     ... puffU[puff & 7] ...
template <class T> class DataTable {
public:
    explicit DataTable(TableId id, uint32_t index = 0) : key_{id, index} {
        registerTableUse({id, index, false, T::kBytes, [](TableKey k) { (void)dataSource().load<T>(k); }});
    }

    const T& get() const {
        const DataSource& s = dataSource();
        if (serial_ != s.serial()) {
            table_ = &s.load<T>(key_);
            serial_ = s.serial();
        }
        return *table_;
    }
    const T* operator->() const { return &get(); }
    const T& operator*() const { return get(); }
    // entry i (an index past the table: DataSourceError naming the table)
    decltype(auto) operator[](size_t i) const {
        const T& t = get();
        if constexpr (requires { T::size(); }) {
            if (i >= T::size()) tableIndexError(describe(key_), i, T::size());
        }
        return t[i];
    }
    static constexpr size_t size() { return T::size(); }
    TableKey key() const { return key_; }
    // the bound source has the table (a host without one, or a source without this table: false)
    bool available() const { return dataSourceBound() && dataSource().hasTable(key_); }
    // the value the game state stores for a reference to this table (DataSource::stateAddress)
    uint32_t stateAddress() const { return dataSource().stateAddress(key_); }

private:
    TableKey key_;
    mutable const T* table_ = nullptr;
    mutable uint64_t serial_ = 0;
};

// A table the game keeps one of per stage / per room (the index: the stage, or roomIndex), as a namespace-scope
// handle:
//     const TableFamily<RoomCamera> roomCameras{TableId::RoomCamera};
//     ... if (roomCameras.has(roomIndex(stage, room))) ... roomCameras[roomIndex(stage, room)] ...
template <class T> class TableFamily {
public:
    explicit TableFamily(TableId id) : id_(id) {
        registerTableUse({id, 0, true, T::kBytes, [](TableKey k) { (void)dataSource().load<T>(k); }});
    }
    // the bound source has the table of `index`
    bool has(uint32_t index) const { return dataSource().hasTable({id_, index}); }
    const T& operator[](uint32_t index) const { return dataSource().load<T>({id_, index}); }
    TableId id() const { return id_; }

private:
    TableId id_;
};

} // namespace od
