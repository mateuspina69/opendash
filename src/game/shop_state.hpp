// The state shop.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::shop_state {

// The exe globals shop.cpp keeps (their exe addresses in the comments)
struct ShopGlobals {
    StateArray<uint16_t, 0x10> ownedParts{}; // 0xa5aa40: the sell list's built copy of them, then the parts worn
    StateArray<uint16_t, 0x80> itemList{};   // 0xa5a940: the u16 item ids of the list, kShopBack last
};
inline ShopGlobals& shopGlobals() {
    static ShopGlobals s;
    return s;
}

} // namespace od::port::shop_state
