/* Layout is in a 640-wide virtual space: w is the mode width, loaded unsigned,
 * times 1/640.  Text cells are integer products first, (float)(w * 12) / 640
 * and (float)(w * 14) / 640.  Every quad is four pretransformed vertices in a
 * triangle strip, z 0, rhw 10, white.  The device pointer is re-read for every
 * call, as the game does, so the call traffic is identical.
 *
 * The quads, the widget model and the nine textures are built once per device
 * by Menu_BuildMenuGeometry. */

#include "menuscreens.h"
#include "game.h"
#include "renderdevice.h"
#include "textrenderer.h"
#include "texture.h"
#include "menutree.h"
#include "saveslots.h"
#include "levelselect.h"
#include "scoreoverlay.h"
#include "d3dmath.h"
#include "progctrl.h"
#include "scenetexture.h"
#include "theme.h"
#include <stdio.h>
#include "gameglobals.h"
#include <math.h>

/* The menu's textures and quads. */
SceneTexture g_menuTexOff;
SceneTexture g_menuTex3;
SceneTexture g_menuTex2;
SceneTexture g_menuTexOn;
SceneTexture g_menuTexScale;
SceneTexture g_menuTexSelector;
SceneTexture g_menuTexKnob;
SceneTexture g_menuTex1;
SceneTexture g_menuTex4;

#define K640          (1.0f / 640.0f)
#define MENU_FVF      VertexFormat::Screen  // XYZRHW | DIFFUSE | SPECULAR | TEX1
static ScreenVertex g_backdropQuad[4];
ScreenVertex g_panelQuad[4];
#define g_panelTexture (&g_menuTex1)          // menu_1.tga
#define g_markerTexture (&g_menuTexSelector)  // selector.tga
static ScreenVertex g_listQuad[4];
#define g_optionsTexture (&g_menuTex2)  // menu_2.tga
#define g_saveTexture (&g_menuTex4)    // menu_4.tga

/* Fields of the theme object: its backdrop texture and text colour pairs. */

static inline DWORD mode_width(RenderDevice *d3d)
{
    return d3d->width();
}

static void set_blend(RenderDevice *d3d)
{
    d3d->SetRenderState(RS::AlphaBlendEnable, 1);
    d3d->SetRenderState(RS::SrcBlend,  Blend::SrcAlpha);
    d3d->SetRenderState(RS::DestBlend, Blend::InvSrcAlpha);
}

static ScreenVertex tlv(float x, float y, float u, float v)
{
    ScreenVertex t;
    t.sx = x; t.sy = y; t.sz = 0.0f; t.rhw = 10.0f;
    t.color = 0xffffffff; t.specular = 0;
    t.tu = u; t.tv = v;
    return t;
}

void Menu_DrawBackdrop(RenderDevice *d3d, ThemeAssetBlock *theme)
{
    set_blend(d3d);
    SceneTexture *tex = theme->images[THEME_IMG_MENU];
    d3d->SetTexture(0, tex);
    d3d->Draw(Prim::TriangleStrip, MENU_FVF,
                                g_backdropQuad, 4, 0);
}

/* The marker pair itself: top y0 in pixels, centres in 640-space.  Two 32x32
 * quads that breathe in and out in antiphase, 4 units either way; they are
 * mirror images (u = 1 on the left, 0 on the right), which the texture relies
 * on.  Double sin() stands in for the x87 FSIN: the lost bits move a marker by
 * far less than a pixel. */
static void draw_markers(RenderDevice *d3d, float y0, float left, float right)
{
    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float y1 = (float)(w << 5) * K640 + y0;

    set_blend(d3d);

    const float l0 = (left + 16.0f) * fw * K640, l1 = (left - 16.0f) * fw * K640;
    ScreenVertex q[4] = {
        tlv(l0, y0, 1.0f, 0.0f), tlv(l0, y1, 1.0f, 1.0f),
        tlv(l1, y0, 0.0f, 0.0f), tlv(l1, y1, 0.0f, 1.0f),
    };
    d3d->SetTexture(0, g_markerTexture);
    d3d->Draw(Prim::TriangleStrip, MENU_FVF, q, 4, 0);

    const float r0 = (right + 16.0f) * fw * K640, r1 = (right - 16.0f) * fw * K640;
    ScreenVertex p[4] = {
        tlv(r0, y0, 0.0f, 0.0f), tlv(r0, y1, 0.0f, 1.0f),
        tlv(r1, y0, 1.0f, 0.0f), tlv(r1, y1, 1.0f, 1.0f),
    };
    d3d->Draw(Prim::TriangleStrip, MENU_FVF, p, 4, 0);
}

void
Menu_DrawCursorMarkers(Game *g, RenderDevice *d3d, DWORD ms, float rowOffset)
{
    const DWORD w  = mode_width(d3d);
    const float y0 = (float)(w * 172) * K640
                   + ((float)g->menu()->cursor() * 0.05f + rowOffset * K640) * (float)w;
    const double t = (double)ms * 0.01;
    draw_markers(d3d, y0, (float)(sin(t) * 4.0 + 244.0),
                 (float)(sin(t + 3.14159274101257) * 4.0 + 396.0));
}

/* The controls page's own marker pair: the same quads on its 20-unit row pitch
 * from y 102, at the page's edges (centres 40 and 600) so they bracket the
 * whole row. */
void Menu_DrawControlsCursorMarkers(Game *g, RenderDevice *d3d, DWORD ms)
{
    const DWORD w  = mode_width(d3d);
    const float y0 = (float)(w * 102) * K640
                   + (float)g->menu()->cursor() * (float)w * 0.03125f;
    const double t = (double)ms * 0.01;
    draw_markers(d3d, y0, (float)(sin(t) * 4.0 + 40.0),
                 (float)(sin(t + 3.14159274101257) * 4.0 + 600.0));
}

/* The list screens (main, options, Load Game, Save Game) are one shape:
 * backdrop; the blend block again (redundant, but the game does it); a panel
 * quad with its own texture; centred rows at x = w/2 in 12x14 cells; the
 * cursor markers.  Fixed rows sit at 180 + 32 i virtual, the cell sizes
 * re-reading the width per row.  Slot rows accumulate y from 180 in steps of
 * 32; the count is the save-slot count, re-read every pass, unsigned. */
static const float k_rowY[6] = {
    0.28125f, 0.33125001f, 0.38124999f, 0.43125001f, 0.48124999f, 0.53125f,
};

static void draw_panel(RenderDevice *d3d, ThemeAssetBlock *theme, const SceneTexture *tex,
                       void *quad)
{
    Menu_DrawBackdrop(d3d, theme);
    set_blend(d3d);
    d3d->SetTexture(0, tex);
    d3d->Draw(Prim::TriangleStrip, MENU_FVF, quad, 4, 0);
}

static void draw_fixed_rows(RenderDevice *d3d, ThemeAssetBlock *theme, TextRenderer *text,
                            const char *const *rows, int n,
                            ThemeTextColorSlot first)
{
    const float fw = (float)mode_width(d3d);
    const ThemeTextColorPair *col = &theme->textColors[first];
    for (int i = 0; i < n; i++) {
        const DWORD wr = mode_width(d3d);
        const float cw = (float)(wr * 12) * K640, ch = (float)(wr * 14) * K640;
        text->drawCentered(fw * 0.5f, fw * k_rowY[i], cw, ch, 0.75f,
                           rows[i], d3d, 0, col[i].color1, col[i].color2);
    }
}

static void draw_slot_rows(Game *g, RenderDevice *d3d, ThemeAssetBlock *theme,
                           TextRenderer *text, ThemeTextColorSlot slot)
{
    SaveSlots *ss = g->saveSlots();
    if (ss->count() == 0)
        return;
    const float fw = (float)mode_width(d3d);
    const float x  = fw * 0.5f;
    const DWORD top = theme->textColors[slot].color1;
    const DWORD bot = theme->textColors[slot].color2;
    float y = 180.0f;
    for (unsigned i = 0; i < ss->count(); i++) {
        const DWORD wr = mode_width(d3d);
        const float cw = (float)(wr * 12) * K640, ch = (float)(wr * 14) * K640;
        text->drawCentered(x, fw * y * K640, cw, ch, 0.75f,
                           ss->slot((unsigned char)i)->name, d3d, 0, top, bot);
        y += 32.0f;
    }
}

/* The level-select page: the slot list's panel and text, packed tighter.  The
 * panel texture has its own "Load Game" heading, so the page starts where the
 * slot rows do: the theme as "< name >" at 180, then LEVELSELECT_ROWS levels
 * at 208 + 20 i, inside the space the six slot rows use.  Longer themes
 * scroll.  The markers sit 8 above the row, as on the 32-unit menus, spread
 * wider; the title is a fixed gold. */
#define LS_TITLE_Y  180.0f
#define LS_TITLE_TOP 0xffffd040u  // gold, fixed: not a theme colour
#define LS_TITLE_BOT 0xffc08000u
#define LS_MARKER_SPREAD 1.3f  // level names run wider than menu rows
#define LS_ROW_Y    208.0f
#define LS_ROW_STEP  20.0f
void Menu_RenderLevelSelect(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                            TextRenderer *text, DWORD ms)
{
    LevelSelectView v;
    LevelSelect_View(g, &v);
    draw_panel(d3d, theme, g_panelTexture, g_listQuad);
    const DWORD w  = mode_width(d3d);
    const float fw = (float)w, x = fw * 0.5f;
    const float cw = (float)(w * 12) * K640, ch = (float)(w * 14) * K640;
    const ThemeTextColorPair &rowCol = theme->textColors[THEME_COLOR_MENULOADGAMEENTRIES];
    char title[80];

    snprintf(title, sizeof(title), "< %s >", v.theme);
    text->drawCentered(x, fw * LS_TITLE_Y * K640, cw, ch, 0.75f, title, d3d, 0,
                       LS_TITLE_TOP, LS_TITLE_BOT);
    for (int i = 0; i < v.count; i++)
        text->drawCentered(x, fw * (LS_ROW_Y + LS_ROW_STEP * i) * K640, cw, ch, 0.75f,
                           v.rows[i], d3d, 0, rowCol.color1, rowCol.color2);
    // The main menu markers' centres (320 -+ 76, a 4-unit wobble), spread
    // wider.
    const double t = (double)ms * 0.01;
    const float half = 76.0f * LS_MARKER_SPREAD;
    draw_markers(d3d, fw * (LS_ROW_Y - 8.0f + LS_ROW_STEP * v.selected) * K640,
                 (float)(sin(t) * 4.0) + 320.0f - half,
                 (float)(sin(t + 3.14159274101257) * 4.0) + 320.0f + half);
}

static const char *const k_mainMenuRows[6] = {
    "New Game", "Load Game", "Highscores", "Options", "Credits", "Quit",
};
static const char *const k_optionsRows[3] = { "Controls", "Video", "Audio" };

void
Menu_RenderMainMenu(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d, TextRenderer *text,
                    DWORD ms)
{
    draw_panel(d3d, theme, g_panelTexture, g_panelQuad);
    draw_fixed_rows(d3d, theme, text, k_mainMenuRows, 6, THEME_COLOR_MENUNEWGAME);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

void
Menu_RenderOptionsMenu(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d, TextRenderer *text,
                       DWORD ms)
{
    draw_panel(d3d, theme, g_optionsTexture, g_listQuad);
    draw_fixed_rows(d3d, theme, text, k_optionsRows, 3, THEME_COLOR_MENUOPTIONSCONTROL);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

void Menu_RenderRestoreSlotList(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                                TextRenderer *text, DWORD ms)
{
    draw_panel(d3d, theme, g_panelTexture, g_listQuad);
    draw_slot_rows(g, d3d, theme, text, THEME_COLOR_MENULOADGAMEENTRIES);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

void Menu_RenderSaveSlotList(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                             TextRenderer *text, DWORD ms)
{
    draw_panel(d3d, theme, g_saveTexture, g_listQuad);
    draw_slot_rows(g, d3d, theme, text, THEME_COLOR_MENUSAVEGAMEENTRIES);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

/* The option widgets: a four-point square model pushed through a matrix and
 * drawn as a strip with UVs (1,0) (1,1) (0,0) (0,1), rhw 10, white.  The
 * matrix is a translation, or for a knob RotZ(a) then the translation: rotate
 * about the model's centre, then place.  A knob's angle is 3pi/4 - v * 3pi/200
 * for a 0..100 value, a 270-degree sweep.  Double cos and sin: the lost x87
 * bits move a knob by far less than a pixel. */
static Vec3 g_widgetModel[4];
#define g_texOn (&g_menuTexOn)     // knopf_ein.tga
#define g_texOff (&g_menuTexOff)    // knopf_aus.tga
#define g_texKnobBase (&g_menuTexScale)  // scale.tga
#define g_texKnob (&g_menuTexKnob)   // drehknopf.tga

struct Affine { float c, s, tx, ty; };  // RotZ(c, s) then T(tx, ty, 0)

static Affine place(float tx, float ty)  { Affine a = { 1.0f, 0.0f, tx, ty }; return a; }
static Affine knob(float tx, float ty, unsigned value)
{
    const double ang = (double)(2.3561945f - (float)value * 0.0471238904f);
    Affine a = { (float)cos(ang), (float)sin(ang), tx, ty };
    return a;
}

/* The video page's three-position knobs (0, 1, 2) use fixed angles: +3pi/4,
 * none, -3pi/4, the ends and middle of the same sweep.  Any other value leaves
 * the plain placement. */
static Affine knob3(float tx, float ty, unsigned char value)
{
    if (value == 0) { Affine a = { (float)cos(2.35619449615478515625),
                                   (float)sin(2.35619449615478515625), tx, ty }; return a; }
    if (value == 2) { Affine a = { (float)cos(-2.35619449615478515625),
                                   (float)sin(-2.35619449615478515625), tx, ty }; return a; }
    return place(tx, ty);
}

static void draw_widget(RenderDevice *d3d, const Affine &m, const SceneTexture *tex,
                        DWORD colour = 0xffffffff)
{
    // A row vector times [[c,-s,0,0],[s,c,0,0],[0,0,1,0],[tx,ty,0,1]]: w stays
    // exactly 1, so the perspective divide is skipped.
    static const float uv[4][2] = { {1, 0}, {1, 1}, {0, 0}, {0, 1} };
    ScreenVertex q[4];
    for (int i = 0; i < 4; i++) {
        const Vec3 &p = g_widgetModel[i];
        q[i] = tlv(p.x * m.c + p.y * m.s + m.tx,
                   -p.x * m.s + p.y * m.c + m.ty, uv[i][0], uv[i][1]);
        q[i].sz = p.z;
        q[i].color = colour;
    }
    d3d->SetTexture(0, tex);
    d3d->Draw(Prim::TriangleStrip, MENU_FVF, q, 4, 0);
}

/* A left-aligned option label in the 12x14 cell, at x = xv virtual. */
static void draw_label_c(RenderDevice *d3d, TextRenderer *text, float xv,
                         float yK, const char *str, DWORD top, DWORD bot)
{
    const DWORD w = mode_width(d3d);
    text->drawLeft((float)(DWORD)(w * (DWORD)xv) * K640, (float)w * yK,
                   (float)(w * 12) * K640, (float)(w * 14) * K640, 0.75f,
                   str, d3d, 0, top, bot);
}

static void draw_label(RenderDevice *d3d, ThemeAssetBlock *theme, TextRenderer *text,
                       float xv, float yK, const char *str,
                       ThemeTextColorSlot slot)
{
    const ThemeTextColorPair &col = theme->textColors[slot];
    draw_label_c(d3d, text, xv, yK, str, col.color1, col.color2);
}

/* Menu node 0xc.  Four rows of a label at x 262 and its widget at x 368: 3D
 * sound (toggle), sound volume (knob), CD music (toggle), CD volume (knob). */
void Menu_RenderSoundOptions(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                             TextRenderer *text, DWORD ms)
{
    draw_panel(d3d, theme, g_saveTexture, g_panelQuad);

    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float tx = (float)(w * 368) * K640;

    draw_label(d3d, theme, text, 262.0f, 0.28125f, "3D Sound", THEME_COLOR_MENUAUDIO3DSOUND);
    draw_widget(d3d, place(tx, fw * 0.29374999f),
                g->sound3D() != 0 ? g_texOn : g_texOff);

    draw_label(d3d, theme, text, 262.0f, 0.33125001f, "Sound Vol.", THEME_COLOR_MENUAUDIOSOUNDVOL);
    draw_widget(d3d, place(tx, fw * 0.34375f), g_texKnobBase);
    draw_widget(d3d, knob(tx, fw * 0.34375f, g->waveVolume()), g_texKnob);

    draw_label(d3d, theme, text, 262.0f, 0.38124999f, "CD Music", THEME_COLOR_MENUAUDIOCDMUSIC);
    draw_widget(d3d, place(tx, fw * 0.39375001f),
                g->musicOn() != 0 ? g_texOn : g_texOff);

    draw_label(d3d, theme, text, 262.0f, 0.43125001f, "CD Vol.", THEME_COLOR_MENUAUDIOCDVOL);
    draw_widget(d3d, place(tx, fw * 0.44374999f), g_texKnobBase);
    draw_widget(d3d, knob(tx, fw * 0.44374999f, g->cdVolume()), g_texKnob);

    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

/* Menu node 0xb: reflection (toggle), shadows, highlights and particles
 * (three-position knobs).  Shadows need a stencil buffer and a mode deeper
 * than 16 bits; without them the label is a translucent grey, its base
 * half-grey and its knob not drawn.  Both tests are re-made at each use. */
#define g_videoTexture (&g_menuTex3)  // menu_3.tga

static bool shadows_available(RenderDevice *d3d)
{
    return d3d->hasStencil() && d3d->bitDepth() > 16;
}

void Menu_RenderVideoOptions(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                             TextRenderer *text, DWORD ms)
{
    draw_panel(d3d, theme, g_videoTexture, g_listQuad);

    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float tx = (float)(w * 368) * K640;

    draw_label(d3d, theme, text, 262.0f, 0.28125f, "Reflection", THEME_COLOR_MENUVIDEOREFLECTION);
    draw_widget(d3d, place(tx, fw * 0.29374999f),
                g->videoReflection() != 0 ? g_texOn : g_texOff);

    if (shadows_available(d3d))
        draw_label(d3d, theme, text, 262.0f, 0.33125001f, "Shadows", THEME_COLOR_MENUVIDEOSHADOW);
    else
        draw_label_c(d3d, text, 262.0f, 0.33125001f, "Shadows",
                     0x80555555, 0x80aaaaaa);
    draw_widget(d3d, place(tx, fw * 0.34375f), g_texKnobBase,
                shadows_available(d3d) ? 0xffffffff : 0x80808080);
    if (shadows_available(d3d))
        draw_widget(d3d, knob3(tx, fw * 0.34375f, g->videoShadows()), g_texKnob);

    draw_label(d3d, theme, text, 262.0f, 0.38124999f, "Highlights", THEME_COLOR_MENUVIDEOHIGHLIGHT);
    draw_widget(d3d, place(tx, fw * 0.39375001f), g_texKnobBase);
    draw_widget(d3d, knob3(tx, fw * 0.39375001f, g->videoHighlights()), g_texKnob);

    draw_label(d3d, theme, text, 262.0f, 0.43125001f, "Particles", THEME_COLOR_MENUVIDEOPARTICLE);
    draw_widget(d3d, place(tx, fw * 0.44374999f), g_texKnobBase);
    draw_widget(d3d, knob3(tx, fw * 0.44374999f, g->videoParticles()), g_texKnob);

    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

/* Menu node 0xa.  It opens with the full-screen backdrop the score screens
 * use, and a header strip from the top half of the video panel's texture.
 * Then thirteen action labels at x 60, y 110 + 20 i; the following-camera
 * toggle and the joystick dead-zone knob; and, right-aligned at x 580, each
 * action's bound key names, or "???" while that row is being rebound.  The
 * rebind nodes are not in row order; they are the menu tree's children. */
struct ControlRow { const char *label; const char *action; unsigned char node; };
static const ControlRow k_controls[13] = {
    { "forwards",     "John_Move_Forward", 0x14 },
    { "backwards",    "John_Move_Back",    0x15 },
    { "turn left",    "John_Turn_Left",    0x17 },
    { "turn right",   "John_Turn_Right",   0x16 },
    { "zoom in",      "John_Zoom_In",      0x18 },
    { "zoom out",     "John_Zoom_Out",     0x19 },
    { "overview",     "John_OverView",     0x1c },
    { "bomb",         "John_Release_Bomb", 0x1a },
    { "suicide",      "John_Harakiri",     0x1b },
    { "camera left",  "CamModeLeft",       0x1d },
    { "camera right", "CamModeRight",      0x1e },
    { "camera up",    "CamModeUp",         0x1f },
    { "camera down",  "CamModeDown",       0x20 },
};

void Menu_RenderControlsRemap(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d,
                              TextRenderer *text, DWORD ms)
{
    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float fh = (float)d3d->height();

    ScreenVertex back[4] = {
        tlv(fw, 0.0f, 0.6f, 0.4f), tlv(fw, fh, 0.6f, 0.6f),
        tlv(0.0f, 0.0f, 0.4f, 0.4f), tlv(0.0f, fh, 0.4f, 0.6f),
    };
    set_blend(d3d);
    SceneTexture *tex = theme->images[THEME_IMG_MENU];
    d3d->SetTexture(0, tex);
    d3d->Draw(Prim::TriangleStrip, MENU_FVF, back, 4, 0);

    const float hx0 = fw * 0.60000002f, hx1 = fw * 0.40000001f;
    const float hy0 = fw * 0.065624997f, hy1 = fw * 0.16562501f;
    ScreenVertex head[4] = {
        tlv(hx0, hy0, 1.0f, 0.0f), tlv(hx0, hy1, 1.0f, 0.5f),
        tlv(hx1, hy0, 0.0f, 0.0f), tlv(hx1, hy1, 0.0f, 0.5f),
    };
    d3d->SetTexture(0, g_videoTexture);
    d3d->Draw(Prim::TriangleStrip, MENU_FVF, head, 4, 0);

    const ThemeTextColorPair &col = theme->textColors[THEME_COLOR_MENUCONTROLENTRIES];
    float rowY[13];
    for (int i = 0; i < 13; i++) {
        rowY[i] = fw * (0.171875f + 0.03125f * (float)i);
        const DWORD wr = mode_width(d3d);
        text->drawLeft(fw * 0.09375f, rowY[i],
                       (float)(wr * 12) * K640, (float)(wr * 14) * K640, 0.75f,
                       k_controls[i].label, d3d, 0, col.color1, col.color2);
    }

    const float tx = (float)(w * 450) * K640;
    draw_label(d3d, theme, text, 180.0f, 0.578125f, "following camera", THEME_COLOR_MENUCONTROLCAMERA);
    draw_widget(d3d, place(tx, fw * 0.59062499f),
                g->cameraTurnsWithPlayer() != 0 ? g_texOn : g_texOff);

    draw_label(d3d, theme, text, 180.0f, 0.609375f, "joystick deathzone", THEME_COLOR_MENUCONTROLCAMERA);
    draw_widget(d3d, place(tx, fw * 0.62187499f), g_texKnobBase);
    draw_widget(d3d, knob(tx, fw * 0.62187499f, g->joyDeadzone()), g_texKnob);

    char buf[0x100];
    for (int i = 0; i < 13; i++) {
        ProgCtrl_GetBindingStr(&g_progCtrl, 1, k_controls[i].action, buf, sizeof(buf));
        const bool asking = g->rebindActive() != 0 && g->rebindCode() == k_controls[i].node;
        const DWORD wr = mode_width(d3d);
        text->drawRight(fw * 0.90625f, rowY[i],
                        (float)(wr * 12) * K640, (float)(wr * 14) * K640, 0.75f,
                        asking ? "???" : buf, d3d, 0, col.color1, col.color2);
    }

    Menu_DrawControlsCursorMarkers(g, d3d, ms);
}

/* Builds the quads the screens draw and loads the nine menu textures, once per
 * device.  In 640-space times w, with the width unsigned:
 *   panel quad   x 0.6..0.4  y 0.175..0.275  v 0..0.5 (top half)
 *   list quad    the same rectangle          v 0.5..1 (bottom half)
 *   backdrop     x 0.7..0.3  y 0.175..0.575  v 0..1
 *   widget model the square (+-w/64, +-w/64, 0), 10 virtual units
 * Strip order in each quad: (x0,y0) (x0,y1) (x1,y0) (x1,y1), u 1 1 0 0.
 * Textures are "<prefix>\textures\<file>".  PRESERVED: the path is formatted
 * unbounded into 260 bytes. */
struct MenuTextureLoad { SceneTexture *obj; const char *file; };
static const MenuTextureLoad k_menuTextures[9] = {
    { &g_menuTex1, "menu_1.tga" },
    { &g_menuTex2, "menu_2.tga" },
    { &g_menuTex3, "menu_3.tga" },
    { &g_menuTex4, "menu_4.tga" },
    { &g_menuTexSelector, "selector.tga" },
    { &g_menuTexOn, "knopf_ein.tga" },
    { &g_menuTexOff, "knopf_aus.tga" },
    { &g_menuTexKnob, "drehknopf.tga" },
    { &g_menuTexScale, "scale.tga" },
};

static void fill_quad(ScreenVertex *q, float x0, float x1, float y0, float y1,
                      float v0, float v1)
{
    q[0] = tlv(x0, y0, 1.0f, v0);
    q[1] = tlv(x0, y1, 1.0f, v1);
    q[2] = tlv(x1, y0, 0.0f, v0);
    q[3] = tlv(x1, y1, 0.0f, v1);
}

void Menu_BuildMenuGeometry(RenderDevice *d3d, const char *prefix)
{
    const float fw = (float)d3d->width();

    fill_quad((ScreenVertex *)g_panelQuad, fw * 0.60000002f, fw * 0.40000001f,
              fw * 0.17499999f, fw * 0.27500001f, 0.0f, 0.5f);
    fill_quad((ScreenVertex *)g_listQuad, fw * 0.60000002f, fw * 0.40000001f,
              fw * 0.17499999f, fw * 0.27500001f, 0.5f, 1.0f);
    fill_quad((ScreenVertex *)g_backdropQuad, fw * 0.69999999f, fw * 0.30000001f,
              fw * 0.17499999f, fw * 0.57499999f, 0.0f, 1.0f);

    const float sp = fw * 0.015625f, sn = fw * -0.015625f;
    Vec3 *m = (Vec3 *)g_widgetModel;
    m[0].x = sp; m[0].y = sn; m[0].z = 0.0f;
    m[1].x = sp; m[1].y = sp; m[1].z = 0.0f;
    m[2].x = sn; m[2].y = sn; m[2].z = 0.0f;
    m[3].x = sn; m[3].y = sp; m[3].z = 0.0f;

    char path[260];
    for (const MenuTextureLoad &t : k_menuTextures) {
        sprintf(path, "%s\\textures\\%s", prefix, t.file);
        Texture_ImportSceneTextures(t.obj, d3d, path, 1, 0, 0);
    }
}

/* The credits: 69 text draws in two columns, scrolling up with time.
 *   headings  centred        x 320, cell 20
 *   roles     right-aligned  x 310, cell 16 (right edge at the gutter)
 *   names     left-aligned   x 330, cell 16
 *   y         (scroll + row offset), scaled
 *   scroll    500 - elapsed ms * 0.05 (20 ms per virtual pixel)
 * The start time is re-armed when the Game's credits flag is set (the
 * keypress handler sets it on entering) and the flag cleared; once scroll
 * falls below -1800 (46 s) the credits start again.  DETERMINISM: the
 * width and the elapsed time are unsigned, so a clock behind the start
 * gives a huge elapsed time and an immediate restart.  The scroll is float
 * arithmetic; sub-pixel differences are unobservable. */
enum CreditAlign { CR_CENTRE, CR_RIGHT, CR_LEFT };

struct CreditRow {
    CreditAlign align;
    float       offset;  // virtual pixels below the scroll origin
    const char *text;
};

static const CreditRow k_credits[] = {
    { CR_CENTRE,     0.0f, "PROGRAMMING" },
    { CR_RIGHT,    32.0f, "DarkLight2 3D-Engine" },
    { CR_LEFT,    32.0f, "Andreas Lenk" },
    { CR_RIGHT,    56.0f, "Representation" },
    { CR_LEFT,    56.0f, "Andreas Lenk" },
    { CR_RIGHT,    80.0f, "Particlesystems" },
    { CR_LEFT,    80.0f, "Marco Kalweit" },
    { CR_RIGHT,   104.0f, "3D-Soundengine" },
    { CR_LEFT,   104.0f, "Marco Kalweit" },
    { CR_RIGHT,   128.0f, "Gamebehavior" },
    { CR_LEFT,   128.0f, "Falk M\xf6" "ckel" },
    { CR_RIGHT,   152.0f, "Control" },
    { CR_LEFT,   152.0f, "Marco Kalweit" },
    { CR_CENTRE,   216.0f, "GRAPHICS" },
    { CR_RIGHT,   248.0f, "Models" },
    { CR_LEFT,   248.0f, "Thomas Heinschke" },
    { CR_LEFT,   264.0f, "Marco Kalweit" },
    { CR_LEFT,   280.0f, "Andreas Lenk" },
    { CR_RIGHT,   304.0f, "Textures" },
    { CR_LEFT,   304.0f, "Thomas Heinschke" },
    { CR_LEFT,   320.0f, "Marco Kalweit" },
    { CR_LEFT,   336.0f, "Andreas Lenk" },
    { CR_RIGHT,   360.0f, "Menu" },
    { CR_LEFT,   360.0f, "Thomas Heinschke" },
    { CR_CENTRE,   424.0f, "SOUND" },
    { CR_RIGHT,   456.0f, "FX" },
    { CR_LEFT,   456.0f, "Falk M\xf6" "ckel" },
    { CR_RIGHT,   480.0f, "Music" },
    { CR_LEFT,   480.0f, "Fredrik Sand" },
    { CR_CENTRE,   544.0f, "LEVELDESIGN" },
    { CR_RIGHT,   576.0f, "Falk M\xf6" "ckel" },
    { CR_LEFT,   576.0f, "Sandra Tieg" },
    { CR_CENTRE,   640.0f, "TOOLS" },
    { CR_RIGHT,   672.0f, "Leveleditor" },
    { CR_LEFT,   672.0f, "Falk M\xf6" "ckel" },
    { CR_RIGHT,   696.0f, "Particlesystemeditor" },
    { CR_LEFT,   696.0f, "Marco Kalweit" },
    { CR_RIGHT,   720.0f, "PlugIns" },
    { CR_LEFT,   720.0f, "Andreas Lenk" },
    { CR_CENTRE,   784.0f, "TESTING" },
    { CR_RIGHT,   816.0f, "Christoph Heinschke" },
    { CR_LEFT,   816.0f, "Sven Kalweit" },
    { CR_RIGHT,   832.0f, "Kristin Hoffmann" },
    { CR_LEFT,   832.0f, "Daniel Reschke" },
    { CR_RIGHT,   848.0f, "Barbara Holler" },
    { CR_LEFT,   848.0f, "Sandra Tieg" },
    { CR_RIGHT,   864.0f, "Sebastian Holler" },
    { CR_LEFT,   864.0f, "" },  // an empty line
    { CR_CENTRE,   928.0f, "MANUAL" },
    { CR_RIGHT,   960.0f, "Andreas Lenk" },
    { CR_LEFT,   960.0f, "Falk M\xf6" "ckel" },
    { CR_CENTRE,  1024.0f, "GREETINGS" },
    { CR_RIGHT,  1056.0f, "Harriet Bach" },
    { CR_LEFT,  1056.0f, "Thomas M\xf6" "ckel" },
    { CR_RIGHT,  1072.0f, "Rene Bauer" },
    { CR_LEFT,  1072.0f, "Holger Nippe" },
    { CR_RIGHT,  1088.0f, "John Carmack" },
    { CR_LEFT,  1088.0f, "Frank Pleitz" },
    { CR_RIGHT,  1104.0f, "Mareen Franke" },
    { CR_LEFT,  1104.0f, "Werner Remke" },
    { CR_RIGHT,  1120.0f, "Jochen Hamma" },
    { CR_LEFT,  1120.0f, "Oliver Tomaschewski" },
    { CR_RIGHT,  1136.0f, "Tobias H\xfc" "ttner" },
    { CR_LEFT,  1136.0f, "Sven Trautrims" },
    { CR_RIGHT,  1152.0f, "it works" },
    { CR_LEFT,  1152.0f, "Lars Uhlmann" },
    { CR_RIGHT,  1168.0f, "Michael K\xe4" "mpf" },
    { CR_LEFT,  1168.0f, "Marcus Weidlich" },
    { CR_RIGHT,  1184.0f, "George Lucas" },
};

static float g_creditsScroll;
static DWORD g_creditsStartMs;

void Menu_RenderCreditsScroll(Game *game, RenderDevice *d3d, TextRenderer *text,
                              DWORD nowMs)
{
    const float scale = (float)d3d->width() * (1.0f / 640.0f);

    if (game->field_13cc8c() != 0) {
        g_creditsStartMs = nowMs;
        game->setField13cc8c(0);
    }
    g_creditsScroll = 500.0f - (float)(DWORD)(nowMs - g_creditsStartMs) * 0.05f;

    const float headCell = scale * 20.0f;
    const float cell     = scale * 16.0f;
    for (const CreditRow &r : k_credits) {
        const float y = (g_creditsScroll + r.offset) * scale;
        switch (r.align) {
        case CR_CENTRE:
            text->drawCentered(scale * 320.0f, y, headCell, headCell, 0.75f,
                               r.text, d3d, 0, 0xffffffff, 0xffffffff);
            break;
        case CR_RIGHT:
            text->drawRight(scale * 310.0f, y, cell, cell, 0.75f,
                            r.text, d3d, 0, 0xffffffff, 0xffffffff);
            break;
        case CR_LEFT:
            text->drawLeft(scale * 330.0f, y, cell, cell, 0.75f,
                           r.text, d3d, 0, 0xffffffff, 0xffffffff);
            break;
        }
    }

    if (g_creditsScroll < -1800.0f)
        g_creditsStartMs = nowMs;
}

/* Picks the screen for the current menu node; any other node draws nothing.
 * The level select's theme nodes (0x60 + t) draw the level select page. */
void
Menu_DispatchGameState(Game *g, ThemeAssetBlock *theme, RenderDevice *d3d, TextRenderer *text,
                       DWORD ms)
{
    const unsigned char node = g->menu()->node();
    switch (node) {
    case 0:    Menu_RenderMainMenu(g, theme, d3d, text, ms); break;
    case 2:    Menu_RenderRestoreSlotList(g, theme, d3d, text, ms); break;
    case 4:    Menu_RenderOptionsMenu(g, theme, d3d, text, ms); break;
    case 5:    Menu_RenderCreditsScroll(g, d3d, text, ms); break;
    case 0xa:  Menu_RenderControlsRemap(g, theme, d3d, text, ms); break;
    case 0xb:  Menu_RenderVideoOptions(g, theme, d3d, text, ms); break;
    case 0xc:  Menu_RenderSoundOptions(g, theme, d3d, text, ms); break;
    case 0x28: Menu_RenderLevelComplete(g, theme, d3d, text, ms); break;
    case 0x2a: Menu_RenderSaveSlotList(g, theme, d3d, text, ms); break;
    default:
        if (LevelSelect_IsThemeNode(node))
            Menu_RenderLevelSelect(g, theme, d3d, text, ms);
        break;
    }
}
