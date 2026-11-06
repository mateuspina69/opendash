// The multiplayer lobby: the title menu's fifth row (game/title TitleState::Lobby runs one frame of it). Host or join a
// session (the hosts on the local network listed, an address typed or pasted), then the lobby room: everyone in the
// session, the host starts the game from the menu's own new game or load menu, the others wait there and enter when
// the host's game is on (app/app_frame: the host's save, then its game). Leave ends the session. The name and the key
// are opendash.ini's values, typed on the keyboard (Ctrl+V pastes, Ctrl+C copies). Drawn as text in the game's font
// over the title's background, dimmed.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/netplay.hpp"
#include "engine/screen_prim.hpp"
#include "game/title.hpp"
#include "net/lan_finder.hpp"
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
    Lobby();
    ~Lobby();
    void setDefaults(NetSettings defaults) { settings_ = std::move(defaults); }
    void setHooks(LobbyHooks hooks) { hooks_ = std::move(hooks); }
    // one title frame: the pad, then the page's primitives into `draw` (nullptr: none)
    od::port::LobbyChoice frame(std::vector<od::port::ScreenPrim>* draw);
    // a field is being typed (the app sends the keyboard's text here, and nothing of it to the game)
    bool editing() const { return field_ != nullptr; }
    void textInput(const char* utf8);
    void backspace();
    void paste();            // Ctrl+V: the clipboard's text into the field
    void copy() const;       // Ctrl+C: the field's text (on the lobby room: this host's address)
    void endEdit(bool keep); // Enter keeps, Esc puts the value back

private:
    enum class Page : uint8_t { Choose, Find, Room };
    void beginEdit(std::string& field);
    void go(Page p);
    std::string startSession(NetMode mode);
    void stopSession();
    std::string hostAddress() const; // this machine's first local network address:port ("" none)
    // the primitives
    void screen(const std::string& title, int rows);
    void line(int i, const std::string& text, uint32_t colour, int column = 0);
    void row(int i, const std::string& text, uint32_t colour = 0);

    NetSettings settings_;
    LobbyHooks hooks_;
    Page page_ = Page::Choose;
    int cursor_ = 0;
    std::string* field_ = nullptr; // the field being typed
    std::string before_;           // its value when the typing began
    bool waitRelease_ = false;     // the typing's Enter / Esc: the pad's buttons are let go first
    std::string error_;
    std::unique_ptr<od::net::LanFinder> finder_; // the Find page's
    std::vector<od::net::LanHost> found_;        // this frame's list (the rows' order)
    uint16_t hostPort_ = 0;
    std::string copied_; // what a copy row put on the clipboard (its row says so)
    // the frame's layout
    std::vector<od::port::ScreenPrim>* draw_ = nullptr;
    std::deque<std::string> texts_; // this frame's strings (the Text primitives point at them)
    int top_ = 0;
};

} // namespace od::app
