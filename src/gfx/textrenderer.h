/* TextRenderer -- the bitmap-font glyph service: the object RenderGameFrame
 * hands to every overlay as its text `this`.  It is a font atlas, not a
 * general text engine: a Texture whose image is a `cols` x `rows` grid
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
 * The two instances are globals, g_fontMain and g_fontNumbers.
 */
#pragma once

#include <stdint.h>
#include "texture.h"    /* Texture -- the atlas */

class RenderDevice;

class TextRenderer {
public:
    Texture* atlas() { return &atlas_; }

    /* Left-aligned: (x, y) is the first cell's
     * top-left corner. */
    void drawLeft(float x, float y, float cellW, float cellH, float spacing,
                  const char *str, RenderDevice *d3d, char firstChar,
                  uint32_t colourTop, uint32_t colourBottom);

    /* x is the string's right edge. */
    void drawRight(float x, float y, float cellW, float cellH, float spacing,
                   const char *str, RenderDevice *d3d, char firstChar,
                   uint32_t colourTop, uint32_t colourBottom);

    /* x is the string's centre. */
    void drawCentered(float x, float y, float cellW, float cellH, float spacing,
                      const char *str, RenderDevice *d3d, char firstChar,
                      uint32_t colourTop, uint32_t colourBottom);

    /* Load a .fon: line 1 is the atlas's
     * texture path, line 2 the column count, line 3 the row count.  Returns
     * non-zero in the low byte on success.  Both shipped fonts are three
     * lines long (fonts/FONT1.FON is textures\font2.tga, 16, 16;
     * fonts/NUMBERS.FON is textures\numbers.tga, 4, 3). */
    unsigned int load(const char *path, RenderDevice *d3d);

    /* The centring wrapper around drawWobble, with
     * exactly drawCentered's arithmetic: x -= width * 0.5f, then a tail call
     * with all twelve remaining arguments untouched.  Used for "GAME OVER". */
    void drawBig(float x, float y, float cellW, float cellH, float spacing,
                 const char *str, RenderDevice *d3d, char firstChar,
                 uint32_t colourTop, uint32_t colourBottom,
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
     * 0xff000000, the DIFFUSE gradient, SetTexture + SetBlend
     * calls ahead of the empty test, and the pen advance.  drawBig is its
     * only caller. */
    void drawWobble(float x, float y, float cellW, float cellH, float spacing,
                    const char *str, RenderDevice *d3d, char firstChar,
                    uint32_t colourTop, uint32_t colourBottom,
                    float amplitude, float rate, int n);

    /* A multi-line caption over two full-width
     * backdrop strips (textrenderer.cpp). */
    void drawPanel(float x, float y, float cellW, float cellH, float spacing,
                   float lineH, const char *str, RenderDevice *d3d,
                   uint32_t colourTop, uint32_t colourBottom,
                   Texture *panelTex, Texture *frameTex);

    /* An empty atlas; the destructor releases it. */
    TextRenderer();
    virtual ~TextRenderer();
    TextRenderer(const TextRenderer &) = delete;
    TextRenderer &operator=(const TextRenderer &) = delete;

private:
    unsigned int  cols_;    /* atlas columns; also the cell divisor */
    unsigned int  rows_;    /* atlas rows */
    Texture  atlas_;
};

/* The two fonts: fonts\font1.fon and fonts\numbers.fon. */
extern TextRenderer g_fontMain;
extern TextRenderer g_fontNumbers;
