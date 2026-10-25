// The state dev_room.cpp keeps: the menu's block (Roll's and Nino's copy) and the list below it.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/list_cursor.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::dev_room_state {


// The menu's block (0x38 bytes; Roll's at 0xa5a0d0, Nino's copy at 0xa5a1c8) and the list 0x80 below it
struct DevRoomMenu {
    StateArray<int16_t, 0x40> listEntries{}; // -0x80: the list's item ids (kBackItem last)
    uint8_t page = 0;            // +0x00: DevPage
    uint8_t state = 0;           // +0x01: the page's state
    uint8_t step = 0;            // +0x02: the state's step ...
    uint8_t subStep = 0;         // +0x03: ... and its sub-step
    ListCursor list;             // +0x04 .. +0x09
    uint8_t unread0a = 0;        // +0x0a, +0x0b: cleared with the cursor's animations (no reader traced)
    uint8_t unread0b = 0;
    StateArray<int8_t, 5> barLength{};   // +0x0c: the stat bars' lengths, new / old, and their slide
    StateArray<int8_t, 5> barPrevious{}; // +0x11
    uint16_t barSlide = 0;       // +0x16
    int32_t price = 0;           // +0x18
    int16_t weaponItem = 0;      // +0x1c: the weapon item under the cursor
    int16_t itemUnder = 0;       // +0x1e: the development item under the cursor
    int8_t chosenWeapon = 0;     // +0x20: the special weapon chosen (the player's at the start)
    int8_t modelWeapon = 0;      // +0x21: the weapon whose model is shown
    int8_t stat = 0;             // +0x22: the stat chosen (the bars preview it)
    uint8_t recipeKind = 0;      // +0x23
    int16_t recipeOther = 0;     // +0x24: the recipe's other item (-1: none)
    int16_t recipeResult = 0;    // +0x26
    int16_t recipeMessage = 0;   // +0x28
    int16_t openingMessage = 0;  // +0x2a: Roll's opening message (< 0: none)
    ShortVec3 fieldPoint;        // +0x2c: the player's field position and yaw, put back at the end
    int16_t fieldYaw = 0;        // +0x32
    ActorRef<AnimatedActor> modelActor; // +0x34

    // the page p at state 0, its step and sub-step cleared (the exe's dword store)
    template <class P> void enterPage(P p) {
        page = uint8_t(p);
        state = 0;
        step = 0;
        subStep = 0;
    }
    // the step s, its sub-step cleared (the exe's word store at +2)
    void enterStep(uint8_t s) {
        step = s;
        subStep = 0;
    }
    // the cursor's animations and the two bytes after them cleared (the exe's dword store at +8)
    void clearCursorAnimations() {
        list.rowStep = 0;
        list.scrollStep = 0;
        unread0a = 0;
        unread0b = 0;
    }
    // exe 0x40dc40(block, block + 0x34): the block cleared one byte at a time (the list below it and the model actor
    // kept)
    void clearBlock() {
        const StateArray<int16_t, 0x40> entries = listEntries;
        const ActorRef<AnimatedActor> model = modelActor;
        *this = DevRoomMenu{};
        listEntries = entries;
        modelActor = model;
    }
};
struct DevRoomMenus {
    DevRoomMenu roll; // 0xa5a0d0
    DevRoomMenu nino; // 0xa5a1c8
};
inline DevRoomMenus& devRoomMenus() {
    static DevRoomMenus s;
    return s;
}

} // namespace od::port::dev_room_state
