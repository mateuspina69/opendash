// The game memory: the one GameMem image the ported game code runs on, bound once by its host (app/play_session,
// each test) when it creates the image. Game code and the host read and write it through the views and the field
// proxies a view is made of (MemField / MemArray / MemPtr / MemFixed16 / MemFixed16Vec3).
//
// A view derives from MemView (which holds its base address `at`) and declares its fields as members with default
// member initializers:
//     struct Actor : MemView {
//         using MemView::MemView;                  // Actor a(address)
//         MemField<uint8_t> live{at + 0x00};
//         MemFixed16Vec3 pos{at + 0x10};
//     };
// Use sites read as they will on the native type: `actor.velY = kJumpSpeed;`, `if (actor.pos.y.whole > floor)`.
// Views are never copy-assigned: MemView's deleted assignment makes `view = otherView` a compile error (it would
// rebind `at` and write the source's values to the old addresses). Field-to-field copies go through the proxies'
// value semantics (`a.pos = b.pos;`, `a.hp = b.hp;`).
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>

#include "translator/gamemem.hpp"
#include "game/resolution_error.hpp"
#include "game/vectors.hpp"

namespace od {

namespace detail {
inline GameMem* boundGameMemory = nullptr;
[[noreturn]] void gameMemoryUnbound();
} // namespace detail

// the bound game memory; throws std::logic_error when no image is bound
inline GameMem& gameMemory() {
    if (!detail::boundGameMemory) detail::gameMemoryUnbound();
    return *detail::boundGameMemory;
}
// binds `m` as the game memory (nullptr unbinds) and its loaded files as the game's
void bindGameMemory(GameMem* m);

// Binds another image for a scope and restores the previously bound one (also when the scope throws): game routines
// run on an image that is not the game state (the message formatters' scratch image, a ghost player's image).
class ScopedGameMemory {
public:
    explicit ScopedGameMemory(GameMem& m) : previous_(detail::boundGameMemory) { bindGameMemory(&m); }
    ~ScopedGameMemory() { bindGameMemory(previous_); }
    ScopedGameMemory(const ScopedGameMemory&) = delete;
    ScopedGameMemory& operator=(const ScopedGameMemory&) = delete;

private:
    GameMem* previous_;
};


// ---- views and field proxies -----------------------------------------------------------------------------------

// The base of every view: its base address. Copyable (a view is a handle), never assignable.
struct MemView {
    explicit constexpr MemView(uint32_t address) : at(address) {}
    MemView(const MemView&) = default;
    MemView& operator=(const MemView&) = delete;

    // the view's first `bytes` bytes are mapped in the bound image (a host that maps only part of the image, or a
    // record read before its data is loaded, asks first)
    bool mapped(uint32_t bytes) const { return gameMemory().mapped(at, bytes); }
    // an OpenDash region of the image (a buffer the exe allocates, a scratch area) mapped, zeroed, when it is not yet
    void mapZeroIfUnmapped(uint32_t bytes) const {
        if (!mapped(bytes)) gameMemory().mapZero(at, bytes);
    }
    // an OpenDash region of the image (a copy of loaded data the exe would allocate) mapped from these bytes
    void mapData(std::span<const uint8_t> bytes) const { gameMemory().map(at, bytes); }

    uint32_t at;
};

// One scalar field of the game memory at a fixed address: reads / writes with T's width and signedness. The
// arithmetic of the compound operators wraps at T's width like the raw code it replaces (`w32(a, u32(a) + v)`):
// + - ++ -- << are computed in unsigned 32-bit arithmetic and truncated to T; >> keeps T's signedness (an
// arithmetic shift for signed fields, as `s16(a) >> n`).
template <class T> class MemField {
    static_assert(std::is_integral_v<T> && (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4),
                  "MemField: 8 / 16 / 32-bit integers");
    using Bits = std::make_unsigned_t<T>;

    static uint32_t bits(T v) { return uint32_t(Bits(v)); }
    template <class U> static uint32_t bitsOf(U v) {
        static_assert(std::is_integral_v<U>, "MemField: integral operand");
        return uint32_t(v);
    }

public:
    explicit constexpr MemField(uint32_t address) : address_(address) {}
    MemField(const MemField&) = default;

    uint32_t address() const { return address_; }
    // the field's bytes are mapped in the bound image (a host that maps only part of the image asks first)
    bool mapped() const { return gameMemory().mapped(address_, sizeof(T)); }
    T get() const {
        T v;
        gameMemory().gameRead(address_, &v, sizeof(T));
        return v;
    }
    void set(T v) { gameMemory().gameWrite(address_, &v, sizeof(T)); }
    operator T() const { return get(); }
    // the field zeroed with byte stores, first byte to last (a transcription whose exe clears it a byte at a time)
    void clearBytes() {
        const uint8_t zero = 0;
        for (uint32_t i = 0; i < uint32_t(sizeof(T)); ++i) gameMemory().gameWrite(address_ + i, &zero, 1);
    }

    // value semantics: assigning one field to another copies the value, never the address
    MemField& operator=(const MemField& o) {
        set(o.get());
        return *this;
    }
    MemField& operator=(T v) {
        set(v);
        return *this;
    }
    template <class U> MemField& operator+=(U v) { return *this = T(bits(get()) + bitsOf(v)); }
    template <class U> MemField& operator-=(U v) { return *this = T(bits(get()) - bitsOf(v)); }
    template <class U> MemField& operator|=(U v) { return *this = T(bits(get()) | bitsOf(v)); }
    template <class U> MemField& operator&=(U v) { return *this = T(bits(get()) & bitsOf(v)); }
    template <class U> MemField& operator^=(U v) { return *this = T(bits(get()) ^ bitsOf(v)); }
    template <class U> MemField& operator<<=(U v) { return *this = T(bits(get()) << bitsOf(v)); }
    template <class U> MemField& operator>>=(U v) { return *this = T(get() >> bitsOf(v)); }
    MemField& operator++() { return *this = T(bits(get()) + 1u); }
    MemField& operator--() { return *this = T(bits(get()) - 1u); }
    T operator++(int) {
        const T old = get();
        set(T(bits(old) + 1u));
        return old;
    }
    T operator--(int) {
        const T old = get();
        set(T(bits(old) - 1u));
        return old;
    }

private:
    uint32_t address_;
};

// N consecutive fields of type T (stride sizeof(T)) starting at a fixed address.
template <class T, size_t N> class MemArray {
public:
    explicit constexpr MemArray(uint32_t address) : address_(address) {}
    MemArray(const MemArray&) = default;
    MemArray& operator=(const MemArray&) = delete;

    MemField<T> operator[](size_t i) { return MemField<T>(address_ + uint32_t(i * sizeof(T))); }
    const MemField<T> operator[](size_t i) const { return MemField<T>(address_ + uint32_t(i * sizeof(T))); }
    static constexpr size_t size() { return N; }
    uint32_t address() const { return address_; }
    // the array's bytes are mapped in the bound image
    bool mapped() const { return gameMemory().mapped(address_, uint32_t(N * sizeof(T))); }
    // an OpenDash region of the image (an address the exe does not have) mapped, zeroed, when it is not yet
    void mapIfUnmapped() {
        if (!mapped()) gameMemory().mapZero(address_, uint32_t(N * sizeof(T)));
    }
    // every byte of the array zeroed with byte stores, first to last (exe 0x40dc40's clear as the port writes it)
    void clearBytes() {
        for (uint32_t i = 0; i < uint32_t(N * sizeof(T)); ++i) MemField<uint8_t>(address_ + i) = 0;
    }

private:
    uint32_t address_;
};

// N consecutive records of the view kind V, Stride bytes apart (a table of structs: an actor's part table, the
// entries of an exe table): operator[] builds the view of record i by value.
template <class V, size_t N, uint32_t Stride> class MemViewArray {
public:
    explicit constexpr MemViewArray(uint32_t address) : address_(address) {}
    MemViewArray(const MemViewArray&) = default;
    MemViewArray& operator=(const MemViewArray&) = delete;

    V operator[](size_t i) { return V(address_ + uint32_t(i) * Stride); }
    const V operator[](size_t i) const { return V(address_ + uint32_t(i) * Stride); }
    static constexpr size_t size() { return N; }
    static constexpr uint32_t stride() { return Stride; }
    uint32_t address() const { return address_; }

private:
    uint32_t address_;
};

// A read-only 64-bit IEEE double at a fixed address (a constant of the exe's data, e.g. a scale factor).
class MemDouble {
public:
    explicit constexpr MemDouble(uint32_t address) : address_(address) {}
    MemDouble(const MemDouble&) = default;
    MemDouble& operator=(const MemDouble&) = delete;

    double get() const {
        double d;
        gameMemory().gameRead(address_, &d, 8);
        return d;
    }
    operator double() const { return get(); }
    uint32_t address() const { return address_; }

private:
    uint32_t address_;
};

// How a MemPtr<V> turns the exe address it holds into a view (the rule of every MemPtr):
// - a view kind with persistent views (the pool slots: game/actor_pools) declares, next to V, a resolver found by
//   argument-dependent lookup: `V* viewAt(std::type_identity<V>, uint32_t address)` -- the pool's view of that slot,
//   nullptr for 0. MemPtr then hands out `V*` / `V&`.
// - any other view kind (a class tail, an exe table) declares nothing: the view is built by value, `V(address)`,
//   and handed out by value (`->` goes through a proxy that holds it).
template <class V>
concept PersistentView = requires(uint32_t a) {
    { viewAt(std::type_identity<V>{}, a) } -> std::same_as<V*>;
};

// The view of a MemPtr<const V>'s target handed out by value: only const access, so a copy (`auto v = *ptr`) keeps
// the protection; it converts to `const V&` (`const V v = *ptr`, a const V& parameter)
template <class V> class ConstViewRef {
public:
    explicit ConstViewRef(uint32_t address) : view_(address) {}
    operator const V&() const { return view_; }
    const V* operator->() const { return &view_; }
    const V& view() const { return view_; }

private:
    V view_;
};

template <class V> class MemPtr {
public:
    // the view of the target handed out by value, for `ptr->field`
    struct Arrow {
        V view;
        V* operator->() { return &view; }
    };

    explicit constexpr MemPtr(uint32_t address) : field_(address) {}
    MemPtr(const MemPtr&) = default;

    // the exe address held (0 = null)
    uint32_t value() const { return uint32_t(field_); }
    explicit operator bool() const { return value() != 0; }
    // the target: V* for persistent views (nullptr for 0), else the view by value (for MemPtr<const V>: a
    // ConstViewRef, so no copy of it gives write access)
    decltype(auto) get() const {
        if constexpr (PersistentView<V>) return viewAt(std::type_identity<V>{}, value());
        else if constexpr (std::is_const_v<V>) return ConstViewRef<std::remove_const_t<V>>(value());
        else return V(value());
    }
    auto operator->() const {
        if constexpr (PersistentView<V> || std::is_const_v<V>) return get();
        else return Arrow{get()};
    }
    decltype(auto) operator*() const {
        if constexpr (PersistentView<V>) return *get();
        else return get();
    }

    // points at the view v (a const target needs a MemPtr<const V>)
    MemPtr& operator=(V* v) {
        field_ = v ? v->at : 0u;
        return *this;
    }
    // points at the exe data at `address` (a record of an exe table / of a loaded bank: the address the exe stores)
    void pointAt(uint32_t address) { field_ = address; }
    MemPtr& operator=(std::nullptr_t) {
        field_ = 0u;
        return *this;
    }
    MemPtr& operator=(const MemPtr& o) {
        field_ = o.value();
        return *this;
    }
    // the address another pointer kind holds (a native state's ImagePtr)
    template <class P>
        requires requires(const P& p) { { p.value() } -> std::convertible_to<uint32_t>; }
    MemPtr& operator=(const P& o) {
        field_ = o.value();
        return *this;
    }
    bool operator==(std::nullptr_t) const { return value() == 0; }
    bool operator==(const V* v) const { return value() == (v ? v->at : 0u); }
    uint32_t address() const { return field_.address(); }
    // the n bytes the pointer holds the address of are mapped in the bound image (a pointer read before its data is
    // loaded: a host that maps only part of the image asks first)
    bool targetMapped(uint32_t n = 1) const { return gameMemory().mapped(value(), n); }

private:
    MemField<uint32_t> field_;
};

// A native state's pointer to a view of the game memory: a member of a native struct (game/state_array's systems)
// holding the exe address of a target that still lives in the image (an actor slot, a loaded record, an exe table).
// Its use is MemPtr's; the test translator writes the address it holds into the exe's layout (value / setValue). It
// goes with its target: once the target is native, the member becomes a typed reference / index.
template <class V> class ImagePtr {
public:
    ImagePtr() = default;

    uint32_t value() const { return address_; }
    void setValue(uint32_t address) { address_ = address; }
    explicit operator bool() const { return address_ != 0; }
    decltype(auto) get() const {
        if constexpr (PersistentView<V>) return viewAt(std::type_identity<V>{}, address_);
        else if constexpr (std::is_const_v<V>) return ConstViewRef<std::remove_const_t<V>>(address_);
        else return V(address_);
    }
    auto operator->() const {
        if constexpr (PersistentView<V> || std::is_const_v<V>) return get();
        else return typename MemPtr<V>::Arrow{get()};
    }
    decltype(auto) operator*() const {
        if constexpr (PersistentView<V>) return *get();
        else return get();
    }
    ImagePtr& operator=(V* v) {
        address_ = v ? v->at : 0u;
        return *this;
    }
    // points at the exe data at `address` (a record of an exe table / of a loaded bank)
    void pointAt(uint32_t address) { address_ = address; }
    ImagePtr& operator=(std::nullptr_t) {
        address_ = 0;
        return *this;
    }
    ImagePtr& operator=(const MemPtr<V>& o) {
        address_ = o.value();
        return *this;
    }
    bool operator==(std::nullptr_t) const { return address_ == 0; }
    bool operator==(const V* v) const { return address_ == (v ? v->at : 0u); }
    bool targetMapped(uint32_t n = 1) const { return gameMemory().mapped(address_, n); }

private:
    uint32_t address_ = 0;
};

// A 16.16 fixed-point value: .frac (low u16), .whole (high s16), and the whole 32-bit value through the proxy itself.
class MemFixed16 : public MemField<int32_t> {
public:
    explicit constexpr MemFixed16(uint32_t address) : MemField<int32_t>(address), frac(address), whole(address + 2) {}
    MemFixed16(const MemFixed16&) = default;
    using MemField<int32_t>::operator=;
    MemFixed16& operator=(const MemFixed16& o) {
        MemField<int32_t>::operator=(o);
        return *this;
    }

    MemField<uint16_t> frac;
    MemField<int16_t> whole;
};

// Three consecutive 16.16 values (x, y, z); assigning one to another copies the three values.
class MemFixed16Vec3 {
public:
    explicit constexpr MemFixed16Vec3(uint32_t address) : x(address), y(address + 4), z(address + 8) {}
    MemFixed16Vec3(const MemFixed16Vec3&) = default;
    // the values as a native vector / set from one (a state still in the game memory next to the native state)
    operator port::Fixed16Vec3() const {
        port::Fixed16Vec3 v;
        v.x = int32_t(x);
        v.y = int32_t(y);
        v.z = int32_t(z);
        return v;
    }
    MemFixed16Vec3& operator=(const port::Fixed16Vec3& o) {
        x = o.x.value();
        y = o.y.value();
        z = o.z.value();
        return *this;
    }
    MemFixed16Vec3& operator=(const MemFixed16Vec3& o) {
        x = o.x;
        y = o.y;
        z = o.z;
        return *this;
    }
    uint32_t address() const { return x.address(); }

    MemFixed16 x, y, z;
};

// Three consecutive shorts (x, y, z): an s16 point or angle triple; assigning one to another copies the three.
class MemShortVec3 {
public:
    explicit constexpr MemShortVec3(uint32_t address) : x(address), y(address + 2), z(address + 4) {}
    MemShortVec3(const MemShortVec3&) = default;
    operator port::ShortVec3() const { return {x, y, z}; }
    MemShortVec3& operator=(const port::ShortVec3& o) {
        x = o.x;
        y = o.y;
        z = o.z;
        return *this;
    }
    MemShortVec3& operator=(const MemShortVec3& o) {
        x = o.x;
        y = o.y;
        z = o.z;
        return *this;
    }
    uint32_t address() const { return x.address(); }

    MemField<int16_t> x, y, z;
};

// Four consecutive shorts: x, y, z and a spare 4th lane. The generic code reads x / y / z; the spare lane is padding
// to it, and a class may keep a value of its own in those bytes (named in the class's view). Assigning one vector
// to another copies the four lanes as the exe does, as two dwords; clearWords() zeroes them as two dwords, clearBytes()
// as eight byte stores (each caller keeps the width of its exe store); assign(x, y, z) stores the three lanes and a
// zero spare lane one short at a time; setXY / setZSpare store two lanes as one dword.
class MemShortVec4 {
public:
    explicit constexpr MemShortVec4(uint32_t address)
        : x(address), y(address + 2), z(address + 4), spare_(address + 6), low_(address), high_(address + 4) {}
    MemShortVec4(const MemShortVec4&) = default;
    operator port::ShortVec4() const { return {x, y, z, spare_}; }
    // the four lanes from a native vector (the exe's two dword stores)
    MemShortVec4& operator=(const port::ShortVec4& o) {
        setWords(o.xyWord(), o.zSpareWord());
        return *this;
    }
    MemShortVec4& operator=(const MemShortVec4& o) {
        low_ = o.low_.get();
        high_ = o.high_.get();
        return *this;
    }
    void clearWords() {
        low_ = 0;
        high_ = 0;
    }
    void clearBytes() {
        for (uint32_t i = 0; i < 8; ++i) MemField<uint8_t>(address() + i) = 0;
    }
    void setXY(int16_t vx, int16_t vy) { low_ = uint32_t(uint16_t(vx)) | uint32_t(uint16_t(vy)) << 16; }
    // the four lanes from two dwords (x | y << 16, z | spare << 16) as the exe copies them
    void setWords(uint32_t xy, uint32_t zSpare) {
        low_ = xy;
        high_ = zSpare;
    }
    void setZSpare(int16_t vz, int16_t spare) { high_ = uint32_t(uint16_t(vz)) | uint32_t(uint16_t(spare)) << 16; }
    void assign(int16_t vx, int16_t vy, int16_t vz) {
        x = vx;
        y = vy;
        z = vz;
        spare_ = 0;
    }
    uint32_t address() const { return x.address(); }
    // the spare lane's address (for the class view that names it)
    uint32_t spareAddress() const { return spare_.address(); }
    // the two dwords the exe copies the vector as (x | y << 16, z | spare << 16)
    uint32_t xyWord() const { return low_; }
    uint32_t zSpareWord() const { return high_; }

    MemField<int16_t> x, y, z;

private:
    MemField<int16_t> spare_;
    MemField<uint32_t> low_, high_; // the two dwords the exe copies / stores the vector as
};

} // namespace od
