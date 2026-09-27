/* A level's asset load, done on its first frame: when the level builder sets
 * the Game's "prepare assets" flag, the frame renderer calls this, clears the
 * flag and draws nothing that frame. */
#pragma once

extern "C" __declspec(dllexport) void __cdecl LevelEntry_PrepareAssets(void);
