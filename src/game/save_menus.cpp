// The save / load menus (see save_menus.hpp), one function per exe function; every write keeps the exe's width.
#include "game/save_menus.hpp"

#include <vector>

#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/port_gap.hpp"
#include "game/save_image.hpp"
#include "game/save_slots.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

// the areas of the menus' port-gap errors
constexpr const char* kSaveMenuPort = "save menu";
constexpr const char* kClearSaveMenuPort = "clear save menu";
constexpr const char* kLoadMenuPort = "load menu";

// their messages
constexpr int16_t kCardCancelledMessage = 0xb, kCardRetryMessage = 0xc, kNoSaveInSlotMessage = 0xd;
constexpr int16_t kOverwriteMessage = 0x14, kSavedMessage = 0x15, kLoadThisMessage = 0x1b, kLoadedMessage = 0x1c;
constexpr int16_t kSlotDetailsMessage = 3, kSlotChoiceMessage = 5, kMenuTitleMessage = 6;
// the windows: the title, the choice / the questions, the slot's details, the fourth one they close
constexpr int8_t kTitleWindow = 1, kChoiceWindow = 2, kDetailsWindow = 3, kSpareWindow = 4;
constexpr uint8_t kSlotCount = 5; // a choice past the slots: cancelled
constexpr int16_t kNoSlot = -1;
// the fades the load menu asks for
constexpr uint8_t kLoadMenuFadeIn = kFadeInRequest | 2, kLoadMenuCancelFade = kFadeOutRequest | 2;
constexpr int16_t kLoadedMusicFadeStep = 0xfff;

std::function<int(uint8_t, std::span<const uint8_t>)> gSaveWrite;

// the image's bytes (the host writes them)
std::vector<uint8_t> imageBytes(const SaveImage& image) {
    const std::array<uint8_t, kSaveSize> b = image.bytes();
    return std::vector<uint8_t>(b.begin(), b.end());
}
// the host's bytes (a file read) into the image's first `size` bytes
void imageFromBytes(SaveImage& image, const uint8_t* bytes, uint32_t size) {
    std::array<uint8_t, kSaveSize> b = image.bytes();
    std::copy_n(bytes, std::min<uint32_t>(size, kSaveSize), b.begin());
    image.setBytes(b);
}
// the steps cleared (four byte stores: the open / the close)
void clearSteps(SaveMenuBlock& m) {
    m.step = 0;
    m.messageStep = 0;
    m.unread3 = 0;
}
// the windows 1..4 closed gently
void closeMenuWindows() {
    for (int8_t w = kTitleWindow; w <= kSpareWindow; ++w) windowClose(w, WindowClose::Gentle);
}
// the image written to the slot through the host's hook (exe 0x5493b0; it marks the slot): false = retry next tick
bool writeImage(const char* area, uint8_t slot, const SaveImage& image) {
    if (!gSaveWrite) notPorted(area, "no save file hook (exe 0x5493b0)");
    if (gSaveWrite(slot, imageBytes(image)) == -1) return false;
    saveSlotBits().present |= 1u << (slot & 0x1f);
    return true;
}

// exe 0x549010 (the save menu's Open / Close): the story menu's flag set (-> SlotList) / the windows 1..4 closed, the
// save and story menu flags cleared
void saveMenuOpenClose(SaveMenuBlock& m) {
    if (m.currentState() == SaveMenuState::Open) {
        setStoryFlag(kFlagStoryMenuOpen);
        m.state = uint8_t(SaveMenuState::SlotList);
        clearSteps(m);
    } else if (m.currentState() == SaveMenuState::Close) {
        closeMenuWindows();
        m.state = uint8_t(SaveMenuState::Open);
        clearSteps(m);
        clearStoryFlag(kFlagSaveMenuAsked);
        clearStoryFlag(kFlagStoryMenuOpen);
    }
}
// exe 0x548ae0 (CardMessage): a memory-card message (Cancelled: then the menu's close, cancelled; Retry: then the slot
// list)
void cardMessage(SaveMenuBlock& m) {
    if (m.messageStep == uint8_t(CardMessageStep::Prepare)) {
        windowClose(kChoiceWindow, WindowClose::Gentle);
        windowClose(kDetailsWindow, WindowClose::Gentle);
    }
    const CardMessage which = CardMessage(m.step);
    if (which != CardMessage::Cancelled && which != CardMessage::Retry) return;
    const CardMessageStep k = CardMessageStep(m.messageStep);
    if (k == CardMessageStep::Prepare) {
        m.messageStep = uint8_t(CardMessageStep::Open);
    } else if (k == CardMessageStep::Wait) {
        if (windowStatus(kChoiceWindow) != 0) return;
        if (which == CardMessage::Cancelled) {
            m.flags |= kMenuCancelled;
            m.enter(SaveMenuState::Close);
        } else {
            m.enter(SaveMenuState::SlotList);
        }
        return;
    } else if (k != CardMessageStep::Open) {
        return;
    }
    if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusCardMessageBank),
                   which == CardMessage::Cancelled ? kCardCancelledMessage : kCardRetryMessage) != -1)
        m.messageStep += 1;
}
// the slot list's choice (SlotListStep::Choose): window 3 follows the choice; answered: a slot (Ask's check next) or
// cancelled (the menu's close)
void chooseSlot(SaveMenuBlock& m) {
    MessageWindow& details = messageWindow(kDetailsWindow);
    details.record = int8_t(messageWindow(kChoiceWindow).choice());
    if (windowStatus(kChoiceWindow) != 0) return;
    const uint8_t v = messageSystem().answerOf(kChoiceWindow);
    details.record = int8_t(v);
    if (v < kSlotCount) {
        m.slot = int16_t(int8_t(v));
        m.step += 1;
        return;
    }
    windowClose(kDetailsWindow, WindowClose::Gentle);
    windowClose(kSpareWindow, WindowClose::Gentle);
    m.flags |= kMenuCancelled;
    m.enter(SaveMenuState::Close);
}
// SlotListStep::CheckSlot: a slot with a save asks first
void checkSlot(SaveMenuBlock& m) {
    if (saveSlotBits().present & (1u << (uint8_t(m.slot) & 0x1f))) m.step += 1;
    else m.step = uint8_t(SlotListStep::EmptySlot);
}
// exe 0x549080 (the save menu's SlotList): the slot list (window 3 the details of the slot under the choice, window 2
// the choice); a slot with a save asks to overwrite it, then Transfer
void saveSlotList(SaveMenuBlock& m) {
    switch (SlotListStep(m.step)) {
    case SlotListStep::Prepare:
        windowClose(kChoiceWindow, WindowClose::Gentle);
        windowClose(kDetailsWindow, WindowClose::Gentle);
        m.step += 1;
        [[fallthrough]];
    case SlotListStep::Details:
        messageWindow(kDetailsWindow).record = 0;
        if (windowOpen(kDetailsWindow, stateAddress(TableId::SaveMenusSaveTextBank), kSlotDetailsMessage) == -1) return;
        m.step += 1;
        [[fallthrough]];
    case SlotListStep::Choice:
        m.slot = kNoSlot;
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusSaveTextBank), kSlotChoiceMessage) == -1) return;
        m.step += 1;
        [[fallthrough]];
    case SlotListStep::Choose: chooseSlot(m); return;
    case SlotListStep::CheckSlot: checkSlot(m); return;
    case SlotListStep::Ask:
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusCardMessageBank), kOverwriteMessage) != -1) m.step += 1;
        return;
    case SlotListStep::Answer:
        if (windowStatus(kChoiceWindow) != 0) return;
        if (messageSystem().answerOf(kChoiceWindow) != 0) {
            m.step = uint8_t(SlotListStep::Choice);
            return;
        }
        [[fallthrough]];
    case SlotListStep::EmptySlot: m.enter(SaveMenuState::Transfer); return;
    default: return;
    }
}
// exe 0x549430 (the save menu's Transfer): the image built (exe 0x549600 / 0x549350 / 0x542ac0) and written, the slot's
// summary from it, "saved", then the menu's close
void saveWriteSlot(SaveMenuBlock& m) {
    SaveImage& image = saveImageAt(kSaveBuf);
    const uint8_t room = image.room; // read before the step (the summary's room)
    switch (SaveWriteStep(m.step)) {
    case SaveWriteStep::Prepare:
        windowClose(kChoiceWindow, WindowClose::Gentle);
        m.step += 1;
        [[fallthrough]];
    case SaveWriteStep::Build:
        saveBuildImage(uint8_t(m.slot));
        m.step += 1;
        [[fallthrough]];
    case SaveWriteStep::Write:
        if (!writeImage(kSaveMenuPort, uint8_t(uint16_t(m.slot)), image)) return;
        m.step += 1;
        return;
    case SaveWriteStep::Summary: {
        SaveSlotSummaries& s = saveSlotSummaries();
        const size_t slot = size_t(int32_t(m.slot));
        s.slots[slot].stage = image.stage;
        s.slots[slot].room = room;
        s.slots[slot].difficulty = image.difficulty;
        s.slots[slot].place = image.place;
        s.slots[slot].time = int32_t(uint32_t(image.playTime));
        m.step += 1;
        [[fallthrough]];
    }
    case SaveWriteStep::Saved:
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusCardMessageBank), kSavedMessage) != -1) m.step += 1;
        return;
    case SaveWriteStep::Wait:
        if (windowStatus(kChoiceWindow) == 0) m.enter(SaveMenuState::Close);
        return;
    default: return;
    }
}

// exe 0x548f80 (the clear-data menu's Open / Close): the story menu's flag, the windows closed, window 1 the menu's title
// (-> SlotList) / the windows 1..4 closed, kMenuClosed
void clearMenuOpenClose(SaveMenuBlock& m) {
    if (m.currentState() == SaveMenuState::Open) {
        if (m.step == 0) {
            setStoryFlag(kFlagStoryMenuOpen);
            windowCloseAll(WindowClose::Force); // exe 0x5577d0(1)
            m.step += 1;
        } else if (m.step != 1) {
            return;
        }
        if (windowOpen(kTitleWindow, stateAddress(TableId::SaveMenusSaveTextBank), kMenuTitleMessage) == -1) return;
        m.state = uint8_t(SaveMenuState::SlotList);
        clearSteps(m);
    } else if (m.currentState() == SaveMenuState::Close) {
        closeMenuWindows();
        m.flags |= kMenuClosed;
    }
}
// exe 0x549200 (the clear-data menu's Transfer): the clear data into the chosen slot's image kClearDataImage (its title
// from that image with the clear time, the header, the clear time and the records, the checksums), written; the slot's
// summary from it; "saved"; then the slot list again
void clearWriteSlot(SaveMenuBlock& m) {
    SaveImage& image = saveImageAt(kClearDataImage);
    GameState& g = gameState();
    switch (SaveWriteStep(m.step)) {
    case SaveWriteStep::Prepare:
        windowClose(kChoiceWindow, WindowClose::Gentle);
        m.step += 1;
        [[fallthrough]];
    case SaveWriteStep::Build:
        saveTitle(uint8_t(m.slot), kClearDataImage, g.clearTime, 1);
        saveHeader(kClearDataImage);
        image.playTime = g.clearTime;
        image.bestTime = g.bestTime;
        image.bestDifficulty = g.bestDifficulty;
        image.clears = g.clears;
        saveChecksumWrite(kClearDataImage);
        m.step += 1;
        [[fallthrough]];
    case SaveWriteStep::Write:
        if (!writeImage(kClearSaveMenuPort, uint8_t(uint16_t(m.slot)), image)) return;
        m.step += 1;
        return;
    case SaveWriteStep::Summary:
        saveSlotSummaryFromImage(uint32_t(int32_t(m.slot)), image);
        m.step += 1;
        [[fallthrough]];
    case SaveWriteStep::Saved:
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusCardMessageBank), kSavedMessage) != -1) m.step += 1;
        return;
    case SaveWriteStep::Wait:
        if (windowStatus(kChoiceWindow) == 0) m.enter(SaveMenuState::SlotList);
        return;
    default: return;
    }
}

// exe 0x5489d0 (the load menu's Open / Close): the menu opened with a fade in (window 1 the title; no save: the card
// message) / closed with a fade (to black when cancelled, the wipe to the game when loaded), then kMenuClosed
void loadMenuOpenClose(FadeTask& fade, SaveMenuBlock& m) {
    if (m.currentState() == SaveMenuState::Open) {
        if (m.step == 0) {
            windowCloseAll(WindowClose::Force);
            fadeStart(fade, kLoadMenuFadeIn, 0);
            m.step += 1;
        } else if (m.step != 1) {
            return;
        }
        if (windowOpen(kTitleWindow, stateAddress(TableId::SaveMenusLoadTextBank), kMenuTitleMessage) == -1) return;
        if (saveSlotBits().present == 0) { // no save: the memory-card message
            m.state = uint8_t(SaveMenuState::CardMessage);
            m.step = uint8_t(CardMessage::Cancelled);
        } else {
            m.state = uint8_t(SaveMenuState::SlotList);
            m.step = 0;
        }
        m.messageStep = 0;
        m.unread3 = 0;
    } else if (m.currentState() == SaveMenuState::Close) {
        switch (LoadCloseStep(m.step)) {
        case LoadCloseStep::WaitFade:
            if (fadeState().busy != 0) return;
            m.step = uint8_t(LoadCloseStep::Fade);
            [[fallthrough]];
        case LoadCloseStep::Fade:
            if (display().drawBuffer != 0) return;
            if (m.flags & kMenuCancelled) {
                fadeStart(fade, kLoadMenuCancelFade, 0);
                m.step += 1;
                return;
            }
            fadeStart(fade, kWipe26Request, 0);
            m.step += 2;
            return;
        case LoadCloseStep::WaitFadeOut:
            if (fadeState().busy != 0) return;
            m.step = uint8_t(LoadCloseStep::Closed);
            [[fallthrough]];
        case LoadCloseStep::Closed: m.flags |= kMenuClosed; return;
        default: return;
        }
    }
}
// exe 0x548bc0 (the load menu's SlotList): the headers of the slots read into kSaveBuf and their summaries; window 3 the
// details of the slot under the choice, window 2 the choice; an empty slot says so; a save asks to be loaded, then
// Transfer
void loadSlotList(SaveMenuBlock& m, const std::function<int(uint32_t, uint8_t*, uint32_t)>& readSave) {
    switch (SlotListStep(m.step)) {
    case SlotListStep::Prepare: {
        windowClose(kChoiceWindow, WindowClose::Gentle);
        windowClose(kDetailsWindow, WindowClose::Gentle);
        SaveImage& image = saveImageAt(kSaveBuf);
        for (uint32_t i = 0; i < kSaveSlots; ++i) { // exe 0x548de0(i, kSaveBuf, the header), the header's checksum
            std::vector<uint8_t> h(kSaveHeaderSize);
            if (!readSave || readSave(i, h.data(), kSaveHeaderSize) == -1) continue;
            imageFromBytes(image, h.data(), kSaveHeaderSize);
            if (saveChecksumBad(kSaveBuf, true)) continue;
            saveSlotSummaryFromImage(i, image);
        }
        m.step += 1;
        [[fallthrough]];
    }
    case SlotListStep::Details:
        messageWindow(kDetailsWindow).record = 0;
        if (windowOpen(kDetailsWindow, stateAddress(TableId::SaveMenusLoadTextBank), kSlotDetailsMessage) == -1) return;
        m.step += 1;
        [[fallthrough]];
    case SlotListStep::Choice:
        m.slot = kNoSlot;
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusLoadTextBank), kSlotChoiceMessage) == -1) return;
        m.step += 1;
        [[fallthrough]];
    case SlotListStep::Choose: chooseSlot(m); return;
    case SlotListStep::CheckSlot: checkSlot(m); return;
    case SlotListStep::Ask:
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusCardMessageBank), kLoadThisMessage) != -1) m.step += 1;
        return;
    case SlotListStep::Answer:
        if (windowStatus(kChoiceWindow) != 0) return;
        if (messageSystem().answerOf(kChoiceWindow) != 0) {
            m.step = uint8_t(SlotListStep::Choice);
            return;
        }
        m.enter(SaveMenuState::Transfer);
        return;
    case SlotListStep::EmptySlot:
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusCardMessageBank), kNoSaveInSlotMessage) == -1) return;
        m.step += 1;
        [[fallthrough]];
    case SlotListStep::EmptyWait: {
        const uint8_t r = windowStatus(kChoiceWindow);
        if (r == 0) m.step = r;
        return;
    }
    default: return;
    }
}
// exe 0x548e40 (the load menu's Transfer): the slot's whole image into kSaveBuf; bad checksums: the card message (Retry);
// else "loaded", then the music fades out, kMenuLoaded, the menu's close
void loadReadSlot(SaveMenuBlock& m, const std::function<int(uint32_t, uint8_t*, uint32_t)>& readSave) {
    switch (LoadReadStep(m.step)) {
    case LoadReadStep::Prepare:
        windowClose(kChoiceWindow, WindowClose::Gentle);
        m.step += 1;
        // the exe falls into Read with the slot argument's high half left from exe 0x5576b0's EAX (CONCAT22):
        // "DASH2-%02d.DAT" of that number does not exist, the read fails, Read reads it on the next frame
        return;
    case LoadReadStep::Read: {
        std::vector<uint8_t> img(kSaveSize);
        if (!readSave || readSave(uint16_t(m.slot), img.data(), kSaveSize) == -1) return;
        imageFromBytes(saveImageAt(kSaveBuf), img.data(), kSaveSize);
        m.step += 1;
        return;
    }
    case LoadReadStep::Check:
        if (saveChecksumBad(kSaveBuf, false)) {
            m.state = uint8_t(SaveMenuState::CardMessage);
            m.step = uint8_t(CardMessage::Retry);
            m.messageStep = 0;
            m.unread3 = 0;
            return;
        }
        m.step += 1;
        return;
    case LoadReadStep::Loaded:
        if (windowOpen(kChoiceWindow, stateAddress(TableId::SaveMenusCardMessageBank), kLoadedMessage) != -1) m.step += 1;
        return;
    case LoadReadStep::Wait:
        if (windowStatus(kChoiceWindow) & kWindowStatusWaiting) {
            soundFadeOut(kMusicFades, kLoadedMusicFadeStep, 0);
            m.flags |= kMenuLoaded;
            m.enter(SaveMenuState::Close);
        }
        return;
    default: return;
    }
}

} // namespace

void saveMenuTick(SaveMenuBlock& block) {
    switch (block.currentState()) { // the table 0x8afa6c
    case SaveMenuState::Open:
    case SaveMenuState::Close: saveMenuOpenClose(block); break;
    case SaveMenuState::CardMessage: cardMessage(block); break;
    case SaveMenuState::SlotList: saveSlotList(block); break;
    case SaveMenuState::Transfer: saveWriteSlot(block); break;
    default: notPorted(kSaveMenuPort, "state past the table 0x8afa6c");
    }
}

bool clearSaveMenuTick() {
    LoadMenuBlock& m = loadMenuBlock();
    switch (m.currentState()) { // the table 0x8afa58
    case SaveMenuState::Open:
    case SaveMenuState::Close: clearMenuOpenClose(m); break;
    case SaveMenuState::CardMessage: cardMessage(m); break;
    case SaveMenuState::SlotList: saveSlotList(m); break;
    case SaveMenuState::Transfer: clearWriteSlot(m); break;
    default: notPorted(kClearSaveMenuPort, "state past the table 0x8afa58");
    }
    if (!(m.flags & kMenuClosed)) return false;
    windowCloseAll(WindowClose::Force); // exe 0x5577d0(1)
    clearStoryFlag(kFlagStoryMenuOpen);
    return true;
}

void setSaveWriteHook(std::function<int(uint8_t, std::span<const uint8_t>)> hook) { gSaveWrite = std::move(hook); }

int loadMenuTick(FadeTask& fade, const std::function<int(uint32_t, uint8_t*, uint32_t)>& readSave) {
    LoadMenuBlock& m = loadMenuBlock();
    switch (m.currentState()) { // the table 0x8afa44
    case SaveMenuState::Open:
    case SaveMenuState::Close: loadMenuOpenClose(fade, m); break;
    case SaveMenuState::CardMessage: cardMessage(m); break;
    case SaveMenuState::SlotList: loadSlotList(m, readSave); break;
    case SaveMenuState::Transfer: loadReadSlot(m, readSave); break;
    default: notPorted(kLoadMenuPort, "state past 4 of the table 0x8afa44");
    }
    // exe 0x5e7fe0(0): the tiled background (draw only; game/title draws it)
    if (!(m.flags & kMenuClosed)) return 0;
    windowCloseAll(WindowClose::Force); // exe 0x5577d0(1)
    clearStoryFlag(kFlagStoryMenuOpen);
    return ((m.flags & kMenuCancelled) != 0) + 1;
}

} // namespace od::port
