/* TextRenderer -- the game's glyph service: the object RenderGameFrame hands
 * to every overlay as its text `this` (scoreoverlay.cpp's arg4).
 *
 * PLACEHOLDER CLASS.  Every method below still calls the game's original at
 * its fixed address; nothing here is reimplemented yet.  It exists so that
 * callers already speak to the interface: when a method is replaced, only
 * its body in textrenderer.cpp changes, never a call site.  This is the
 * SoundManager arrangement (soundmanager.h), for the same reason.
 *
 * Nothing of the layout is known, so no field is declared and there is no
 * layout registration: every method is the original called through `this`.
 * The four entry points are __thiscall and callee-cleanup, confirmed by the
 * caller reading [ESP+0x1dc] as arg1 immediately after each call
 * (scoreoverlay.cpp's header comment).
 *
 * `fontA`/`fontB` are the two font pointers the caller pulls out of its own
 * object; they stay `void *` until something maps a font.
 */
#pragma once

#include <windows.h>

struct Direct3D;

class TextRenderer {
public:
    /* 0x00413690 RenderText -- left-aligned at (x, y). */
    void drawLeft(float x, float y, float cellW, float cellH, float scale,
                  const char *str, Direct3D *d3d, DWORD zero,
                  void *fontA, void *fontB);

    /* 0x00413e30 -- the same, right-aligned: x is the column's right edge. */
    void drawRight(float x, float y, float cellW, float cellH, float scale,
                   const char *str, Direct3D *d3d, DWORD zero,
                   void *fontA, void *fontB);

    /* 0x00413d00 DrawCenteredText -- centred on x. */
    void drawCentered(float x, float y, float cellW, float cellH, float scale,
                      const char *str, Direct3D *d3d, DWORD zero,
                      void *fontA, void *fontB);

    /* 0x00413d90 DrawBigText -- centred, two-colour, with an outline width
     * and a wobble amplitude; `n` is the animation counter the caller
     * derives from a double.  Used for "GAME OVER". */
    void drawBig(float x, float y, float cellW, float cellH, float scale,
                 const char *str, Direct3D *d3d, DWORD zero,
                 DWORD colourA, DWORD colourB,
                 float outline, float wobble, int n);

private:
    TextRenderer() = delete;   /* game-owned; only ever reached by pointer */
};
