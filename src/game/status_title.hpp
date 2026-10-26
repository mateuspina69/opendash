// OpenDash's "To Title" of the status sub-screen: a row of the panel (page 1) in play, before BACK, that asks yes / no
// and on yes returns to the title through the host (OptionsHost::returnToTitle, the game's soft reset). Its caption is
// OpenDash's own cell (game/menu_art, drawn by the host into the sub-screen's VRAM); its help is one more message of the
// panel's help bank, built whole into OpenDash's region (game/menu_help).
#pragma once

#include <cstdint>

namespace od::port {

struct StatusScreen;

// the panel has the row: in play (not the title's own options), with the host's way back to the title
bool panelHasTitle();
// the panel's rows (the exe's five, and the title row before BACK)
constexpr int kPanelTitleRow = 4;
constexpr int kPanelTitleLabelRow = 5; // the vector UI's name of the row (status.row.5, past the exe's five)
int panelRowCount();
// the exe's row (its picture, its page) of the panel's row k; -1 the title row
int panelExeRow(int k);

// the panel entered: its cursor entry (the rows' count) and its help bank (one more message) for the rows in play, the
// exe's as they are otherwise
void panelTitleSetUp();
// the panel's state ToTitle: the question (no first; circle on yes asks the host to return to the title)
void panelTitleQuestion(StatusScreen& s);

// the row's caption: the Japanese glyph codes (the PC font's, hiragana from 0x80), the Latin texts by language (1 English,
// 2 Portuguese; UTF-8)
inline constexpr uint16_t kTitleCaptionJp[] = {0x8f, 0x81, 0x93, 0xa8}; // たいとる
inline constexpr const char* kTitleCaptionLatin[3] = {"", "To Title", "T\xc3\xadtulo"};

} // namespace od::port
