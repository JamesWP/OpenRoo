/* The high-score table, the game-over score and the level-complete screen.
 * Each opens with the same backdrop: alpha blending on, the theme's overlay
 * texture, and a full-screen quad sampling the middle fifth (0.4..0.6) of it.
 * The high-score table then draws its panel quad on top.
 *
 * Everything is laid out in a 640-wide virtual space scaled by width/640.
 * PRESERVED: both axes scale by the width, so on a mode that is not 4:3 the
 * layout stays square and does not fill the screen vertically.  Every text row
 * uses a 12x14 cell at scale 0.75.
 *
 * KAROO_SCORE_FX is a visual negative control: "tint" makes the backdrop
 * magenta (only these functions build it), "nodraw" skips the quads and leaves
 * the text. */

#include <stdio.h>
#include "scoreoverlay.h"
#include "renderdevice.h"
#include "texture.h"
#include "log.h"
#include "game.h"
#include "textrenderer.h"
#include "gamestr.h"
#include "menuscreens.h"

#define OVERLAY_FVF   VertexFormat::Screen  // XYZRHW | DIFFUSE | SPECULAR | TEX1
#define SCORE_LOG_FIRST 4

/* 1/640, the virtual-space scale, bit for bit the game's float. */
#define VSCALE 0x1.99999ap-10f

/* The high-score panel's quad and texture. */
#define g_pPanelVerts   ((const void *)g_panelQuad)
#define g_pPanelTexture (&g_menuTex2)

/* Offsets within one HighScoreRecord. */
#define HS_NAME_OFF    0x00
#define HS_SCORE_OFF   0x32  // drawn in the last column
#define HS_LEVEL_OFF   0x36  // drawn in the middle column

/* The game-over breakdown's values: unaligned dwords in the Game. */
#define GO_V(off)  (*(const DWORD *)((const BYTE *)g + (off)))

/* game is the theme object: an unmapped global, read at fixed offsets.  Its
 * text colours come from the theme file, differ per theme, and are re-read
 * every draw.  Each pair is the glyph quad's top and bottom vertex colour,
 * stored 0x00RRGGBB (the file gives six hex digits). */
#define GM_P(off)  (*(void **)((BYTE *)game + (off)))
#define GM_D(off)  (*(DWORD *)((BYTE *)game + (off)))
#define GM_OVERLAY_TEX  0x6f8a8  // SceneTexture* for the backdrop
#define GM_HUD_COL_TOP  0x6f8cc  // HUDTextColors; also "...press Enter"
#define GM_HUD_COL_BOT  0x6f8d0
#define GM_HS_COL_TOP   0x6f914  // MenuHighscoresEntriesTextColors
#define GM_HS_COL_BOT   0x6f918
#define GM_GO_COL_TOP   0x6f984  // MenuSummaryEntriesTextColors
#define GM_GO_COL_BOT   0x6f988

enum ScoreFx { SCORE_FX_OFF = 0, SCORE_FX_TINT, SCORE_FX_NODRAW };

static ScoreFx score_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (ScoreFx)cached;
    char buf[32];
    // By value, not by presence.
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

struct TLVertex {  // FVF 0x1c4
    float x, y, z, rhw;
    DWORD diffuse, specular;
    float tu, tv;
};
static_assert(sizeof(TLVertex) == 0x20, "TLVertex size mismatch");

/* The four-vertex strip: (w,0) (w,h) (0,0) (0,h), z 0, rhw 10. */
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

/* Alpha blending and the overlay texture.  The device is re-read before every
 * call, as the game does, so the call traffic is identical. */
static void setup_overlay_state(RenderDevice *d3d, void *game)
{
    d3d->SetRenderState(RS::AlphaBlendEnable, 1);
    d3d->SetRenderState(RS::SrcBlend,  Blend::SrcAlpha);
    d3d->SetRenderState(RS::DestBlend, Blend::InvSrcAlpha);

    SceneTexture *tex = (SceneTexture *)GM_P(GM_OVERLAY_TEX);
    d3d->SetTexture(0, tex);
}

void Score_DrawHighScoreTable(Game *g, void *game, RenderDevice *d3d,
                              TextRenderer *text, int n)
{
    (void)n;  // pushed by the caller, never read

    const float w = (float)d3d->width();
    const float h = (float)d3d->height();

    TLVertex quad[4];
    build_backdrop(quad, w, h);

    setup_overlay_state(d3d, game);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->Draw(Prim::TriangleStrip, OVERLAY_FVF,
                                    quad, 4, 0);

    d3d->SetTexture(0, g_pPanelTexture);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->Draw(Prim::TriangleStrip, OVERLAY_FVF,
                                    (LPVOID)g_pPanelVerts, 4, 0);

    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SCORE_LOG_FIRST)
        log_write("scoreoverlay: highscore %.0fx%.0f entries=%u\n",
                   w, h, (unsigned)g->highScores()->count());

    // PRESERVED: an unsigned early-out, then a signed loop against a count
    // re-read every pass.
    if (g->highScores()->count() == 0)
        return;

    const float cellW = (float)(d3d->width() * 12) * VSCALE;
    const float cellH = (float)(d3d->width() * 14) * VSCALE;
    const float xName  = (float)(d3d->width() * 187) * VSCALE;
    const float xLevel = (float)(d3d->width() * 363) * VSCALE;
    const float xScore = (float)(d3d->width() * 453) * VSCALE;

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

/* One breakdown row: caption at x 130, its value right-aligned at 380, the
 * multiplier caption at 380 and the product right-aligned at 510.  The last
 * two rows have no multiplier and put the value at 510. */
struct ScoreRow {
    float        vy;       // virtual y / 640
    const char  *label;    // caption at x 130
    unsigned     valOff;   // Game offset of the count
    const char  *mul;      // caption at x 380, or NULL
    unsigned     prodOff;  // Game offset of the product
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

/* The body game over and level complete share: backdrop, title and the eight
 * tally rows.  Only the title and what follows differ. */
static void draw_summary(Game *g, void *game, RenderDevice *d3d,
                         TextRenderer *text, int n, const char *title)
{
    const DWORD dwWidth = d3d->width();
    const float w = (float)dwWidth;
    const float h = (float)d3d->height();

    TLVertex quad[4];
    build_backdrop(quad, w, h);

    setup_overlay_state(d3d, game);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->Draw(Prim::TriangleStrip, OVERLAY_FVF,
                                    quad, 4, 0);

    const float S = w * VSCALE;  // one virtual unit, in pixels

    // A 24-unit cell at scale 0.8, yellow over red, with a wobble of three
    // units at 0.01 and phase n.
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

void Score_DrawGameOverScore(Game *g, void *game, RenderDevice *d3d,
                             TextRenderer *text, int n)
{
    static LONG calls = 0;
    if (InterlockedIncrement(&calls) <= SCORE_LOG_FIRST)
        log_write("scoreoverlay: gameover %lux%lu n=%d\n",
                  (unsigned long)d3d->width(),
                  (unsigned long)d3d->height(), n);

    draw_summary(g, game, d3d, text, n, GS_HUD_GAME_OVER);

    const DWORD dwWidth = d3d->width();
    const float w = (float)dwWidth;
    text->drawCentered(w * 0.5f, w * 0.59375f,
                       (float)(dwWidth * 12) * VSCALE,
                       (float)(dwWidth * 14) * VSCALE, 0.75f,
                       GS_HUD_PRESS_ENTER, d3d, 0,
                       GM_D(GM_HUD_COL_TOP), GM_D(GM_HUD_COL_BOT));
}

/* Menu node 0x28.  After the summary: "Next", "Save" only when there is no
 * next-level bonus, each in its own theme colours, and the cursor markers 200
 * virtual units down. */

#define GM_NEXT_COL_TOP 0x6f98c
#define GM_NEXT_COL_BOT 0x6f990
#define GM_SAVE_COL_TOP 0x6f994
#define GM_SAVE_COL_BOT 0x6f998

void Menu_RenderLevelComplete(Game *g, void *game, RenderDevice *d3d,
                              TextRenderer *text, DWORD ms)
{
    draw_summary(g, game, d3d, text, (int)ms, "LEVEL COMPLETED");

    const DWORD dwWidth = d3d->width();
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
