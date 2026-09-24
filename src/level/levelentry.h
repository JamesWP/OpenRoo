/* levelentry.h -- PrepareLevelAssetsOnEntry 0x00426c50 (was misnamed
 * UpdatePlayerCamera), ENDGAME_PLAN.md E4.
 *
 * RenderGameFrame's first act: when Game+0x173584 is set (by the level
 * builder, levelsetup.cpp) it calls this, clears the flag, and returns
 * without drawing.  So a level's first frame is its asset load. */
#pragma once

/* One caller: RenderGameFrame 0x426f6f. */
extern "C" __declspec(dllexport) void __cdecl LevelEntry_PrepareAssets(void);
