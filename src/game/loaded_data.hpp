// The data the game reads through a pointer its state or its data holds: a loaded file's bytes (the stage's banks,
// the player's files, the messages: the bound loaded-file store, engine/file_data) or the game program's data (the
// bound data source, engine/data_source, in its own pointer encoding). Typed records of the loaded files read from a
// reader (FileDataReader / DataReader); the pointer encoding is the one the state keeps (a loaded file's load address,
// the program data's address).
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <variant>

#include "engine/data_source.hpp"
#include "engine/file_data.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"

namespace od::port {

// a loaded file's bytes from `pointer` (at least `bytes` of them in its block)
inline FileDataReader loadedFile(uint32_t pointer, uint32_t bytes = 1) { return loadedFiles().reader(pointer, bytes); }
// the loaded files hold [pointer, pointer + bytes)
inline bool loadedFileMapped(uint32_t pointer, uint32_t bytes = 1) {
    return loadedFilesBound() && loadedFiles().mapped(pointer, bytes);
}
template <class T> T loadedScalar(uint32_t pointer) { return loadedFile(pointer, sizeof(T)).read<T>(0); }
// a write of the game into a loaded file (a texture word, a segment switch: the file's bytes are the state's)
template <class T> void storeLoaded(uint32_t pointer, T value) {
    static_assert(std::is_integral_v<T>);
    loadedFile(pointer, sizeof(T)); // a pointer no loaded file holds: the store's error naming it
    loadedFiles().write(pointer, &value, sizeof(T));
}

// One field of a loaded file, read and written in place (a room block's record the scripts rewrite: game/room_records)
template <class T> class LoadedField : public AliasOps<LoadedField<T>, T> {
public:
    explicit LoadedField(uint32_t pointer) : p_(pointer) {}
    LoadedField(const LoadedField&) = default;
    LoadedField& operator=(const LoadedField& o) { return assign(o.get()); }
    LoadedField& operator=(T v) { return assign(v); }
    T get() const { return loadedScalar<T>(p_); }
    void set(T v) { storeLoaded(p_, v); }
    LoadedField& assign(T v) {
        set(v);
        return *this;
    }
    uint32_t address() const { return p_; }
    // the field's bytes are loaded (a host that loaded part of the files asks first)
    bool mapped() const { return loadedFileMapped(p_, sizeof(T)); }

private:
    uint32_t p_;
};

// A scalar array in an owned file block. The reference owns its backing bytes;
// indexing has no dependency on the image binding.
template <class T> class LoadedArray {
public:
    explicit LoadedArray(uint32_t pointer) : reader_(loadedFile(pointer)) {}
    T operator[](size_t index) const { return reader_.read<T>(index * sizeof(T)); }
private:
    FileDataReader reader_;
};

struct LoadedVector : DataRecord {
    int16_t x, y, z;
    template <class R> static LoadedVector load(const R& r) {
        return {{}, r.template read<int16_t>(0), r.template read<int16_t>(2), r.template read<int16_t>(4)};
    }
};

// ---- the data the game builds -------------------------------------------------------------------------------------
// The data the game builds at addresses of its own and hands on by pointer like the data it reads (the texts a
// window's code inserts, the options page's copies of the menus' tables): each an owned native byte array its owner
// registers under that address; the pointers name them in the exe's encoding.
struct BuiltData {
    uint32_t base = 0;
    std::span<uint8_t> bytes;
    bool holds(uint32_t pointer, uint32_t n) const {
        return pointer >= base && uint64_t(pointer) + n <= uint64_t(base) + bytes.size();
    }
};
// registers `bytes` under `base` (its owner, once: the storage lives for the run)
void registerBuiltData(uint32_t base, std::span<uint8_t> bytes);
// the built data holding [pointer, pointer + n) (nullptr: none)
const BuiltData* builtDataAt(uint32_t pointer, uint32_t n);

// The bytes a data pointer names, wherever they are (built by the game, a loaded file or the program's data)
class DataReader {
public:
    // a read of the game's built bytes (the storage outlives the reader)
    struct Built {
        std::span<const uint8_t> bytes;
        template <class T> T read(size_t o) const {
            if (o + sizeof(T) > bytes.size()) throw DataSourceError("built data: a read past its end");
            T v;
            std::memcpy(&v, bytes.data() + o, sizeof(T));
            return v;
        }
    };
    DataReader() = default;
    DataReader(FileDataReader r) : r_(std::move(r)) {}
    DataReader(TableReader r) : r_(std::move(r)) {}
    DataReader(Built r) : r_(r) {}
    template <class T> T read(size_t o) const {
        if (const auto* f = std::get_if<FileDataReader>(&r_)) return f->read<T>(o);
        if (const auto* b = std::get_if<Built>(&r_)) return b->read<T>(o);
        return std::get<TableReader>(r_).read<T>(o);
    }
    uint8_t u8(size_t o) const { return read<uint8_t>(o); }
    int8_t s8(size_t o) const { return read<int8_t>(o); }
    uint16_t u16(size_t o) const { return read<uint16_t>(o); }
    int16_t s16(size_t o) const { return read<int16_t>(o); }
    uint32_t u32(size_t o) const { return read<uint32_t>(o); }
    int32_t s32(size_t o) const { return read<int32_t>(o); }
    // the reader from o on (a record of the data)
    DataReader sub(size_t o) const {
        if (const auto* f = std::get_if<FileDataReader>(&r_)) return DataReader(f->sub(o));
        if (const auto* bl = std::get_if<Built>(&r_))
            return DataReader(Built{bl->bytes.subspan(std::min(o, bl->bytes.size()))});
        const TableReader& t = std::get<TableReader>(r_);
        return DataReader(t.sub(o, t.size() - o));
    }

private:
    std::variant<FileDataReader, TableReader, Built> r_;
};

// the bytes from a data pointer the game holds (`bytes`: at least that many): the game's built data, a loaded file's,
// else the program data's (a pointer none holds: the data source's error)
inline DataReader dataAt(uint32_t pointer, uint32_t bytes) {
    if (const BuiltData* b = builtDataAt(pointer, bytes))
        return DataReader(DataReader::Built{std::span<const uint8_t>(b->bytes).subspan(pointer - b->base)});
    if (loadedFileMapped(pointer, bytes)) return DataReader(loadedFile(pointer, bytes));
    return DataReader(dataSource().stateData(pointer, bytes));
}
// the n bytes at the pointer are readable
inline bool dataMapped(uint32_t pointer, uint32_t n) {
    if (pointer == 0) return false;
    if (builtDataAt(pointer, n) || loadedFileMapped(pointer, n)) return true;
    return dataSourceBound() && dataSource().stateData(pointer, n).size() == n;
}
// a write of the game into data it holds a pointer to: its built data or a loaded file (the program's data is never
// written: the records the exe rewrites are patched records, game/patched_data)
template <class T> void storeData(uint32_t pointer, T value) {
    static_assert(std::is_integral_v<T>);
    if (const BuiltData* b = builtDataAt(pointer, sizeof(T))) {
        std::memcpy(b->bytes.data() + (pointer - b->base), &value, sizeof(T));
        return;
    }
    storeLoaded(pointer, value);
}
// one record R (R::load(reader)) at a data pointer
template <class R> R dataRecord(uint32_t pointer, uint32_t bytes) { return R::load(dataAt(pointer, bytes)); }
template <class T> T dataScalar(uint32_t pointer) { return dataAt(pointer, sizeof(T)).read<T>(0); }

// the C string at a data pointer (at most maxLength characters; it stops at the data's end)
inline std::string dataString(uint32_t pointer, size_t maxLength) {
    std::string s;
    for (size_t i = 0; i < maxLength && dataMapped(pointer + uint32_t(i), 1); ++i) {
        const char c = char(dataScalar<uint8_t>(pointer + uint32_t(i)));
        if (c == 0) break;
        s += c;
    }
    return s;
}
// the 16.16 triple at a data pointer
inline Fixed16Vec3 dataFixed16Vec3(uint32_t pointer) {
    const DataReader r = dataAt(pointer, 12);
    Fixed16Vec3 v;
    v.x = r.s32(0);
    v.y = r.s32(4);
    v.z = r.s32(8);
    return v;
}
// the s16 triple / quad at a data pointer (a point of a table)
inline ShortVec3 dataShortVec3(uint32_t pointer) {
    const DataReader r = dataAt(pointer, 6);
    return {r.s16(0), r.s16(2), r.s16(4)};
}
inline ShortVec4 dataShortVec4(uint32_t pointer) {
    const DataReader r = dataAt(pointer, 8);
    return {r.s16(0), r.s16(2), r.s16(4), r.s16(6)};
}
// The data's values of type T from a pointer on (a table of the data read by index; ShortVec3 / ShortVec4 elements
// six / eight bytes apart)
template <class T> class DataArray {
public:
    explicit DataArray(uint32_t pointer) : p_(pointer) {}
    T operator[](size_t i) const {
        if constexpr (std::is_same_v<T, ShortVec3>) return dataShortVec3(p_ + uint32_t(i) * 6);
        else if constexpr (std::is_same_v<T, ShortVec4>) return dataShortVec4(p_ + uint32_t(i) * 8);
        else return dataScalar<T>(p_ + uint32_t(i * sizeof(T)));
    }
    uint32_t address() const { return p_; }

private:
    uint32_t p_;
};

// A pointer the game state keeps to a record of the data (a box, an animation key, a model's tables): its value in the
// state's pointer encoding (the translator writes it as the exe does), the record read through dataAt. R: a record
// with kBytes and load(const DataReader&) (void: a pointer only read as its value)
template <class R = void> class DataPtr {
public:
    DataPtr() = default;
    uint32_t value() const { return v_; }
    void setValue(uint32_t v) { v_ = v; }
    // points at the data at `pointer` (a record of a table / of a loaded file: the value the exe stores)
    void pointAt(uint32_t pointer) { v_ = pointer; }
    // points at the table `id` of the program's data
    void pointAt(TableId id) { v_ = stateAddress(id); }
    DataPtr& operator=(std::nullptr_t) {
        v_ = 0;
        return *this;
    }
    template <class O> DataPtr& operator=(const DataPtr<O>& o) {
        v_ = o.value();
        return *this;
    }
    explicit operator bool() const { return v_ != 0; }
    bool operator==(std::nullptr_t) const { return v_ == 0; }
    template <class O> bool operator==(const DataPtr<O>& o) const { return v_ == o.value(); }
    // the record it points at (a null pointer: the data's error naming address 0)
    template <class Q = R>
        requires(!std::is_void_v<Q>)
    Q operator*() const {
        return dataRecord<Q>(v_, Q::kBytes);
    }
    template <class Q> struct Arrow {
        Q r;
        const Q* operator->() const { return &r; }
    };
    template <class Q = R>
        requires(!std::is_void_v<Q>)
    Arrow<Q> operator->() const {
        return {**this};
    }
    // the n bytes it points at are readable (a pointer read before its data is loaded: a host asks first)
    bool targetMapped(uint32_t n = 1) const { return dataMapped(v_, n); }

private:
    uint32_t v_ = 0;
};

} // namespace od::port
