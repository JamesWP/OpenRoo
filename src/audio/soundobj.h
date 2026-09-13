/* soundobj.cpp -- AcquireObjectSoundBuffersForIndex 0x0041cf50: attach a
 * foe's sounds.  Callers: GameTick's runtime foe spawner and
 * InitLevelBasedSounds. */
#pragma once

class Game;

/* __thiscall(Game *, foe ID in the low byte), RET 4.  Returns 1 in AL. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AcquireObjectSoundBuffersForIndex(Game *self, unsigned int objArg);
