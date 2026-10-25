// The save / load menus of dash2.exe, each a table of handlers over a 7-byte menu block (SaveMenuBlock): the save menu
// the talking NPC runs (exe 0x548f60, Data's "save"), the clear-data save menu of the game clear (exe 0x548f20's step,
// script 0x76) and the title's load menu (exe 0x548980, "continue"). They use the message windows 1..4 (game/message)
// on the exe banks of the save texts, the slot bits and the slot summaries (game/save_slots) and the save image
// (game/save_image). Bit-exact against tests/data/title_continue.bin (test_title) and act_ST0B_save (test_actors).
#pragma once

#include <cstdint>
#include <functional>
#include <span>


namespace od::port {

struct FadeTask;

// +0 of a menu block: the handler
enum class SaveMenuState : uint8_t {
    Open = 0,        // the menu opened (the story menu's flag set)
    Close = 1,       // the menu closed (+6: kMenuClosed for the load / clear-data menus)
    CardMessage = 2, // a memory-card message (MemoryCardStep)
    SlotList = 3,    // the slot list, the overwrite question
    Transfer = 4,    // the image written (the save menus) / read (the load menu)
};
// +1 of CardMessage: the message (+2 its own step: CardMessageStep)
enum class CardMessage : uint8_t { Cancelled = 1, Retry = 2 };
enum class CardMessageStep : uint8_t { Prepare = 0, Open = 1, Wait = 2 };
// +1 of SlotList
enum class SlotListStep : uint8_t {
    Prepare = 0,   // the windows closed (the load menu: the slots' headers read)
    Details = 1,   // window 3: the details of the slot under the choice
    Choice = 2,    // window 2: the choice of the slots
    Choose = 3,    // until it answers: a slot, or cancelled
    CheckSlot = 4, // a slot with a save asks first; an empty one goes on (the save menu) / says so (the load menu)
    Ask = 5,       // the question (overwrite / load it)
    Answer = 6,
    EmptySlot = 10, // the save menu: write; the load menu: "no save in this slot"
    EmptyWait = 11,
};
// +1 of Transfer
enum class SaveWriteStep : uint8_t { Prepare = 0, Build = 1, Write = 2, Summary = 3, Saved = 4, Wait = 5 };
enum class LoadReadStep : uint8_t { Prepare = 0, Read = 1, Check = 2, Loaded = 3, Wait = 4 };
// +1 of the load menu's Close
enum class LoadCloseStep : uint8_t { WaitFade = 0, Fade = 1, WaitFadeOut = 2, Closed = 3 };
// +6: the menu's flags
constexpr uint8_t kMenuCancelled = 0x20;
constexpr uint8_t kMenuLoaded = 0x40;
constexpr uint8_t kMenuClosed = 0x80;

// A menu's block (a talking NPC's state keeps its save menu's; loadMenuBlock() for the load / clear-data menus)
struct SaveMenuBlock {
    uint8_t state = 0;       // SaveMenuState
    uint8_t step = 0;        // the state's step
    uint8_t messageStep = 0; // CardMessageStep
    uint8_t unread3 = 0;     // cleared with the steps (its reader is not traced)
    int16_t slot = 0;        // the slot chosen (-1: none)
    uint8_t flags = 0;       // kMenu*

    SaveMenuState currentState() const { return SaveMenuState(state); }
    // the state s, its steps cleared (the exe's dword store at +0)
    void enter(SaveMenuState s) {
        state = uint8_t(s);
        step = 0;
        messageStep = 0;
        unread3 = 0;
    }
};
// The load / clear-data menus' block and the byte after it
struct LoadMenuBlock : SaveMenuBlock {
    uint8_t after = 0;
    // the eight bytes cleared one at a time (exe 0x5488f0: the title opens the load menu)
    void clearBytes() {
        static_cast<SaveMenuBlock&>(*this) = SaveMenuBlock{};
        after = 0;
    }
};
inline LoadMenuBlock& loadMenuBlock() {
    static LoadMenuBlock s;
    return s;
}

// exe 0x548f60(block): the save menu the talking NPC runs while its save flag is set, one tick
void saveMenuTick(SaveMenuBlock& block);
// exe 0x548f20's menu (script 0x76, the game clear): the clear data written into the slot's image kClearDataImage; true
// once closed (kMenuClosed: the windows closed, the story menu's flag cleared)
bool clearSaveMenuTick();
// exe 0x5493b0(slot, image, 0xc00): the host writes the image (OpenDash: its own saves dir, never the game's); -1 =
// failed (the menu retries next tick). Without a hook the menus throw notPorted.
void setSaveWriteHook(std::function<int(uint8_t slot, std::span<const uint8_t> image)> hook);
// The load menu of the title's "continue" (exe 0x548980; its background is the title's draw). When the menu closed
// (kMenuClosed): the windows closed, the story menu's flag cleared; returns 1 = loaded (kSaveBuf holds the save), 2 =
// cancelled, 0 = still open. readSave = exe 0x548de0(slot, buffer, size) (-1 = failed).
int loadMenuTick(FadeTask& fade, const std::function<int(uint32_t, uint8_t*, uint32_t)>& readSave);

} // namespace od::port
