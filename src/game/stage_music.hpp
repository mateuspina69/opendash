// The stage's music: the sound part of the per-stage stage setup [0x8d3808 + stage * 4] (exe 0x5d5a80, once per stage
// load) -- the music exe 0x5d3cf0 starts (by the story, the story flags, the sub-room) with the stage's sound flags,
// and the ambience some stages add (a music with its fade, exe 0x5d52f0; a forced sound, exe 0x5d5260). The rest of
// those functions (the dword writes, the encounter slots, the encounter layouts) is game/stage_setup's. Transcribed
// from the 93 functions (the branchy ones checked against the disassembly); exe 0x5d4f30, called by ST36 / 37 / 38 /
// 50, is an empty ret.
#pragma once

namespace od::port {

// for the stage, the sub-room, the story and the story flags of the game state; the stage's SFX banks / MP3 slots
// must be loaded (the host calls it after binding them)
void stageMusic();

} // namespace od::port
