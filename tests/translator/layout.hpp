// The native state's layout in dash2.exe's memory, for the test-only translator (state_translator.hpp): each native
// system lists its fields with the exe address the original keeps them at, once, in its own layout function
// (<system>_layout.cpp). The translator runs the same lists in both directions (the image into the native state, the
// native state into the image) and fences the bytes they own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <typeinfo>

#include "engine/data_source.hpp"
#include "engine/routine_ids.hpp"
#include "game/actor_state.hpp"
#include "translator/game_memory.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/loaded_data.hpp"
#include "game/state_array.hpp"
#include "game/transition_state.hpp"
#include "game/vectors.hpp"

namespace od::test {

// One pass over the native fields and their exe addresses
class LayoutVisitor {
public:
    virtual ~LayoutVisitor() = default;
    // n bytes at the exe address `address` held by the native bytes at p (little-endian, the exe's own order)
    virtual void bytes(uint32_t address, void* p, size_t n) = 0;
    virtual bool loading() const { return false; }
    virtual bool storing() const { return false; }
    virtual bool interprets(uint32_t, size_t) const { return true; }
    virtual bool strictSlots() const { return true; }
    // Register named native objects before resolving pointers. These hooks are test-only.
    virtual void object(uint32_t, void*, const std::type_info&) {}
    virtual void pointer(uint32_t a, void*&, const std::type_info&) {
        uint32_t word = 0;
        bytes(a, &word, 4);
    }

    template <class T> void bind(uint32_t address, T& value) {
        object(address, &value, typeid(T));
    }
    template <class T> void field(uint32_t address, T*& value) {
        if (!interprets(address, 4)) return;
        void* p = const_cast<std::remove_const_t<T>*>(value);
        pointer(address, p, typeid(std::remove_const_t<T>));
        value = static_cast<T*>(p);
    }
    template <class D, class T> void alias(uint32_t address, od::port::AliasOps<D, T>& value) {
        D& d = static_cast<D&>(value);
        T bits = d.get();
        bytes(address, &bits, sizeof bits);
        if (loading()) d.set(bits);
    }
    template <class T, class M> void field(uint32_t a, od::port::FieldAlias<T, M> v) { alias(a, v); }
    template <class T, class M> void field(uint32_t a, od::port::PartField<T, M> v) { alias(a, v); }
    template <class T, class M, size_t N> void field(uint32_t a, od::port::ArrayField<T, M, N> v) { alias(a, v); }
    template <class T> void field(uint32_t a, od::port::PiecesField<T> v) { alias(a, v); }
    template <class T> void field(uint32_t a, od::port::BytesOfField<T> v) { alias(a, v); }
    void field(uint32_t a, od::port::ShortTriple v) {
        field(a, v.x); field(a + 2, v.y); field(a + 4, v.z);
    }
    void field(uint32_t a, od::port::BoxExtents& b) {
        bind(a, b);
        field(a, b.minX); field(a + 2, b.maxX);
        field(a + 4, b.minY); field(a + 6, b.maxY);
        field(a + 8, b.minZ); field(a + 10, b.maxZ);
    }
    void field(uint32_t address, od::port::CollisionBoxRef& box);

    // a scalar field (an integer or an enum)
    template <class T>
        requires(std::is_integral_v<T> || std::is_enum_v<T>)
    void field(uint32_t address, T& v) {
        bind(address, v);
        bytes(address, &v, sizeof v);
    }
    // an array of scalars, one after the other (the native array's bytes are the exe's: one run)
    template <class T, size_t N>
        requires(std::is_integral_v<T> || std::is_enum_v<T>)
    void field(uint32_t address, od::port::StateArray<T, N>& a) {
        bytes(address, a.data(), N * sizeof(T));
    }
    // a pointer into the image (the exe address it holds)
    template <class V> void field(uint32_t address, ImagePtr<V>& p) {
        if (!interprets(address, 4)) return;
        uint32_t v = p.value();
        bytes(address, &v, 4);
        p.setValue(v);
    }
    // a pointer to a record of the data (the value the exe stores)
    template <class R> void field(uint32_t address, od::port::DataPtr<R>& p) {
        if (!interprets(address, 4)) return;
        uint32_t v = p.value();
        bytes(address, &v, 4);
        p.setValue(v);
    }
    // a routine the native state names: the exe stores its function pointer (the data source's encoding)
    void field(uint32_t address, RoutineId& id) {
        uint32_t v = id == RoutineId::None ? 0 : routineStateValue(id);
        bytes(address, &v, 4);
        id = routineOf(v);
    }
    // a native short quad: x / y / z / spare
    void field(uint32_t address, od::port::ShortVec4& p) {
        bind(address, p);
        bind(address, static_cast<od::port::ShortVec3&>(p));
        field(address, p.x);
        field(address + 2, p.y);
        field(address + 4, p.z);
        field(address + 6, p.spare);
    }
    void field(uint32_t address, od::port::ShortVec3& p) {
        bind(address, p);
        field(address, p.x);
        field(address + 2, p.y);
        field(address + 4, p.z);
    }
    // a room place: x / y / z / yaw
    void field(uint32_t address, od::port::RoomPlace& p) {
        field(address, p.x);
        field(address + 2, p.y);
        field(address + 4, p.z);
        field(address + 6, p.yaw);
    }
    // a 16.16 value / a 16.16 vector
    void field(uint32_t address, od::port::Fixed16& f) {
        field(address, f.frac);
        field(address + 2, f.whole);
    }
    void field(uint32_t address, od::port::Fixed16Vec3& p) {
        bind(address, p); // a position the state points at (the event's speaker)
        field(address, p.x);
        field(address + 4, p.y);
        field(address + 8, p.z);
    }
    // a 3x3 matrix's nine shorts
    void field(uint32_t address, od::port::Matrix3& m) { bytes(address, m.e, sizeof m.e); }
    // a result box (its head / with the contact output)
    void field(uint32_t address, od::port::HitBoxHead& h) {
        bind(address, h);
        field(address, h.result);
        field(address + 4, h.previousX);
        field(address + 6, h.previousZ);
    }
    void field(uint32_t address, od::port::HitBox& h) {
        bind(address, h);
        field(address, static_cast<od::port::HitBoxHead&>(h));
        field(address + 8, h.contact);
        field(address + 0xe, h.contactKind);
    }
    // an animation key's copy
    void field(uint32_t address, od::port::AnimKey& k) {
        field(address, k.frame);
        field(address + 1, k.ticks);
        field(address + 2, k.events);
        field(address + 3, k.flags);
    }
    // a part of an animated model's part table
    void field(uint32_t address, od::port::PartEntry& p) {
        field(address, p.pointSlot);
        field(address + 1, p.parentSlot);
        field(address + 2, p.boneSlot);
        field(address + 3, p.flags);
    }
    // a keyframe track (8 bytes) / a pair of them
    void field(uint32_t address, od::port::KeyframeTrack& t) {
        field(address, t.table);
        field(address + 4, t.sequence);
        field(address + 5, t.key);
        field(address + 6, t.tick);
        field(address + 7, t.stopped);
    }
    void field(uint32_t address, od::port::KeyframeTracks& t) {
        field(address, t.first);
        field(address + 8, t.second);
    }
    // a reference to an actor: the exe stores its slot's address (0: none)
    template <class V> void field(uint32_t address, od::port::ActorRef<V>& r) {
        if (!interprets(address, 4)) return;
        uint32_t v = r.get() ? slotAddress(r.get()) : r.foreignWord();
        bytes(address, &v, 4);
        if (loading()) {
            try { r = slotOf<V>(v); }
            catch (const std::runtime_error&) {
                if (strictSlots()) throw;
                r = od::port::ActorRef<V>::foreign(v); // a class not created yet may overlay this generic word
            }
        }
    }
    // an array of n records, `stride` bytes apart, each laid out by field()
    template <class T, size_t N> void records(uint32_t address, od::port::StateArray<T, N>& a, uint32_t stride) {
        for (size_t i = 0; i < N; ++i) field(address + uint32_t(i) * stride, a[i]);
    }

    // the exe address of a slot (0 for none) / the slot at an exe address, of the kind V (0: none; an address that is
    // no slot of that kind: an error naming it)
    static uint32_t slotAddress(const od::port::ActorHeader* slot) { return slot ? slot->at : 0; }
    template <class V> static V* slotOf(uint32_t address) {
        if (address == 0) return nullptr;
        od::port::ActorHeader* h = slotAt(address);
        V* v = dynamic_cast<V*>(h);
        if (!v) badSlot(address);
        return v;
    }

private:
    static od::port::ActorHeader* slotAt(uint32_t address);
    [[noreturn]] static void badSlot(uint32_t address);
    // the routine an exe function pointer names (0: None; a pointer with no routine: an error naming it)
    static RoutineId routineOf(uint32_t pointer);
};

// the systems' layouts (one per native system)
void gameGlobalsLayout(LayoutVisitor& v);
void stageStateLayout(LayoutVisitor& v);
void cameraLayout(LayoutVisitor& v);
void audioLayout(LayoutVisitor& v);
void displayLayout(LayoutVisitor& v);
void worldLayout(LayoutVisitor& v);
void encounterLayout(LayoutVisitor& v);
void hudLayout(LayoutVisitor& v);
void inputLayout(LayoutVisitor& v);
void messageLayout(LayoutVisitor& v);
void filesLayout(LayoutVisitor& v);
void actorSystemLayout(LayoutVisitor& v);
void playerGlobalsLayout(LayoutVisitor& v);
void contentGlobalsLayout(LayoutVisitor& v);
void geometryLayout(LayoutVisitor& v);
void stateGlobalsLayout(LayoutVisitor& v);
void boneBlocksLayout(LayoutVisitor& v);
void patchedDataLayout(LayoutVisitor& v);
void textLayout(LayoutVisitor& v);
void weatherDrawLayout(LayoutVisitor& v);

} // namespace od::test
