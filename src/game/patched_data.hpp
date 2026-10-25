// The records of the program's data the game rewrites in place (an exit entry a stage setup redirects, the box the
// buster cannon resizes for each shot, the formation picks a stage's spawner points its field table at): the exe
// writes its own data, which keeps the change for the rest of the run. The native state keeps a copy of each such
// record (made from the data source on its first use, kept for the run); the game reads and writes the copy, and the
// readers of the record's address (an actor's box pointer, the room's exit table) find it here before the data
// (recordAt). A record is named by its table (PatchedRecord: a fixed record) or by the pointer the state holds to it
// (patchedRecordFor: the stage's field encounter table). The test translator lays every copy out at its address.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/vectors.hpp"

namespace od::port {

// v stored little-endian at out[o..] (a record saved in the exe's layout)
template <class T> void storeLe(std::span<uint8_t> out, size_t o, T v) {
    static_assert(std::is_integral_v<T>);
    using U = std::make_unsigned_t<T>;
    for (size_t i = 0; i < sizeof(T); ++i) out[o + i] = uint8_t(U(v) >> (8 * i));
}

// An s16 point of the program's data the game rewrites in place (a table a bone transform shifts as it reads it)
struct PatchedPoint : ShortVec3, DataRecord {
    static constexpr uint32_t kBytes = 6;
    template <class R> static PatchedPoint load(const R& r) {
        PatchedPoint p;
        p.x = r.s16(0);
        p.y = r.s16(2);
        p.z = r.s16(4);
        return p;
    }
    void save(std::span<uint8_t> out) const {
        storeLe(out, 0, x);
        storeLe(out, 2, y);
        storeLe(out, 4, z);
    }
};

// One patched record (the registry's view of a patched record of type R)
class PatchedData {
public:
    virtual ~PatchedData() = default;
    // the value the game state stores for a pointer to the record
    virtual uint32_t address() const = 0;
    virtual uint32_t bytes() const = 0;
    // the record in the exe's layout (the copy, made from the data first if it was not yet)
    virtual void save(std::span<uint8_t> out) = 0;
    // the copy = these bytes in the exe's layout
    virtual void load(std::span<const uint8_t> in) = 0;
    // the copy (made first) and its type
    virtual void* object() = 0;
    virtual const std::type_info& type() const = 0;
};
// every patched record of the run (each registers itself when it is made)
std::vector<PatchedData*>& patchedRecords();

// The copy of a record of type R (kBytes, load(reader) and save(std::span<uint8_t>) in the exe's layout)
template <class R> class PatchedCopy : public PatchedData {
public:
    R& get() {
        if (!made_) {
            record_ = dataRecord<R>(address(), R::kBytes);
            made_ = true;
        }
        return record_;
    }
    R* operator->() { return &get(); }
    R& operator*() { return get(); }

    uint32_t bytes() const override { return R::kBytes; }
    void save(std::span<uint8_t> out) override { get().save(out); }
    void load(std::span<const uint8_t> in) override {
        record_ = R::load(DataReader(DataReader::Built{in}));
        made_ = true;
    }
    void* object() override { return &get(); }
    const std::type_info& type() const override { return typeid(R); }

protected:
    PatchedCopy() { patchedRecords().push_back(this); }

private:
    R record_{};
    bool made_ = false;
};

// a fixed patched record: its table (its address: the source's stateAddress)
template <class R> class PatchedRecord final : public PatchedCopy<R> {
public:
    explicit PatchedRecord(TableId id) : key_{id} {}
    PatchedRecord(const PatchedRecord&) = delete;
    PatchedRecord& operator=(const PatchedRecord&) = delete;
    uint32_t address() const override { return dataSource().stateAddress(key_); }

private:
    TableKey key_;
};

// a patched record named by the pointer the state holds to it
template <class R> class PatchedRecordAt final : public PatchedCopy<R> {
public:
    explicit PatchedRecordAt(uint32_t pointer) : pointer_(pointer) {}
    uint32_t address() const override { return pointer_; }

private:
    uint32_t pointer_;
};
// the patched records made at their pointers (owned for the run)
std::vector<std::unique_ptr<PatchedData>>& patchedRecordsAt();

// the patched record of type R whose address is `pointer` (nullptr: the pointer names none, read the data)
template <class R> R* patchedRecordAt(uint32_t pointer) {
    if (pointer == 0) return nullptr;
    for (PatchedData* d : patchedRecords())
        if (d->type() == typeid(R) && d->address() == pointer) return static_cast<R*>(d->object());
    return nullptr;
}
// the patched record of type R at `pointer`, made (a copy of the data's) on its first write
template <class R> R& patchedRecordFor(uint32_t pointer) {
    if (R* r = patchedRecordAt<R>(pointer)) return *r;
    auto p = std::make_unique<PatchedRecordAt<R>>(pointer);
    R& r = p->get();
    patchedRecordsAt().push_back(std::move(p));
    return r;
}
// the record of type R at `pointer`: its patched copy, else the data's
template <class R> R recordAt(uint32_t pointer) {
    if (const R* r = patchedRecordAt<R>(pointer)) return *r;
    return dataRecord<R>(pointer, R::kBytes);
}

} // namespace od::port
