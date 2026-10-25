// Named fields over native members of another width or signedness: what a class names in the fields the slots share
// (the four spawn parameters a class reads as one word, the signed view of an unsigned byte, the half of a word). The
// value reads / writes the member(s) with the exe's little-endian layout; the arithmetic of the compound operators wraps
// at T's width (as the game state's fields did).
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

// The arithmetic every alias shares (Derived provides get() / set(T))
template <class Derived, class T> class AliasOps {
    using Bits = std::make_unsigned_t<T>;
    static uint32_t bits(T v) { return uint32_t(Bits(v)); }
    Derived& self() { return static_cast<Derived&>(*this); }
    const Derived& self() const { return static_cast<const Derived&>(*this); }

public:
    operator T() const { return self().get(); }
    template <class U> Derived& operator+=(U v) { return self().assign(T(bits(self().get()) + uint32_t(v))); }
    template <class U> Derived& operator-=(U v) { return self().assign(T(bits(self().get()) - uint32_t(v))); }
    template <class U> Derived& operator|=(U v) { return self().assign(T(bits(self().get()) | uint32_t(v))); }
    template <class U> Derived& operator&=(U v) { return self().assign(T(bits(self().get()) & uint32_t(v))); }
    template <class U> Derived& operator^=(U v) { return self().assign(T(bits(self().get()) ^ uint32_t(v))); }
    template <class U> Derived& operator<<=(U v) { return self().assign(T(bits(self().get()) << uint32_t(v))); }
    template <class U> Derived& operator>>=(U v) { return self().assign(T(self().get() >> uint32_t(v))); }
    Derived& operator++() { return self().assign(T(bits(self().get()) + 1u)); }
    Derived& operator--() { return self().assign(T(bits(self().get()) - 1u)); }
    T operator++(int) {
        const T old = self().get();
        self().assign(T(bits(old) + 1u));
        return old;
    }
    T operator--(int) {
        const T old = self().get();
        self().assign(T(bits(old) - 1u));
        return old;
    }
};

// T over a member M of the same width (another signedness)
template <class T, class M> class FieldAlias : public AliasOps<FieldAlias<T, M>, T> {
    static_assert(sizeof(T) == sizeof(M) && std::is_integral_v<T> && std::is_integral_v<M>);

public:
    explicit FieldAlias(M& m) : m_(m) {}
    FieldAlias(const FieldAlias&) = default;
    FieldAlias& operator=(const FieldAlias& o) { return assign(o.get()); }
    FieldAlias& operator=(T v) { return assign(v); }
    T get() const { return T(m_); }
    void set(T v) { m_ = M(v); }
    FieldAlias& assign(T v) {
        set(v);
        return *this;
    }

private:
    M& m_;
};

// T inside a wider member M, at byte `offset` of its little-endian value
template <class T, class M> class PartField : public AliasOps<PartField<T, M>, T> {
    static_assert(sizeof(T) < sizeof(M) && std::is_integral_v<T> && std::is_integral_v<M>);
    using MU = std::make_unsigned_t<M>;
    static constexpr MU kMask = MU((uint64_t(1) << (8 * sizeof(T))) - 1);

public:
    PartField(M& m, unsigned offset) : m_(m), shift_(8 * offset) {}
    PartField(const PartField&) = default;
    PartField& operator=(const PartField& o) { return assign(o.get()); }
    PartField& operator=(T v) { return assign(v); }
    T get() const { return T(std::make_unsigned_t<T>(MU(m_) >> shift_)); }
    void set(T v) {
        const MU w = MU((MU(m_) & MU(~(kMask << shift_))) | (MU(std::make_unsigned_t<T>(v)) & kMask) << shift_);
        m_ = M(w);
    }
    PartField& assign(T v) {
        set(v);
        return *this;
    }

private:
    M& m_;
    unsigned shift_;
};

// T over consecutive bytes of an array of integers E (little-endian, the exe's layout): a slot's spawn parameters read
// as a word, two lanes of a short array read as one dword; from byte `first`
template <class T, class E, size_t N> class ArrayField : public AliasOps<ArrayField<T, E, N>, T> {
    static_assert(std::is_integral_v<T> && std::is_integral_v<E>);
    using EU = std::make_unsigned_t<E>;

public:
    ArrayField(StateArray<E, N>& a, size_t first) : a_(a), first_(first) {}
    ArrayField(const ArrayField&) = default;
    ArrayField& operator=(const ArrayField& o) { return assign(o.get()); }
    ArrayField& operator=(T v) { return assign(v); }
    T get() const {
        uint64_t v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v |= uint64_t(byteAt(first_ + i)) << (8 * i);
        return T(std::make_unsigned_t<T>(v));
    }
    void set(T v) {
        const uint64_t u = uint64_t(std::make_unsigned_t<T>(v));
        for (size_t i = 0; i < sizeof(T); ++i) setByteAt(first_ + i, uint8_t(u >> (8 * i)));
    }
    ArrayField& assign(T v) {
        set(v);
        return *this;
    }

private:
    uint8_t byteAt(size_t b) const { return uint8_t(EU(a_[b / sizeof(E)]) >> (8 * (b % sizeof(E)))); }
    void setByteAt(size_t b, uint8_t v) {
        const unsigned shift = unsigned(8 * (b % sizeof(E)));
        const EU m = EU(EU(0xff) << shift);
        a_[b / sizeof(E)] = E(EU((EU(a_[b / sizeof(E)]) & EU(~m)) | EU(EU(v) << shift)));
    }
    StateArray<E, N>& a_;
    size_t first_;
};
// T over the bytes of a byte array (the slots' spawn parameters)
template <class T, size_t N> using BytesField = ArrayField<T, uint8_t, N>;

// Count fields T over consecutive bytes of an array of integers E, from byte `first` (a class's byte arrays in the
// effect slot's trail shorts)
template <class T, size_t Count, class E, size_t N> class ArrayFields {
public:
    ArrayFields(StateArray<E, N>& a, size_t first) : a_(a), first_(first) {}
    ArrayField<T, E, N> operator[](size_t i) const {
        if (i >= Count) stateIndexError(i, Count);
        return {a_, first_ + i * sizeof(T)};
    }
    static constexpr size_t size() { return Count; }

private:
    StateArray<E, N>& a_;
    size_t first_;
};

// Three shorts of a state named as a point (x, y, z): references to them
struct ShortTriple {
    int16_t& x;
    int16_t& y;
    int16_t& z;
    ShortTriple& operator=(const ShortTriple& o) {
        x = o.x;
        y = o.y;
        z = o.z;
        return *this;
    }
};

// T over separate integer members of any widths (little-endian: the first is the lowest), their widths adding up to
// T's (a word a class keeps over two shorts of its hit box's contact)
template <class T> class PiecesField : public AliasOps<PiecesField<T>, T> {
    static_assert(std::is_integral_v<T>);
    struct Piece {
        void* p;
        unsigned bytes;
    };

public:
    template <class... M> explicit PiecesField(M&... members) : n_(sizeof...(M)), pieces_{Piece{&members, sizeof(M)}...} {
        static_assert((sizeof(M) + ...) == sizeof(T), "PiecesField: the members' widths add up to T's");
        static_assert((std::is_integral_v<M> && ...));
    }
    PiecesField(const PiecesField&) = default;
    PiecesField& operator=(const PiecesField& o) { return assign(o.get()); }
    PiecesField& operator=(T v) { return assign(v); }
    T get() const {
        uint64_t v = 0;
        unsigned shift = 0;
        for (size_t i = 0; i < n_; ++i) {
            v |= read(pieces_[i]) << shift;
            shift += 8 * pieces_[i].bytes;
        }
        return T(std::make_unsigned_t<T>(v));
    }
    void set(T v) {
        uint64_t u = uint64_t(std::make_unsigned_t<T>(v));
        for (size_t i = 0; i < n_; ++i) {
            write(pieces_[i], u);
            u >>= 8 * pieces_[i].bytes;
        }
    }
    PiecesField& assign(T v) {
        set(v);
        return *this;
    }

private:
    static uint64_t read(const Piece& q) {
        switch (q.bytes) {
        case 1: return *static_cast<const uint8_t*>(q.p);
        case 2: return *static_cast<const uint16_t*>(q.p);
        default: return *static_cast<const uint32_t*>(q.p);
        }
    }
    static void write(const Piece& q, uint64_t u) {
        switch (q.bytes) {
        case 1: *static_cast<uint8_t*>(q.p) = uint8_t(u); break;
        case 2: *static_cast<uint16_t*>(q.p) = uint16_t(u); break;
        default: *static_cast<uint32_t*>(q.p) = uint32_t(u); break;
        }
    }
    size_t n_;
    Piece pieces_[sizeof(T)];
};

// T over separate byte members (little-endian: the first is the lowest; the header's step and step counter read as
// one word)
template <class T> class BytesOfField : public AliasOps<BytesOfField<T>, T> {
    static_assert(std::is_integral_v<T>);

public:
    template <class... B> explicit BytesOfField(B&... bytes) : b_{&bytes...} {
        static_assert(sizeof...(B) == sizeof(T), "BytesOfField: one byte per byte of T");
    }
    BytesOfField(const BytesOfField&) = default;
    BytesOfField& operator=(const BytesOfField& o) { return assign(o.get()); }
    BytesOfField& operator=(T v) { return assign(v); }
    T get() const {
        uint64_t v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v |= uint64_t(*b_[i]) << (8 * i);
        return T(std::make_unsigned_t<T>(v));
    }
    void set(T v) {
        const uint64_t u = uint64_t(std::make_unsigned_t<T>(v));
        for (size_t i = 0; i < sizeof(T); ++i) *b_[i] = uint8_t(u >> (8 * i));
    }
    BytesOfField& assign(T v) {
        set(v);
        return *this;
    }

private:
    uint8_t* b_[sizeof(T)];
};

// A 16-bit lane over native members (an area the exe shares among users that name its bytes their own way, as the
// geometry scratch's, game/geometry_scratch): a member of 16 bits, two byte members, or half of a 32-bit member
class Lane16 : public AliasOps<Lane16, int16_t> {
public:
    explicit Lane16(int16_t& m) : p_(&m), kind_(Kind::Short) {}
    explicit Lane16(uint16_t& m) : p_(&m), kind_(Kind::Short) {}
    Lane16(uint8_t& low, uint8_t& high) : p_(&low), p2_(&high), kind_(Kind::Bytes) {}
    Lane16(uint32_t& word, unsigned half) : p_(&word), kind_(half ? Kind::HighHalf : Kind::LowHalf) {}
    Lane16(int32_t& word, unsigned half) : Lane16(reinterpret_cast<uint32_t&>(word), half) {}
    Lane16(const Lane16&) = default;
    Lane16& operator=(const Lane16& o) { return assign(o.get()); }
    Lane16& operator=(int16_t v) { return assign(v); }
    int16_t get() const {
        switch (kind_) {
        case Kind::Short: return *static_cast<const int16_t*>(p_);
        case Kind::Bytes:
            return int16_t(uint16_t(*static_cast<const uint8_t*>(p_) | *static_cast<const uint8_t*>(p2_) << 8));
        case Kind::LowHalf: return int16_t(uint16_t(*static_cast<const uint32_t*>(p_)));
        case Kind::HighHalf: break;
        }
        return int16_t(uint16_t(*static_cast<const uint32_t*>(p_) >> 16));
    }
    void set(int16_t v) {
        const uint16_t u = uint16_t(v);
        switch (kind_) {
        case Kind::Short: *static_cast<int16_t*>(p_) = v; return;
        case Kind::Bytes:
            *static_cast<uint8_t*>(p_) = uint8_t(u);
            *static_cast<uint8_t*>(p2_) = uint8_t(u >> 8);
            return;
        case Kind::LowHalf: {
            uint32_t& w = *static_cast<uint32_t*>(p_);
            w = (w & 0xffff0000u) | u;
            return;
        }
        case Kind::HighHalf: break;
        }
        uint32_t& w = *static_cast<uint32_t*>(p_);
        w = (w & 0x0000ffffu) | uint32_t(u) << 16;
    }
    Lane16& assign(int16_t v) {
        set(v);
        return *this;
    }

private:
    enum class Kind : uint8_t { Short, Bytes, LowHalf, HighHalf };
    void* p_;
    void* p2_ = nullptr;
    Kind kind_;
};

// A byte of a lane (its low / high half)
class Lane8 : public AliasOps<Lane8, uint8_t> {
public:
    Lane8(Lane16 lane, unsigned half) : lane_(lane), shift_(half ? 8u : 0u) {}
    Lane8(const Lane8&) = default;
    Lane8& operator=(const Lane8& o) { return assign(o.get()); }
    Lane8& operator=(uint8_t v) { return assign(v); }
    uint8_t get() const { return uint8_t(uint16_t(lane_.get()) >> shift_); }
    void set(uint8_t v) {
        const uint16_t w = uint16_t(lane_.get());
        lane_ = int16_t(uint16_t((w & ~(0xffu << shift_)) | uint32_t(v) << shift_));
    }
    Lane8& assign(uint8_t v) {
        set(v);
        return *this;
    }

private:
    Lane16 lane_;
    unsigned shift_;
};

// A 32-bit word over two lanes (the low one first)
class Lane32 : public AliasOps<Lane32, int32_t> {
public:
    Lane32(Lane16 low, Lane16 high) : low_(low), high_(high) {}
    explicit Lane32(int32_t& w) : low_(w, 0), high_(w, 1) {}
    explicit Lane32(uint32_t& w) : low_(w, 0), high_(w, 1) {}
    Lane32(const Lane32&) = default;
    Lane32& operator=(const Lane32& o) { return assign(o.get()); }
    Lane32& operator=(int32_t v) { return assign(v); }
    int32_t get() const { return int32_t(uint32_t(uint16_t(low_.get())) | uint32_t(uint16_t(high_.get())) << 16); }
    void set(int32_t v) {
        low_ = int16_t(uint16_t(uint32_t(v)));
        high_ = int16_t(uint16_t(uint32_t(v) >> 16));
    }
    Lane32& assign(int32_t v) {
        set(v);
        return *this;
    }
    Lane16 low() const { return low_; }
    Lane16 high() const { return high_; }

private:
    Lane16 low_, high_;
};

// Three lanes as a short point (x / y / z)
struct LaneVec3 {
    Lane16 x, y, z;
    LaneVec3& operator=(const ShortVec3& v) {
        x = v.x;
        y = v.y;
        z = v.z;
        return *this;
    }
    LaneVec3& operator=(const LaneVec3& o) { return *this = o.vec3(); }
    ShortVec3 vec3() const { return {x.get(), y.get(), z.get()}; }
    operator ShortVec3() const { return vec3(); }
};

// Four lanes as a short point (x / y / z / the spare lane)
struct LaneVec4 {
    Lane16 x, y, z, spare;
    LaneVec4& operator=(const ShortVec4& v) {
        x = v.x;
        y = v.y;
        z = v.z;
        spare = v.spare;
        return *this;
    }
    LaneVec4& operator=(const ShortVec3& v) {
        x = v.x;
        y = v.y;
        z = v.z;
        return *this;
    }
    LaneVec4& operator=(const LaneVec4& o) { return *this = o.vec4(); }
    ShortVec4 vec4() const { return ShortVec4(x.get(), y.get(), z.get(), spare.get()); }
    ShortVec3 vec3() const { return {x.get(), y.get(), z.get()}; }
    operator ShortVec4() const { return vec4(); }
    void assign(int16_t vx, int16_t vy, int16_t vz) {
        x = vx;
        y = vy;
        z = vz;
        spare = 0;
    }
    uint32_t xyWord() const { return uint32_t(uint16_t(x.get())) | uint32_t(uint16_t(y.get())) << 16; }
    uint32_t zSpareWord() const { return uint32_t(uint16_t(z.get())) | uint32_t(uint16_t(spare.get())) << 16; }
    void setWords(uint32_t xy, uint32_t zSpare) {
        x = int16_t(uint16_t(xy));
        y = int16_t(uint16_t(xy >> 16));
        z = int16_t(uint16_t(zSpare));
        spare = int16_t(uint16_t(zSpare >> 16));
    }
    void clearWords() { setWords(0, 0); }
};

} // namespace od::port
