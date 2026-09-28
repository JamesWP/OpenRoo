/* TextRenderer: the glyph loops, the .fon loader, the text panel and the
 * lifecycle (textrenderer.h).
 *
 * drawLeft issues one DrawPrimitive per character: a four-vertex
 * Prim::TriangleFan in screen space.  Per glyph, with g = (unsigned
 * char)(str[i] - firstChar):
 *
 *   cell  = (g % cols, g / cols)                    unsigned div, both times
 *   u, v  = cell.x * (1/cols), cell.y * (1/rows)
 *   quad  = (penX, y) (penX+cellW, y) (penX+cellW, y+cellH) (penX, y+cellH)
 *   diffuse = colourTop on the two top vertices, colourBottom on the bottom two
 *   penX += cellW * spacing
 *
 * SetTexture and the three SetRenderState calls happen before the empty-string
 * test, so drawing "" still leaves alpha blending on and the atlas bound.
 *
 * drawCentered and drawRight shift x left by half the rendered width, or all
 * of it, and then call drawLeft. */

#include "textrenderer.h"
#include "renderdevice.h"
#include "log.h"
#include "scenetexture.h"
#include "gamestr.h"
#include <stdlib.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
TextRenderer g_fontMain;
TextRenderer g_fontNumbers;

/* FVF 0x1C4 is 32 bytes: the SPECULAR set is really written (0xff000000 into
 * every vertex).  A shorter vertex would hand the driver a stride four bytes
 * short of what the FVF declares. */
#define TEXT_FVF  VertexFormat::Screen

struct TextVertex {
    float x, y, z, rhw;
    DWORD diffuse;
    DWORD specular;
    float u, v;
};

/* KAROO_TEXT_FX -- controls that change direction, each of which only one
 * piece of this file can produce:
 *   mirror    the pen advances by -(cellW * spacing), so every string runs
 *             right to left from its first glyph; centring still uses the
 *             true width.
 *   loadswap  the .fon loader swaps columns and rows, re-indexing every
 *             glyph of the 4x3 numbers font (font1 is 16x16, its own
 *             transpose).
 *   bigwave   drawWobble's per-glyph phase steps by -2 instead of +2, so the
 *             wave travels along the string the other way. */
enum TextFx { TEXT_FX_OFF = 0, TEXT_FX_MIRROR = 1, TEXT_FX_LOADSWAP = 2,
              TEXT_FX_BIGWAVE = 3 };

static TextFx text_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (TextFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_TEXT_FX", buf, sizeof(buf));
    TextFx fx = TEXT_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "mirror") == 0)   fx = TEXT_FX_MIRROR;
        if (lstrcmpiA(buf, "loadswap") == 0) fx = TEXT_FX_LOADSWAP;
        if (lstrcmpiA(buf, "bigwave") == 0)  fx = TEXT_FX_BIGWAVE;
    }
    log_write("textrenderer: FX mode = %s\n",
              fx == TEXT_FX_MIRROR   ? "mirror" :
              fx == TEXT_FX_LOADSWAP ? "loadswap" :
              fx == TEXT_FX_BIGWAVE  ? "bigwave" : "off");
    cached = (int)fx;
    return fx;
}

/* KAROO_TEXT_DIAG=1: which entry points run, and how many glyphs each draws.
 * Every function announces its first call as well as feeding the periodic
 * tally. */
static bool text_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_TEXT_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nRender, g_nGlyphs, g_nEmpty, g_nCentred, g_nRight;
static unsigned long g_nLoad;
static unsigned long g_nBig, g_nWobble, g_nWobbleGlyphs;

static void text_first(const char *fn, unsigned long *pSeen)
{
    if (!text_diag() || *pSeen != 0)
        return;
    *pSeen = 1;
    log_write("textrenderer: first call to %s\n", fn);
}

static void text_census(void)
{
    if (!text_diag())
        return;
    unsigned long n = g_nRender;
    if (!(n == 1 || n == 100 || n == 1000 || n == 10000 || n % 20000 == 0))
        return;
    log_write("textrenderer: census render=%lu (empty %lu) glyphs=%lu "
              "centred=%lu right=%lu load=%lu big=%lu wobble=%lu "
              "wobbleGlyphs=%lu\n",
              g_nRender, g_nEmpty, g_nGlyphs, g_nCentred, g_nRight, g_nLoad,
              g_nBig, g_nWobble, g_nWobbleGlyphs);
}

/* `len` cells, less the overlap a spacing below 1 introduces between each
 * adjacent pair. */
static float text_width(const char *str, float cellW, float spacing)
{
    const float len = (float)(int)strlen(str);
    return (len - (len - 1.0f) * (1.0f - spacing)) * cellW;
}

/* The bodies live on the class, which reads private fields; the exports below
 * forward to them. */

void TextRenderer::drawLeft(float x, float y, float cellW, float cellH,
                            float spacing, const char *str, RenderDevice *d3d,
                            char firstChar, DWORD colourTop,
                            DWORD colourBottom)
{
    ++g_nRender;
    { static unsigned long seen; text_first("RenderText", &seen); }

    // The reciprocals are taken once, before the loop, and multiplied per
    // glyph.
    const float invCols = 1.0f / (float)(int)cols_;
    const float invRows = 1.0f / (float)(int)rows_;

    RenderDevice *dev = d3d;
    dev->SetTexture(0, &atlas_);
    dev->SetRenderState(RS::SrcBlend,         Blend::SrcAlpha);
    dev->SetRenderState(RS::DestBlend,        Blend::InvSrcAlpha);
    dev->SetRenderState(RS::AlphaBlendEnable, 1);

    if (strlen(str) == 0) {
        ++g_nEmpty;
        text_census();
        return;
    }

    const float yBottom = y + cellH;
    float advance = cellW * spacing;
    if (text_fx() == TEXT_FX_MIRROR)
        advance = -advance;

    TextVertex quad[4];
    for (int k = 0; k < 4; k++) {
        quad[k].z        = 0.1f;
        quad[k].rhw      = 10.0f;
        quad[k].specular = 0xff000000;
    }
    quad[0].y = y;        quad[0].diffuse = colourTop;
    quad[1].y = y;        quad[1].diffuse = colourTop;
    quad[2].y = yBottom;  quad[2].diffuse = colourBottom;
    quad[3].y = yBottom;  quad[3].diffuse = colourBottom;

    unsigned int i = 0;
    do {
        const unsigned int g = (unsigned char)(str[i] - firstChar);
        const float u = (float)(int)(g % cols_) * invCols;
        const float v = (float)(int)(g / cols_) * invRows;

        quad[0].x = x;          quad[0].u = u;           quad[0].v = v;
        quad[1].x = x + cellW;  quad[1].u = u + invCols; quad[1].v = v;
        quad[2].x = x + cellW;  quad[2].u = u + invCols; quad[2].v = v + invRows;
        quad[3].x = x;          quad[3].u = u;           quad[3].v = v + invRows;

        d3d->Draw(Prim::TriangleFan, TEXT_FVF, quad, 4, 0);
        ++g_nGlyphs;

        x += advance;
        ++i;
    } while (i < strlen(str));

    text_census();
}

/* ─── The .fon loader ──────────────────────────────────────────────────────
 *
 * FORMAT: three lines of text: the atlas's texture path, the column count, the
 * row count.  The file is opened in text mode, which is load-bearing: the .fon
 * files are CRLF, and only text mode turns "strip the last character" into a
 * usable path.
 *
 * A zero column or row count is a failure.  Only the low byte of the result is
 * the success flag; the upper bytes are fclose's, or the texture import's on
 * that failure. */

/* MSVC's atoi, single-byte path: C-locale whitespace and digits only, sign,
 * and an int accumulator that wraps on overflow. */
static bool c_space(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13); }
static bool c_digit(unsigned char c) { return c >= '0' && c <= '9'; }

static int font_atoi(const char *p)
{
    while (c_space((unsigned char)*p))
        ++p;

    const unsigned char sign = (unsigned char)*p;
    if (sign == '-' || sign == '+')
        ++p;

    int acc = 0;
    while (c_digit((unsigned char)*p)) {
        acc = acc * 10 + ((unsigned char)*p - '0');
        ++p;
    }
    return sign == '-' ? -acc : acc;
}

unsigned int TextRenderer::load(const char *path, RenderDevice *d3d)
{
    ++g_nLoad;
    { static unsigned long seen; text_first("ReadBitmapFontFile", &seen); }

    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    char line[0x100];

    // PRESERVED: every failure after the fopen leaks the file handle; only the
    // success path reaches fclose.
    if (fgets(line, 0xff, fp) == NULL)
        return 0;
    line[strlen(line) - 1] = '\0';

    const unsigned int ok = this->atlas()->importSceneTextures(d3d, line, 1, 0, 0);
    if ((ok & 0xffu) == 0)
        return ok;  // its result, upper bytes and all

    // Columns.
    if (fgets(line, 0xff, fp) == NULL)
        return 0;
    line[strlen(line) - 1] = '\0';
    cols_ = (unsigned int)font_atoi(line);
    if (cols_ == 0)
        return 0;

    // Rows.
    if (fgets(line, 0xff, fp) == NULL)
        return 0;
    line[strlen(line) - 1] = '\0';
    rows_ = (unsigned int)font_atoi(line);
    if (rows_ == 0)
        return 0;

    if (text_fx() == TEXT_FX_LOADSWAP) {
        const unsigned int t = cols_;
        cols_ = rows_;
        rows_ = t;
    }

    if (text_diag())
        log_write("textrenderer: loaded %s -- %u x %u cells\n",
                  path, cols_, rows_);

    // The low byte is the success flag; the upper three are fclose's.
    const unsigned int closed = (unsigned int)fclose(fp);
    return (closed & 0xffffff00u) | 1u;
}

void TextRenderer::drawCentered(float x, float y, float cellW, float cellH,
                                float spacing, const char *str, RenderDevice *d3d,
                                char firstChar, DWORD colourTop,
                                DWORD colourBottom)
{
    ++g_nCentred;
    { static unsigned long seen; text_first("DrawCenteredText", &seen); }

    drawLeft(x - text_width(str, cellW, spacing) * 0.5f, y, cellW, cellH,
             spacing, str, d3d, firstChar, colourTop, colourBottom);
}

void TextRenderer::drawRight(float x, float y, float cellW, float cellH,
                             float spacing, const char *str, RenderDevice *d3d,
                             char firstChar, DWORD colourTop,
                             DWORD colourBottom)
{
    ++g_nRight;
    { static unsigned long seen; text_first("DrawRightAlignedText", &seen); }

    drawLeft(x - text_width(str, cellW, spacing), y, cellW, cellH,
             spacing, str, d3d, firstChar, colourTop, colourBottom);
}

/* ─── Exports ──────────────────────────────────────────────────────────────
 */

  //  

/* ─── The big-text pair ─────────────────────────────────────────────────────
 *
 * drawWobble is drawLeft with a vertical wave.  Per glyph, with i2 = 2 * i:
 *
 *   dy   = cellH * 0.5f + amplitude * sin(n * rate + i2)
 *   quad = (x, y-dy) (x+cellW, y-dy) (x+cellW, y+dy) (x, y+dy)
 *
 * so `y` is the row's centre, and each glyph's quad grows and shrinks about it
 * two radians out of phase with its neighbour.  Everything else, including the
 * state set before the empty-string test, is drawLeft's.
 *
 * n and i2 are converted to floating point as unsigned: a negative value would
 * give a different phase, so the casts are load-bearing. */
static float wobble_phase(unsigned int n, float rate)
{
    return (float)((double)n * rate);
}

static float wobble_dy(int i2, float phase, float amplitude, float halfH)
{
    return (float)(sin((double)(unsigned int)i2 + phase) * amplitude + halfH);
}

void TextRenderer::drawWobble(float x, float y, float cellW, float cellH,
                              float spacing, const char *str, RenderDevice *d3d,
                              char firstChar, DWORD colourTop,
                              DWORD colourBottom, float amplitude, float rate,
                              int n)
{
    ++g_nWobble;
    { static unsigned long seen; text_first("DrawWobbleGlyphRow", &seen); }

    const float invCols = 1.0f / (float)(int)cols_;
    const float invRows = 1.0f / (float)(int)rows_;

    RenderDevice *dev = d3d;
    dev->SetTexture(0, &atlas_);
    dev->SetRenderState(RS::SrcBlend,         Blend::SrcAlpha);
    dev->SetRenderState(RS::DestBlend,        Blend::InvSrcAlpha);
    dev->SetRenderState(RS::AlphaBlendEnable, 1);

    if (strlen(str) == 0)
        return;

    const float phase = wobble_phase((unsigned int)n, rate);
    const float halfH = cellH * 0.5f;
    const float advance = cellW * spacing;

    // The per-glyph phase step; KAROO_TEXT_FX=bigwave reverses it.
    const int step = (text_fx() == TEXT_FX_BIGWAVE) ? -2 : 2;

    TextVertex quad[4];
    for (int k = 0; k < 4; k++) {
        quad[k].z        = 0.1f;
        quad[k].rhw      = 10.0f;
        quad[k].specular = 0xff000000;
    }
    quad[0].diffuse = colourTop;
    quad[1].diffuse = colourTop;
    quad[2].diffuse = colourBottom;
    quad[3].diffuse = colourBottom;

    unsigned int i = 0;
    int i2 = 0;
    do {
        const unsigned int g = (unsigned char)(str[i] - firstChar);
        const float u  = (float)(int)(g % cols_) * invCols;
        const float v  = (float)(int)(g / cols_) * invRows;
        const float dy = wobble_dy(i2, phase, amplitude, halfH);

        quad[0].x = x;          quad[0].y = y - dy;
        quad[1].x = x + cellW;  quad[1].y = y - dy;
        quad[2].x = x + cellW;  quad[2].y = y + dy;
        quad[3].x = x;          quad[3].y = y + dy;

        quad[0].u = u;            quad[0].v = v;
        quad[1].u = u + invCols;  quad[1].v = v;
        quad[2].u = u + invCols;  quad[2].v = v + invRows;
        quad[3].u = u;            quad[3].v = v + invRows;

        dev->Draw(Prim::TriangleFan, TEXT_FVF, quad, 4, 0);
        ++g_nWobbleGlyphs;

        i2 += step;
        ++i;
        x += advance;
    } while (i < strlen(str));
}

void TextRenderer::drawBig(float x, float y, float cellW, float cellH,
                           float spacing, const char *str, RenderDevice *d3d,
                           char firstChar, DWORD colourTop, DWORD colourBottom,
                           float amplitude, float rate, int n)
{
    ++g_nBig;
    { static unsigned long seen; text_first("DrawBigText", &seen); }

    // drawCentered's arithmetic, with the three extra arguments passed
    // through.
    drawWobble(x - text_width(str, cellW, spacing) * 0.5f, y, cellW, cellH,
               spacing, str, d3d, firstChar, colourTop, colourBottom,
               amplitude, rate, n);
}

/* ─── The big-text exports ──────────────────────────────────────────────────
 */
  //  

/* ─── The lifecycle ─────────────────────────────────────────────────────────
 *
 * The two fonts are globals, constructed and destroyed by staticinit.cpp.  The
 * scalar dtor is the one vtable slot; a global is never deleted, so its free
 * is never reached. */
static void *const g_TextVtable[1] = { (void *)&TextRenderer::scalarDeletingDtor };

void TextRenderer::construct()
{
    atlas()->construct();
    vtable_ = g_TextVtable;
    rows_ = 0;
    cols_ = 0;
}

void TextRenderer::destruct()
{
    vtable_ = g_TextVtable;
    atlas()->dtorBody();
}

TextRenderer * 
TextRenderer::scalarDeletingDtor(TextRenderer *self, unsigned int flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* ─── DrawTextPanel ───────────────────────────────────────────────────────
 *
 * A multi-line caption over two full-width backdrop strips:
 *   - lines = 1 + the '\n' count; the block is raised so its last line sits
 *     at y: y -= lines * lineH.
 *   - SRCBLEND 5 / DESTBLEND 6 / ALPHABLEND 1.
 *   - strip 1 (panelTex, or no texture): screen width W, from
 *     y - W*0.009375 down to the screen height H, diffuse white, specular 0,
 *     z 0 / rhw 10, uv the atlas's centre (0.4..0.6).  Strip order is
 *     (W,top) (W,H) (0,top) (0,H), a TRIANGLESTRIP.
 *   - strip 2, only when frameTex is non-NULL: y - W*0.015625 to
 *     y - W*0.00625, uv 0..1, same strip order.
 *   - the glyphs: one TRIANGLEFAN per character, the cell index the raw
 *     unsigned byte (no firstChar, unlike drawLeft), the cell uv from
 *     unsigned divides; '\n' returns x to the start and moves y down one
 *     lineH.  The pen advances cellW * spacing.
 *   - ALPHABLENDENABLE 0.
 * The strips' z is 0 and the glyphs' 0.1. */
void TextRenderer::drawPanel(float x, float y, float cellW, float cellH,
                             float spacing, float lineH, const char *str,
                             RenderDevice *d3d, DWORD colourTop, DWORD colourBottom,
                             SceneTexture *panelTex, SceneTexture *frameTex)
{
    const float du = 1.0f / (float)cols_;  // cols_ read as unsigned
    const float dv = 1.0f / (float)rows_;

    unsigned int lines = 1;
    for (unsigned int i = 0; i < strlen(str); ++i)
        if (str[i] == '\n')
            ++lines;
    const float x0 = x;
    y = y - (float)lines * lineH;

    RenderDevice *dev = d3d;
    dev->SetRenderState(RS::SrcBlend,         Blend::SrcAlpha);
    dev->SetRenderState(RS::DestBlend,        Blend::InvSrcAlpha);
    dev->SetRenderState(RS::AlphaBlendEnable, 1);

    const float W = (float)d3d->width();
    const float H = (float)d3d->height();

    TextVertex strip[4];
    for (int k = 0; k < 4; k++) {
        strip[k].z        = 0.0f;
        strip[k].rhw      = 10.0f;
        strip[k].diffuse  = 0xffffffff;
        strip[k].specular = 0;
    }

    const float top1 = y - W * 0.009375f;
    strip[0].x = W; strip[0].y = top1; strip[0].u = 0.6f; strip[0].v = 0.4f;
    strip[1].x = W; strip[1].y = H;    strip[1].u = 0.6f; strip[1].v = 0.6f;
    strip[2].x = 0; strip[2].y = top1; strip[2].u = 0.4f; strip[2].v = 0.4f;
    strip[3].x = 0; strip[3].y = H;    strip[3].u = 0.4f; strip[3].v = 0.6f;
    dev->SetTexture(0, panelTex);
    dev->Draw(Prim::TriangleStrip, TEXT_FVF, strip, 4, 0);

    const float top2 = y - W * 0.015625f;
    const float bot2 = y - W * 0.00625f;
    strip[0].x = W; strip[0].y = top2; strip[0].u = 1.0f; strip[0].v = 0.0f;
    strip[1].x = W; strip[1].y = bot2; strip[1].u = 1.0f; strip[1].v = 1.0f;
    strip[2].x = 0; strip[2].y = top2; strip[2].u = 0.0f; strip[2].v = 0.0f;
    strip[3].x = 0; strip[3].y = bot2; strip[3].u = 0.0f; strip[3].v = 1.0f;
    if (frameTex) {
        dev->SetTexture(0, frameTex);
        dev->Draw(Prim::TriangleStrip, TEXT_FVF, strip, 4, 0);
    }

    dev->SetTexture(0, &atlas_);

    TextVertex quad[4];
    for (int k = 0; k < 4; k++) {
        quad[k].z        = 0.1f;
        quad[k].rhw      = 10.0f;
        quad[k].specular = 0xff000000;
    }
    for (unsigned int i = 0; i < strlen(str); ++i) {
        const unsigned char ch = (unsigned char)str[i];
        if (ch == '\n') {
            y += lineH;
            x = x0;
            continue;
        }
        const float u = (float)(ch % cols_) * du;
        const float v = (float)(ch / cols_) * dv;
        const float x1 = x + cellW, y1 = y + cellH;

        quad[0].x = x;  quad[0].y = y;  quad[0].u = u;      quad[0].v = v;      quad[0].diffuse = colourTop;
        quad[1].x = x1; quad[1].y = y;  quad[1].u = u + du; quad[1].v = v;      quad[1].diffuse = colourTop;
        quad[2].x = x1; quad[2].y = y1; quad[2].u = u + du; quad[2].v = v + dv; quad[2].diffuse = colourBottom;
        quad[3].x = x;  quad[3].y = y1; quad[3].u = u;      quad[3].v = v + dv; quad[3].diffuse = colourBottom;
        dev->Draw(Prim::TriangleFan, TEXT_FVF, quad, 4, 0);

        x += cellW * spacing;
    }

    dev->SetRenderState(RS::AlphaBlendEnable, 0);
}

