/* TextRenderer -- every entry point reimplemented.
 *
 *   0x00413690 RenderText            __thiscall(this, 10 args)  RET 0x28
 *   0x00413d00 DrawCenteredText      __thiscall(this, 10 args)  RET 0x28
 *   0x00413e30 DrawRightAlignedText  __thiscall(this, 10 args)  RET 0x28
 *   0x00413d90 DrawBigText           __thiscall(this, 13 args)  RET 0x34
 *   0x00413990 DrawWobbleGlyphRow    __thiscall(this, 13 args)  RET 0x34
 *
 * Each signature was read off the original's `RET n` and off the caller-side
 * push order, not off the decompiler's parameter list.  ENDGAME_PLAN.md E1;
 * textrenderer.h records what the argument list means, including the two
 * arguments the old placeholder header named wrongly.
 *
 * ── What RenderText does ────────────────────────────────────────────────────
 * One DrawPrimitive per character: a D3DPT_TRIANGLEFAN of four vertices in
 * screen space, FVF 0x1C4.
 *
 * 0x1C4 is XYZRHW | DIFFUSE | SPECULAR | TEX1 -- **32** bytes, not the 28 the
 * old Ghidra plate comment claimed.  The SPECULAR set (0x080) is easy to miss
 * and it is really written: 0xff000000 into every vertex, at 0x4137b2 and its
 * three siblings.  Getting this wrong would have handed the driver a vertex
 * stride four bytes short of what the FVF declares, which is the scenequad.cpp
 * class of fault (CRASH.md).
 *
 * Per glyph, with g = (unsigned char)(str[i] - firstChar):
 *
 *   cell  = (g % cols, g / cols)                    unsigned div, both times
 *   u, v  = cell.x * (1/cols), cell.y * (1/rows)
 *   quad  = (penX, y) (penX+cellW, y) (penX+cellW, y+cellH) (penX, y+cellH)
 *   diffuse = colourTop on the two top vertices, colourBottom on the bottom two
 *   penX += cellW * spacing
 *
 * The two reciprocals are computed ONCE before the loop and multiplied, never
 * divided per glyph -- the original's rounding, kept: `1.0f / (float)cols`
 * stored to a float, then a float multiply.
 *
 * ── Order preserved deliberately ────────────────────────────────────────────
 *   - SetTexture and the three SetRenderState calls happen BEFORE the empty-
 *     string test, so drawing "" still leaves alpha blending enabled and the
 *     atlas bound.  Callers depend on that leak whether they know it or not.
 *   - The original re-runs strlen on every iteration (0x41395e) instead of
 *     hoisting it.  Reproduced -- it cannot change the result, since nothing
 *     writes to the string, but it is the original's shape and the cost is
 *     the original's cost.
 *   - The device is the one the game passed (d3d->pDevice), so every call
 *     stays visible to the com_proxy layer.
 *
 * ── The two wrappers ────────────────────────────────────────────────────────
 * Both compute the string's rendered width and shift x left by it, then tail
 * into RenderText with all nine remaining arguments untouched:
 *
 *   width = (len - (len - 1) * (1 - spacing)) * cellW
 *
 * DrawCenteredText subtracts width * 0.5f, DrawRightAlignedText subtracts
 * width.  That is the only difference between the two functions -- one
 * `FMUL [0x0045d318]`, and 0x0045d318 holds 0.5f (read, not assumed; 0x0045d298
 * alongside it holds the 1.0f both expressions use).
 */
#include "textrenderer.h"
#include "com_proxy.h"
#include "direct3d.h"
#include "log.h"
#include "scenetexture.h"   /* Texture_ImportSceneTextures, through its owner header */
#include "gamestr.h"        /* GS_FON_MODE_READ */
#include <stdlib.h>

#include <stdio.h>
#include <math.h>
#include <string.h>
TextRenderer g_fontMain;   /* was 0x004e0480 */
TextRenderer g_fontNumbers;   /* was 0x004e02e8 */

/* The FVF the original declares, and the vertex it really writes. */
#define TEXT_FVF  (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | \
                   D3DFVF_TEX1)

struct TextVertex {              /* 32 bytes -- FVF 0x1C4 */
    float x, y, z, rhw;
    DWORD diffuse;
    DWORD specular;
    float u, v;
};

static_assert(sizeof(TextVertex) == 32, "FVF 0x1C4 vertex is 32 bytes");
static_assert(TEXT_FVF == 0x1c4, "FVF constant must match the original's");

/* ─── KAROO_TEXT_FX -- the negative control (CONTROLS.md) ─────────────────
 *
 * Read by value, never by presence.
 *
 *   mirror  -- the pen advances by -(cellW * spacing), so every string is laid
 *              out right-to-left from its first glyph.  A DIRECTION change,
 *              not a value perturbation: it can only be produced by the pen
 *              arithmetic in the loop below, so seeing it proves the advance
 *              maths runs here rather than merely that the function is
 *              entered.  The wrappers are unaffected, which is itself the
 *              point -- centring still uses the true width.
 *
 *   loadswap -- the .fon loader stores the grid transposed, columns into
 *              rows and back.  Also a direction change rather than a value
 *              perturbation, and it can only come from the loader: the glyph
 *              loop divides by `cols`, so a transposed grid re-indexes every
 *              character of the numbers font (4x3 becomes 3x4).  font1 is
 *              16x16 and so is its own transpose, which is itself useful --
 *              it says the control acts on the file's contents rather than
 *              on the code path.
 *
 *   bigwave -- DrawWobbleGlyphRow's per-glyph phase steps by -2 instead of
 *              +2, so the vertical wave travels along the string the other
 *              way.  A direction change again, and one only this loop can
 *              produce: drawBig and the wrapper arithmetic are untouched, the
 *              pen advance is untouched, and the amplitude is untouched --
 *              the string keeps its position and its size, and only the
 *              travelling direction of the wave reverses.
 *
 * Blast radius, chosen against the gate that hosts it: this moves glyph quads
 * only.  It writes no coordinate, axis or tile index back into the world, so
 * it cannot reach the unbounded bridge/slide spawn scans that crash
 * levelreport.py.
 */
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

/* ─── KAROO_TEXT_DIAG -- the census ───────────────────────────────────────
 *
 * Which of the three the gates actually reach, and how many glyphs each
 * draws.  Every function announces its own first call as well as feeding the
 * periodic tally: a purely periodic sample reads zero forever for anything
 * first reached after the last threshold, which is the exact failure a census
 * exists to prevent (linkedlist.cpp learned this the hard way).
 */
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

/* The width the two wrappers shift by: `len` cells, less the overlap that a
 * spacing below 1 introduces between each adjacent pair. */
static float text_width(const char *str, float cellW, float spacing)
{
    const float len = (float)(int)strlen(str);
    return (len - (len - 1.0f) * (1.0f - spacing)) * cellW;
}

/* The bodies live on the class -- they read private fields, and the exports
 * below are thin forwarders.  That way there is exactly one implementation and
 * the rest of karoo-hooks/ reaches it through the owning header, which is the
 * arrangement CLAUDE.md asks for. */

void TextRenderer::drawLeft(float x, float y, float cellW, float cellH,
                            float spacing, const char *str, Direct3D *d3d,
                            char firstChar, DWORD colourTop,
                            DWORD colourBottom)
{
    ++g_nRender;
    { static unsigned long seen; text_first("RenderText", &seen); }

    /* One float divide each, before the loop and before the length test --
     * the original's order, and the original's rounding. */
    const float invCols = 1.0f / (float)(int)cols_;
    const float invRows = 1.0f / (float)(int)rows_;

    IDirect3DDevice3 *dev = d3d->pDevice;
    dev->SetTexture(0, atlas_.pTexture2);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND,         D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND,        D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);

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

        d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, TEXT_FVF, quad, 4, 0);
        ++g_nGlyphs;

        x += advance;
        ++i;
        /* strlen every iteration, as the original does. */
    } while (i < strlen(str));

    text_census();
}

/* ─── ReadBitmapFontFile 0x00413520 ───────────────────────────────────────
 *
 * Three lines of text: the atlas's texture path, the column count, the row
 * count.  Both shipped fonts are exactly that (fonts/FONT1.FON =
 * textures\font2.tga / 16 / 16; fonts/NUMBERS.FON = textures\numbers.tga /
 * 4 / 3), and the file is opened in TEXT mode -- 0x00464200 is "r", read out
 * rather than assumed -- which is load-bearing: the .fon files are CRLF, and
 * only text mode's CRLF -> LF translation makes "strip the last character"
 * leave a usable path instead of one ending in CR.
 *
 * Bugs and quirks preserved deliberately:
 *
 *   - EVERY failure path after the fopen LEAKS THE FILE HANDLE.  Only the
 *     success path reaches fclose (0x0041366e).  Six early returns, six
 *     leaks; reproduced exactly, including the order of the tests.
 *   - `line[strlen(line) - 1] = 0` is an unguarded strip.  On an empty line
 *     it writes one byte BEFORE the buffer.  fgets never returns "" -- it
 *     returns NULL instead -- so the index cannot go negative here, which is
 *     why the original gets away with it.  Kept as the original wrote it.
 *   - A zero column or row count is treated as failure, so a font whose
 *     grid is legitimately "0" cannot load.  That is the original's test.
 *   - The return value is a byte in AL with the upper three bytes left as
 *     whatever happened to be in EAX.  On success that is fclose's return
 *     (0x00413673 `MOV AL,1` over it), and on the ImportSceneTextures
 *     failure it is that call's own result.  Both are reproduced rather than
 *     normalised to 0/1: a caller that reads the full dword would see the
 *     original's bytes.
 *
 * The three CRT calls: fopen/fclose/fgets are our own CRT's.  The game's
 * CRT is no longer called for file I/O anywhere (its FILE is not shared
 * with any live game code).
 *
 * `parseintfromstring` 0x004505ac is NOT called: it is pure (char * in, int
 * out), which by the same rule makes it ours, and it is reimplemented below.
 */

/* MSVC's `atoi` (0x00450521, behind the 0x004505ac thunk), reimplemented.
 *
 * Classification goes through the GAME's own ctype table rather than our
 * CRT's, so "which bytes are space" and "which are digits" are identical by
 * construction rather than by assumption: the table pointer is the game's
 * `_pctype` at 0x00469f64, whose entries are 16-bit and are indexed here a
 * byte at a time with stride 2 -- exactly as the original indexes them.
 * Masks 8 and 4 are _SPACE and _DIGIT.
 *
 * The original also has an MBCS branch, taken when the game CRT's
 * `__mbcurmax` at 0x0046a170 is >= 2.  That branch is UNREACHABLE and this
 * is a static fact, not an assumption: the value is 1 in `.data` and a scan
 * of the whole of `.text` finds fifteen references to 0x0046a170, every one
 * of them a read.  Nothing in the binary writes it, so the single-byte path
 * is the only one that can run.  It is therefore the only one implemented,
 * and this comment is the record of why.
 *
 * Overflow wraps, because the original's accumulator is a plain `int`.
 */
/* The game's CRT classified through its C-locale ctype table (0x00469f64);
 * read out of the image, its _SPACE set is 9..13 and 32 and its _DIGIT set
 * '0'..'9', nothing above 0x7f -- so these two tests are that table. */
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

unsigned int TextRenderer::load(const char *path, Direct3D *d3d)
{
    ++g_nLoad;
    { static unsigned long seen; text_first("ReadBitmapFontFile", &seen); }

    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return 0;

    char line[0x100];

    /* Line 1 -- the atlas image. */
    if (fgets(line, 0xff, fp) == NULL)
        return 0;                              /* leaks fp, as the original does */
    line[strlen(line) - 1] = '\0';

    const unsigned int ok = Texture_ImportSceneTextures(
        this->atlas(), d3d->pDD4, d3d->pDevice, line, 1, 0, 0);
    if ((ok & 0xffu) == 0)
        return ok;                             /* its result, upper bytes and all */

    /* Line 2 -- columns. */
    if (fgets(line, 0xff, fp) == NULL)
        return 0;
    line[strlen(line) - 1] = '\0';
    cols_ = (unsigned int)font_atoi(line);
    if (cols_ == 0)
        return 0;

    /* Line 3 -- rows. */
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

    /* `MOV AL,1` over fclose's return: the low byte is the success flag and
     * the upper three are fclose's, which is what the original hands back. */
    const unsigned int closed = (unsigned int)fclose(fp);
    return (closed & 0xffffff00u) | 1u;
}

void TextRenderer::drawCentered(float x, float y, float cellW, float cellH,
                                float spacing, const char *str, Direct3D *d3d,
                                char firstChar, DWORD colourTop,
                                DWORD colourBottom)
{
    ++g_nCentred;
    { static unsigned long seen; text_first("DrawCenteredText", &seen); }

    drawLeft(x - text_width(str, cellW, spacing) * 0.5f, y, cellW, cellH,
             spacing, str, d3d, firstChar, colourTop, colourBottom);
}

void TextRenderer::drawRight(float x, float y, float cellW, float cellH,
                             float spacing, const char *str, Direct3D *d3d,
                             char firstChar, DWORD colourTop,
                             DWORD colourBottom)
{
    ++g_nRight;
    { static unsigned long seen; text_first("DrawRightAlignedText", &seen); }

    drawLeft(x - text_width(str, cellW, spacing), y, cellW, cellH,
             spacing, str, d3d, firstChar, colourTop, colourBottom);
}

/* ─── The exports patch.py's CALL_PATCHES redirects the game's sites to ───── */

extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
Text_RenderText(TextRenderer *self, float x, float y, float cellW, float cellH,
                float spacing, const char *str, Direct3D *d3d, char firstChar,
                DWORD colourTop, DWORD colourBottom)
{
    self->drawLeft(x, y, cellW, cellH, spacing, str, d3d, firstChar,
                   colourTop, colourBottom);
}

__declspec(dllexport) void __attribute__((thiscall))
Text_DrawCentered(TextRenderer *self, float x, float y, float cellW,
                  float cellH, float spacing, const char *str, Direct3D *d3d,
                  char firstChar, DWORD colourTop, DWORD colourBottom)
{
    self->drawCentered(x, y, cellW, cellH, spacing, str, d3d, firstChar,
                       colourTop, colourBottom);
}

__declspec(dllexport) void __attribute__((thiscall))
Text_DrawRightAligned(TextRenderer *self, float x, float y, float cellW,
                      float cellH, float spacing, const char *str,
                      Direct3D *d3d, char firstChar,
                      DWORD colourTop, DWORD colourBottom)
{
    self->drawRight(x, y, cellW, cellH, spacing, str, d3d, firstChar,
                    colourTop, colourBottom);
}

__declspec(dllexport) unsigned int __attribute__((thiscall))
Text_LoadFont(TextRenderer *self, const char *path, Direct3D *d3d)
{
    return self->load(path, d3d);
}

} /* extern "C" */

/* ─── The big-text pair: 0x00413d90 and 0x00413990 ────────────────────────
 *
 * ENDGAME_PLAN.md E1's last text cycle.  0x00413d90 was a `callback` -- our
 * textrenderer.cpp called it by absolute address -- and it had to go with
 * 0x00413990 rather than before it: the wrapper's whole body is a tail call
 * into the renderer, so replacing it alone would have swapped one callback
 * for another rather than retiring one.
 *
 * xref.py over Karoo.exe.orig: 0x00413d90 has two CALL sites (0x434046,
 * 0x4356a6) and nothing else; 0x00413990 has exactly ONE reference in the
 * entire binary and it is the wrapper's own CALL at 0x413e17.  No JMP, no
 * DATA push, no vtable slot for either.  A grep of karoo-hooks/ for both
 * addresses found only this file's own ORIG_DRAW_BIG_TEXT, now gone --
 * CLAUDE.md's "xref.py cannot see callers inside our DLL" step, which is the
 * one that has bitten twice.
 *
 * ── The renderer, from the disassembly ──────────────────────────────────────
 * The decompiler puts this function's thirteen stack arguments in the wrong
 * places -- `unaff_retaddr`, `in_stack_0000001c` and a `float *piVar2` used
 * as a float are the giveaways -- so, exactly as with RenderText, the frame
 * was recovered by counting from the prologue instead.  `SUB ESP,0x15c` plus
 * four pushes puts the argument block at [ESP+0x170] upwards:
 *
 *   +0x170 x   +0x174 y   +0x178 cellW  +0x17c cellH  +0x180 spacing
 *   +0x184 str +0x188 d3d +0x18c firstChar
 *   +0x190 colourTop  +0x194 colourBottom
 *   +0x198 amplitude  +0x19c rate  +0x1a0 n
 *
 * Which is what settles the two names this file's own header had wrong.
 * +0x198 is the multiplier applied to FSIN's RESULT (0x413b8e) -- an
 * amplitude.  +0x19c multiplies `n` ONCE, before the loop (0x413a5d), to form
 * the starting phase -- a rate.  Neither is an outline width; there is no
 * second pass and no outline anywhere in the function.
 *
 * ── What it draws ───────────────────────────────────────────────────────────
 * Per glyph, with g = (unsigned char)(str[i] - firstChar) and i2 = 2 * i:
 *
 *   dy   = cellH * 0.5f + amplitude * sin(n * rate + i2)
 *   quad = (x, y-dy) (x+cellW, y-dy) (x+cellW, y+dy) (x, y+dy)
 *
 * so `y` is the row's CENTRE here, where RenderText's `y` is its top, and
 * each glyph's quad grows and shrinks vertically about that centre two
 * radians out of phase with its neighbour.  Everything else is RenderText's:
 * FVF 0x1C4 (32 bytes -- XYZRHW | DIFFUSE | SPECULAR | TEX1), z = 0.1f,
 * rhw = 10.0f, specular = 0xff000000 in all four, colourTop on the two top
 * vertices and colourBottom on the two bottom ones, D3DPT_TRIANGLEFAN of 4.
 *
 * Quirks preserved, and they are RenderText's quirks in the same order:
 *   - SetTexture and the three SetRenderState calls precede the empty-string
 *     test (0x4139e6..0x413a26), so drawing "" still leaves alpha blending on
 *     and the atlas bound.
 *   - strlen is re-run on every iteration (0x413cd9) rather than hoisted.
 *   - the pen position is advanced IN THE ARGUMENT SLOT (0x413cdb writes back
 *     to [ESP+0x170]); by-value `x` here reproduces that exactly, since the
 *     caller's copy is a push the callee owns.
 *
 * ── The two arithmetic chains ───────────────────────────────────────────────
 * Plain C.  The original computes both on the x87 stack in extended precision
 * with a single FSTP to float at the end, and `sin` here is not the same
 * function as the original's FSIN, so the last bit or two of `dy` may differ.
 * That does not matter: dy is a screen coordinate handed straight to
 * DrawPrimitive, so the visible consequence is bounded by a sub-pixel, and
 * nothing downstream reads it back.  Readable code is worth more than a
 * bit-identical float here -- an asm chain would buy precision nobody can
 * observe at the cost of a function nobody can read.
 *
 * What IS preserved is the part that changes results rather than rounding:
 * both counters are FILD'd as QWORDS with the high dword written as zero
 * (0x413a33, 0x413b3a), so they are UNSIGNED 64-bit loads rather than
 * sign-extended ints.  `n` comes in as a signed int and a negative one would
 * take a different branch entirely if it were sign-extended, so the cast is
 * load-bearing and stays.
 */
static float wobble_phase(unsigned int n, float rate)
{
    return (float)((double)n * rate);
}

static float wobble_dy(int i2, float phase, float amplitude, float halfH)
{
    return (float)(sin((double)(unsigned int)i2 + phase) * amplitude + halfH);
}

void TextRenderer::drawWobble(float x, float y, float cellW, float cellH,
                              float spacing, const char *str, Direct3D *d3d,
                              char firstChar, DWORD colourTop,
                              DWORD colourBottom, float amplitude, float rate,
                              int n)
{
    ++g_nWobble;
    { static unsigned long seen; text_first("DrawWobbleGlyphRow", &seen); }

    const float invCols = 1.0f / (float)(int)cols_;
    const float invRows = 1.0f / (float)(int)rows_;

    IDirect3DDevice3 *dev = d3d->pDevice;
    dev->SetTexture(0, atlas_.pTexture2);
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND,         D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND,        D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);

    if (strlen(str) == 0)
        return;

    const float phase = wobble_phase((unsigned int)n, rate);
    const float halfH = cellH * 0.5f;
    const float advance = cellW * spacing;

    /* The per-glyph phase step.  +2 is the original's; bigwave makes it -2,
     * which reverses the direction the wave travels along the string without
     * moving the string, changing its size, or touching the pen. */
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

        dev->DrawPrimitive(D3DPT_TRIANGLEFAN, TEXT_FVF, quad, 4, 0);
        ++g_nWobbleGlyphs;

        i2 += step;
        ++i;
        x += advance;
        /* strlen every iteration, as the original does. */
    } while (i < strlen(str));
}

void TextRenderer::drawBig(float x, float y, float cellW, float cellH,
                           float spacing, const char *str, Direct3D *d3d,
                           char firstChar, DWORD colourTop, DWORD colourBottom,
                           float amplitude, float rate, int n)
{
    ++g_nBig;
    { static unsigned long seen; text_first("DrawBigText", &seen); }

    /* Bit for bit drawCentered's arithmetic -- the same `len -
     * (len - 1) * (1 - spacing)` width, the same FMUL by the 0.5f at
     * 0x0045d318 -- with the three extra arguments passed straight through. */
    drawWobble(x - text_width(str, cellW, spacing) * 0.5f, y, cellW, cellH,
               spacing, str, d3d, firstChar, colourTop, colourBottom,
               amplitude, rate, n);
}

/* ─── The two exports patch.py redirects to ────────────────────────────────
 *
 * Only Text_DrawBigText has game call sites; Text_DrawWobbleGlyphRow exists so
 * that the inner original can be UD2-stubbed with a named replacement behind
 * it, which is what makes progress.py count it `replaced` rather than `dead`.
 */
extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
Text_DrawBigText(TextRenderer *self, float x, float y, float cellW,
                 float cellH, float spacing, const char *str, Direct3D *d3d,
                 char firstChar, DWORD colourTop, DWORD colourBottom,
                 float amplitude, float rate, int n)
{
    self->drawBig(x, y, cellW, cellH, spacing, str, d3d, firstChar,
                  colourTop, colourBottom, amplitude, rate, n);
}

__declspec(dllexport) void __attribute__((thiscall))
Text_DrawWobbleGlyphRow(TextRenderer *self, float x, float y, float cellW,
                        float cellH, float spacing, const char *str,
                        Direct3D *d3d, char firstChar, DWORD colourTop,
                        DWORD colourBottom, float amplitude, float rate,
                        int n)
{
    self->drawWobble(x, y, cellW, cellH, spacing, str, d3d, firstChar,
                     colourTop, colourBottom, amplitude, rate, n);
}

} /* extern "C" */

/* ─── The lifecycle: 0x4134d0 ctor, 0x413510 dtor body, 0x4134f0 scalar ────
 *
 * Reached only from the static-init / atexit thunks (0x425e30, 0x425e60).
 * Own one-slot vtable; the game's 0x45d3a8 is a tripwire (byte scan: only
 * the replaced ctor and dtor write it).  The scalar dtor is unreached -- a
 * global is never deleted -- and frees on the game heap by precedent. */
static void *const g_TextVtable[1] = { (void *)&Text_ScalarDtor };

void TextRenderer::construct()
{
    Texture_SceneCtor(atlas());
    vtable_ = g_TextVtable;
    rows_ = 0;
    cols_ = 0;
}

void TextRenderer::destruct()
{
    vtable_ = g_TextVtable;
    Texture_SceneDtorBody(atlas());
}

extern "C" __declspec(dllexport) TextRenderer *__attribute__((thiscall))
Text_Construct(TextRenderer *self)
{
    self->construct();
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DtorBody(TextRenderer *self)
{
    self->destruct();
}

extern "C" __declspec(dllexport) TextRenderer *__attribute__((thiscall))
Text_ScalarDtor(TextRenderer *self, unsigned int flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* ─── 0x00413eb0 DrawTextPanel (ENDGAME E5) ────────────────────────────────
 *
 * __thiscall, twelve stack arguments, RET 0x30.  One caller, RenderGameFrame
 * 0x42C2CF.  Written from the listing.  A multi-line caption over two
 * full-width backdrop strips:
 *
 *   - lines = 1 + the '\n' count; the text block is raised so its last line
 *     sits at y: y -= lines * lineH.
 *   - SRCBLEND 5 / DESTBLEND 6 / ALPHABLEND 1.
 *   - strip 1 (panelTex, or no texture): screen width W, from
 *     y - W*0.009375 down to the screen height H, diffuse white, specular 0,
 *     z 0 / rhw 10, uv the atlas's centre (0.4..0.6).  Strip order is
 *     (W,top) (W,H) (0,top) (0,H), a TRIANGLESTRIP.
 *   - strip 2, only when frameTex is non-NULL: y - W*0.015625 to
 *     y - W*0.00625, uv 0..1, same strip order.
 *   - the glyphs: the atlas texture, one TRIANGLEFAN per character, the cell
 *     index the raw unsigned byte (no firstChar here, unlike drawLeft), the
 *     cell uv from unsigned divides; '\n' returns x to the start and moves
 *     y down one lineH.  The pen advances cellW * spacing.
 *   - ALPHABLENDENABLE 0.
 *
 * The strip vertices' z is 0 while the glyphs' is 0.1: both as the original. */
void TextRenderer::drawPanel(float x, float y, float cellW, float cellH,
                             float spacing, float lineH, const char *str,
                             Direct3D *d3d, DWORD colourTop, DWORD colourBottom,
                             SceneTexture *panelTex, SceneTexture *frameTex)
{
    const float du = 1.0f / (float)cols_;      /* FILD qword: unsigned */
    const float dv = 1.0f / (float)rows_;

    unsigned int lines = 1;
    for (unsigned int i = 0; i < strlen(str); ++i)
        if (str[i] == '\n')
            ++lines;
    const float x0 = x;
    y = y - (float)lines * lineH;

    IDirect3DDevice3 *dev = d3d->pDevice;
    dev->SetRenderState(D3DRENDERSTATE_SRCBLEND,         D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRENDERSTATE_DESTBLEND,        D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);

    const float W = (float)d3d->pSelectedMode->dwWidth;
    const float H = (float)d3d->pSelectedMode->dwHeight;

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
    dev->SetTexture(0, panelTex ? panelTex->pTexture2 : NULL);
    dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, TEXT_FVF, strip, 4, 0);

    const float top2 = y - W * 0.015625f;
    const float bot2 = y - W * 0.00625f;
    strip[0].x = W; strip[0].y = top2; strip[0].u = 1.0f; strip[0].v = 0.0f;
    strip[1].x = W; strip[1].y = bot2; strip[1].u = 1.0f; strip[1].v = 1.0f;
    strip[2].x = 0; strip[2].y = top2; strip[2].u = 0.0f; strip[2].v = 0.0f;
    strip[3].x = 0; strip[3].y = bot2; strip[3].u = 0.0f; strip[3].v = 1.0f;
    if (frameTex) {
        dev->SetTexture(0, frameTex->pTexture2);
        dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, TEXT_FVF, strip, 4, 0);
    }

    dev->SetTexture(0, atlas_.pTexture2);

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
        dev->DrawPrimitive(D3DPT_TRIANGLEFAN, TEXT_FVF, quad, 4, 0);

        x += cellW * spacing;
    }

    dev->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 0);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Text_DrawPanelText(TextRenderer *self, float x, float y, float cellW,
                   float cellH, float spacing, float lineH, const char *str,
                   Direct3D *d3d, DWORD colourTop, DWORD colourBottom,
                   SceneTexture *panelTex, SceneTexture *frameTex)
{
    self->drawPanel(x, y, cellW, cellH, spacing, lineH, str, d3d,
                    colourTop, colourBottom, panelTex, frameTex);
}
