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

#include "portable.h"
#include <stdint.h>
#include <stdio.h>
#include "sysdev.h"
#include "scoreoverlay.h"
#include "renderdevice.h"
#include "texture.h"
#include "logger.h"
#include "game.h"
#include "textrenderer.h"
#include "gamestr.h"
#include "menuscreens.h"
#include "theme.h"
#include "highscores.h"

#define OVERLAY_FVF   VertexFormat::Screen  // XYZRHW | DIFFUSE | SPECULAR | TEX1
#define SCORE_LOG_FIRST 4

/* 1/640, the virtual-space scale, bit for bit the game's float. */
#define VSCALE 0x1.99999ap-10f

/* The high-score panel's quad and texture. */
#define g_pPanelVerts   ((const void *)g_panelQuad)
#define g_pPanelTexture (&g_menuTex2)

/* The theme's text colours come from the theme file, differ per theme, and
 * are re-read every draw.  Each pair is the glyph quad's top and bottom vertex
 * colour, stored 0x00RRGGBB (the file gives six hex digits). */
#define THEME_COL(slot) theme->textColor(slot).color1, theme->textColor(slot).color2

enum ScoreFx { SCORE_FX_OFF = 0, SCORE_FX_TINT, SCORE_FX_NODRAW };

static ScoreFx score_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (ScoreFx)cached;
    char buf[32];
    // By value, not by presence.
    uint32_t n = sysdev::getEnv("KAROO_SCORE_FX", buf, sizeof(buf));
    ScoreFx fx = SCORE_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (strcaseCompare(buf, "tint") == 0)        fx = SCORE_FX_TINT;
        else if (strcaseCompare(buf, "nodraw") == 0) fx = SCORE_FX_NODRAW;
    }
    g_logger.write("scoreoverlay: FX mode = %s\n",
               fx == SCORE_FX_TINT   ? "tint"   :
               fx == SCORE_FX_NODRAW ? "nodraw" : "off");
    cached = (int)fx;
    return fx;
}

struct TLVertex {  // FVF 0x1c4
    float x, y, z, rhw;
    uint32_t diffuse, specular;
    float tu, tv;
};

/* The four-vertex strip: (w,0) (w,h) (0,0) (0,h), z 0, rhw 10. */
static void build_backdrop(TLVertex v[4], float w, float h)
{
    const uint32_t diffuse = (score_fx() == SCORE_FX_TINT) ? 0xffff00ff : 0xffffffff;
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
static void setup_overlay_state(RenderDevice *d3d, ThemeAssetBlock *theme)
{
    d3d->SetRenderState(RS::AlphaBlendEnable, 1);
    d3d->SetRenderState(RS::SrcBlend,  Blend::SrcAlpha);
    d3d->SetRenderState(RS::DestBlend, Blend::InvSrcAlpha);

    Texture *tex = theme->image(THEME_IMG_MENU);
    d3d->SetTexture(0, tex);
}

void Score_DrawHighScoreTable(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                              TextRenderer *text, int n)
{
    (void)n;  // pushed by the caller, never read

    const float w = (float)d3d->width();
    const float h = (float)d3d->height();

    TLVertex quad[4];
    build_backdrop(quad, w, h);

    setup_overlay_state(d3d, theme);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->Draw(Prim::TriangleStrip, OVERLAY_FVF,
                                    quad, 4, 0);

    d3d->SetTexture(0, g_pPanelTexture);
    if (score_fx() != SCORE_FX_NODRAW)
        d3d->Draw(Prim::TriangleStrip, OVERLAY_FVF,
                                    (void *)g_pPanelVerts, 4, 0);

    static AtomicInt calls = 0;
    if (atomicIncrement(&calls) <= SCORE_LOG_FIRST)
        g_logger.write("scoreoverlay: highscore %.0fx%.0f entries=%u\n",
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
        const HighScoreRecord *rec = g->highScores()->record(row);
        const float y = ((float)dy + 180.0f) * w * VSCALE;

        sprintf(buf, GS_FMT_S, rec->name);
        text->drawLeft(xName, y, cellW, cellH, 0.75f, buf, d3d, 0,
                       THEME_COL(THEME_COLOR_MENUHIGHSCORESENTRIES));

        sprintf(buf, GS_FMT_D, (unsigned)rec->level);
        text->drawRight(xLevel, y, cellW, cellH, 0.75f, buf, d3d, 0,
                        THEME_COL(THEME_COLOR_MENUHIGHSCORESENTRIES));

        sprintf(buf, GS_FMT_D, rec->score);
        text->drawRight(xScore, y, cellW, cellH, 0.75f, buf, d3d, 0,
                        THEME_COL(THEME_COLOR_MENUHIGHSCORESENTRIES));

        dy += 20;
        row++;
    } while (row < (int)(unsigned)g->highScores()->count());
}

/* One breakdown row: caption at x 130, its value right-aligned at 380, the
 * multiplier caption at 380 and the product right-aligned at 510.  The last
 * two rows have no multiplier and put the value at 510. */
struct ScoreRow {
    float        vy;     // virtual y / 640
    const char  *label;  // caption at x 130
    int          row;    // the tally row, or one of the two totals below
    const char  *mul;    // caption at x 380, or NULL
};

enum { ROW_LEVEL_TOTAL = TALLY_ROWS, ROW_GRAND_TOTAL };

static const ScoreRow k_rows[] = {
    { 0.28125f,  GS_HUD_CRYSTALS,          TALLY_GEMS,      GS_HUD_TIMES_5 },
    { 0.3125f,   GS_HUD_EXTRA_CRYSTALS,    TALLY_SURPLUS,   GS_HUD_TIMES_10 },
    { 0.34375f,  GS_HUD_DESTROYED_ENEMIES, TALLY_FOES,      GS_HUD_TIMES_50 },
    { 0.375f,    GS_HUD_TIME_LEFT,         TALLY_TIME,      GS_HUD_TIMES_2 },
    { 0.40625f,  GS_HUD_SISYPHUS_BONUS,    TALLY_ALLITEMS,  GS_HUD_TIMES_5 },
    { 0.4375f,   GS_HUD_VITALITY,          TALLY_VITALITY,  GS_HUD_TIMES_1 },
    { 0.46875f,  GS_HUD_LEVEL_SCORE,       ROW_LEVEL_TOTAL, NULL },
    { 0.515625f, GS_HUD_TOTAL_SCORE,       ROW_GRAND_TOTAL, NULL },
};

/* The value a row shows: the counted-up count, or a total. */
static int row_value(const ScoreTally *t, int row)
{
    if (row == ROW_LEVEL_TOTAL) return t->shownLevelTotal;
    if (row == ROW_GRAND_TOTAL) return t->shownGrandTotal;
    return t->shownCount[row];
}

/* The body game over and level complete share: backdrop, title and the eight
 * tally rows.  Only the title and what follows differ. */
static void draw_summary(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                         TextRenderer *text, int n, const char *title)
{
    const uint32_t dwWidth = d3d->width();
    const float w = (float)dwWidth;
    const float h = (float)d3d->height();

    TLVertex quad[4];
    build_backdrop(quad, w, h);

    setup_overlay_state(d3d, theme);
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
                       THEME_COL(THEME_COLOR_MENUSUMMARYENTRIES));

        sprintf(buf, GS_FMT_D, row_value(g->tally(), r.row));
        text->drawRight(r.mul ? xValue : xProd, y, cellW, cellH, 0.75f,
                        buf, d3d, 0,
                        THEME_COL(THEME_COLOR_MENUSUMMARYENTRIES));

        if (r.mul) {
            text->drawLeft(xValue, y, cellW, cellH, 0.75f, r.mul, d3d, 0,
                           THEME_COL(THEME_COLOR_MENUSUMMARYENTRIES));
            sprintf(buf, GS_FMT_D, g->tally()->shownScore[r.row]);
            text->drawRight(xProd, y, cellW, cellH, 0.75f, buf, d3d, 0,
                            THEME_COL(THEME_COLOR_MENUSUMMARYENTRIES));
        }
    }
}

void Score_DrawGameOverScore(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                             TextRenderer *text, int n)
{
    static AtomicInt calls = 0;
    if (atomicIncrement(&calls) <= SCORE_LOG_FIRST)
        g_logger.write("scoreoverlay: gameover %lux%lu n=%d\n",
                  (unsigned long)d3d->width(),
                  (unsigned long)d3d->height(), n);

    draw_summary(g, theme, d3d, text, n, GS_HUD_GAME_OVER);

    const uint32_t dwWidth = d3d->width();
    const float w = (float)dwWidth;
    text->drawCentered(w * 0.5f, w * 0.59375f,
                       (float)(dwWidth * 12) * VSCALE,
                       (float)(dwWidth * 14) * VSCALE, 0.75f,
                       GS_HUD_PRESS_ENTER, d3d, 0,
                       THEME_COL(THEME_COLOR_HUD));
}

/* Menu node 0x28.  After the summary: "Next", "Save" only when there is no
 * next-level bonus, each in its own theme colours, and the cursor markers 200
 * virtual units down. */

void Menu_RenderLevelComplete(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                              TextRenderer *text, uint32_t ms)
{
    draw_summary(g, theme, d3d, text, (int)ms, "LEVEL COMPLETED");

    const uint32_t dwWidth = d3d->width();
    const float w = (float)dwWidth;
    const float cellW = (float)(dwWidth * 12) * VSCALE;
    const float cellH = (float)(dwWidth * 14) * VSCALE;
    text->drawCentered(w * 0.5f, w * 0.59375f, cellW, cellH, 0.75f, "Next",
                       d3d, 0, THEME_COL(THEME_COLOR_MENUSUMMARYNEXT));
    if (g->nextLevelBonus() == 0)
        text->drawCentered(w * 0.5f, w * 0.64375001f, cellW, cellH, 0.75f,
                           "Save", d3d, 0,
                           THEME_COL(THEME_COLOR_MENUSUMMARYSAVE));

    Menu_DrawCursorMarkers(g, d3d, ms, 200.0f);
}
