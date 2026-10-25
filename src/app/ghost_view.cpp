// The ghost players as the host draws them (see ghost_view.hpp).
#include "app/ghost_view.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace od::app {

void GhostView::setRemote(int slot, const od::port::RemotePlayerState& s) {
    Slot& g = slots_.at(size_t(slot));
    g.remote = true;
    g.remoteState = s;
}

void GhostView::clearRemote(int slot) {
    Slot& g = slots_.at(size_t(slot));
    g.remote = false;
    g.shown = false;
    set_.clear(slot);
}

const char* GhostView::stateText(State st, od::ghost::Hidden h) {
    switch (st) {
    case State::Shown: return "shown";
    case State::NoRemote: return "no remote state";
    case State::NotFieldPlay: return "not in field play";
    case State::OtherRoom: return "other room";
    case State::LoadedDataDiffers: return "loaded data differs (other stage files or script banks)";
    case State::HiddenByPose: return od::ghost::hiddenReason(h);
    case State::LookNotBuilt: return "look not built";
    }
    return "";
}

void GhostView::update(const GhostRoomUpload& room, uint64_t localFingerprint, const od::ghost::ReadFile& read,
                       const SliceSink& sink) {
    const od::port::GhostRoom here = od::port::ghostRoomOf();
    std::array<State, od::ghost::kMaxGhosts> st{};
    std::array<od::ghost::Hidden, od::ghost::kMaxGhosts> hidden{};
    std::vector<od::port::PlayerLook> wanted; // the looks of the posed slots: the set GhostLooks keeps
    for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) {
        Slot& g = slots_[size_t(slot)];
        const od::port::GhostRoom& at = g.remoteState.room;
        State& s = st[size_t(slot)];
        if (!g.remote) s = State::NoRemote;
        else if (!here.fieldPlay || !at.fieldPlay) s = State::NotFieldPlay;
        else if (at.stage != here.stage || at.sub != here.sub) s = State::OtherRoom;
        else if (g.remoteState.fingerprint != localFingerprint) s = State::LoadedDataDiffers;
        else if ((hidden[size_t(slot)] = set_.pose(slot, room.generation, g.remoteState.snapshot, read)) !=
                 od::ghost::Hidden::No)
            s = State::HiddenByPose;
        else {
            s = State::Shown; // once its look is built (below)
            const od::port::PlayerLook& look = g.remoteState.snapshot.look;
            if (std::find(wanted.begin(), wanted.end(), look) == wanted.end()) wanted.push_back(look);
        }
    }
    looks_.sync(wanted, room.generation, room.slices, *room.vram, read, sink);
    generation_ = room.generation;
    shift_ = room.shift;
    for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) {
        Slot& g = slots_[size_t(slot)];
        State s = st[size_t(slot)];
        const od::port::PlayerLook& l = g.remoteState.snapshot.look;
        std::string failure;
        if (s == State::Shown && !looks_.find(l)) {
            s = State::LookNotBuilt;
            failure = looks_.failure(l);
        }
        g.shown = s == State::Shown;
        if (s != g.state || failure != g.lookFailure) {
            const od::port::GhostRoom& at = g.remoteState.room;
            SDL_Log("ghost %d: %s%s%s (remote ST%02X sub %u, local ST%02X sub %u, look kind %u costume %u/%u "
                    "weapon %d)",
                    slot, stateText(s, hidden[size_t(slot)]), failure.empty() ? "" : ": ", failure.c_str(),
                    unsigned(at.stage), unsigned(at.sub), unsigned(here.stage), unsigned(here.sub), unsigned(l.kind),
                    unsigned(l.costume), unsigned(l.costumeVariant), int(l.specialWeapon));
            g.state = s;
            g.hidden = hidden[size_t(slot)];
            g.lookFailure = failure;
        }
    }
}

void GhostView::appendDynamic(std::vector<od::DynamicObject>& dyn, uint32_t roomGeneration) const {
    if (roomGeneration != generation_) return; // a room upload since the last update: the looks' slices are gone
    for (int slot = 0; slot < od::ghost::kMaxGhosts; ++slot) {
        const Slot& g = slots_[size_t(slot)];
        if (!g.shown) continue;
        const CharacterMesh* cm = looks_.find(g.remoteState.snapshot.look);
        const od::port::Player* pl = set_.player(slot);
        if (!cm || !pl) continue;
        dyn.push_back({od::objectId(od::kKindPlayer, 1u + uint32_t(slot)), buildCharacter(shift_, *cm, *pl)});
    }
}

const od::port::RemotePlayerState* GhostView::shownState(int slot) const {
    const Slot& s = slots_.at(size_t(slot));
    return s.remote && s.shown ? &s.remoteState : nullptr;
}

std::string GhostView::status(int slot) const {
    const Slot& g = slots_.at(size_t(slot));
    std::string text = stateText(g.state, g.hidden);
    if (!g.lookFailure.empty()) text += ": " + g.lookFailure;
    return text;
}

} // namespace od::app
