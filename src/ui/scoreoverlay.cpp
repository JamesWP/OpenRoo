/* DrawHighScoreTable (0x434f90) + DrawGameOverScore (0x435420) reimplementation.
 *
 * The two end-of-game overlays, and the last pair of "overlay quad + text"
 * functions in the render closure.  Both are
 *
 *   __cdecl(GameGlobal *g, Game *game, Direct3D *d3d, TextRenderer *text, int n)
 *
 * — FIVE dword args, not the three the closure table in RENDER_PLAN.md
 * credits them with.  The extra two were found in the disassembly, not the
 * decompile: `this` for every text call is arg4 (read as [ESP+0x238] at a
 * push depth of 0x34), and arg5 is the integer the caller derives from a
 * double at 0x0042CA27 / 0x0042CA62.  Ghidra renders both functions with
 * phantom uninitialised locals (iStack_1c, iStack_68, uStack_5c) precisely
 * because it lost those two arguments, so this file is written from the
 * listing throughout.  Each has exactly one E8 call site, both in
 * RenderGameFrame (0x42CA7E and 0x42CA42), and no E9/PUSH/DATA refs.
 *
 * ── The shared backdrop quad ──────────────────────────────────────────────
 * Both open with the identical block:
 *
 *   SetRenderState(ALPHABLENDENABLE, 1)
 *   SetRenderState(SRCBLEND,  D3DBLEND_SRCALPHA)      // 5
 *   SetRenderState(DESTBLEND, D3DBLEND_INVSRCALPHA)   // 6
 *   SetTexture(0, game->overlayTexture ? tex->pTexture2 : NULL)
 *   DrawPrimitive(TRIANGLESTRIP, FVF 0x1c4, quad, 4, 0)
 *
 * The quad is four D3DTLVERTEX built on the stack, in strip order
 * (w,0) (w,h) (0,0) (0,h), z=0, rhw=10.0, diffuse=0xffffffff, specular=0,
 * and UVs taken from the 0.4/0.6 corners of the texture — i.e. it samples the
 * middle ~20% of the overlay bitmap and stretches it over the whole screen.
 * HighScoreTable then draws a *second* quad, from the pre-built global vertex
 * array at 0x004E0580 with the texture at 0x004E0760 (both .bss, filled
 * elsewhere), for the leaderboard panel.
 *
 * ── Coordinates ───────────────────────────────────────────────────────────
 * Everything is authored in a 640-wide virtual space and scaled by
 * width/640 (K = 0.0015625 at 0x0045D4C4).  Note both axes scale by *width*,
 * not by width and height respectively — on a non-4:3 mode the layout stays
 * square and does not fill the screen vertically.  That is the original's
 * behaviour and is reproduced.
 *
 * Glyph cell is 12x14 virtual, scale 0.75, for every row of both functions.
 *
 * HighScoreTable: one row per entry, y = 20*i + 180, columns 187 (name, %s),
 * 363 (the byte at record+0x36, %d) and 453 (the dword at record+0x32, %d).
 * The record stride is 0x37 and tiles exactly: char name[0x32], DWORD, BYTE.
 * The count is the *byte* at g+0x1404C0 and the loop is `while (i < count)`
 * after an unsigned `count == 0` early-out.
 *
 * GameOverScore: a centred "GAME OVER" through 0x413D90, then eight rows at
 * y = 180, 200, 220, 240, 260, 280, 300, 330, and a centred "...press Enter"
 * at y = 380.  Six rows are four cells wide — label at x=130, a value at
 * x=380, a multiplier caption at x=380, a product at x=510 — and the last two
 * (level score, total score) are label + value at x=510.  The value columns go
 * through 0x413E30 (right-aligned) and the captions through 0x413690.
 *
 * ── What is NOT owned here ────────────────────────────────────────────────
 * The four text entry points (0x413690 RenderText, 0x413E30, 0x413D90,
 * 0x413D00 DrawCenteredText) and the sprintf at 0x450655 stay the game's and
 * are called through, so all glyph traffic still reaches the proxy layer the
 * same way.  All five are __thiscall/__cdecl exactly as declared below; the
 * text ones are callee-cleanup, confirmed by the caller reading [ESP+0x1dc]
 * as arg1 immediately after each call.
 *
 * The two argument pointers stay opaque byte bases: the fields live at
 * offsets like +0x1404DD and +0x6F984 in structures nothing in this project
 * has mapped, and inventing a layout for them would be a guess.  They are
 * named by what the code does with them, and read at the literal offsets.
 *
 * KAROO_SCORE_FX visual-proof modes (read by value, never by presence):
 *   tint   — backdrop quad diffuse magenta instead of 0xFFFFFFFF.  Only these
 *            two functions build that quad, so a magenta wash behind the
 *            high-score table or the game-over screen is proof the vertices
 *            on screen are the ones written here.
 *   nodraw — skip the backdrop DrawPrimitive(s), leaving every render state
 *            and every text row untouched: the panels vanish, the text stays.
 */
#include "direct3d.h"
#include "texture.h"
#include "log.h"

#define THISCALL __attribute__((thiscall))

#define OVERLAY_FVF   0x1c4     /* XYZRHW | DIFFUSE | SPECULAR | TEX1 */
#define SCORE_LOG_FIRST 4

/* 1/640 — the virtual-space scale, float at 0x0045D4C4. */
#define VSCALE (*(const float *)0x0045d4c4)

/* Format strings and captions, referenced at their own addresses so the
 * bytes handed to the game's sprintf are literally the game's own. */
#define FMT_S    ((const char *)0x004641f8)   /* "%s" */
#define FMT_D    ((const char *)0x004668b8)   /* "%d" */

/* .bss globals: the leaderboard panel's pre-built quad and its texture. */
#define g_pPanelVerts   ((const void *)0x004e0580)
#define g_pPanelTexture (*(IDirect3DTexture2 **)0x004e0760)

/* GameGlobal (*0x0046C498) — high-score table. */
#define HS_COUNT_OFF   0x1404c0   /* BYTE  entry count                        */
#define HS_TABLE_OFF   0x13cdc0   /* first record; stride 0x37                */
#define HS_STRIDE      0x37
#define HS_NAME_OFF    0x00       /* char[0x32], %s                           */
#define HS_SCORE_OFF   0x32       /* DWORD, %d, drawn in the last column      */
#define HS_LEVEL_OFF   0x36       /* BYTE,  %d, drawn in the middle column    */

/* GameGlobal — game-over score breakdown.  Unaligned dwords, in the order the
 * original reads them (which is not the order they sit in memory). */
#define GO_V(off)  (*(const DWORD *)((const BYTE *)g + (off)))

/* Game (0x0046C890) — the fonts and the overlay texture. */
#define GM_P(off)  (*(void **)((BYTE *)game + (off)))
#define GM_OVERLAY_TEX  0x6f8a8   /* SceneTexture* for the backdrop quad      */
#define GM_HS_FONT_A    0x6f914   /* high-score rows                          */
#define GM_HS_FONT_B    0x6f918
#define GM_GO_FONT_A    0x6f984   /* game-over rows                           */
#define GM_GO_FONT_B    0x6f988
#define GM_PE_FONT_A    0x6f8cc   /* "...press Enter"                         */
#define GM_PE_FONT_B    0x6f8d0

/* ─── The game's own text entry points, called through ───────────────────── */

typedef void (THISCALL *text_fn)(void *self, float x, float y,
                                 float cellW, float cellH, float scale,
                                 const char *str, Direct3D *d3d, DWORD zero,
                                 void *fontA, void *fontB);
typedef void (THISCALL *bigtext_fn)(void *self, float x, float y,
                                    float cellW, float cellH, float scale,
                                    const char *str, Direct3D *d3d, DWORD zero,
                                    DWORD colourA, DWORD colourB,
                                    float outline, float wobble, int n);
typedef int (__cdecl *sprintf_fn)(char *, const char *, ...);

#define ORIG_RENDER_TEXT     ((text_fn)0x00413690)     /* left-aligned        */
#define ORIG_RENDER_TEXT_R   ((text_fn)0x00413e30)     /* the value columns   */
#define ORIG_DRAW_BIG_TEXT   ((bigtext_fn)0x00413d90)  /* "GAME OVER"         */
#define ORIG_DRAW_CENTERED   ((text_fn)0x00413d00)     /* "...press Enter"    */
#define ORIG_MAYBE_SPRINTF   ((sprintf_fn)0x00450655)

/* ─── FX mode ────────────────────────────────────────────────────────────── */

enum ScoreFx { SCORE_FX_OFF = 0, SCORE_FX_TINT, SCORE_FX_NODRAW };

static ScoreFx score_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (ScoreFx)cached;
    char buf[32];
    /* By value, not by presence: GetEnvironmentVariableA returns 0 for both
     * unset and empty, which is what we want. */
    DWORD n = GetEnvironmentVariableA("KAROO_SCORE_FX", buf, sizeof(buf));
    ScoreFx fx = SCORE_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "tint") == 0)        fx = SCORE_FX_TINT;
        else if (lstrcmpiA(buf, "nodraw") == 0) fx = SCORE_FX_NODRAW;
    }
    log_write("scoreoverlay: FX mode = %s\n",
               fx == SCORE_FX_TINT   ? "tint"   :
               fx == SCORE_FX_NODRAW ? "nodraw" : "off");
    cached = (int)fx;
    return fx;
}

/* ─── The shared backdrop ────────────────────────────────────────────────── */

struct TLVertex {          /* FVF 0x1c4 */
    float x, y, z, rhw;
    DWORD diffuse, specular;
    float tu, tv;
};
static_assert(sizeof(TLVertex) == 0x20, "TLVertex size mismatch");

/* Build the four-vertex strip the two functions share.  The originals write
 * the same eight dwords four times through one stack scratch vertex; the
 * values, not the copying, are what matters. */
static void build_backdrop(TLVertex v[4], float w, float h)
{
    const DWORD diffuse = (score_fx() == SCORE_FX_TINT) ? 0xffff00ff : 0xffffffff;
    static const float u[4] = { 0.6f, 0.6f, 0.4f, 0.4f };
    static const float t[4] = { 0.4f, 0.6f, 0.4f, 0.6f };
    const float xs[4] = { w, w, 0.0f, 0.0f };
    const float ys[4] = { 0.0f, h, 0.0f, h };
    for (int i = 0; i < 4; i++) {
        v[i].x = xs[i];
        v[i].y = ys[i];
        v[i].z = 0.0f;
        v[i].rhw = 10.0f;
        v[i].diffuse = diffuse;
        v[i].specular = 0;
        v[i].tu = u[i];
        v[i].tv = t[i];
    }
}

/* The alpha-blend + overlay-texture preamble both functions open with.  The
 * originals re-read d3d->pDevice before every dispatch rather than caching
 * it; kept, so the traffic through the proxy layer is call-for-call
 * identical. */
static void setup_overlay_state(Direct3D *d3d, void *game)
{
    d3d->pDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
    d3d->pDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND,  D3DBLEND_SRCALPHA);
    d3d->pDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);

    SceneTexture *tex = (SceneTexture *)GM_P(GM_OVERLAY_TEX);
    d3d->pDevice->SetTexture(0, tex ? tex->pTexture2 : NULL);
}

/* ─── DrawHighScoreTable (0x434f90) ──────────────────────────────────────── */

extern "C" __declspec(dllexport) void __cdecl
Score_DrawHighScoreTable(void *g, void *game, Direct3D *d3d, void *text, int n)
{
    (void)n;   /* arg5 is pushed by the caller and never read here. */

    const float w = (float)d3d->pSelectedMode->dwWidth;
    const float h = (float)d3d->pSelectedMode->dwHeight;

    TLVertex quad[4];
    build_backdrop(quad, w, h);

    setup_overlay_state(d3d, game);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, OVERLAY_FVF,
                                    quad, 4, 0);

    d3d->pDevice->SetTexture(0, g_pPanelTexture);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, OVERLAY_FVF,
                                    (LPVOID)g_pPanelVerts, 4, 0);

    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SCORE_LOG_FIRST)
        log_write("scoreoverlay: highscore %.0fx%.0f entries=%u\n",
                   w, h, (unsigned)*((const BYTE *)g + HS_COUNT_OFF));

    /* Unsigned early-out, then a signed loop against a re-read count — both
     * as in the original. */
    if (*((const BYTE *)g + HS_COUNT_OFF) == 0)
        return;

    const float cellW = (float)(d3d->pSelectedMode->dwWidth * 12) * VSCALE;
    const float cellH = (float)(d3d->pSelectedMode->dwWidth * 14) * VSCALE;
    const float xName  = (float)(d3d->pSelectedMode->dwWidth * 187) * VSCALE;
    const float xLevel = (float)(d3d->pSelectedMode->dwWidth * 363) * VSCALE;
    const float xScore = (float)(d3d->pSelectedMode->dwWidth * 453) * VSCALE;

    char buf[256];
    int row = 0, dy = 0;
    do {
        const BYTE *rec = (const BYTE *)g + HS_TABLE_OFF + row * HS_STRIDE;
        const float y = ((float)dy + 180.0f) * w * VSCALE;

        ORIG_MAYBE_SPRINTF(buf, FMT_S, rec + HS_NAME_OFF);
        ORIG_RENDER_TEXT(text, xName, y, cellW, cellH, 0.75f, buf, d3d, 0,
                         GM_P(GM_HS_FONT_A), GM_P(GM_HS_FONT_B));

        ORIG_MAYBE_SPRINTF(buf, FMT_D, (unsigned)rec[HS_LEVEL_OFF]);
        ORIG_RENDER_TEXT_R(text, xLevel, y, cellW, cellH, 0.75f, buf, d3d, 0,
                           GM_P(GM_HS_FONT_A), GM_P(GM_HS_FONT_B));

        ORIG_MAYBE_SPRINTF(buf, FMT_D, *(const DWORD *)(rec + HS_SCORE_OFF));
        ORIG_RENDER_TEXT_R(text, xScore, y, cellW, cellH, 0.75f, buf, d3d, 0,
                           GM_P(GM_HS_FONT_A), GM_P(GM_HS_FONT_B));

        dy += 20;
        row++;
    } while (row < (int)(unsigned)*((const BYTE *)g + HS_COUNT_OFF));
}

/* ─── DrawGameOverScore (0x435420) ───────────────────────────────────────── */

/* One breakdown row: caption at x=130, its value right-aligned at x=380, the
 * multiplier caption at x=380, and the product right-aligned at x=510.  Rows
 * 7 and 8 have no multiplier and pass mul == NULL. */
struct ScoreRow {
    float        vy;        /* virtual y / 640 — the original's own constant */
    const char  *label;     /* caption at x = 130                            */
    unsigned     valOff;    /* GameGlobal offset of the count (unaligned)     */
    const char  *mul;       /* caption at x = 380, or NULL                   */
    unsigned     prodOff;   /* GameGlobal offset of the product, if mul       */
};

static const ScoreRow k_rows[] = {
    { 0.28125f, (const char *)0x00466ce0, 0x1404dd, (const char *)0x00466cd8, 0x1404c1 },
    { 0.3125f, (const char *)0x00466cc8, 0x1404e1, (const char *)0x00466cc0, 0x1404c5 },
    { 0.34375f, (const char *)0x00466cac, 0x1404e9, (const char *)0x00466ca4, 0x1404cd },
    { 0.375f, (const char *)0x00466c98, 0x1404e5, (const char *)0x00466c90, 0x1404c9 },
    { 0.40625f, (const char *)0x00466c80, 0x1404ed, (const char *)0x00466cd8, 0x1404d1 },
    { 0.4375f, (const char *)0x00466c74, 0x1404f1, (const char *)0x00466c6c, 0x1404d5 },
    { 0.46875f, (const char *)0x00466c5c, 0x1404f5, NULL,                     0 },
    { 0.515625f, (const char *)0x00466c4c, 0x1404f9, NULL,                     0 },
};

extern "C" __declspec(dllexport) void __cdecl
Score_DrawGameOverScore(void *g, void *game, Direct3D *d3d, void *text, int n)
{
    const DWORD dwWidth = d3d->pSelectedMode->dwWidth;
    const float w = (float)dwWidth;
    const float h = (float)d3d->pSelectedMode->dwHeight;

    TLVertex quad[4];
    build_backdrop(quad, w, h);

    setup_overlay_state(d3d, game);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, OVERLAY_FVF,
                                    quad, 4, 0);

    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SCORE_LOG_FIRST)
        log_write("scoreoverlay: gameover %.0fx%.0f n=%d\n", w, h, n);

    const float S = w * VSCALE;          /* one virtual unit, in pixels */

    /* "GAME OVER" — 24-unit cell, scale 0.8, two colours and two extra
     * floats the smaller entry point does not take. */
    ORIG_DRAW_BIG_TEXT(text, w * 0.5f, S * 130.0f, S * 24.0f, S * 24.0f, 0.8f,
                       (const char *)0x00466cfc, d3d, 0,
                       0xffffff00, 0xffff0000, S * 3.0f, 0.01f, n);

    const float cellW = (float)(dwWidth * 12) * VSCALE;
    const float cellH = (float)(dwWidth * 14) * VSCALE;
    const float xLabel = (float)(dwWidth * 130) * VSCALE;
    const float xValue = (float)(dwWidth * 380) * VSCALE;
    const float xProd  = (float)(dwWidth * 510) * VSCALE;

    char buf[256];
    for (unsigned i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); i++) {
        const ScoreRow &r = k_rows[i];
        const float y = w * r.vy;

        ORIG_RENDER_TEXT(text, xLabel, y, cellW, cellH, 0.75f, r.label, d3d, 0,
                         GM_P(GM_GO_FONT_A), GM_P(GM_GO_FONT_B));

        ORIG_MAYBE_SPRINTF(buf, FMT_D, GO_V(r.valOff));
        ORIG_RENDER_TEXT_R(text, r.mul ? xValue : xProd, y, cellW, cellH, 0.75f,
                           buf, d3d, 0,
                           GM_P(GM_GO_FONT_A), GM_P(GM_GO_FONT_B));

        if (r.mul) {
            ORIG_RENDER_TEXT(text, xValue, y, cellW, cellH, 0.75f, r.mul, d3d, 0,
                             GM_P(GM_GO_FONT_A), GM_P(GM_GO_FONT_B));
            ORIG_MAYBE_SPRINTF(buf, FMT_D, GO_V(r.prodOff));
            ORIG_RENDER_TEXT_R(text, xProd, y, cellW, cellH, 0.75f, buf, d3d, 0,
                               GM_P(GM_GO_FONT_A), GM_P(GM_GO_FONT_B));
        }
    }

    ORIG_DRAW_CENTERED(text, w * 0.5f, w * 0.59375f, cellW, cellH, 0.75f,
                       (const char *)0x00466898, d3d, 0,
                       GM_P(GM_PE_FONT_A), GM_P(GM_PE_FONT_B));
}
