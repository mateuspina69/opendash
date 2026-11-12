// Button prompts of the vector UI: a PS1 button (the game's logic speaks PS1 bits) shown as the control that drives
// it on the device the player used last -- the key it is bound to (the key configuration's table), or the gamepad
// button bound to it as that gamepad family labels it (PlayStation symbols, Xbox / Nintendo letters).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "app/ui/ui_text.hpp"
#include "render/ui_canvas.hpp"

namespace od::app {

enum class PromptDevice { Keyboard, PlayStation, Xbox, Nintendo, PlayStation1, SteamDeck }; // PlayStation1: the PS1 pad's names (START / SELECT)

// OpenDash (Herbert): the gamepad prompts' style, picked in the options (config button_style=): 0 PlayStation (the
// PS1's names, the default), 1 Xbox / PC, 2 Steam Deck, 3 Nintendo Switch. Keyboard / mouse input still shows keys.
constexpr int kButtonStyles = 4;
void setButtonStyle(int style);
int buttonStyle();
PromptDevice buttonStyleDevice(); // the style as a device (the key configuration's list, the gamepad prompts)

// the device the prompts show: keyboard / mouse input selects the keyboard, gamepad input the pad's family
PromptDevice promptDevice();
void notePromptEvent(const SDL_Event& e);

// a PS1 button (0x1000 triangle, 0x2000 circle, 0x4000 cross, 0x8000 square, ...) as a prompt h px high, its left
// edge at x, centred on y (window px); returns its width. The binding tables come from the game state (the defaults
// when it does not map them). `text`: the font of the key / letter labels
float drawPrompt(ui::Canvas& cv, UiText& text, uint16_t ps1, float x, float y, float h);
// pad button `code` (1..12: input padButtonDown) in the style of `device` (the key configuration's list)
float drawPadCode(ui::Canvas& cv, UiText& text, int code, float x, float y, float h, PromptDevice device);

} // namespace od::app
