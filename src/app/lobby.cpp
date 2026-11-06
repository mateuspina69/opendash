// The multiplayer lobby (see lobby.hpp).
#include "app/lobby.hpp"

#include <algorithm>
#include "game/audio_logic.hpp"
#include "game/menu_pointer.hpp"
#include "game/player/player_input.hpp"

namespace od::app {

namespace {

using od::port::ScreenPrim;

constexpr size_t kFieldLength = 40;   // what a field takes
constexpr int kPanelX = 0x80, kPanelY = 0x108, kPanelW = 0x180, kLineH = 0x16;
constexpr int kTextX = 0x90, kPlayersX = 0x150;
constexpr uint32_t kRowOn = 0x808080, kRowOff = 0x585858, kInfo = 0x707070, kWarn = 0x404080; // 0xBBGGRR
constexpr uint32_t kPanelColour = 0x401808;
constexpr uint16_t kButtons = od::port::kPadCircle | od::port::kPadCross | od::port::kPadStart;

} // namespace

void Lobby::go(Page p) {
    page_ = p;
    cursor_ = 0;
}

void Lobby::beginEdit(std::string& field) {
    field_ = &field;
    before_ = field;
}

void Lobby::textInput(const char* utf8) {
    if (!field_ || !utf8) return;
    for (const char* c = utf8; *c; ++c) // the game's font: printable ASCII
        if (*c >= 0x20 && *c < 0x7f && field_->size() < kFieldLength) field_->push_back(*c);
}

void Lobby::backspace() {
    if (field_ && !field_->empty()) field_->pop_back();
}

void Lobby::endEdit(bool keep) {
    if (!field_) return;
    if (!keep) *field_ = before_;
    field_ = nullptr;
    waitRelease_ = true;
}

std::string Lobby::startSession(NetMode mode) {
    NetSettings s = settings_;
    s.mode = mode;
    if (mode == NetMode::Join && s.join.find(':') == std::string::npos) s.join += ":" + std::to_string(kDefaultNetPort);
    if (mode == NetMode::Join && s.join.size() <= 1 + std::to_string(kDefaultNetPort).size()) return "no address";
    return hooks_.start ? hooks_.start(s) : "no session";
}

void Lobby::stopSession() {
    if (hooks_.active && hooks_.active() && hooks_.stop) hooks_.stop();
}

void Lobby::panel(int rows) {
    if (!draw_) return;
    ScreenPrim p;
    p.kind = ScreenPrim::Flat;
    p.x = kPanelX;
    p.y = kPanelY;
    p.w = kPanelW;
    p.h = int16_t(rows * kLineH + 0x10);
    p.colour = kPanelColour;
    p.semi = true;
    draw_->push_back(p);
}

void Lobby::line(int i, const std::string& text, uint32_t colour, int column) {
    if (!draw_) return;
    texts_.push_back(text);
    ScreenPrim t;
    t.kind = ScreenPrim::Text;
    t.utf8 = texts_.back().c_str();
    t.x = t.ax = int16_t(column ? kPlayersX : kTextX);
    t.y = int16_t(kPanelY + 8 + i * kLineH);
    t.colour = colour;
    draw_->push_back(t);
}

void Lobby::row(int i, const std::string& text) {
    if (!draw_) return;
    ScreenPrim hit; // the mouse's target
    hit.kind = ScreenPrim::Sprite;
    hit.hitOnly = true;
    hit.row = int16_t(i);
    hit.x = kPanelX;
    hit.y = int16_t(kPanelY + 4 + i * kLineH);
    hit.w = kPanelW;
    hit.h = kLineH;
    draw_->push_back(hit);
    line(i, (i == cursor_ ? "> " : "  ") + text, i == cursor_ ? kRowOn : kRowOff);
}

od::port::LobbyChoice Lobby::frame(std::vector<ScreenPrim>* draw) {
    using od::port::LobbyChoice;
    const bool running = hooks_.active && hooks_.active();
    const od::net::SessionStatus status = running && hooks_.status ? hooks_.status() : od::net::SessionStatus{};
    // a session already running (opendash.ini's, or the host back from the load menu): its page
    if (running && (page_ == Page::Choose || page_ == Page::Address)) {
        go(status.role == od::net::Role::Host ? Page::Host : Page::Joined);
    } else if (!running && (page_ == Page::Host || page_ == Page::Joined)) {
        go(Page::Choose); // the session ended under it
    }
    // the pad: the cursor, confirm, cancel (none while a field is typed, nor until the typing's keys are let go)
    const od::port::PadState& pad = od::port::pad0();
    if (waitRelease_ && !(pad.held & kButtons)) waitRelease_ = false;
    const int rows = page_ == Page::Choose ? 5 : page_ == Page::Joined ? 1 : 3;
    bool confirm = false, cancel = false;
    if (!editing() && !waitRelease_) {
        if (od::port::MenuPointer& pt = od::port::menuPointer(); pt.moved && pt.row < od::port::kListCell) {
            pt.moved = false;
            if (pt.row >= 0 && pt.row < rows && pt.row != cursor_) {
                cursor_ = pt.row;
                od::port::soundPlayFlat(od::port::UiSound::Cursor);
            }
        }
        const uint16_t p = pad.pressed | od::port::menuDirections(); // a held direction repeats (as the game's menus)
        if (p & (od::port::kPadUp | od::port::kPadDown)) {
            cursor_ = (cursor_ + ((p & od::port::kPadUp) ? rows - 1 : 1)) % rows;
            od::port::soundPlayFlat(od::port::UiSound::Cursor);
        }
        if (p & kButtons) {
            if (p & od::port::kPadConfirm) confirm = true;
            else cancel = true;
        }
    }
    LobbyChoice choice = LobbyChoice::Stay;
    switch (page_) {
    case Page::Choose:
        if (cancel || (confirm && cursor_ == 4)) {
            stopSession();
            error_.clear();
            choice = LobbyChoice::Back;
        } else if (confirm && cursor_ == 0) {
            error_ = startSession(NetMode::Host);
            if (error_.empty()) go(Page::Host);
        } else if (confirm && cursor_ == 1) {
            error_.clear();
            go(Page::Address);
            beginEdit(settings_.join);
        } else if (confirm && cursor_ == 2) {
            beginEdit(settings_.name);
        } else if (confirm && cursor_ == 3) {
            beginEdit(settings_.key);
        }
        break;
    case Page::Host:
        if (confirm && cursor_ == 0) choice = LobbyChoice::NewGame;
        else if (confirm && cursor_ == 1) choice = LobbyChoice::Load;
        else if (cancel || (confirm && cursor_ == 2)) {
            stopSession();
            go(Page::Choose);
        }
        break;
    case Page::Address:
        if (confirm && cursor_ == 0) {
            beginEdit(settings_.join);
        } else if (confirm && cursor_ == 1) {
            error_ = startSession(NetMode::Join);
            if (error_.empty()) go(Page::Joined);
        } else if (cancel || (confirm && cursor_ == 2)) {
            go(Page::Choose);
            cursor_ = 1;
        }
        break;
    case Page::Joined:
        if (cancel || confirm) {
            stopSession();
            go(Page::Choose);
        }
        break;
    }
    // a choice that leaves the lobby has its sound from the title's own step (the menu's new game / load confirm, the
    // back's cancel): once, as the game's menu does
    if (choice == LobbyChoice::Stay) {
        if (confirm) od::port::soundPlayFlat(od::port::UiSound::Confirm);
        if (cancel) od::port::soundPlayFlat(od::port::UiSound::Cancel);
    }
    if (choice != LobbyChoice::Stay || !draw) return choice;
    // the page
    draw_ = draw;
    texts_.clear();
    const auto typed = [&](const std::string& v, const std::string* f) { return f == field_ ? v + "_" : v; };
    const auto players = [&](int from) {
        line(from, "PLAYERS", kInfo, 1);
        int i = from + 1;
        line(i++, settings_.name, kInfo, 1);
        for (const od::net::PlayerStatus& p : status.players) line(i++, p.name, kInfo, 1);
    };
    switch (page_) {
    case Page::Choose:
        panel(6);
        row(0, "HOST GAME");
        row(1, "JOIN GAME");
        row(2, "NAME  " + typed(settings_.name, &settings_.name));
        row(3, "KEY   " + typed(settings_.key.empty() ? (field_ == &settings_.key ? "" : "-") : settings_.key, &settings_.key));
        row(4, "BACK");
        if (!error_.empty()) line(5, error_, kWarn);
        break;
    case Page::Host:
        panel(std::max<int>(6, 3 + int(status.players.size())));
        row(0, "NEW GAME");
        row(1, "LOAD GAME");
        row(2, "BACK");
        line(4, "PORT " + std::to_string(status.port), kInfo);
        players(0);
        break;
    case Page::Address:
        panel(5);
        row(0, "ADDRESS " + typed(settings_.join, &settings_.join));
        row(1, "CONNECT");
        row(2, "BACK");
        if (!error_.empty()) line(4, error_, kWarn);
        break;
    case Page::Joined: {
        panel(std::max<int>(5, 3 + int(status.players.size())));
        row(0, "BACK");
        const char* state = status.connection == od::net::Connection::Connecting ? "CONNECTING..."
                            : status.connection == od::net::Connection::Refused  ? "REFUSED"
                                                                                 : "WAITING FOR THE HOST";
        line(2, state, status.connection == od::net::Connection::Refused ? kWarn : kInfo);
        if (status.connection == od::net::Connection::Refused && !status.text.empty()) line(3, status.text, kWarn);
        if (status.connection == od::net::Connection::Connected) players(0);
        break;
    }
    }
    draw_ = nullptr;
    return choice;
}

} // namespace od::app
