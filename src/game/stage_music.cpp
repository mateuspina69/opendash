// The stage's music, one case per stage function [0x8d3808 + stage * 4] (see stage_music.hpp). The music ids are named
// by the stages that play them.
#include "game/stage_music.hpp"

#include "game/audio_logic.hpp"
#include "game/game_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

// the musics (sound ids of the music descriptors; none: kNoMusic)
constexpr uint32_t kMusicTravelMap = 0x45;      // ST01
constexpr uint32_t kMusicFlutter = 0x3b;        // ST04..ST07
constexpr uint32_t kMusicTown = 0x15;           // ST08 / ST09
constexpr uint32_t kMusicSt0bRoom1 = 0x1c;      // ST0B sub 1
constexpr uint32_t kMusicSt0cSt47 = 0x16;       // ST0C, ST47
constexpr uint32_t kMusicSt0dSt0e = 0x17;       // ST0D, ST0E
constexpr uint32_t kMusicSt0eEventDone = 0x47;  // ST0E at story 0xf, its events done / started
constexpr uint32_t kMusicSt0eEvent = 0x46;
constexpr uint32_t kMusicFirstRuin = 0x19;      // ST0F
constexpr uint32_t kMusicIslandClear = 0x20;    // ST10 / ST11 / ST1D after the storm
constexpr uint32_t kMusicIslandStorm = 0x1d;    // ... in the storm
constexpr uint32_t kMusicMandaRuin = 0x10;      // ST12..ST14
constexpr uint32_t kMusicSt15St16 = 0xd;
constexpr uint32_t kMusicNinoField = 0x23;      // ST17..ST1B
constexpr uint32_t kMusicNinoFieldAlert = 0x24; // ... while the story's attack runs
constexpr uint32_t kMusicFireMission = 0x1b;    // ST1E
constexpr uint32_t kMusicSquare = 8;            // ST1F / ST22 / ST23
constexpr uint32_t kMusicCalbaniaTown = 1;      // ST20
constexpr uint32_t kMusicSt21 = 7;
constexpr uint32_t kMusicPokte = 0x2b;          // ST24
constexpr uint32_t kMusicPokteTank = 0x2c;      // ST25 from story 4
constexpr uint32_t kMusicSaulKada = 0x3c;       // ST26 / ST27 / ST28 / ST34, ST33
constexpr uint32_t kMusicSaulKadaRuin = 0x31;   // ST29 / ST2A..ST2C / ST3B / ST3C (a music with its fade)
constexpr uint32_t kMusicSaulKadaLate = 0x33;   // ST29 / ST3C from story 0xd
constexpr uint32_t kMusicSubruin = 0x34;        // ST2D..ST2F
constexpr uint32_t kMusicQuiz = 0x2e;           // ST32
constexpr uint32_t kMusicSt33 = 0xf, kMusicSt33Start = 0;
constexpr uint32_t kMusicNinoRuin = 0x36;       // ST35..ST38
constexpr uint32_t kMusicSt3dSt3e = 0x48;
constexpr uint32_t kMusicSt3f = 0x52;
constexpr uint32_t kMusicSt40 = 0x49;           // a music with its fade
constexpr uint32_t kMusicSt41St42 = 0x4a;
constexpr uint32_t kMusicLateField = 0x4b;      // ST43..ST45, ST58
constexpr uint32_t kMusicSt48 = 0x39;
constexpr uint32_t kMusicSt4aSt55 = 0x40;
constexpr uint32_t kMusicSt4c = 0x4d;
constexpr uint32_t kMusicSubruinA = 0x41, kMusicSubruinB = 0x42, kMusicSubruinC = 0x43, kMusicSubruinD = 0x44;
// the storm's ambience (ST10 / ST11 / ST1D through story 1)
constexpr uint16_t kSoundStormAmbience = 0x17e;
// the field encounters' battle musics
constexpr uint8_t kBattleMusicRuin = 0x18;   // ST0D / ST0E
constexpr uint8_t kBattleMusicSquare = 9;    // ST22
constexpr uint8_t kBattleMusicSt48 = 0x3a;
// the stage sound flags (kSoundStage*)
constexpr uint32_t kStageSoundPlain = 0;
constexpr uint32_t kStageSoundIsland = kSoundStageVoice15Sfx;
constexpr uint32_t kStageSoundNinoRuin = kSoundStageVoice15Sfx | kSoundStageUnreadBit0;
constexpr uint32_t kStageSoundNinoRuinMachine = kStageSoundNinoRuin | kSoundStageSkipEmptyCall;
constexpr uint32_t kStageSoundSubruinD = kSoundStageSkipEmptyCall | kSoundStageUnreadBit0;
constexpr uint32_t kStageSoundSilentUnread = kSoundStageSilent | kSoundStageUnreadBit0;
constexpr uint32_t kStageSoundBattleRuin = soundStageBattleMusic(kBattleMusicRuin);
constexpr uint32_t kStageSoundBattleSquare = soundStageBattleMusic(kBattleMusicSquare);
constexpr uint32_t kStageSoundBattleSt48 = soundStageBattleMusic(kBattleMusicSt48);
// the musics with their fade (exe 0x5d52f0): the argument the channel start receives unread, by stage; the fade entry
// the flags (kMusicRequest*) set
constexpr uint16_t kRuinMusicChannelArg = 0xfe, kSubruinMusicChannelArg = 0x103, kSt40MusicChannelArg = 1,
                   kMandaMusicChannelArg = 0x1e;
constexpr uint16_t kStageMusicFadeEntry = 2;
constexpr uint32_t kMusicRequestQuiet = 0, kMusicRequestQuietUnread = kMusicRequestUnreadBit5;

// story flags
constexpr uint32_t kFlagFlutterLanded = 0x5e4;             // ST08 at story 0: the Flutter came down
constexpr uint32_t kFlagSt0eEventDone = 0x5a1, kFlagSt0eEvent = 0x5a0;
constexpr uint32_t kFlagNinoAttackA = 0x25, kFlagNinoAttackB = 0x2c, kFlagNinoAttackOver = 0x2e,
                   kFlagNinoAttackC = 0x2b;
constexpr uint32_t kFlagSt19ShipA = 0x580, kFlagSt19ShipB = 0x581;
constexpr uint32_t kFlagSquareFieldEnds = 0x586, kFlagSquareGateBroken = 0x590;
constexpr uint32_t kFlagSt29RuinA = 0x5c0, kFlagSt29RuinB = 0x5b0;
constexpr uint32_t kFlagSt2aRuin = 0x5bf, kFlagSt2bRuin = 0x58b, kFlagSt3bRuin = 0x5a1;
constexpr uint32_t kFlagSt33Late = 0x3ed, kFlagSt33Started = 0x48;
constexpr uint32_t kFlagSt36Quiet = 0x90, kFlagSt37Quiet = 0x91, kFlagSt38Quiet = 0x92;
constexpr uint32_t kFlagSt47Scene = 0x5b0;

// the Saul Kada ruin's music with its fade, the request's flags given
void ruinMusic(uint32_t request) {
    soundPlayMusicWithFade(kMusicSaulKadaRuin, kRuinMusicChannelArg, kStageMusicFadeEntry, request);
}
// a ruin room's music request: in the music's room a held dip once the flag is set (else the full volume), quiet in
// the others
uint32_t ruinRoomRequest(bool musicRoom, uint32_t flag) {
    if (!musicRoom) return kMusicRequestQuiet;
    return storyFlag(flag) ? kMusicRequestHeldDip : kMusicRequestFullVolume;
}

} // namespace

void stageMusic() {
    const GameState& g = gameState();
    const uint8_t story = g.story, sub = g.room;
    switch (g.stage) {
    case 0x01: stageSoundStart(kMusicTravelMap, kStageSoundPlain); break; // exe 0x5d6490
    case 0x04: case 0x05: case 0x06: case 0x07: stageSoundStart(kMusicFlutter, kStageSoundPlain); break;
    case 0x08: // exe 0x5d6fd0
        stageSoundStart(story == 0 && !storyFlag(kFlagFlutterLanded) ? kNoMusic : kMusicTown, kStageSoundPlain);
        break;
    case 0x09: stageSoundStart(kMusicTown, kStageSoundPlain); break; // exe 0x5d71a0
    case 0x0b: stageSoundStart(sub == 1 ? kMusicSt0bRoom1 : kNoMusic, kStageSoundPlain); break; // exe 0x5d74c0
    case 0x0c: stageSoundStart(kMusicSt0cSt47, kStageSoundPlain); break; // exe 0x5d7670
    case 0x0d: stageSoundStart(kMusicSt0dSt0e, kStageSoundBattleRuin); break; // exe 0x5d77e0
    case 0x0e: // exe 0x5d7960
        if (story == 0xf && storyFlag(kFlagSt0eEventDone)) stageSoundStart(kMusicSt0eEventDone, kStageSoundPlain);
        else if (story == 0xf && storyFlag(kFlagSt0eEvent)) stageSoundStart(kMusicSt0eEvent, kStageSoundPlain);
        else stageSoundStart(kMusicSt0dSt0e, kStageSoundBattleRuin);
        break;
    case 0x0f: stageSoundStart(kMusicFirstRuin, kSoundStageUnreadBit0); break; // exe 0x5d7b30
    case 0x10: case 0x11: case 0x1d: // exe 0x5d7cb0 / 0x5d7eb0 / 0x5d9460
        if (story >= 2) {
            stageSoundStart(kMusicIslandClear, kStageSoundIsland);
        } else {
            stageSoundStart(kMusicIslandStorm, kStageSoundIsland);
            soundPlayFlatForced(kSoundStormAmbience); // exe 0x5d5260
        }
        break;
    case 0x12: case 0x13: stageSoundStart(kMusicMandaRuin, kSoundStageUnreadBit0); break; // exe 0x5d80b0 / 0x5d8230
    case 0x14: // exe 0x5d83b0
        stageSoundStart(kNoMusic, kSoundStageUnreadBit0);
        soundPlayMusicWithFade(kMusicMandaRuin, kMandaMusicChannelArg, kStageMusicFadeEntry, kMusicRequestFullVolume);
        break;
    case 0x15: case 0x16: stageSoundStart(kMusicSt15St16, kStageSoundPlain); break; // exe 0x5d8540 / 0x5d8710
    case 0x17: // exe 0x5d88e0
        stageSoundStart(story < 7                                              ? kNoMusic
                        : story == 9 && storyFlag(kFlagNinoAttackB)            ? kMusicNinoFieldAlert
                                                                               : kMusicNinoField,
                        kStageSoundPlain);
        break;
    case 0x18: // exe 0x5d8b00
        stageSoundStart(kNoMusic, kStageSoundPlain);
        if (!(story >= 9 && storyFlag(kFlagNinoAttackB)) &&
            !(story == 6 ? storyFlag(kFlagNinoAttackA) : storyFlag(kFlagNinoAttackB)))
            stageSoundStart(kMusicNinoField, kStageSoundPlain);
        break;
    case 0x19: { // exe 0x5d8d70
        uint32_t id = kMusicNinoField;
        if (sub >= 2) id = kNoMusic;
        else if (story != 6) {
            if (story == 9 && storyFlag(kFlagNinoAttackB) && !storyFlag(kFlagNinoAttackOver)) id = kMusicNinoFieldAlert;
        } else if (!storyFlag(kFlagNinoAttackC) &&
                   (storyFlag(kFlagNinoAttackA) || (storyFlag(kFlagSt19ShipA) && storyFlag(kFlagSt19ShipB)))) {
            id = kMusicNinoFieldAlert;
        }
        stageSoundStart(id, kStageSoundPlain);
        break;
    }
    case 0x1a: case 0x1b: // exe 0x5d8f90 / 0x5d9140
        stageSoundStart((story == 6 && storyFlag(kFlagNinoAttackA)) || (story == 9 && storyFlag(kFlagNinoAttackB))
                            ? kMusicNinoFieldAlert
                            : kMusicNinoField,
                        kStageSoundPlain);
        break;
    case 0x1e: stageSoundStart(kMusicFireMission, kStageSoundPlain); break; // exe 0x5d9670
    case 0x1f: // exe 0x5d97e0
        stageSoundStart(story >= 8 && (story != 8 || storyFlag(kFlagSquareFieldEnds)) &&
                                (story != 9 || storyFlag(kFlagSquareGateBroken))
                            ? kMusicSquare
                            : kNoMusic,
                        kStageSoundPlain);
        break;
    case 0x20: stageSoundStart(kMusicCalbaniaTown, kStageSoundPlain); break; // exe 0x5d9a20
    case 0x21: stageSoundStart(kMusicSt21, kStageSoundPlain); break; // exe 0x5d9ba0
    case 0x22: stageSoundStart(kMusicSquare, kStageSoundBattleSquare); break; // exe 0x5d9d50
    case 0x23: stageSoundStart(kMusicSquare, kStageSoundPlain); break; // exe 0x5d9f00
    case 0x24: stageSoundStart(kMusicPokte, kStageSoundPlain); break; // exe 0x5da0b0
    case 0x25: stageSoundStart(story >= 4 ? kMusicPokteTank : kNoMusic, kStageSoundPlain); break; // exe 0x5da2b0
    case 0x26: case 0x27: case 0x34: // exe 0x5da470 / 0x5da5f0 / 0x5dbb80
        stageSoundStart(kMusicSaulKada, kSoundStageUnreadBit0);
        break;
    case 0x28: stageSoundStart(kMusicSaulKada, kStageSoundSilentUnread); break; // exe 0x5da790
    case 0x29: // exe 0x5da930
        if (story <= 0xc) {
            stageSoundStart(kNoMusic, kStageSoundPlain);
            ruinMusic(storyFlag(sub == 0 ? kFlagSt29RuinA : kFlagSt29RuinB) ? kMusicRequestQuietUnread
                                                                             : kMusicRequestFullVolume);
        } else {
            stageSoundStart(kMusicSaulKadaLate, kStageSoundPlain);
        }
        break;
    case 0x2a: // exe 0x5dab40
        stageSoundStart(kNoMusic, kStageSoundPlain);
        ruinMusic(ruinRoomRequest(sub == 1, kFlagSt2aRuin));
        break;
    case 0x2b: // exe 0x5dad10
        stageSoundStart(kNoMusic, kStageSoundPlain);
        ruinMusic(ruinRoomRequest(sub == 1, kFlagSt2bRuin));
        break;
    case 0x2c: // exe 0x5daef0
        stageSoundStart(kNoMusic, kStageSoundPlain);
        ruinMusic(kMusicRequestQuiet);
        break;
    case 0x2d: case 0x2e: stageSoundStart(kMusicSubruin, kSoundStageUnreadBit0); break; // exe 0x5db070 / 0x5db200
    case 0x2f: // exe 0x5db390
        stageSoundStart(kNoMusic, kStageSoundSilentUnread);
        soundPlayMusicWithFade(kMusicSubruin, kSubruinMusicChannelArg, kStageMusicFadeEntry, kMusicRequestFullVolume);
        break;
    case 0x32: stageSoundStart(kMusicQuiz, kStageSoundPlain); break; // exe 0x5db860
    case 0x33: // exe 0x5db9d0
        stageSoundStart(storyFlag(kFlagSt33Late)      ? kMusicSaulKada
                        : storyFlag(kFlagSt33Started) ? kMusicSt33
                                                      : kMusicSt33Start,
                        kSoundStageUnreadBit0);
        break;
    case 0x35: stageSoundStart(sub == 2 ? kMusicNinoRuin : kNoMusic, kSoundStageUnreadBit0); break; // exe 0x5dbf80
    case 0x36: // exe 0x5dc110
        stageSoundStart(kMusicNinoRuin,
                        storyFlag(kFlagSt36Quiet) && sub != 0xd ? kStageSoundNinoRuinMachine : kStageSoundNinoRuin);
        break;
    case 0x37: // exe 0x5dc2c0
        stageSoundStart(kMusicNinoRuin,
                        !storyFlag(kFlagSt37Quiet) && sub != 0xf ? kStageSoundNinoRuinMachine : kStageSoundNinoRuin);
        break;
    case 0x38: // exe 0x5dc470
        stageSoundStart(kMusicNinoRuin, !storyFlag(kFlagSt38Quiet) ? kStageSoundNinoRuinMachine : kStageSoundNinoRuin);
        break;
    case 0x3b: // exe 0x5dc900
        stageSoundStart(kNoMusic, kStageSoundPlain);
        ruinMusic(ruinRoomRequest(sub == 0, kFlagSt3bRuin));
        break;
    case 0x3c: // exe 0x5dcae0
        if (story <= 0xc) {
            stageSoundStart(kNoMusic, kStageSoundPlain);
            ruinMusic(kMusicRequestHeldDip);
        } else {
            stageSoundStart(kMusicSaulKadaLate, kStageSoundPlain);
        }
        break;
    case 0x3d: case 0x3e: stageSoundStart(kMusicSt3dSt3e, kStageSoundPlain); break; // exe 0x5dccb0 / 0x5dce50
    case 0x3f: stageSoundStart(kMusicSt3f, kStageSoundPlain); break; // exe 0x5dd0c0
    case 0x40: // exe 0x5dd260
        stageSoundStart(kNoMusic, kStageSoundPlain);
        soundPlayMusicWithFade(kMusicSt40, kSt40MusicChannelArg, kStageMusicFadeEntry, kMusicRequestHeldDip);
        break;
    case 0x41: case 0x42: stageSoundStart(kMusicSt41St42, kSoundStageUnreadBit0); break; // exe 0x5dd430 / 0x5dd5b0
    case 0x43: case 0x44: case 0x45: case 0x58: // exe 0x5dd730..
        stageSoundStart(kMusicLateField, kSoundStageUnreadBit0);
        break;
    case 0x46: stageSoundStart(kNoMusic, kSoundStageBanksMuted); break; // exe 0x5ddbb0
    case 0x47: // exe 0x5ddd10
        stageSoundStart(story == 1 && !storyFlag(kFlagSt47Scene) ? kNoMusic : kMusicSt0cSt47, kStageSoundPlain);
        break;
    case 0x48: stageSoundStart(kMusicSt48, kStageSoundBattleSt48); break; // exe 0x5ddea0
    case 0x4a: case 0x55: stageSoundStart(kMusicSt4aSt55, kSoundStageUnreadBit0); break; // exe 0x5de1b0 / 0x5df540
    case 0x4c: stageSoundStart(kMusicSt4c, kSoundStageUnreadBit0); break; // exe 0x5de500
    case 0x4d: stageSoundStart(kMusicSubruinA, kSoundStageUnreadBit0); break; // exe 0x5de680
    case 0x4e: case 0x51: stageSoundStart(kMusicSubruinB, kSoundStageUnreadBit0); break; // exe 0x5de8a0 / 0x5def60
    case 0x4f: stageSoundStart(kMusicSubruinC, kSoundStageUnreadBit0); break; // exe 0x5deac0
    case 0x50: stageSoundStart(kMusicSubruinD, kStageSoundSubruinD); break; // exe 0x5ded00
    default: // 00 02 03 0A 1C 30 31 39 3A 49 4B 52 53 54 56 57 59 5A 5B 5C: no music
        stageSoundStart(kNoMusic, kStageSoundPlain);
        break;
    }
}

} // namespace od::port
