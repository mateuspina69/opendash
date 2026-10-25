// Helpers shared by the actor/player/class layouts. Never linked into the game.
#pragma once
#include <type_traits>
#include "translator/class_layouts.hpp"
#include "game/actor_pools.hpp"
#include "game/save_menus.hpp"

namespace od::test {

template <class T>
void offsetActorRef(LayoutVisitor& v, uint32_t a, od::port::ActorRef<T>& ref, uint32_t offset) {
    uint32_t word = ref ? LayoutVisitor::slotAddress(ref.get()) + offset : 0;
    v.bytes(a, &word, 4);
    if (v.loading()) ref = LayoutVisitor::slotOf<T>(word ? word - offset : 0);
}

inline void layoutValue(LayoutVisitor& v, uint32_t a, od::port::InteractionTarget& target) {
    uint32_t word = target.word();
    v.bytes(a, &word, 4);
    if (v.loading()) {
        target.mode = uint8_t(word >> 24);
        target.actor = LayoutVisitor::slotOf<od::port::ActorHeader>(word & od::port::kInteractActorMask);
    }
}
// the save menu's block (game/save_menus) held by a class (the talking NPC's): its seven bytes
inline void layoutValue(LayoutVisitor& v, uint32_t a, od::port::SaveMenuBlock& m) {
    v.field(a + 0, m.state);
    v.field(a + 1, m.step);
    v.field(a + 2, m.messageStep);
    v.field(a + 3, m.unread3);
    v.field(a + 4, m.slot);
    v.field(a + 6, m.flags);
}
// a named field over native members of another width (game/field_alias): its value in the exe's bytes
template <class A>
concept AliasField = !std::is_integral_v<std::remove_cvref_t<A>> && requires(std::remove_cvref_t<A>& x) {
    x.get();
    x.set(x.get());
};
template <AliasField A> void layoutValue(LayoutVisitor& v, uint32_t a, A&& alias) {
    auto value = alias.get();
    v.bytes(a, &value, sizeof value);
    if (v.loading()) alias.set(value);
}
template <class T> requires(!std::is_base_of_v<od::MemView, std::remove_cvref_t<T>> && !AliasField<T>)
void layoutValue(LayoutVisitor& v, uint32_t a, T&& value) { v.field(a, value); }
template <class T> requires std::is_base_of_v<od::MemView, std::remove_cvref_t<T>>
void layoutValue(LayoutVisitor&, uint32_t, T&&) {
    // This record still lives in the image on this branch.
}
template <class T, size_t N>
void layoutValue(LayoutVisitor& v, uint32_t a, od::port::StateArray<T, N>& values) {
    for (size_t i = 0; i < N; ++i) layoutValue(v, a + uint32_t(i * sizeof(T)), values[i]);
}
template <class T> void layoutValue(LayoutVisitor& v, uint32_t a, od::port::BoneBlockArea<T>& area) {
    uint32_t address = area.address();
    v.field(a, address);
    if (v.loading()) area.pointAt(address);
    if (area) layoutValue(v, area.address(), *area);
}

void classLayout(LayoutVisitor& v, uint32_t address, od::port::ActorClassState& state);
void actorPoolsLayout(LayoutVisitor& v);
void actorClassesLayout(LayoutVisitor& v, const od::port::ActorHeader* except = nullptr);
void prepareActorLoad(const GameMem& image);
void prepareActorStore(GameMem& image);
// the held class state of the slot owns none of its bytes (a previous occupant's since the slot's allocation)
bool heldStateRetired(const od::port::ActorHeader& slot);
// one pool slot's shared layout (its pool's)
void slotLayout(LayoutVisitor& v, od::port::ActorHeader& slot);
void finishActorLoad();
void refreshNativeFences(GameMem& image);
// Generic fields overlapped by a held class are not interpreted on a load.
bool classOwns(uint32_t address, size_t bytes);
bool resolveNativePointer(uint32_t field, uint32_t word, const std::type_info& type, void*& pointer);
uint32_t nativePointerWord(uint32_t field, const void* pointer, const std::type_info& type);
od::port::HitBox* nativeContactBox(od::port::HitBoxHead* head);

} // namespace od::test
