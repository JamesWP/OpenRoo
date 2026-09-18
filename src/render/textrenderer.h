/* TextRenderer -- the game's bitmap-font glyph service: the object
 * RenderGameFrame hands to every overlay as its text `this` (scoreoverlay.cpp's
 * arg4).  It is a font atlas, not a general text engine: a SceneTexture whose
 * image is a `cols` x `rows` grid of fixed-size glyph cells, loaded from a
 * .fon by ReadBitmapFontFile (0x00413520, still the original).
 *
 * Three of the four entry points are ours as of ENDGAME_PLAN.md E1;
 * drawBig still calls the original, because the renderer it forwards to
 * (FUN_00413990) has not been replaced yet.
 *
 * ── The two arguments this header used to get wrong ─────────────────────────
 * It described arg8 as `zero` and args 9/10 as `fontA`/`fontB`, "the two font
 * pointers the caller pulls out of its own object".  Both were wrong, and the
 * decompile of 0x00413690 says so plainly:
 *
 *   - arg8 is the atlas's FIRST CHARACTER CODE.  RenderText computes each
 *     glyph's cell as `(unsigned char)(str[i] - arg8)`.  Every caller passes
 *     0, so the field was invisible; it is not a spare.
 *   - args 9 and 10 are two D3DCOLORs, written into the quad's DIFFUSE: arg9
 *     on the two top vertices, arg10 on the two bottom ones.  Text is drawn
 *     with a vertical gradient.  scoreoverlay.cpp's drawBig call site already
 *     passed 0xffffff00 / 0xffff0000 there -- yellow over red -- which is what
 *     put the question.  The Game fields the other call sites read (+0x6f914
 *     and friends) are colours too, and are renamed there.
 *
 * ── The layout ──────────────────────────────────────────────────────────────
 * Only the three fields the replaced functions read are named.  +0x00 is
 * untouched by all four entry points and stays unknown; the object's total
 * size is not asserted because no allocation site has been read yet.
 *
 * `cellW`/`cellH` are the quad's size in screen pixels and `spacing` is the
 * tracking: the pen advances by `cellW * spacing`, so a spacing below 1
 * overlaps successive cells.  That is why the centring wrappers subtract
 * `(len - (len-1)*(1-spacing)) * cellW` and not simply `len * cellW`.
 */
#pragma once

#include <windows.h>
#include "layout.h"
#include "texture.h"    /* SceneTexture -- the atlas at +0x0c */

struct Direct3D;

class __attribute__((packed)) TextRenderer {
public:
    static const int ORIGIN = 0;   /* our first byte is the game's +0x00 */

    /* 0x00413690 RenderText -- left-aligned: (x, y) is the first cell's
     * top-left corner. */
    void drawLeft(float x, float y, float cellW, float cellH, float spacing,
                  const char *str, Direct3D *d3d, char firstChar,
                  DWORD colourTop, DWORD colourBottom);

    /* 0x00413e30 DrawRightAlignedText -- x is the string's right edge. */
    void drawRight(float x, float y, float cellW, float cellH, float spacing,
                   const char *str, Direct3D *d3d, char firstChar,
                   DWORD colourTop, DWORD colourBottom);

    /* 0x00413d00 DrawCenteredText -- x is the string's centre. */
    void drawCentered(float x, float y, float cellW, float cellH, float spacing,
                      const char *str, Direct3D *d3d, char firstChar,
                      DWORD colourTop, DWORD colourBottom);

    /* 0x00413d90 DrawBigText -- centred like drawCentered, but forwarding to
     * the larger renderer at 0x00413990 (still the original), which takes
     * three more arguments: an outline width, a wobble amplitude and the
     * animation counter the caller derives from a double.  Used for
     * "GAME OVER".  STILL A CALLBACK. */
    void drawBig(float x, float y, float cellW, float cellH, float spacing,
                 const char *str, Direct3D *d3d, char firstChar,
                 DWORD colourTop, DWORD colourBottom,
                 float outline, float wobble, int n);

private:
    KAROO_LAYOUT_REGISTER(TextRenderer);

    void         *unknown00_;   /* +0x00  untouched by all four entry points */
    unsigned int  cols_;        /* +0x04  atlas columns; also the cell divisor */
    unsigned int  rows_;        /* +0x08  atlas rows                          */
    SceneTexture  atlas_;       /* +0x0c  .pTexture2 lands on +0x24           */

    TextRenderer() = delete;    /* game-owned; only ever reached by pointer */
};

KAROO_LAYOUT_CHECKS(TextRenderer)
{
    KAROO_LAYOUT_AT(cols_, 0x04);
    KAROO_LAYOUT_AT(rows_, 0x08);
    KAROO_LAYOUT_AT(atlas_, 0x0c);
    /* The one the glyph loop actually dereferences, and the reason the atlas
     * is embedded by value rather than named as a bare pointer. */
    static_assert(0x0c + offsetof(SceneTexture, pTexture2) == 0x24,
                  "TextRenderer: atlas texture must land on the game's +0x24");
}

/* ─── Exports (COHESION_PLAN.md template 10) ──────────────────────────────
 *
 * patch.py's CALL_PATCHES redirects every E8 site to these; the originals are
 * UD2-stubbed.  Signatures are read off each original's `RET n`, not off the
 * decompiler's parameter list: all three are __thiscall with ten stack
 * arguments and `RET 0x28`.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_RenderText(TextRenderer *self, float x, float y, float cellW, float cellH,
                float spacing, const char *str, Direct3D *d3d, char firstChar,
                DWORD colourTop, DWORD colourBottom);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DrawCentered(TextRenderer *self, float x, float y, float cellW,
                  float cellH, float spacing, const char *str, Direct3D *d3d,
                  char firstChar, DWORD colourTop, DWORD colourBottom);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DrawRightAligned(TextRenderer *self, float x, float y, float cellW,
                      float cellH, float spacing, const char *str,
                      Direct3D *d3d, char firstChar,
                      DWORD colourTop, DWORD colourBottom);
