/* hud.cpp -- the in-level HUD, the frame counter, the captions and timed-effect
 * icons, and the logo, drawn over the scene by RenderGameFrame. */
#include <stdio.h>
#include <math.h>
#include "hud.h"
#include "keycodes.h"
#include "sysdev.h"
#include "keys.h"
#include "renderdevice.h"
#include "d3dmath.h"
#include "texture.h"
#include "textrenderer.h"
#include "world.h"
#include "session.h"
#include "scriptplayer.h"
#include "levelmap.h"
#include "player.h"
#include "theme.h"
#include "camera.h"
#include "foe.h"
#include "renderer.h"

/* ─── The HUD ─────────────────────────────────────────────────────────── */

/* The one text buffer the HUD, frame counter and effect icons share.
 * PRESERVED: an icon row that formats nothing draws whatever the buffer last
 * held -- the previous row, the counter, the HUD's last line, or an earlier
 * frame's. */
static char s_text[0x100];

static ScreenVertex tl(float x, float y, uint32_t c, float u, float v)
{
    ScreenVertex t;
    t.sx = x; t.sy = y; t.sz = 0.0f; t.rhw = 10.0f;
    t.color = c; t.specular = 0; t.tu = u; t.tv = v;
    return t;
}

static void draw_strip(RenderDevice *d3d, ScreenVertex *q)
{
    d3d->Draw(Prim::TriangleStrip, VertexFormat::Screen, q, 4, 0);
}

static void blend_on(RenderDevice *d3d)
{
    d3d->SetBlend(BlendState::alpha());
}

static const Texture *image(const ThemeAssetBlock *theme, ThemeImageSlot s)
{
    return theme->image(s);
}

/* The HUD's text positions are integer multiples of the screen width, taken
 * unsigned and scaled by 1/640 (0.0015625). */
static float wx(unsigned w, unsigned k) { return (float)(w * k) * 0.0015625f; }

static void hud_text(RenderDevice *d3d, TextRenderer *font, int align, float x, float y, float cw,
                     float ch, float spacing, const char *s, char first, uint32_t c1, uint32_t c2)
{
    if (align < 0)
        font->drawLeft(x, y, cw, ch, spacing, s, d3d, first, c1, c2);
    else if (align == 0)
        font->drawCentered(x, y, cw, ch, spacing, s, d3d, first, c1, c2);
    else
        font->drawRight(x, y, cw, ch, spacing, s, d3d, first, c1, c2);
}

/* The in-level HUD: the side panels, the vitality needle, the radar and
 * its foe blips, then the timer and counters. */
void Hud_Draw(Renderer *renderer, World *world, Session *session, GameState state,
              unsigned w, unsigned h, float W, float H, float hudH)
{
    RenderDevice *dev = renderer->device;
    const ThemeAssetBlock *theme = &renderer->themeBlock;
    Player *pl = world->player();

    /* The two corner panels, mirrored halves of the HUD image. */
    if (theme->image(THEME_IMG_HUD) != NULL) {
        bindTexture(dev, 0, image(theme, THEME_IMG_HUD));
        blend_on(dev);
        const float a  = hudH + hudH;
        const float y1 = hudH - W * 0.003125f;
        ScreenVertex q[4] = {
            tl(a,    0.0f, 0xffffffff, 1.0f, 0.0f),
            tl(a,    y1,   0xffffffff, 1.0f, 0.4921875f),
            tl(0.0f, 0.0f, 0xffffffff, 0.0f, 0.0f),
            tl(0.0f, y1,   0xffffffff, 0.0f, 0.4921875f),
        };
        draw_strip(dev, q);
        ScreenVertex r[4] = {
            tl(W,     0.0f, 0xffffffff, 1.0f, 0.5078125f),
            tl(W,     y1,   0xffffffff, 1.0f, 1.0f),
            tl(W - a, 0.0f, 0xffffffff, 0.0f, 0.5078125f),
            tl(W - a, y1,   0xffffffff, 0.0f, 1.0f),
        };
        draw_strip(dev, r);
        dev->SetBlend(BlendState::off());
        dev->SetTexture(0, nullptr);
    }

    /* The vitality needle: a quad about the origin, turned by 0.9519978 -
     * vitality% * 0.019039957 and moved to (541/640 W, 92/480 H). */
    if (theme->image(THEME_IMG_POINTER) != NULL) {
        float p[4][3] = {
            { W * 0.1f,  H * -0.13333334f, 0.0f },
            { W * 0.1f,  H * 0.13333334f,  0.0f },
            { W * -0.1f, H * -0.13333334f, 0.0f },
            { W * -0.1f, H * 0.13333334f,  0.0f },
        };
        const double ang = 0.9519978 - (int)session->vitalityPercent() * 0.019039957;
        const float c = (float)cos(ang), s = (float)sin(ang);
        float m[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        m[0] = c;  m[1] = -s;  m[4] = s;  m[5] = c;
        m[12] = (float)(w * 541u) * 0.0015625f;
        m[13] = (float)(h * 92u) * 0.0020833334f;
        for (int k = 0; k < 4; ++k) {
            const float v[4] = { p[k][0], p[k][1], p[k][2], 1.0f };
            float o[4] = { 0, 0, 0, 0 };
            for (int col = 0; col < 4; ++col) {
                float acc = o[col];
                for (int i = 0; i < 4; ++i)
                    acc = v[i] * m[i * 4 + col] + acc;
                o[col] = acc;
            }
            if (o[3] != 1.0f) { o[0] /= o[3]; o[1] /= o[3]; o[2] /= o[3]; }
            p[k][0] = o[0]; p[k][1] = o[1]; p[k][2] = o[2];
        }
        ScreenVertex q[4] = {
            tl(p[0][0], p[0][1], 0xffffffff, 1.0f, 0.0f),
            tl(p[1][0], p[1][1], 0xffffffff, 1.0f, 1.0f),
            tl(p[2][0], p[2][1], 0xffffffff, 0.0f, 0.0f),
            tl(p[3][0], p[3][1], 0xffffffff, 0.0f, 1.0f),
        };
        for (int k = 0; k < 4; ++k)
            q[k].sz = p[k][2];
        bindTexture(dev, 0, image(theme, THEME_IMG_POINTER));
        blend_on(dev);
        draw_strip(dev, q);
        dev->SetBlend(BlendState::off());
        dev->SetTexture(0, nullptr);
    }

    /* The radar, centred at (0.9 W, 0.8667 H). */
    const float cx = W * 0.9f, cy = H * 0.8666667f;
    CameraFocus *focus = &renderer->cameraFocus;
    Vec3 me = { focus->f[2], focus->f[3], focus->f[4] };
    if (theme->image(THEME_IMG_RADAR) != NULL) {
        const float r = hudH * 0.5f;
        me.y = 0.0f;
        ScreenVertex q[4] = {
            tl(cx + r, cy - r, 0xffffffff, 1.0f, 0.0f),
            tl(cx + r, cy + r, 0xffffffff, 1.0f, 1.0f),
            tl(cx - r, cy - r, 0xffffffff, 0.0f, 0.0f),
            tl(cx - r, cy + r, 0xffffffff, 0.0f, 1.0f),
        };
        blend_on(dev);
        bindTexture(dev, 0, image(theme, THEME_IMG_RADAR));
        draw_strip(dev, q);
        dev->SetBlend(BlendState::off());
        dev->SetTexture(0, nullptr);
    }

    /* A blip for every foe within 13 cells, turned into the camera's frame
     * (RotY(-yaw)) and scaled by 4; drawn untextured. */
    for (const Foe *e : world->foes()) {
        Vec3 a = { e->posU(), 0.0f, -e->posV() }, d;
        Math_Vec3Sub(&d, &a, &me);
        if (!(Math_Vec3Length(&d) < 13.0))
            continue;
        Mat4 rot;
        Math_Mat4RotY(&rot, -focus->f[5]);
        Vec3 t;
        Math_Vec3TransformPoint(&t, rot, d);
        Math_Vec3ScaleInPlace(&t, 4.0f);
        const float half = W * 0.0015625f;
        const float bx = t.x + cx, by = cy - t.z;
        ScreenVertex q[4] = {
            tl(bx + half, by - half, 0xffffffff, 1.0f, 0.0f),
            tl(bx + half, by + half, 0xffffffff, 1.0f, 1.0f),
            tl(bx - half, by - half, 0xffffffff, 0.0f, 0.0f),
            tl(bx - half, by + half, 0xffffffff, 0.0f, 1.0f),
        };
        draw_strip(dev, q);
    }

    /* The timer: minutes, seconds and tenths of what is left (or of the
     * limit, outside play); red under ten seconds. */
    LevelMap *map = world->map();
    const unsigned ms = state == GameState::Playing
        ? (unsigned)map->timeLimit() * 1000u - map->timeElapsed()
        : (unsigned)map->timeLimit() * 1000u;
    const unsigned mins = ms / 60000u;
    const unsigned secs = ms / 1000u - mins * 60u;
    const unsigned tenths = ms / 100u - mins * 600u - secs * 10u;
    char *buf = s_text;
    sprintf(buf, "%02.0f:%02.0f;%d", (double)mins, (double)secs, tenths);
    const uint32_t tc = ms > 10000u ? 0xffffffff : 0xffff0000;
    hud_text(dev, &renderer->fontNumbers, 0, W * 0.5f, 0.0f, W * 0.05f, W * 0.06666667f, 1.0f,
             buf, '0', tc, tc);

    const ThemeTextColorPair &hc = theme->textColor(THEME_COLOR_HUD);
    const float cw = W * 0.025f, ch = H * 0.033333335f;
    sprintf(buf, "x%d", pl->lives());
    hud_text(dev, &renderer->fontMain, -1, wx(w, 490), W * 0.009375f, cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "%d/%d", pl->gemsCollected(), map->gemsRequired());
    hud_text(dev, &renderer->fontMain, -1, wx(w, 10), wx(w, 32), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "x%d", pl->glides());
    hud_text(dev, &renderer->fontMain, -1, wx(w, 121), wx(w, 38), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "%d", session->levelIndex() + 1);
    hud_text(dev, &renderer->fontMain, 1, wx(w, 630), wx(w, 23), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
    sprintf(buf, "%dx", pl->bombsCarried());
    hud_text(dev, &renderer->fontMain, 0, wx(w, 32), wx(w, 62), cw, ch, 0.75f, buf, 0, hc.color1, hc.color2);
}

/* The frame counter: frames over each second of the timer, shown while
 * F1 is held.  Its three globals stay at their game addresses. */
static float g_fps;
static float g_fpsMark;
static uint32_t g_fpsFrames;

void Hud_DrawFps(RenderDevice *d3d, TextRenderer *font, float W, float H)
{
    const float t = (float)((double)sysdev::timerMs() * 0.001f);
    g_fpsFrames++;
    const double span = (double)t - g_fpsMark;
    if (span > 1.0) {
        const uint32_t n = g_fpsFrames;
        g_fpsFrames = 0;
        g_fps = (float)((double)n / span);
        g_fpsMark = t;
    }
    if (input::down(input::KEY_F1)) {
        char *buf = s_text;
        sprintf(buf, "%.1f fps", (double)g_fps);
        hud_text(d3d, font, 0, W * 0.5f, H * 0.96041667f, W * 0.025f, H * 0.033333335f,
                 0.75f, buf, 0, 0xffffffff, 0xffffffff);
    }
}

/* The captions and the timed-effect icons down the left edge. */
void Hud_DrawMessages(RenderDevice *d3d, TextRenderer *font, const ThemeAssetBlock *theme,
                      World *world, ScriptPlayer *scriptPlayer, GameState state, float W, float H, float pad)
{
    const ThemeTextColorPair &hc = theme->textColor(THEME_COLOR_HUD);
    const float cw = W * 0.025f, ch = H * 0.033333335f;
    ScriptPlayer *sp = scriptPlayer;
    Player *pl = world->player();

    if (sp->running() != 0) {
        hud_text(d3d, font, 0, W * 0.5f, W * 0.0015625f * 3.0f, cw, ch, 0.75f,
                 world->map()->title(), 0, hc.color1, hc.color2);
        if (sp->caption()[0] != '\0')
            font->drawPanel(pad, H - pad, cw, ch, 0.75f, H * 0.03750938f,
                               sp->caption(), d3d, hc.color1, hc.color2,
                               theme->image(THEME_IMG_MENU),
                               theme->image(THEME_IMG_EDGE));
    }
    if ((state == GameState::Flythrough && sp->running() == 0) || (state == GameState::Playing && pl->moveState() != 0))
        hud_text(d3d, font, 0, W * 0.5f, W * 0.0015625f * 232.0f, cw, ch, 0.75f,
                 "...press Enter", 0, hc.color1, hc.color2);
    if (state == GameState::Flythrough && world->map()->bonus() != 0)
        hud_text(d3d, font, 0, W * 0.5f, W * 0.0015625f * 200.0f, cw, ch, 0.75f,
                 "BONUSLEVEL", 0, hc.color1, hc.color2);

    /* One icon per active effect, bottom up, with its seconds left.  An
     * effect code without an icon (9), or an icon the theme lacks, still
     * draws the quad and the text -- with whatever texture and text the
     * previous row left. */
    char *buf = s_text;
    const float rowStep = H * 0.06666667f + pad;
    const float xText = W * 0.05f + pad;
    const float textLift = H * 0.016666668f;
    float y = H - ch - pad;
    const std::vector<int> &effects = pl->effectList();
    for (size_t ei = 0; ei < effects.size(); ++ei) {
        const unsigned code = (unsigned char)effects[ei];
        ThemeImageSlot img;
        double start = 0.0, span = 10.0;
        bool icon = true;
        switch (code) {
        case 8:  img = THEME_IMG_FREEZE;         start = pl->freezeStart(); span = 5.0; break;
        case 10: img = THEME_IMG_SPEED;          start = pl->effectAStart(); break;
        case 11: img = THEME_IMG_INVERSECONTROL; start = pl->effectBStart(); break;
        case 12: img = THEME_IMG_SLOWDOWN;       start = pl->effectCStart(); break;
        case 13: img = THEME_IMG_PROTECTION;     start = pl->protectionStart(); break;
        default: icon = false; img = THEME_IMG_HUD; break;
        }
        if (icon && theme->image(img) != NULL) {
            bindTexture(d3d, 0, image(theme, img));
            sprintf(buf, "%.1f", span - (*world->clock() - start) * 0.001);
        }
        ScreenVertex q[4] = {
            tl(xText, y - ch, 0x00ffffff, 1.0f, 0.0f),
            tl(xText, y + ch, 0x00ffffff, 1.0f, 1.0f),
            tl(pad,   y - ch, 0x00ffffff, 0.0f, 0.0f),
            tl(pad,   y + ch, 0x00ffffff, 0.0f, 1.0f),
        };
        blend_on(d3d);
        draw_strip(d3d, q);
        hud_text(d3d, font, -1, xText, y - textLift, cw, ch, 0.75f, buf, 0,
                 hc.color1, hc.color2);
        y -= rowStep;
    }
}

/* The Ka'roo logo in the bottom-left corner, on the menus only; the
 * blend and texture state around it are set regardless. */
void Hud_DrawLogo(RenderDevice *d3d, Texture *logo, GameState state, float H,
                  float hudH, float pad)
{
    const float xr = pad + hudH, yt = H - hudH - pad, yb = H - pad;
    ScreenVertex q[4] = {
        tl(xr,  yt, 0xffffffff, 1.0f, 0.0f),
        tl(xr,  yb, 0xffffffff, 1.0f, 1.0f),
        tl(pad, yt, 0xffffffff, 0.0f, 0.0f),
        tl(pad, yb, 0xffffffff, 0.0f, 1.0f),
    };
    blend_on(d3d);
    bindTexture(d3d, 0, logo);
    if (state == GameState::MainMenu || state == GameState::PauseMenu)
        draw_strip(d3d, q);
    d3d->SetBlend(BlendState::off());
    d3d->SetTexture(0, nullptr);
}
