/* DrawHighScoreTable (0x434f90) + DrawGameOverScore (0x435420) reimplementation.
 *
 * The two end-of-game overlays, and the last pair of "overlay quad + text"
 * functions in the render closure.  Both are
 *
 *   __cdecl(Game *g, void *game, Direct3D *d3d, TextRenderer *text, int n)
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
 * same way.  The four now have an owner -- TextRenderer, a placeholder class
 * in textrenderer.h, on the SoundManager pattern -- so this file calls
 * methods and no call site changes when one is replaced.  All five are
 * __thiscall/__cdecl exactly as declared there; the text ones are
 * callee-cleanup, confirmed by the caller reading [ESP+0x1dc] as arg1
 * immediately after each call.
 *
 * arg1 is the Game (Game::instance(), *0x0046c498), so it is typed.  `game`
 * stays an opaque byte base: its fields live at offsets like +0x6F984 in a
 * structure nothing in this project has mapped, and inventing a layout for
 * it would be a guess.  It is named by what the code does with it, and read
 * at the literal offsets.
 *
 * KAROO_SCORE_FX visual-proof modes (read by value, never by presence):
 *   tint   — backdrop quad diffuse magenta instead of 0xFFFFFFFF.  Only these
 *            two functions build that quad, so a magenta wash behind the
 *            high-score table or the game-over screen is proof the vertices
 *            on screen are the ones written here.
 *   nodraw — skip the backdrop DrawPrimitive(s), leaving every render state
 *            and every text row untouched: the panels vanish, the text stays.
 */
#include <stdio.h>
#include "direct3d.h"
#include "texture.h"
#include "log.h"
#include "game.h"
#include "textrenderer.h"
#include "gamestr.h"
#include "menuscreens.h"

#define OVERLAY_FVF   0x1c4     /* XYZRHW | DIFFUSE | SPECULAR | TEX1 */
#define SCORE_LOG_FIRST 4

/* 1/640 — the virtual-space scale, float at 0x0045D4C4. */
#define VSCALE (*(const float *)0x0045d4c4)

/* Format strings and captions, referenced at their own addresses so the
 * bytes handed to the game's sprintf are literally the game's own. */

/* .bss globals: the leaderboard panel's pre-built quad and its texture. */
#define g_pPanelVerts   ((const void *)0x004e0580)
#define g_pPanelTexture (*(IDirect3DTexture2 **)0x004e0760)

/* Game (Game::instance(), *0x0046C498) — high-score table (highscores.h);
 * offsets within one HighScoreRecord. */
#define HS_NAME_OFF    0x00       /* char[0x32], %s                           */
#define HS_SCORE_OFF   0x32       /* DWORD, %d, drawn in the last column      */
#define HS_LEVEL_OFF   0x36       /* BYTE,  %d, drawn in the middle column    */

/* Game — game-over score breakdown.  Unaligned dwords, in the order the
 * original reads them (which is not the order they sit in memory). */
#define GO_V(off)  (*(const DWORD *)((const BYTE *)g + (off)))

/* `game` — the theme/environment object.  NOT a pointer: 0x0046C890 IS the
 * object, a global in `.data`, pushed as an immediate at all 14 of its sites
 * (RenderGameFrame's three `PUSH 0x46c890` are how it reaches us).  An earlier
 * comment here wrote it `*0x0046C890`, which misdescribed a global as a
 * pointer.
 *
 * It is unmapped and stays an opaque byte base, but its outline is now known,
 * from the static-initialiser ctor at 0x004259a0 (reached by the thunk at
 * 0x004256c0, `MOV ECX,0x46c890; JMP`, run before WinMain; 0x004256e0 is the
 * matching atexit teardown):
 *
 *   +0x00000              header, 0x104 bytes
 *   +0x104 + i * 0x2ef0   38 sub-objects, i = 0..37, each
 *                         { vtable 0x0045d6f8, ?, Element[0x5dd] of 8 bytes }
 *                         built by __ehvec_ctor (0x00451db5)
 *   +0x6f8a4..            the theme scalars below, in the gap after the array
 *   +0x6f99d              one further object, ctor 0x0043c560
 *
 * That runs to roughly 0x6f9a0 — about 457 KB, some 88% of the whole `.data`
 * section, and the largest structure in the binary.  It is also the owner of
 * the "38-element array of stride 0x2ef0" that ENDGAME_PLAN.md E1's TU audit
 * found ctor/dtor loops for without being able to name.
 *
 * Mapping it is ThemeFileLoader 0x0040c110's job, not ours: that function is
 * its only writer and is ASSET_PLAN.md Phase 5.  Until then, reading literal
 * offsets into a byte base is the honest position — inventing a layout for
 * 457 KB from three colour fields would be a guess. */
#define GM_P(off)  (*(void **)((BYTE *)game + (off)))
#define GM_D(off)  (*(DWORD *)((BYTE *)game + (off)))
#define GM_OVERLAY_TEX  0x6f8a8   /* SceneTexture* for the backdrop quad      */
/* These are the text entry points' last two arguments, and they are COLOURS,
 * not fonts: the glyph quad's top and bottom vertex DIFFUSE (textrenderer.h).
 *
 * The theme files settle it, and they also name the fields.  Each is parsed
 * by ThemeFileLoader 0x0040c110 with strtol(base 16) out of a TextColors
 * line in the themes directory, whose value is a pair of six-hex-digit RGBs:
 *
 *   +0x6f8cc/+0x6f8d0  HUDTextColors                    Space: FFFFFF 8080FF
 *   +0x6f914/+0x6f918  MenuHighscoresEntriesTextColors  Space: FF0000 FFFF00
 *   +0x6f984/+0x6f988  MenuSummaryEntriesTextColors     Space: FFFFFF 8080FF
 *
 * So the names below are the theme's own, not a guess from the call site --
 * and the PE_ pair was mis-labelled "...press Enter" on first reading: that
 * caption happens to use the HUD pair, which is shared.
 *
 * They CANNOT become literals here: all three differ per theme (Candy's HUD
 * pair is FFFFFF/00FFFF, Water's highscore pair FFFF00/00FFFF, and so on
 * across the six shipped .thm files), which is exactly why the game keeps
 * them in the object and re-reads them every draw.
 *
 * Note the stored dword is 0x00RRGGBB -- alpha zero, because the file gives
 * six digits and nothing ORs in 0xFF000000.  The glyphs are not invisible, so
 * the blend's alpha does not come from here; the D3D default alpha stage
 * (ALPHAOP = SELECTARG1, ALPHAARG1 = TEXTURE) would take it from the font
 * atlas instead.  That is the likely explanation and is NOT verified -- no
 * one has read back the stage state at a text draw. */
#define GM_HUD_COL_TOP  0x6f8cc   /* HUDTextColors; also "...press Enter"     */
#define GM_HUD_COL_BOT  0x6f8d0
#define GM_HS_COL_TOP   0x6f914   /* MenuHighscoresEntriesTextColors          */
#define GM_HS_COL_BOT   0x6f918
#define GM_GO_COL_TOP   0x6f984   /* MenuSummaryEntriesTextColors             */
#define GM_GO_COL_BOT   0x6f988

/* ─── The game's own entry points, called through ────────────────────────── */

/* The four text entry points are TextRenderer methods (textrenderer.h);
 * only the CRT sprintf is still reached by address here. */


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
Score_DrawHighScoreTable(Game *g, void *game, Direct3D *d3d,
                         TextRenderer *text, int n)
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
                   w, h, (unsigned)g->highScores()->count());

    /* Unsigned early-out, then a signed loop against a re-read count — both
     * as in the original. */
    if (g->highScores()->count() == 0)
        return;

    const float cellW = (float)(d3d->pSelectedMode->dwWidth * 12) * VSCALE;
    const float cellH = (float)(d3d->pSelectedMode->dwWidth * 14) * VSCALE;
    const float xName  = (float)(d3d->pSelectedMode->dwWidth * 187) * VSCALE;
    const float xLevel = (float)(d3d->pSelectedMode->dwWidth * 363) * VSCALE;
    const float xScore = (float)(d3d->pSelectedMode->dwWidth * 453) * VSCALE;

    char buf[256];
    int row = 0, dy = 0;
    do {
        const BYTE *rec = (const BYTE *)g->highScores()->record(row);
        const float y = ((float)dy + 180.0f) * w * VSCALE;

        sprintf(buf, GS_FMT_S, rec + HS_NAME_OFF);
        text->drawLeft(xName, y, cellW, cellH, 0.75f, buf, d3d, 0,
                       GM_D(GM_HS_COL_TOP), GM_D(GM_HS_COL_BOT));

        sprintf(buf, GS_FMT_D, (unsigned)rec[HS_LEVEL_OFF]);
        text->drawRight(xLevel, y, cellW, cellH, 0.75f, buf, d3d, 0,
                        GM_D(GM_HS_COL_TOP), GM_D(GM_HS_COL_BOT));

        sprintf(buf, GS_FMT_D, *(const DWORD *)(rec + HS_SCORE_OFF));
        text->drawRight(xScore, y, cellW, cellH, 0.75f, buf, d3d, 0,
                        GM_D(GM_HS_COL_TOP), GM_D(GM_HS_COL_BOT));

        dy += 20;
        row++;
    } while (row < (int)(unsigned)g->highScores()->count());
}

/* ─── DrawGameOverScore (0x435420) ───────────────────────────────────────── */

/* One breakdown row: caption at x=130, its value right-aligned at x=380, the
 * multiplier caption at x=380, and the product right-aligned at x=510.  Rows
 * 7 and 8 have no multiplier and pass mul == NULL. */
struct ScoreRow {
    float        vy;        /* virtual y / 640 — the original's own constant */
    const char  *label;     /* caption at x = 130                            */
    unsigned     valOff;    /* Game offset of the count (unaligned)           */
    const char  *mul;       /* caption at x = 380, or NULL                   */
    unsigned     prodOff;   /* Game offset of the product, if mul             */
};

static const ScoreRow k_rows[] = {
    { 0.28125f, GS_HUD_CRYSTALS, 0x1404dd, GS_HUD_TIMES_5, 0x1404c1 },
    { 0.3125f, GS_HUD_EXTRA_CRYSTALS, 0x1404e1, GS_HUD_TIMES_10, 0x1404c5 },
    { 0.34375f, GS_HUD_DESTROYED_ENEMIES, 0x1404e9, GS_HUD_TIMES_50, 0x1404cd },
    { 0.375f, GS_HUD_TIME_LEFT, 0x1404e5, GS_HUD_TIMES_2, 0x1404c9 },
    { 0.40625f, GS_HUD_SISYPHUS_BONUS, 0x1404ed, GS_HUD_TIMES_5, 0x1404d1 },
    { 0.4375f, GS_HUD_VITALITY, 0x1404f1, GS_HUD_TIMES_1, 0x1404d5 },
    { 0.46875f, GS_HUD_LEVEL_SCORE, 0x1404f5, NULL,                     0 },
    { 0.515625f, GS_HUD_TOTAL_SCORE, 0x1404f9, NULL,                     0 },
};

/* The body game-over and level-complete share: backdrop, title, the eight
 * tally rows.  RenderLevelComplete 0x433dc0 is this function's twin -- the
 * same stack backdrop, the same drawBig title (cell 24, 0.8, yellow/red,
 * wobble 3 and 0.01, phase n), the same row y constants (0x45d520 ..
 * 0x45d568 read back equal to the table below), strings, offsets, columns
 * and colour pair; checked call by call against both listings.  Only the
 * title text and what follows the rows differ. */
static void draw_summary(Game *g, void *game, Direct3D *d3d,
                         TextRenderer *text, int n, const char *title)
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

    const float S = w * VSCALE;          /* one virtual unit, in pixels */

    /* 24-unit cell, scale 0.8, two colours and two extra floats the smaller
     * entry point does not take. */
    text->drawBig(w * 0.5f, S * 130.0f, S * 24.0f, S * 24.0f, 0.8f,
                  title, d3d, 0,
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

        text->drawLeft(xLabel, y, cellW, cellH, 0.75f, r.label, d3d, 0,
                       GM_D(GM_GO_COL_TOP), GM_D(GM_GO_COL_BOT));

        sprintf(buf, GS_FMT_D, GO_V(r.valOff));
        text->drawRight(r.mul ? xValue : xProd, y, cellW, cellH, 0.75f,
                        buf, d3d, 0,
                        GM_D(GM_GO_COL_TOP), GM_D(GM_GO_COL_BOT));

        if (r.mul) {
            text->drawLeft(xValue, y, cellW, cellH, 0.75f, r.mul, d3d, 0,
                           GM_D(GM_GO_COL_TOP), GM_D(GM_GO_COL_BOT));
            sprintf(buf, GS_FMT_D, GO_V(r.prodOff));
            text->drawRight(xProd, y, cellW, cellH, 0.75f, buf, d3d, 0,
                            GM_D(GM_GO_COL_TOP), GM_D(GM_GO_COL_BOT));
        }
    }
}

extern "C" __declspec(dllexport) void __cdecl
Score_DrawGameOverScore(Game *g, void *game, Direct3D *d3d,
                        TextRenderer *text, int n)
{
    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SCORE_LOG_FIRST)
        log_write("scoreoverlay: gameover %lux%lu n=%d\n",
                  (unsigned long)d3d->pSelectedMode->dwWidth,
                  (unsigned long)d3d->pSelectedMode->dwHeight, n);

    draw_summary(g, game, d3d, text, n, GS_HUD_GAME_OVER);

    const DWORD dwWidth = d3d->pSelectedMode->dwWidth;
    const float w = (float)dwWidth;
    text->drawCentered(w * 0.5f, w * 0.59375f,
                       (float)(dwWidth * 12) * VSCALE,
                       (float)(dwWidth * 14) * VSCALE, 0.75f,
                       GS_HUD_PRESS_ENTER, d3d, 0,
                       GM_D(GM_HUD_COL_TOP), GM_D(GM_HUD_COL_BOT));
}

/* ─── RenderLevelComplete (0x433dc0) ─────────────────────────────────────── */

/* Menu node 0x28 (ENDGAME_PLAN.md E6).  A MenuScreens function -- dispatched
 * by DispatchGameState with the usual five arguments -- but it lives here
 * because its body IS draw_summary.  Afterwards: "Next" centred at 0.59375
 * in the theme pair at +0x6f98c (theme key not looked up), "Save" at
 * 0.64375 in its own pair
 * (+0x6f994) only when Game+0x14 (nextLevelBonus) is 0, and the cursor
 * markers 200 virtual units down.  The row cells re-read the width per call
 * in the original; the width does not change within a frame. */
#define GM_NEXT_COL_TOP 0x6f98c
#define GM_NEXT_COL_BOT 0x6f990
#define GM_SAVE_COL_TOP 0x6f994
#define GM_SAVE_COL_BOT 0x6f998

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderLevelComplete(Game *g, void *game, Direct3D *d3d,
                         TextRenderer *text, DWORD ms)
{
    draw_summary(g, game, d3d, text, (int)ms, "LEVEL COMPLETED");

    const DWORD dwWidth = d3d->pSelectedMode->dwWidth;
    const float w = (float)dwWidth;
    const float cellW = (float)(dwWidth * 12) * VSCALE;
    const float cellH = (float)(dwWidth * 14) * VSCALE;
    text->drawCentered(w * 0.5f, w * 0.59375f, cellW, cellH, 0.75f, "Next",
                       d3d, 0, GM_D(GM_NEXT_COL_TOP), GM_D(GM_NEXT_COL_BOT));
    if (g->nextLevelBonus() == 0)
        text->drawCentered(w * 0.5f, w * 0.64375001f, cellW, cellH, 0.75f,
                           "Save", d3d, 0,
                           GM_D(GM_SAVE_COL_TOP), GM_D(GM_SAVE_COL_BOT));

    Menu_DrawCursorMarkers(g, d3d, ms, 200.0f);
}
