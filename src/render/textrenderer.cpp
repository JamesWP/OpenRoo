/* TextRenderer -- three of the four entry points reimplemented.
 *
 *   0x00413690 RenderText            __thiscall(this, 10 args)  RET 0x28
 *   0x00413d00 DrawCenteredText      __thiscall(this, 10 args)  RET 0x28
 *   0x00413e30 DrawRightAlignedText  __thiscall(this, 10 args)  RET 0x28
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

#include <stdio.h>
#include <string.h>

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
 * Blast radius, chosen against the gate that hosts it: this moves glyph quads
 * only.  It writes no coordinate, axis or tile index back into the world, so
 * it cannot reach the unbounded bridge/slide spawn scans that crash
 * levelreport.py.
 */
enum TextFx { TEXT_FX_OFF = 0, TEXT_FX_MIRROR = 1, TEXT_FX_LOADSWAP = 2 };

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
    }
    log_write("textrenderer: FX mode = %s\n",
              fx == TEXT_FX_MIRROR   ? "mirror" :
              fx == TEXT_FX_LOADSWAP ? "loadswap" : "off");
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
              "centred=%lu right=%lu load=%lu\n",
              g_nRender, g_nEmpty, g_nGlyphs, g_nCentred, g_nRight, g_nLoad);
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
 * The three CRT calls: fopen/fclose/fgets all stay the game's, through
 * gamecrt.h.  fgets is stateful in the strongest sense -- it inlines getc,
 * walking `fp->_cnt` and `fp->_ptr` directly -- so it must be the CRT that
 * owns the FILE.  That is gamecrt.h's own rule, not a new exception.
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
#define GAME_CTYPE_TABLE  (*(const unsigned char *const *)0x00469f64)
#define GAME_MB_CUR_MAX   (*(const int *)0x0046a170)

static int font_atoi(const char *p)
{
    const unsigned char *ctype = GAME_CTYPE_TABLE;

    while (ctype[(unsigned char)*p * 2] & 8)   /* _SPACE */
        ++p;

    const unsigned char sign = (unsigned char)*p;
    if (sign == '-' || sign == '+')
        ++p;

    int acc = 0;
    while (ctype[(unsigned char)*p * 2] & 4) { /* _DIGIT */
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

/* ─── The class methods the rest of karoo-hooks/ calls ────────────────────
 *
 * Call our own reimplementations through the owning header, never by
 * redeclaring the export (CLAUDE.md).  drawBig is the one still forwarding to
 * the game: 0x00413d90 is a centring wrapper exactly like drawCentered, but
 * around FUN_00413990 (207 instructions, one reference, its only reference
 * being that wrapper).  Replacing the wrapper without the renderer would just
 * move the callback, so both go in the next cycle.
 */
#define THISCALL __attribute__((thiscall))

typedef void (THISCALL *bigtext_fn)(TextRenderer *self, float x, float y,
                                    float cellW, float cellH, float spacing,
                                    const char *str, Direct3D *d3d,
                                    char firstChar, DWORD colourTop,
                                    DWORD colourBottom, float outline,
                                    float wobble, int n);

#define ORIG_DRAW_BIG_TEXT   ((bigtext_fn)0x00413d90)

void TextRenderer::drawBig(float x, float y, float cellW, float cellH,
                           float spacing, const char *str, Direct3D *d3d,
                           char firstChar, DWORD colourTop, DWORD colourBottom,
                           float outline, float wobble, int n)
{
    ORIG_DRAW_BIG_TEXT(this, x, y, cellW, cellH, spacing, str, d3d, firstChar,
                       colourTop, colourBottom, outline, wobble, n);
}
