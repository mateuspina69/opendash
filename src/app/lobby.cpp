// The multiplayer lobby (see lobby.hpp).
#include "app/lobby.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include "app/build_id.hpp"
#include "game/audio_logic.hpp"
#include "game/menu_draw.hpp"
#include "game/menu_pointer.hpp"
#include "game/player/player_input.hpp"

namespace od::app {

namespace {

using od::port::ScreenPrim;

constexpr size_t kFieldLength = 40; // what a field takes
constexpr int kScreenW = 640, kScreenH = 480;
constexpr int kPanelX = 0x40, kPanelW = kScreenW - 2 * kPanelX, kHeaderH = 0x24, kLineH = 0x16;
constexpr int kTextX = kPanelX + 0x14, kPlayersX = kPanelX + 0x140;
// 0xBBGGRR, 0x80 = 1.0
constexpr uint32_t kRowOn = 0x80ffff, kRowOff = 0x909090, kTitle = 0xffffff, kInfo = 0xa0a0a0, kWarn = 0x4060ff,
                   kGood = 0x60e060, kFrame = 0xc08040, kPanelColour = 0x401808, kBarColour = 0x804020;
constexpr uint16_t kButtons = od::port::kPadCircle | od::port::kPadCross | od::port::kPadStart;
constexpr int kMaxFound = 6; // the hosts listed

std::string pad(std::string s, size_t n) {
    if (s.size() < n) s.append(n - s.size(), ' ');
    return s;
}

} // namespace

Lobby::Lobby() = default;
Lobby::~Lobby() = default;

void Lobby::go(Page p) {
    page_ = p;
    cursor_ = 0;
    copied_.clear();
    if (p == Page::Find) {
        std::vector<uint16_t> ports{kDefaultNetPort};
        if (settings_.port != 0 && settings_.port != kDefaultNetPort) ports.push_back(settings_.port);
        try {
            finder_ = std::make_unique<od::net::LanFinder>(std::move(ports));
        } catch (const std::exception& e) {
            SDL_Log("lobby: no search of the local network: %s", e.what());
            finder_.reset();
        }
    } else {
        finder_.reset();
        found_.clear();
    }
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

void Lobby::paste() {
    if (!field_) return;
    char* text = SDL_GetClipboardText();
    if (!text) return;
    std::string s = text;
    SDL_free(text);
    // one line, no surrounding blanks
    if (const size_t nl = s.find_first_of("\r\n"); nl != std::string::npos) s.resize(nl);
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    textInput(a == std::string::npos ? "" : s.substr(a, b - a + 1).c_str());
}

void Lobby::copy() const {
    if (field_) SDL_SetClipboardText(field_->c_str());
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
    if (mode == NetMode::Host && s.port == 0) s.port = kDefaultNetPort;
    if (mode == NetMode::Join && s.join.find(':') == std::string::npos) s.join += ":" + std::to_string(kDefaultNetPort);
    if (mode == NetMode::Join && s.join.size() <= 1 + std::to_string(kDefaultNetPort).size()) return "no address";
    return hooks_.start ? hooks_.start(s) : "no session";
}

void Lobby::stopSession() {
    if (hooks_.active && hooks_.active() && hooks_.stop) hooks_.stop();
}

std::string Lobby::hostAddress() const {
    const std::vector<od::net::LocalNetwork> nets = od::net::localNetworks();
    if (nets.empty() || hostPort_ == 0) return "";
    return od::net::Endpoint{nets.front().address, hostPort_}.text();
}

void Lobby::screen(const std::string& title, int rows) {
    if (!draw_) return;
    // the whole window: the 640 x 480 frame and the widescreen sides (the title's background opened them)
    const int side = od::port::screenExtra() * 2;
    ScreenPrim dim; // the title's background, dimmed: a screen of its own
    dim.kind = ScreenPrim::Flat;
    dim.x = int16_t(-side);
    dim.y = 0;
    dim.w = int16_t(kScreenW + 2 * side);
    dim.h = kScreenH;
    dim.colour = 0;
    dim.semi = true;
    draw_->push_back(dim);
    const int h = kHeaderH + rows * kLineH + 0x14;
    top_ = (kScreenH - h) / 2;
    ScreenPrim p;
    p.kind = ScreenPrim::Flat;
    p.x = kPanelX;
    p.y = int16_t(top_);
    p.w = kPanelW;
    p.h = int16_t(h);
    p.colour = kPanelColour;
    p.semi = true;
    draw_->push_back(p);
    const auto edge = [&](int x0, int y0, int x1, int y1) {
        ScreenPrim l;
        l.kind = ScreenPrim::Line;
        l.qx[0] = int16_t(x0);
        l.qy[0] = int16_t(y0);
        l.qx[1] = int16_t(x1);
        l.qy[1] = int16_t(y1);
        l.colour = kFrame;
        draw_->push_back(l);
    };
    const int x1 = kPanelX + kPanelW - 1, y1 = top_ + h - 1;
    edge(kPanelX, top_, x1, top_);
    edge(kPanelX, y1, x1, y1);
    edge(kPanelX, top_, kPanelX, y1);
    edge(x1, top_, x1, y1);
    edge(kPanelX + 8, top_ + kHeaderH - 6, x1 - 8, top_ + kHeaderH - 6);
    texts_.push_back(title);
    ScreenPrim t;
    t.kind = ScreenPrim::Text;
    t.utf8 = texts_.back().c_str();
    t.x = t.ax = int16_t(kTextX);
    t.y = int16_t(top_ + 8);
    t.colour = kTitle;
    draw_->push_back(t);
}

void Lobby::line(int i, const std::string& text, uint32_t colour, int column) {
    if (!draw_) return;
    texts_.push_back(text);
    ScreenPrim t;
    t.kind = ScreenPrim::Text;
    t.utf8 = texts_.back().c_str();
    t.x = t.ax = int16_t(column ? kPlayersX : kTextX);
    t.y = int16_t(top_ + kHeaderH + 6 + i * kLineH);
    t.colour = colour;
    draw_->push_back(t);
}

void Lobby::row(int i, const std::string& text, uint32_t colour) {
    if (!draw_) return;
    const bool on = i == cursor_;
    const int y = top_ + kHeaderH + 3 + i * kLineH;
    const int w = (page_ == Page::Find ? kPanelW : kPlayersX - kPanelX) - 0x10;
    if (on) { // the cursor's bar
        ScreenPrim bar;
        bar.kind = ScreenPrim::Flat;
        bar.x = int16_t(kPanelX + 8);
        bar.y = int16_t(y);
        bar.w = int16_t(w);
        bar.h = kLineH - 2;
        bar.colour = kBarColour;
        bar.semi = true;
        draw_->push_back(bar);
    }
    ScreenPrim hit; // the mouse's target
    hit.kind = ScreenPrim::Sprite;
    hit.hitOnly = true;
    hit.row = int16_t(i);
    hit.x = int16_t(kPanelX + 8);
    hit.y = int16_t(y);
    hit.w = int16_t(w);
    hit.h = kLineH;
    draw_->push_back(hit);
    line(i, text, colour ? colour : on ? kRowOn : kRowOff);
}

od::port::LobbyChoice Lobby::frame(std::vector<ScreenPrim>* draw) {
    using od::port::LobbyChoice;
    const uint64_t nowNs = SDL_GetTicksNS();
    const bool running = hooks_.active && hooks_.active();
    const od::net::SessionStatus status = running && hooks_.status ? hooks_.status() : od::net::SessionStatus{};
    const bool hosting = running && status.role == od::net::Role::Host;
    // a session already running (opendash.ini's, or the host back from the load menu): the lobby room
    if (running && page_ != Page::Room) {
        go(Page::Room);
    } else if (!running && page_ == Page::Room) {
        go(Page::Choose); // the session ended under it
    }
    if (hosting) hostPort_ = status.port;
    if (finder_) {
        finder_->poll(nowNs);
        found_.assign(finder_->hosts().begin(), finder_->hosts().begin() + std::min<ptrdiff_t>(kMaxFound, ptrdiff_t(finder_->hosts().size())));
    }
    const int nFound = int(found_.size());
    const int rows = page_ == Page::Choose ? 5 : page_ == Page::Find ? nFound + 3 : hosting ? 4 : 1;
    cursor_ = std::min(cursor_, rows - 1);
    // the pad: the cursor, confirm, cancel (none while a field is typed, nor until the typing's keys are let go)
    const od::port::PadState& pad0 = od::port::pad0();
    if (waitRelease_ && !(pad0.held & kButtons)) waitRelease_ = false;
    bool confirm = false, cancel = false;
    if (!editing() && !waitRelease_) {
        if (od::port::MenuPointer& pt = od::port::menuPointer(); pt.moved && pt.row < od::port::kListCell) {
            pt.moved = false;
            if (pt.row >= 0 && pt.row < rows && pt.row != cursor_) {
                cursor_ = pt.row;
                od::port::soundPlayFlat(od::port::UiSound::Cursor);
            }
        }
        const uint16_t p = pad0.pressed | od::port::menuDirections(); // a held direction repeats (as the game's menus)
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
            if (error_.empty()) go(Page::Room);
        } else if (confirm && cursor_ == 1) {
            error_.clear();
            go(Page::Find);
        } else if (confirm && cursor_ == 2) {
            beginEdit(settings_.name);
        } else if (confirm && cursor_ == 3) {
            beginEdit(settings_.key);
        }
        break;
    case Page::Find:
        if (confirm && cursor_ < nFound) {
            settings_.join = found_[size_t(cursor_)].endpoint.text();
            error_ = startSession(NetMode::Join);
            if (error_.empty()) go(Page::Room);
        } else if (confirm && cursor_ == nFound) {
            beginEdit(settings_.join);
        } else if (confirm && cursor_ == nFound + 1) {
            error_ = startSession(NetMode::Join);
            if (error_.empty()) go(Page::Room);
        } else if (cancel || (confirm && cursor_ == nFound + 2)) {
            error_.clear();
            go(Page::Choose);
            cursor_ = 1;
        }
        break;
    case Page::Room:
        if (hosting && confirm && cursor_ == 0) {
            choice = LobbyChoice::NewGame;
        } else if (hosting && confirm && cursor_ == 1) {
            choice = LobbyChoice::Load;
        } else if (hosting && confirm && cursor_ == 2) {
            copied_ = hostAddress();
            if (!copied_.empty()) SDL_SetClipboardText(copied_.c_str());
        } else if (cancel || (confirm && cursor_ == rows - 1)) {
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
    switch (page_) {
    case Page::Choose:
        screen("MULTIPLAYER", 7);
        row(0, "HOST GAME");
        row(1, "JOIN GAME");
        row(2, "NAME   " + typed(settings_.name, &settings_.name));
        row(3, "KEY    " + typed(settings_.key.empty() && field_ != &settings_.key ? "-" : settings_.key, &settings_.key));
        row(4, "BACK");
        if (!error_.empty()) line(6, error_, kWarn);
        break;
    case Page::Find: {
        screen("JOIN GAME", std::max(nFound, 1) + 6);
        for (int i = 0; i < nFound; ++i) {
            const od::net::LanHost& h = found_[size_t(i)];
            const bool same = h.buildId == buildId();
            row(i, pad(h.name.empty() ? "?" : h.name, 14) + pad(std::to_string(h.players) + "/" + std::to_string(od::net::kMaxPlayers), 6) +
                       h.endpoint.text() + (same ? "" : "   OTHER BUILD"),
                same ? 0 : kWarn);
        }
        row(nFound, "ADDRESS  " + typed(settings_.join, &settings_.join));
        row(nFound + 1, "CONNECT");
        row(nFound + 2, "BACK");
        const int info = nFound + 4;
        if (!error_.empty()) line(info, error_, kWarn);
        else if (!finder_) line(info, "The local network cannot be searched: type the host's address", kInfo);
        else if (nFound == 0) line(info, "Searching the local network...", kInfo);
        else line(info, "Hosts on the local network", kInfo);
        line(info + 1, "Ctrl+V pastes an address", kInfo);
        break;
    }
    case Page::Room: {
        const int players = 1 + int(status.players.size());
        screen("LOBBY", std::max(players + 2, hosting ? 8 : 6));
        line(0, "PLAYERS " + std::to_string(players) + "/" + std::to_string(od::net::kMaxPlayers), kTitle, 1);
        int at = 1;
        const auto tag = [&](uint8_t id) { return id == od::net::kHostPlayerId ? "  HOST" : ""; };
        line(at++, settings_.name + tag(status.localId) + "  (YOU)", kGood, 1);
        for (const od::net::PlayerStatus& p : status.players) line(at++, p.name + tag(p.playerId), kInfo, 1);
        if (hosting) {
            row(0, "START NEW GAME");
            row(1, "LOAD GAME");
            row(2, copied_.empty() ? "COPY ADDRESS" : "ADDRESS COPIED");
            row(3, "LEAVE");
            const std::string me = hostAddress();
            line(5, me.empty() ? "PORT " + std::to_string(status.port) : me, kInfo);
            line(6, players > 1 ? "Start when everyone is in" : "Waiting for players...", kInfo);
        } else {
            row(0, "LEAVE");
            const char* state = status.connection == od::net::Connection::Connecting ? "Connecting..."
                                : status.connection == od::net::Connection::Refused  ? "Refused"
                                                                                     : "Waiting for the host to start";
            line(2, state, status.connection == od::net::Connection::Refused ? kWarn : kInfo);
            if (status.connection == od::net::Connection::Refused && !status.text.empty()) line(3, status.text, kWarn);
        }
        break;
    }
    }
    draw_ = nullptr;
    return choice;
}

} // namespace od::app
