#include "audio/haptics.hpp"

namespace od::audio {

namespace {

struct HapticSound {
    uint16_t id;
    float weight;
    bool heavy = false;
};

// game/player/player_state.hpp's PlayerSound, game/shots/*, game/door_leaves, game/exit_transitions, game/explosions
constexpr HapticSound kSounds[] = {
    // the player's body
    {0x90, 0.7f}, // step
    {0x91, 0.7f}, // footstep
    {0x92, 0.6f}, // jump
    {0x93, 1.0f, true}, // land
    {0x94, 0.6f}, // grab
    {0x95, 0.8f}, // kick
    {0x96, 1.0f, true}, // hurt
    {0x97, 1.0f, true}, // knocked down
    {0x98, 0.9f, true}, // bounce
    {0x99, 1.0f, true}, // fall landing
    {0x9c, 0.6f}, // side step
    {0x9d, 0.4f}, // jet skates' engine (loop)
    {0x9e, 0.6f}, // skate turn
    {0x9f, 0.7f}, // skate stop
    {0xa0, 0.6f}, // hop
    {0xa1, 0.6f}, // lift
    {0xa2, 0.6f}, // throw
    // the weapons
    {0x8f, 0.6f},  // deflected shot
    {0x9a, 0.8f},  // buster shot (and the hunter seeker's crystal)
    {0xbe, 1.0f, true},  // hyper shell
    {0xbf, 0.8f},  // homing missile
    {0xf4, 0.8f},  // orbit buster shot
    {0xf5, 1.0f, true},  // buster cannon
    {0xf6, 1.0f, true},  // its burst
    {0xfa, 0.7f},  // ground crawler
    {0xfb, 0.5f},  // charge
    {0xfd, 0.7f},  // blade arm swing
    {0xfe, 0.7f},  // its swing back
    {0xff, 0.7f},  // reflector
    {0x100, 0.6f}, // reflector sprayed
    {0x101, 0.5f}, // shining laser charge
    {0x102, 1.0f, true}, // its beam
    {0x103, 0.5f}, // its fade
    {0x104, 0.7f}, // crusher throw
    {0x105, 1.0f, true}, // its orb burst
    {0x106, 0.5f}, // its orb hum
    {0x107, 0.6f}, // its orb end
    {0x108, 0.8f}, // spread buster
    {0x109, 0.9f}, // machine gun
    {0x10a, 0.3f}, // its casings
    {0x10b, 0.4f}, // charge hum
    {0x10c, 0.9f}, // charge wave
    {0x10d, 0.5f}, // extinguisher spray
    {0x10e, 0.4f}, // its stop
    {0x14e, 0.6f}, // hunter seeker throw
    {0x305, 0.8f}, // drill arm start
    {0x306, 0.7f}, // its run
    {0x307, 0.6f}, // its stop
    {0x308, 1.0f, true}, // its grind
    // the doors
    {0xb8, 0.8f},  // a hinged leaf opens
    {0xb9, 0.8f},  // it bounces closed
    {0xba, 0.8f},  // sliding leaves open
    {0xbb, 0.8f},  // they close
    {0xbc, 1.0f, true},  // they stop
    {0xbd, 0.8f},  // they stop open
    {0x13f, 0.8f, true}, // a door carrying the player down
    // blasts and hits
    {0x8a, 1.0f, true}, // blast
    {0x8b, 1.0f, true}, // loud blast
    {0x8d, 0.7f}, // hit
    {0x8e, 0.9f, true}, // heavy hit
};

} // namespace

HapticFeel hapticFeel(uint16_t id) {
    for (const HapticSound& s : kSounds)
        if (s.id == id) return {s.weight, s.heavy};
    return {};
}

} // namespace od::audio
