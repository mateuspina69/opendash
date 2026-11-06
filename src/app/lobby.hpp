// The multiplayer lobby: the title menu's fifth row (game/title TitleState::Lobby runs one frame of it). Host or join a
// session, the players in it, the name and the key (opendash.ini's values the defaults, typed on the keyboard); the
// host starts the session's game from the menu's own new game or load menu, a client waits here and enters when the
// host's game is on (app/app_frame: the host's save, then its game). Back ends the session. Drawn as text in the game's
// font over the title's background.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "app/netplay.hpp"
#include "engine/screen_prim.hpp"
#include "game/title.hpp"
#include "net/session.hpp"

namespace od::app {

// what the lobby asks of the app: a session started ("" or why it did not), ended, running, its state
struct LobbyHooks {
    std::function<std::string(const NetSettings&)> start;
    std::function<void()> stop;
    std::function<bool()> active;
    std::function<od::net::SessionStatus()> status;
};

class Lobby {
public:
    void setDefaults(NetSettings defaults) { settings_ = std::move(defaults); }
    void setHooks(LobbyHooks hooks) { hooks_ = std::move(hooks); }
    // one title frame: the pad, then the page's primitives into `draw` (nullptr: none)
    od::port::LobbyChoice frame(std::vector<od::port::ScreenPrim>* draw);
    // a field is being typed (the app sends the keyboard's text here, and nothing of it to the game)
    bool editing() const { return field_ != nullptr; }
    void textInput(const char* utf8);
    void backspace();
    void endEdit(bool keep); // Enter keeps, Esc puts the value back

private:
    enum class Page : uint8_t { Choose, Host, Address, Joined };
    void beginEdit(std::string& field);
    void go(Page p);
    std::string startSession(NetMode mode);
    void stopSession();
    // the primitives
    void panel(int rows);
    void line(int i, const std::string& text, uint32_t colour, int column = 0);
    void row(int i, const std::string& text);

    NetSettings settings_;
    LobbyHooks hooks_;
    Page page_ = Page::Choose;
    int cursor_ = 0;
    std::string* field_ = nullptr; // the field being typed
    std::string before_;           // its value when the typing began
    bool waitRelease_ = false;     // the typing's Enter / Esc: the pad's buttons are let go first
    std::string error_;
    std::vector<od::port::ScreenPrim>* draw_ = nullptr;
    std::deque<std::string> texts_; // this frame's strings (the Text primitives point at them)
};

} // namespace od::app
