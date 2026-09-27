/* TextRenderer -- the bitmap-font glyph service: the object RenderGameFrame
 * hands to every overlay as its text `this`.  It is a font atlas, not a
 * general text engine: a SceneTexture whose image is a `cols` x `rows` grid
 * of fixed-size glyph cells, loaded from a .fon.
 *
 * The draw calls share ten arguments:
 *   - firstChar is the atlas's first character code: each glyph's cell is
 *     `(unsigned char)(str[i] - firstChar)`.  Every caller passes 0.
 *   - colourTop/colourBottom are D3DCOLORs written into the quad's DIFFUSE,
 *     top pair and bottom pair: text is drawn with a vertical gradient.
 *   - `cellW`/`cellH` are the quad's size in screen pixels and `spacing` is
 *     the tracking: the pen advances by `cellW * spacing`, so a spacing below
 *     1 overlaps successive cells.  That is why the centring wrappers
 *     subtract `(len - (len-1)*(1-spacing)) * cellW` and not `len * cellW`.
 *
 * The two instances are globals, g_fontMain and g_fontNumbers, 0x28 bytes
 * each, the atlas ending the object.
 */
#pragma once

#include <windows.h>
#include "layout.h"
#include "texture.h"    /* SceneTexture -- the atlas at +0x0c */

struct Direct3D;

class __attribute__((packed)) TextRenderer {
public:
    SceneTexture* atlas() { return &atlas_; }
    static const int ORIGIN = 0;

    /* Left-aligned: (x, y) is the first cell's
     * top-left corner. */
    void drawLeft(float x, float y, float cellW, float cellH, float spacing,
                  const char *str, Direct3D *d3d, char firstChar,
                  DWORD colourTop, DWORD colourBottom);

    /* x is the string's right edge. */
    void drawRight(float x, float y, float cellW, float cellH, float spacing,
                   const char *str, Direct3D *d3d, char firstChar,
                   DWORD colourTop, DWORD colourBottom);

    /* x is the string's centre. */
    void drawCentered(float x, float y, float cellW, float cellH, float spacing,
                      const char *str, Direct3D *d3d, char firstChar,
                      DWORD colourTop, DWORD colourBottom);

    /* Load a .fon: line 1 is the atlas's
     * texture path, line 2 the column count, line 3 the row count.  Returns
     * non-zero in the low byte on success.  Both shipped fonts are three
     * lines long (fonts/FONT1.FON is textures\font2.tga, 16, 16;
     * fonts/NUMBERS.FON is textures\numbers.tga, 4, 3). */
    unsigned int load(const char *path, Direct3D *d3d);

    /* The centring wrapper around drawWobble, with
     * exactly drawCentered's arithmetic: x -= width * 0.5f, then a tail call
     * with all twelve remaining arguments untouched.  Used for "GAME OVER". */
    void drawBig(float x, float y, float cellW, float cellH, float spacing,
                 const char *str, Direct3D *d3d, char firstChar,
                 DWORD colourTop, DWORD colourBottom,
                 float amplitude, float rate, int n);

    /* drawLeft's sibling, and the only other glyph loop.  The differences are
     * all in the quad:
     *
     *   - `y` is the row's CENTRE, not its top.  Each quad spans y +/- dy.
     *   - dy = cellH * 0.5f + amplitude * sin(n * rate + 2 * i), so every
     *     glyph breathes vertically about that centre, two radians out of
     *     phase with the one before it.
     *
     * Everything else is drawLeft's: FVF 0x1C4, z/rhw 0.1f / 10.0f, specular
     * 0xff000000, the DIFFUSE gradient, SetTexture + three SetRenderState
     * calls ahead of the empty test, and the pen advance.  drawBig is its
     * only caller. */
    void drawWobble(float x, float y, float cellW, float cellH, float spacing,
                    const char *str, Direct3D *d3d, char firstChar,
                    DWORD colourTop, DWORD colourBottom,
                    float amplitude, float rate, int n);

    /* A multi-line caption over two full-width
     * backdrop strips (textrenderer.cpp). */
    void drawPanel(float x, float y, float cellW, float cellH, float spacing,
                   float lineH, const char *str, Direct3D *d3d,
                   DWORD colourTop, DWORD colourBottom,
                   SceneTexture *panelTex, SceneTexture *frameTex);

    /* Constructor and destructor body. */
    void construct();
    void destruct();

private:
    KAROO_LAYOUT_REGISTER(TextRenderer);

    const void   *vtable_;      /* +0x00  one-slot table: the scalar dtor */
    unsigned int  cols_;        /* +0x04  atlas columns; also the cell divisor */
    unsigned int  rows_;        /* +0x08  atlas rows                          */
    SceneTexture  atlas_;       /* +0x0c  .pTexture2 lands on +0x24           */


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
#pragma GCC diagnostic pop

};

KAROO_LAYOUT_CHECKS(TextRenderer)
{
    KAROO_LAYOUT_AT(cols_, 0x04);
    KAROO_LAYOUT_AT(rows_, 0x08);
    KAROO_LAYOUT_AT(atlas_, 0x0c);
    /* The field the glyph loop dereferences. */
    static_assert(0x0c + offsetof(SceneTexture, pTexture2) == 0x24,
                  "TextRenderer: atlas texture must land on the game's +0x24");
}

/* ─── Exports ──────────────────────────────────────────────────────────────
 *
 * The three plain draws are __thiscall with ten stack arguments.
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

/* The path, and the Direct3D the atlas is created against. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Text_LoadFont(TextRenderer *self, const char *path, Direct3D *d3d);

/* The big-text pair take thirteen stack arguments: the ten the other three
 * take plus (amplitude, rate, n). */
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

/* The lifecycle, for the two global fonts. */
extern "C" __declspec(dllexport) TextRenderer *__attribute__((thiscall))
Text_Construct(TextRenderer *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DtorBody(TextRenderer *self);
extern "C" __declspec(dllexport) TextRenderer *__attribute__((thiscall))
Text_ScalarDtor(TextRenderer *self, unsigned int flags);

/* Twelve stack arguments. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DrawPanelText(TextRenderer *self, float x, float y, float cellW,
                   float cellH, float spacing, float lineH, const char *str,
                   Direct3D *d3d, DWORD colourTop, DWORD colourBottom,
                   SceneTexture *panelTex, SceneTexture *frameTex);

/* The two fonts: fonts\font1.fon and fonts\numbers.fon. */
extern TextRenderer g_fontMain;
extern TextRenderer g_fontNumbers;
