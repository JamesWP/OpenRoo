/* TextRenderer -- the game's bitmap-font glyph service: the object
 * RenderGameFrame hands to every overlay as its text `this` (scoreoverlay.cpp's
 * arg4).  It is a font atlas, not a general text engine: a SceneTexture whose
 * image is a `cols` x `rows` grid of fixed-size glyph cells, loaded from a
 * .fon by ReadBitmapFontFile (0x00413520), which is ours as of ENDGAME_PLAN.md E1.
 *
 * All five entry points are ours as of ENDGAME_PLAN.md E1 -- drawBig
 * (0x00413d90) and the wobbling renderer it forwards to, DrawWobbleGlyphRow
 * (0x00413990), went together, because replacing the wrapper alone would have
 * moved the callback rather than retired it.
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
 * ── And two more it got wrong, on drawBig ───────────────────────────────────
 * The same header called drawBig's args 11 and 12 `outline` and `wobble`, "an
 * outline width and a wobble amplitude".  Neither is an outline: 0x00413990
 * draws one quad per glyph and no second pass, so there is nothing to outline.
 *
 *   - arg11 is the sine's AMPLITUDE, in pixels.
 *   - arg12 is the sine's RATE: it multiplies the animation counter `n` once,
 *     before the loop, to make the wave's phase.
 *
 * scoreoverlay.cpp's only call site passes `S * 3.0f, 0.01f` -- three virtual
 * units and a per-tick rate -- which is what a (amplitude, rate) pair looks
 * like and not what an (outline, amplitude) pair does.
 *
 * ── The layout ──────────────────────────────────────────────────────────────
 * +0x00 is the vtable (one slot, the scalar dtor 0x004134f0).  The two
 * instances are globals, 0x004e0480 and 0x004e02e8, built by a static-init
 * thunk -- 0x28 bytes each, the atlas ending the object.
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

    /* 0x00413520 ReadBitmapFontFile -- load a .fon: line 1 is the atlas's
     * texture path, line 2 the column count, line 3 the row count.  Returns
     * non-zero in the low byte on success.  Both shipped fonts are three
     * lines long (fonts/FONT1.FON is textures\font2.tga, 16, 16;
     * fonts/NUMBERS.FON is textures\numbers.tga, 4, 3). */
    unsigned int load(const char *path, Direct3D *d3d);

    /* 0x00413d90 DrawBigText -- the centring wrapper around drawWobble, with
     * exactly drawCentered's arithmetic: x -= width * 0.5f, then a tail call
     * with all twelve remaining arguments untouched.  Used for "GAME OVER". */
    void drawBig(float x, float y, float cellW, float cellH, float spacing,
                 const char *str, Direct3D *d3d, char firstChar,
                 DWORD colourTop, DWORD colourBottom,
                 float amplitude, float rate, int n);

    /* 0x00413990 DrawWobbleGlyphRow -- drawLeft's sibling, and the only other
     * glyph loop in the binary.  Three differences, all of them in the quad:
     *
     *   - `y` is the row's CENTRE, not its top.  Each quad spans y +/- dy.
     *   - dy = cellH * 0.5f + amplitude * sin(n * rate + 2 * i), so every
     *     glyph breathes vertically about that centre, two radians out of
     *     phase with the one before it.
     *   - nothing else: same FVF 0x1C4, same z/rhw (0.1f / 10.0f), same
     *     specular 0xff000000, same top/bottom DIFFUSE gradient, same
     *     SetTexture + three SetRenderState calls ahead of the empty test,
     *     same strlen-per-iteration loop, same pen advance of cellW *
     *     spacing.
     *
     * Its only reference in the whole binary is drawBig's call. */
    void drawWobble(float x, float y, float cellW, float cellH, float spacing,
                    const char *str, Direct3D *d3d, char firstChar,
                    DWORD colourTop, DWORD colourBottom,
                    float amplitude, float rate, int n);

    /* 0x00413eb0 DrawTextPanel -- a multi-line caption over two full-width
     * backdrop strips (textrenderer.cpp). */
    void drawPanel(float x, float y, float cellW, float cellH, float spacing,
                   float lineH, const char *str, Direct3D *d3d,
                   DWORD colourTop, DWORD colourBottom,
                   SceneTexture *panelTex, SceneTexture *frameTex);

    /* 0x004134d0 ctor / 0x00413510 dtor body (textrenderer.cpp). */
    void construct();
    void destruct();

private:
    KAROO_LAYOUT_REGISTER(TextRenderer);

    const void   *vtable_;      /* +0x00  our one-slot table (game's 0x45d3a8) */
    unsigned int  cols_;        /* +0x04  atlas columns; also the cell divisor */
    unsigned int  rows_;        /* +0x08  atlas rows                          */
    SceneTexture  atlas_;       /* +0x0c  .pTexture2 lands on +0x24           */

    TextRenderer() = delete;    /* game-owned; only ever reached by pointer */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    SceneTexture* atlas() { return &atlas_; }
#pragma GCC diagnostic pop

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

/* 0x00413520 is __thiscall with `RET 8` -- two stack arguments, the path and
 * the Direct3D the atlas is created against. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Text_LoadFont(TextRenderer *self, const char *path, Direct3D *d3d);

/* Both of the big-text pair are __thiscall with `RET 0x34` -- thirteen stack
 * arguments, the ten the other three take plus (amplitude, rate, n).  The
 * inner one needs an export of its own only so that patch.py can UD2-stub the
 * original with nothing left reaching it; the game never calls it directly. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DrawBigText(TextRenderer *self, float x, float y, float cellW,
                 float cellH, float spacing, const char *str, Direct3D *d3d,
                 char firstChar, DWORD colourTop, DWORD colourBottom,
                 float amplitude, float rate, int n);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DrawWobbleGlyphRow(TextRenderer *self, float x, float y, float cellW,
                        float cellH, float spacing, const char *str,
                        Direct3D *d3d, char firstChar, DWORD colourTop,
                        DWORD colourBottom, float amplitude, float rate,
                        int n);

/* The lifecycle, for the two global fonts (0x004e0480, 0x004e02e8). */
extern "C" __declspec(dllexport) TextRenderer *__attribute__((thiscall))
Text_Construct(TextRenderer *self);                          /* 0x004134d0 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DtorBody(TextRenderer *self);                           /* 0x00413510 */
extern "C" __declspec(dllexport) TextRenderer *__attribute__((thiscall))
Text_ScalarDtor(TextRenderer *self, unsigned int flags);     /* 0x004134f0 */

/* 0x00413eb0, thiscall with twelve stack arguments, RET 0x30. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DrawPanelText(TextRenderer *self, float x, float y, float cellW,
                   float cellH, float spacing, float lineH, const char *str,
                   Direct3D *d3d, DWORD colourTop, DWORD colourBottom,
                   SceneTexture *panelTex, SceneTexture *frameTex);
