#include "app/subtitles.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "app/lang_sfx.hpp"
#include "app/ui/vector_ui.hpp"
#include "game/audio_logic.hpp"
#include "game/message_state.hpp"

namespace od::app {

void Subtitles::load(const std::filesystem::path& file) {
    if (loadedOnce_ && file == loaded_) return;
    loadedOnce_ = true;
    loaded_ = file;
    lines_.clear();
    current_ = nullptr;
    sfx_.clear();
    sfxLine_ = nullptr;
    if (file.empty()) return;
    std::ifstream in(file, std::ios::binary);
    if (!in) return;
    std::string row;
    int n = 0;
    while (std::getline(in, row)) {
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (row.empty() || row[0] == '#') continue;
        std::vector<std::string> f;
        std::stringstream ss(row);
        std::string cell;
        while (std::getline(ss, cell, '\t')) f.push_back(cell);
        if (f.size() < 5 || f[4].empty()) continue;
        std::string clip = f[0];
        for (char& c : clip) c = char(std::tolower(uint8_t(c)));
        Line l;
        l.start = std::strtof(f[2].c_str(), nullptr);
        l.end = std::strtof(f[3].c_str(), nullptr);
        l.text = f[4];
        lines_[clip + "#" + f[1]].push_back(std::move(l));
        ++n;
    }
    for (auto& [k, v] : lines_)
        std::sort(v.begin(), v.end(), [](const Line& a, const Line& b) { return a.start < b.start; });
    SDL_Log("subtitles: %d lines from %s", n, file.string().c_str());
    // OpenDash (Herbert): the SFX banks' voice barks -- legendas_sfx[_<voice>].tsv beside it: `key<TAB>seconds<TAB>text`
    // (key = <STEM>_<arg>_<extra>_s<k>, app/lang_sfx), shown while the sample plays
    sfx_.clear();
    std::string name = file.filename().string();
    if (name.rfind("legendas", 0) == 0) {
        name.insert(8, "_sfx");
        if (const size_t d = name.find("_direto"); d != std::string::npos) name.erase(d, 7); // (one transcript)
        std::ifstream sf(file.parent_path() / name, std::ios::binary);
        while (sf && std::getline(sf, row)) {
            if (!row.empty() && row.back() == '\r') row.pop_back();
            if (row.empty() || row[0] == '#') continue;
            const size_t a = row.find('\t'), b = a == std::string::npos ? a : row.find('\t', a + 1);
            if (b == std::string::npos || b + 1 >= row.size()) continue;
            std::string key = row.substr(0, a);
            for (char& c : key) c = char(std::toupper(uint8_t(c)));
            Line l;
            l.end = std::strtof(row.substr(a + 1, b - a - 1).c_str(), nullptr);
            l.text = row.substr(b + 1);
            sfx_[key] = std::move(l);
        }
        if (!sfx_.empty()) SDL_Log("subtitles: %d SFX lines from %s", int(sfx_.size()), name.c_str());
    }
}

std::filesystem::path Subtitles::fileFor(const std::filesystem::path& langDir, const char* text, const char* voice,
                                         bool direct) {
    if (langDir.empty()) return {};
    const std::string t = text, v = voice;
    if (t == v) return langDir / t / "legendas.tsv";
    std::error_code ec;
    if (direct) {
        const std::filesystem::path d = langDir / t / ("legendas_" + v + "_direto.tsv");
        if (std::filesystem::is_regular_file(d, ec)) return d;
    }
    return langDir / t / ("legendas_" + v + ".tsv");
}

void Subtitles::draw(od::Renderer& r, ui::Canvas& cv, float width, float height) {
    const od::port::VoiceClipInfo& info = od::port::voiceClipInfo();
    if (info.serial != serial_) { // a clip started: its lines, timed from now
        serial_ = info.serial;
        startedNs_ = SDL_GetTicksNS();
        std::string file = info.file;
        for (char& c : file) c = char(std::tolower(uint8_t(c)));
        const auto it = lines_.find(file + "#" + std::to_string(info.track));
        current_ = it == lines_.end() ? nullptr : &it->second;
    }
    if (!enabled) return;
    // a window's voice: the window has the text; (Herbert) also any text window on screen while a clip plays (Roll's
    // radio lines in play: the game shows them in its window, a subtitle under it repeats them)
    for (size_t w = 0; w < 5; ++w) {
        const uint32_t f = od::port::messageWindow(w).flags;
        if (f & (od::port::kWindowVoice | od::port::kWindowVoiceStarting)) return;
        if ((f & od::port::kWindowOpen) && (f & od::port::kWindowFrameShown)) return;
    }
    const Line* line = nullptr;
    if (current_ && od::port::voiceClipPlaying()) {
        // the clip's own playback position (pauses and stalls excluded); the wall clock from its start if unknown
        const double heard = od::port::voiceClipSeconds();
        const float t = heard >= 0.0 ? float(heard) : float(double(SDL_GetTicksNS() - startedNs_) / 1e9);
        for (const Line& l : *current_)
            if (t >= l.start - 0.05f && t <= l.end + 0.35f) line = &l;
    }
    if (!line && !sfx_.empty()) { // OpenDash: a voice bark of the SFX banks, for its length (+ a moment to read it)
        const od::lang_sfx::Heard& h = od::lang_sfx::lastHeard();
        if (h.serial != sfxSerial_) {
            sfxSerial_ = h.serial;
            const auto it = sfx_.find(h.key);
            sfxLine_ = it == sfx_.end() ? nullptr : &it->second;
            sfxNs_ = h.ns;
        }
        if (sfxLine_ && double(SDL_GetTicksNS() - sfxNs_) / 1e9 <= std::max(1.2, double(sfxLine_->end) + 0.6))
            line = sfxLine_;
    }
    if (!line) return;
    { // the full M+ (kana, kanji) when installed, else its Latin part; the font a pack's font.txt names (the
      // Korean / Chinese packs) for its subtitles
        std::error_code ec;
        std::filesystem::path want = assetsDir() / "fonts" / "MPLUSRounded1c-Medium.ttf";
        if (!std::filesystem::is_regular_file(want, ec)) want = assetsDir() / "fonts" / "MPLUSRounded1c-Medium-Latin.ttf";
        if (std::ifstream ft(loaded_.parent_path() / "font.txt"); ft) {
            std::string name;
            std::getline(ft, name);
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
            if (!name.empty() && std::filesystem::is_regular_file(assetsDir() / "fonts" / name, ec))
                want = assetsDir() / "fonts" / name;
        }
        if (want != fontFile_) {
            if (font.tex) r.destroyTexture(font.tex);
            font.tex = 0;
            font.tried = false;
            fontFile_ = want;
        }
        if (!font.tried) font.load(r, want);
    }
    if (!font.ready()) return;
    // the line's pieces: words (Latin, with their space), single characters (Japanese: no spaces to break at) and
    // ruby groups "{漢字|かんじ}" (the base, its furigana drawn small above it)
    struct Piece {
        std::string base, ruby;
    };
    std::vector<Piece> pieces;
    {
        // (Herbert) Latin text: the Japanese ellipsis (drawn mid-height by the font) as three dots
        std::string s = line->text;
        bool cjk = false;
        for (size_t k = 0; k < s.size();) cjk |= nextCodepoint(s, k) >= 0x3040;
        for (size_t at; !cjk && (at = s.find("\xe2\x80\xa6")) != std::string::npos;) s.replace(at, 3, "...");
        size_t i = 0;
        std::string word;
        auto flush = [&] {
            if (!word.empty()) pieces.push_back({word, {}});
            word.clear();
        };
        while (i < s.size()) {
            const size_t at = i;
            const uint32_t c = nextCodepoint(s, i);
            const std::string ch = s.substr(at, i - at);
            const size_t bar = c == '{' ? s.find('|', i) : std::string::npos;
            const size_t close = bar == std::string::npos ? std::string::npos : s.find('}', bar);
            if (close != std::string::npos) { // a ruby group
                flush();
                pieces.push_back({s.substr(i, bar - i), s.substr(bar + 1, close - bar - 1)});
                i = close + 1;
            } else if (c < 0x3000) { // Latin: a word, broken after its space
                word += ch;
                if (c == ' ') flush();
            } else { // CJK: one character a piece
                flush();
                pieces.push_back({ch, {}});
            }
        }
        flush();
        // names never broken across rows: "Mega " + "Man..." is one piece
        for (size_t k = 0; k + 1 < pieces.size(); ++k)
            if (pieces[k].ruby.empty() && pieces[k + 1].ruby.empty() && pieces[k].base == "Mega " &&
                pieces[k + 1].base.rfind("Man", 0) == 0) {
                pieces[k].base += pieces[k + 1].base;
                pieces.erase(pieces.begin() + std::ptrdiff_t(k + 1));
            }
    }
    // rows broken where the next piece does not fit, centred near the bottom of the 4:3 picture
    const float px = font.em(height * 0.032f), rubyPx = px * 0.5f,
                maxW = std::min(width, height * 4.0f / 3.0f) * 0.86f;
    auto pieceW = [&](const Piece& p) {
        return std::max(font.width(p.base, px), p.ruby.empty() ? 0.0f : font.width(p.ruby, rubyPx));
    };
    bool anyRuby = false;
    for (const Piece& p : pieces) anyRuby |= !p.ruby.empty();
    auto layout = [&](float limit) {
        std::vector<std::vector<Piece>> out(1);
        float rowW = 0;
        for (const Piece& p : pieces) {
            const float w = pieceW(p);
            if (!out.back().empty() && rowW + w > limit) {
                out.emplace_back();
                rowW = 0;
            }
            out.back().push_back(p);
            rowW += w;
        }
        return out;
    };
    std::vector<std::vector<Piece>> rows = layout(maxW);
    // (Herbert) balanced rows, as professional subtitles: the narrowest width that keeps the same number of rows, so
    // the last row is not a lone word
    if (rows.size() > 1) {
        float lo = maxW * 0.3f, hi = maxW;
        for (int k = 0; k < 18; ++k) {
            const float mid = (lo + hi) * 0.5f;
            if (layout(mid).size() <= rows.size()) hi = mid;
            else lo = mid;
        }
        rows = layout(hi);
    }
    const float lineH = px * (anyRuby ? 1.75f : 1.25f), edge = std::max(1.5f, px * 0.09f);
    const ui::Color white = ui::rgba(255, 255, 255, 255), black = ui::rgba(0, 0, 0, 255);
    float y = height * 0.93f - float(rows.size() - 1) * lineH;
    for (const auto& row : rows) {
        float w = 0;
        for (const Piece& p : row) w += pieceW(p);
        float x = (width - w) * 0.5f;
        for (const Piece& p : row) {
            const float pw = pieceW(p), bw = font.width(p.base, px);
            font.draw(cv, p.base, x + (pw - bw) * 0.5f, y, px, white, edge, black);
            if (!p.ruby.empty()) {
                const float rw = font.width(p.ruby, rubyPx);
                font.draw(cv, p.ruby, x + (pw - rw) * 0.5f, y - px * 1.05f, rubyPx, white, edge * 0.7f, black);
            }
            x += pw;
        }
        y += lineH;
    }
}

} // namespace od::app
