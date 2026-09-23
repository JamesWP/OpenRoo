/* MenuScreens draw functions -- see menuscreens.h.
 *
 * All written from the disassembly: the decompiles of these functions lose
 * arguments and reuse argument slots as scratch, so they misattribute reads.
 *
 * ── Shared conventions ───────────────────────────────────────────────────
 * Layout is authored in a 640-wide virtual space; w is the mode width loaded
 * UNSIGNED (FILD qword over a zeroed high dword) and K = 1/640.  Text cells
 * are integer products first -- (float)(w*12) * K, (float)(w*14) * K -- then
 * scaled, exactly as the original does them.  Every quad is 4 D3DTLVERTEX in
 * a TRIANGLESTRIP (FVF 0x1c4) with z 0, rhw 10.0, diffuse 0xFFFFFFFF,
 * specular 0.  Device calls go through d3d->pDevice (the proxy), re-read per
 * call as the original does.
 *
 * The vertex arrays, the widget model and the textures these read by address
 * are .bss that BuildMenuGeometry (below) fills once per device.  Each
 * texture is the IDirect3DTexture2 at +0x18 of one of nine game-global
 * SceneTexture objects; the objects themselves (their ctors, dtors and the
 * release at shutdown) stay the game's.
 *
 * ── DrawMenuBackdrop 0x0042df80 ──────────────────────────────────────────
 * Alpha blend on, SRCALPHA/INVSRCALPHA, the theme's backdrop texture (theme
 * +0x6f8a8, a SceneTexture*, NULL -> no texture), then the pre-built quad at
 * 0x4e06c8.  Same opening block as scoreoverlay.cpp's, but from a global
 * quad rather than one built on the stack.
 *
 * ── DrawMenuCursorMarkers 0x00437390 ─────────────────────────────────────
 * Two 32x32 quads at the highlighted row, texture *0x4e0578:
 *   top    Y = 172*w*K + (cursor*0.05 + rowOffset*K) * w      (cursor = the
 *          menu tree's byte at Game+0x175535; 0.05*w = 32 virtual per row)
 *   bottom Y + 32*w*K
 *   left   centre 244 + 4 sin(ms*0.01),  x = (centre -+ 16) * w * K
 *   right  centre 396 + 4 sin(ms*0.01 + pi)
 * so the pair breathes in and out in antiphase.  Each quad's first vertex
 * pair is at centre+16 with u = 1 on the left marker and u = 0 on the right:
 * the two are mirror images, which the texture relies on.
 *
 * FSIN on an extended-precision ms*0.01: here double sin() -- the lost bits
 * move a marker by far less than a pixel.
 *
 * ── RenderCreditsScroll 0x00436550 ────────────────────────────────────────
 *
 * 69 text draws in a two-column layout, scrolled upwards by time.  The table
 * below was generated from the original's call sequence (each call's FADD
 * offset constant, string PUSH and CALL target, scanned out of
 * Karoo.exe.orig), not transcribed from the decompile.
 *
 * Everything is in a 640-wide virtual space scaled by width/640:
 *   headings  DrawCentered  x 320, cell 20
 *   roles     DrawRight     x 310, cell 16      (right edge at the gutter)
 *   names     RenderText    x 330, cell 16
 *   y         (scroll + row offset) * scale
 *   scroll    500 - elapsed_ms * 0.05          (20 ms per virtual pixel)
 * All colours are 0xFFFFFFFF, spacing 0.75, first character 0.
 *
 * The clock is kept in two globals private to this function (byte scan:
 * 0x4e0680 and 0x4e0518 are referenced nowhere else), so they are statics
 * here.  The start time is re-armed when Game+0x13cc8c is set -- keypress.cpp
 * sets it on entering the credits -- and the flag is cleared.  Once scroll
 * falls below -1800 (46 s) the start is reset: the credits loop.
 *
 * Signedness preserved: both the width and the elapsed time are loaded with
 * FILD qword over a zeroed high dword, i.e. as UNSIGNED 32-bit values.  So a
 * clock that is behind the start (wrap, or a stale start) gives a huge
 * elapsed and an immediate loop, not a negative one.
 *
 * Float rounding: the original keeps the chain in x87 extended precision and
 * stores the scroll as a float; the per-row y is (float)scroll + offset.
 * Here it is float arithmetic -- sub-pixel differences, unobservable. */
#include "menuscreens.h"
#include "game.h"
#include "direct3d.h"
#include "textrenderer.h"
#include "texture.h"
#include "menutree.h"
#include "saveslots.h"
#include "levelselect.h"
#include "d3dmath.h"
#include "progctrl.h"
#include "scenetexture.h"
#include <stdio.h>
#include "gameglobals.h"
#include <math.h>

#define K640          (1.0f / 640.0f)
#define MENU_FVF      0x1c4        /* XYZRHW | DIFFUSE | SPECULAR | TEX1 */
#define g_backdropQuad ((void *)0x004e06c8)
#define g_panelQuad    ((void *)0x004e0580)
#define g_panelTexture (*(IDirect3DTexture2 **)0x004e0538)   /* menu_1.tga */
#define g_markerTexture (*(IDirect3DTexture2 **)0x004e0578)  /* selector.tga */
#define g_listQuad     ((void *)0x004e0600)
#define g_optionsTexture (*(IDirect3DTexture2 **)0x004e0760) /* menu_2.tga */
#define g_saveTexture  (*(IDirect3DTexture2 **)0x004e04e0)   /* menu_4.tga */
#define THEME_BACKDROP_TEX 0x6f8a8   /* SceneTexture* */
#define THEME_MAINMENU_COL 0x6f8d4   /* six (top, bottom) colour pairs */
#define THEME_RESTORE_COL  0x6f904   /* one pair, every slot row */
#define THEME_SAVE_COL     0x6f90c   /* one pair, every slot row */
#define THEME_OPTIONS_COL  0x6f91c   /* three pairs */

static inline DWORD mode_width(Direct3D *d3d)
{
    return d3d->pSelectedMode->dwWidth;
}

static void set_blend(Direct3D *d3d)
{
    d3d->pDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
    d3d->pDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND,  D3DBLEND_SRCALPHA);
    d3d->pDevice->SetRenderState(D3DRENDERSTATE_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

static D3DTLVERTEX tlv(float x, float y, float u, float v)
{
    D3DTLVERTEX t;
    t.sx = x; t.sy = y; t.sz = 0.0f; t.rhw = 10.0f;
    t.color = 0xffffffff; t.specular = 0;
    t.tu = u; t.tv = v;
    return t;
}

extern "C" __declspec(dllexport) void __cdecl
Menu_DrawBackdrop(Direct3D *d3d, void *theme)
{
    set_blend(d3d);
    SceneTexture *tex = *(SceneTexture **)((BYTE *)theme + THEME_BACKDROP_TEX);
    d3d->pDevice->SetTexture(0, tex ? tex->pTexture2 : NULL);
    d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, MENU_FVF,
                                g_backdropQuad, 4, 0);
}

/* The marker pair itself: top y0 in pixels, centres in 640-space. */
static void draw_markers(Direct3D *d3d, float y0, float left, float right)
{
    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float y1 = (float)(w << 5) * K640 + y0;

    set_blend(d3d);

    const float l0 = (left + 16.0f) * fw * K640, l1 = (left - 16.0f) * fw * K640;
    D3DTLVERTEX q[4] = {
        tlv(l0, y0, 1.0f, 0.0f), tlv(l0, y1, 1.0f, 1.0f),
        tlv(l1, y0, 0.0f, 0.0f), tlv(l1, y1, 0.0f, 1.0f),
    };
    d3d->pDevice->SetTexture(0, g_markerTexture);
    d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, MENU_FVF, q, 4, 0);

    const float r0 = (right + 16.0f) * fw * K640, r1 = (right - 16.0f) * fw * K640;
    D3DTLVERTEX p[4] = {
        tlv(r0, y0, 0.0f, 0.0f), tlv(r0, y1, 0.0f, 1.0f),
        tlv(r1, y0, 1.0f, 0.0f), tlv(r1, y1, 1.0f, 1.0f),
    };
    d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, MENU_FVF, p, 4, 0);
}

/* The animated pair at top y0 (pixels): DrawCursorMarkers' tail, shared
 * with the level select, which places it by its own row spacing. */
static void animated_markers(Direct3D *d3d, DWORD ms, float y0)
{
    const double t = (double)ms * 0.01;
    draw_markers(d3d, y0, (float)(sin(t) * 4.0 + 244.0),
                 (float)(sin(t + 3.14159274101257) * 4.0 + 396.0));
}

extern "C" __declspec(dllexport) void __cdecl
Menu_DrawCursorMarkers(Game *g, Direct3D *d3d, DWORD ms, float rowOffset)
{
    const DWORD w  = mode_width(d3d);
    const float y0 = (float)(w * 172) * K640
                   + ((float)g->menu()->cursor() * 0.05f + rowOffset * K640) * (float)w;
    animated_markers(d3d, ms, y0);
}

/* ── DrawControlsCursorMarkers 0x00437740 ─────────────────────────────────
 * The controls page's own marker pair: the same two quads, but on its
 * 20-unit row pitch from y 102, and at the page's edges (centres 40 and 600)
 * so they bracket the whole label ... binding row:
 *   top y = 102*w*K + cursor * w * 0.03125 */
extern "C" __declspec(dllexport) void __cdecl
Menu_DrawControlsCursorMarkers(Game *g, Direct3D *d3d, DWORD ms)
{
    const DWORD w  = mode_width(d3d);
    const float y0 = (float)(w * 102) * K640
                   + (float)g->menu()->cursor() * (float)w * 0.03125f;
    const double t = (double)ms * 0.01;
    draw_markers(d3d, y0, (float)(sin(t) * 4.0 + 40.0),
                 (float)(sin(t + 3.14159274101257) * 4.0 + 600.0));
}

/* ── The list screens ─────────────────────────────────────────────────────
 * RenderMainMenu 0x0042e190, RenderOptionsMenu 0x0042e500,
 * RenderRestoreSlotList 0x0042e710 and RenderSaveSlotList 0x0042e880 are one
 * shape: backdrop; the blend block AGAIN (redundant -- the backdrop just set
 * it; kept); a panel quad with its own texture; centred rows at x = w/2 with
 * 12x14 cells; the cursor markers with rowOffset 0.
 *
 *   screen   node  panel quad  panel texture  rows
 *   main     0     0x4e0580    *0x4e0538      6 fixed, own colour pair each
 *   options  4     0x4e0600    *0x4e0760      3 fixed, own colour pair each
 *   restore  2     0x4e0600    *0x4e0538      save-slot names, one pair
 *   save     0x2a  0x4e0600    *0x4e04e0      save-slot names, one pair
 *
 * Fixed rows sit at y = w * {0.28125 + 0.05 i} (180 + 32 i virtual), the
 * cell sizes re-reading the width per row as the original does.  The slot
 * rows accumulate y = 180, +32 per row in 640-space and scale as w*y*K;
 * the count is the SaveSlots byte, re-read every iteration, unsigned. */
static const float k_rowY[6] = {
    0.28125f, 0.33125001f, 0.38124999f, 0.43125001f, 0.48124999f, 0.53125f,
};

static void draw_panel(Direct3D *d3d, void *theme, IDirect3DTexture2 *tex,
                       void *quad)
{
    Menu_DrawBackdrop(d3d, theme);
    set_blend(d3d);
    d3d->pDevice->SetTexture(0, tex);
    d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, MENU_FVF, quad, 4, 0);
}

static void draw_fixed_rows(Direct3D *d3d, void *theme, TextRenderer *text,
                            const char *const *rows, int n, unsigned colOff)
{
    const float fw = (float)mode_width(d3d);
    const DWORD *col = (const DWORD *)((BYTE *)theme + colOff);
    for (int i = 0; i < n; i++) {
        const DWORD wr = mode_width(d3d);
        const float cw = (float)(wr * 12) * K640, ch = (float)(wr * 14) * K640;
        text->drawCentered(fw * 0.5f, fw * k_rowY[i], cw, ch, 0.75f,
                           rows[i], d3d, 0, col[2 * i], col[2 * i + 1]);
    }
}

static void draw_slot_rows(Game *g, Direct3D *d3d, void *theme,
                           TextRenderer *text, unsigned colOff)
{
    SaveSlots *ss = g->saveSlots();
    if (ss->count() == 0)
        return;
    const float fw = (float)mode_width(d3d);
    const float x  = fw * 0.5f;
    const DWORD top = *(const DWORD *)((BYTE *)theme + colOff);
    const DWORD bot = *(const DWORD *)((BYTE *)theme + colOff + 4);
    float y = 180.0f;
    for (unsigned i = 0; i < ss->count(); i++) {
        const DWORD wr = mode_width(d3d);
        const float cw = (float)(wr * 12) * K640, ch = (float)(wr * 14) * K640;
        text->drawCentered(x, fw * y * K640, cw, ch, 0.75f,
                           ss->slot((unsigned char)i)->name, d3d, 0, top, bot);
        y += 32.0f;
    }
}

/* Our level-select page (levelselect.h): the slot list's panel and 12x14
 * text, packed tighter.  The panel texture carries its own "Load Game"
 * heading above the list, so the page starts where the slot rows do: the
 * theme as "< name >" at 180, then LEVELSELECT_ROWS levels at 208 + 20 i
 * in 640-space, the last at 328 -- inside the 180..340 the six slot rows
 * already occupy.  Longer themes scroll (LevelSelect_View picks the window).
 * The menus' cursor markers sit 8 above the row, as they do on the 32-unit
 * menus (172 against 180). */
#define LS_TITLE_Y  180.0f
#define LS_ROW_Y    208.0f
#define LS_ROW_STEP  20.0f
static void draw_level_select(Game *g, Direct3D *d3d, void *theme,
                              TextRenderer *text, DWORD ms)
{
    LevelSelectView v;
    LevelSelect_View(g, &v);
    draw_panel(d3d, theme, g_panelTexture, g_listQuad);
    const DWORD w  = mode_width(d3d);
    const float fw = (float)w, x = fw * 0.5f;
    const float cw = (float)(w * 12) * K640, ch = (float)(w * 14) * K640;
    const DWORD *rowCol   = (const DWORD *)((BYTE *)theme + THEME_RESTORE_COL);
    const DWORD *themeCol = (const DWORD *)((BYTE *)theme + THEME_MAINMENU_COL);
    char title[80];

    snprintf(title, sizeof(title), "< %s >", v.theme);
    text->drawCentered(x, fw * LS_TITLE_Y * K640, cw, ch, 0.75f, title, d3d, 0,
                       themeCol[0], themeCol[1]);
    for (int i = 0; i < v.count; i++)
        text->drawCentered(x, fw * (LS_ROW_Y + LS_ROW_STEP * i) * K640, cw, ch, 0.75f,
                           v.rows[i], d3d, 0, rowCol[0], rowCol[1]);
    animated_markers(d3d, ms,
                     fw * (LS_ROW_Y - 8.0f + LS_ROW_STEP * v.selected) * K640);
}

static const char *const k_mainMenuRows[6] = {
    "New Game", "Load Game", "Highscores", "Options", "Credits", "Quit",
};
static const char *const k_optionsRows[3] = { "Controls", "Video", "Audio" };

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderMainMenu(Game *g, void *theme, Direct3D *d3d, TextRenderer *text,
                    DWORD ms)
{
    draw_panel(d3d, theme, g_panelTexture, g_panelQuad);
    draw_fixed_rows(d3d, theme, text, k_mainMenuRows, 6, THEME_MAINMENU_COL);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderOptionsMenu(Game *g, void *theme, Direct3D *d3d, TextRenderer *text,
                       DWORD ms)
{
    draw_panel(d3d, theme, g_optionsTexture, g_listQuad);
    draw_fixed_rows(d3d, theme, text, k_optionsRows, 3, THEME_OPTIONS_COL);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderRestoreSlotList(Game *g, void *theme, Direct3D *d3d,
                           TextRenderer *text, DWORD ms)
{
    if (LevelSelect_Active(g)) {
        draw_level_select(g, d3d, theme, text, ms);
        return;
    }
    draw_panel(d3d, theme, g_panelTexture, g_listQuad);
    draw_slot_rows(g, d3d, theme, text, THEME_RESTORE_COL);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderSaveSlotList(Game *g, void *theme, Direct3D *d3d,
                        TextRenderer *text, DWORD ms)
{
    draw_panel(d3d, theme, g_saveTexture, g_listQuad);
    draw_slot_rows(g, d3d, theme, text, THEME_SAVE_COL);
    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

/* ── The options widgets ───────────────────────────────────────────────────
 * The sound, video and controls pages draw their values as textured quads,
 * each the same four-point model at 0x4e04e8 (.bss, filled by the geometry
 * builder) pushed through a matrix and drawn as a strip with UVs
 * (1,0) (1,1) (0,0) (0,1), rhw 10, white:
 *
 *   p' = (p, 1) * M,   then p'.xyz /= p'.w unless p'.w == 1.0
 *
 * (the original's test is against a double 1.0 at 0x45d2e8, not 0 -- a
 * divide by 1 that it skips; the result is the same either way).  M is a
 * translation, or for a knob RotZ(a) * T -- rotate about the model's own
 * centre, then place.  RotZ is [[c,-s],[s,c]]: that is what 0x4234c0 builds
 * (named BuildXRotationMatrix until 2026-09-23 -- the names of
 * the X and Z builders were swapped) and what RenderSoundOptions
 * builds inline for its first knob.  MatrixMultiply4x4 0x4132d0 returns its
 * SECOND argument times its first, and the knob call passes (T, R): R*T.
 *
 * Knob angle = 3pi/4 - v * 3pi/200 (0x45d548, 0x45d54c) for a 0..100 value:
 * a 270-degree sweep.  cos/sin in double; the lost x87 bits move a knob by
 * far less than a pixel. */
#define g_widgetModel  ((const Vec3 *)0x004e04e8)
#define g_texOn        (*(IDirect3DTexture2 **)0x004e06c0)   /* knopf_ein.tga */
#define g_texOff       (*(IDirect3DTexture2 **)0x004e07a0)   /* knopf_aus.tga */
#define g_texKnobBase  (*(IDirect3DTexture2 **)0x004e06a0)   /* scale.tga */
#define g_texKnob      (*(IDirect3DTexture2 **)0x004e0558)   /* drehknopf.tga */

struct Affine { float c, s, tx, ty; };   /* RotZ(c,s) * T(tx,ty,0) */

static Affine place(float tx, float ty)  { Affine a = { 1.0f, 0.0f, tx, ty }; return a; }
static Affine knob(float tx, float ty, unsigned value)
{
    const double ang = (double)(2.3561945f - (float)value * 0.0471238904f);
    Affine a = { (float)cos(ang), (float)sin(ang), tx, ty };
    return a;
}

/* The three-position knobs on the video page (0/1/2) use fixed angles
 * +3pi/4, none, -3pi/4 -- the ends and middle of the same 270-degree sweep.
 * Any other value leaves the widget's plain placement matrix in force. */
static Affine knob3(float tx, float ty, unsigned char value)
{
    if (value == 0) { Affine a = { (float)cos(2.35619449615478515625),
                                   (float)sin(2.35619449615478515625), tx, ty }; return a; }
    if (value == 2) { Affine a = { (float)cos(-2.35619449615478515625),
                                   (float)sin(-2.35619449615478515625), tx, ty }; return a; }
    return place(tx, ty);
}

static void draw_widget(Direct3D *d3d, const Affine &m, IDirect3DTexture2 *tex,
                        DWORD colour = 0xffffffff)
{
    /* Row vector times [[c,-s,0,0],[s,c,0,0],[0,0,1,0],[tx,ty,0,1]]: w stays
     * exactly 1, so the original's divide never happens. */
    static const float uv[4][2] = { {1, 0}, {1, 1}, {0, 0}, {0, 1} };
    D3DTLVERTEX q[4];
    for (int i = 0; i < 4; i++) {
        const Vec3 &p = g_widgetModel[i];
        q[i] = tlv(p.x * m.c + p.y * m.s + m.tx,
                   -p.x * m.s + p.y * m.c + m.ty, uv[i][0], uv[i][1]);
        q[i].sz = p.z;
        q[i].color = colour;
    }
    d3d->pDevice->SetTexture(0, tex);
    d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, MENU_FVF, q, 4, 0);
}

/* A left-aligned option label in the 12x14 cell, at x = xv virtual. */
static void draw_label_c(Direct3D *d3d, TextRenderer *text, float xv,
                         float yK, const char *str, DWORD top, DWORD bot)
{
    const DWORD w = mode_width(d3d);
    text->drawLeft((float)(DWORD)(w * (DWORD)xv) * K640, (float)w * yK,
                   (float)(w * 12) * K640, (float)(w * 14) * K640, 0.75f,
                   str, d3d, 0, top, bot);
}

static void draw_label(Direct3D *d3d, void *theme, TextRenderer *text,
                       float xv, float yK, const char *str, unsigned colOff)
{
    const DWORD *col = (const DWORD *)((BYTE *)theme + colOff);
    draw_label_c(d3d, text, xv, yK, str, col[0], col[1]);
}

/* ── RenderSoundOptions 0x00430600 ───────────────────────────────────────────
 * Menu node 0xc.  Panel quad 0x4e0580 with texture *0x4e04e0, then four
 * label rows at x 262 (theme pairs +0x6f964, 96c, 974, 97c) each with its
 * widget column at x 368, 0.0125*w below the label:
 *   3D Sound   toggle  *0x4e06c0 on / *0x4e07a0 off     Config sound3D
 *   Sound Vol. base *0x4e06a0 + knob *0x4e0558          Config waveVolume
 *   CD Music   toggle                                   Config musicOn
 *   CD Vol.    base + knob                              Config cdVolume */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderSoundOptions(Game *g, void *theme, Direct3D *d3d,
                        TextRenderer *text, DWORD ms)
{
    draw_panel(d3d, theme, g_saveTexture, g_panelQuad);

    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float tx = (float)(w * 368) * K640;

    draw_label(d3d, theme, text, 262.0f, 0.28125f, "3D Sound", 0x6f964);
    draw_widget(d3d, place(tx, fw * 0.29374999f),
                g->sound3D() != 0 ? g_texOn : g_texOff);

    draw_label(d3d, theme, text, 262.0f, 0.33125001f, "Sound Vol.", 0x6f96c);
    draw_widget(d3d, place(tx, fw * 0.34375f), g_texKnobBase);
    draw_widget(d3d, knob(tx, fw * 0.34375f, g->waveVolume()), g_texKnob);

    draw_label(d3d, theme, text, 262.0f, 0.38124999f, "CD Music", 0x6f974);
    draw_widget(d3d, place(tx, fw * 0.39375001f),
                g->musicOn() != 0 ? g_texOn : g_texOff);

    draw_label(d3d, theme, text, 262.0f, 0.43125001f, "CD Vol.", 0x6f97c);
    draw_widget(d3d, place(tx, fw * 0.44374999f), g_texKnobBase);
    draw_widget(d3d, knob(tx, fw * 0.44374999f, g->cdVolume()), g_texKnob);

    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

/* ── RenderVideoOptions 0x0042e9f0 ───────────────────────────────────────────
 * Menu node 0xb.  Panel quad 0x4e0600 with texture *0x4e0780; labels at x
 * 262 (theme pairs +0x6f944, 94c, 954, 95c), widgets at x 368:
 *   Reflection  toggle on/off                          Config video byte 1
 *   Shadows     base + three-position knob             byte 0
 *   Highlights  base + knob                            byte 2
 *   Particles   base + knob                            byte 3
 *
 * Shadows depends on the hardware: it is available only when the device's
 * z-buffer format has stencil bits (d3d+0x24 -- dwStencilBitDepth of the
 * DDPIXELFORMAT stored at +0x14) AND the mode is deeper than 16 bpp.  When it
 * is not, the label is drawn in a fixed translucent grey (0x80555555 top,
 * 0x80aaaaaa bottom) instead of its theme colours, the base in 0x80808080,
 * and the knob not at all.  Both tests are re-made at each use, as in the
 * original; neither can change within a frame. */
#define g_videoTexture (*(IDirect3DTexture2 **)0x004e0780)   /* menu_3.tga */

static bool shadows_available(Direct3D *d3d)
{
    return d3d->zbufFmt[4] != 0 && d3d->pSelectedMode->dwBitDepth > 16;
}

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderVideoOptions(Game *g, void *theme, Direct3D *d3d,
                        TextRenderer *text, DWORD ms)
{
    draw_panel(d3d, theme, g_videoTexture, g_listQuad);

    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float tx = (float)(w * 368) * K640;

    draw_label(d3d, theme, text, 262.0f, 0.28125f, "Reflection", 0x6f944);
    draw_widget(d3d, place(tx, fw * 0.29374999f),
                g->videoReflection() != 0 ? g_texOn : g_texOff);

    if (shadows_available(d3d))
        draw_label(d3d, theme, text, 262.0f, 0.33125001f, "Shadows", 0x6f94c);
    else
        draw_label_c(d3d, text, 262.0f, 0.33125001f, "Shadows",
                     0x80555555, 0x80aaaaaa);
    draw_widget(d3d, place(tx, fw * 0.34375f), g_texKnobBase,
                shadows_available(d3d) ? 0xffffffff : 0x80808080);
    if (shadows_available(d3d))
        draw_widget(d3d, knob3(tx, fw * 0.34375f, g->videoShadows()), g_texKnob);

    draw_label(d3d, theme, text, 262.0f, 0.38124999f, "Highlights", 0x6f954);
    draw_widget(d3d, place(tx, fw * 0.39375001f), g_texKnobBase);
    draw_widget(d3d, knob3(tx, fw * 0.39375001f, g->videoHighlights()), g_texKnob);

    draw_label(d3d, theme, text, 262.0f, 0.43125001f, "Particles", 0x6f95c);
    draw_widget(d3d, place(tx, fw * 0.44374999f), g_texKnobBase);
    draw_widget(d3d, knob3(tx, fw * 0.44374999f, g->videoParticles()), g_texKnob);

    Menu_DrawCursorMarkers(g, d3d, ms, 0.0f);
}

/* ── RenderControlsRemap 0x00431a60 ─────────────────────────────────────────
 * Menu node 0xa.  Unlike the other pages it opens with the stack-built
 * backdrop (DrawGameOverScore's: the full screen sampling the middle of the
 * theme's backdrop texture, UVs 0.4..0.6) and a header strip from the top
 * half of *0x4e0780 at x 0.6w..0.4w, y 0.065625w..0.165625w.  Then:
 *
 *   13 action labels  left at x = 0.09375w (60), y = w * (0.171875 + i/32)
 *                     -- 110 + 20i -- theme pair +0x6f934
 *   following camera  label at x 180, y 0.578125w, pair +0x6f93c; toggle at
 *                     x 450, 0.590625w, on/off by cameraTurnsWithPlayer
 *   joystick deathzone label at x 180, y 0.609375w; base + knob at x 450,
 *                     0.621875w, the knob from the ushort deadzone
 *   13 bindings       right-aligned at x = 0.90625w (580) on the label rows,
 *                     label colours: ProgableControl's mode-1 key names for
 *                     the action, or "???" while that row is being rebound
 *                     (Game rebindActive and rebindCode == the row's node)
 *
 * and its own marker pair, 0x437740.  The rebind nodes are NOT in row order
 * (turn left is 0x17, turn right 0x16; overview 0x1c, bomb 0x1a, suicide
 * 0x1b) -- they are the menu tree's children, taken as the original
 * compares them. */
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

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderControlsRemap(Game *g, void *theme, Direct3D *d3d,
                         TextRenderer *text, DWORD ms)
{
    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float fh = (float)d3d->pSelectedMode->dwHeight;

    D3DTLVERTEX back[4] = {
        tlv(fw, 0.0f, 0.6f, 0.4f), tlv(fw, fh, 0.6f, 0.6f),
        tlv(0.0f, 0.0f, 0.4f, 0.4f), tlv(0.0f, fh, 0.4f, 0.6f),
    };
    set_blend(d3d);
    SceneTexture *tex = *(SceneTexture **)((BYTE *)theme + THEME_BACKDROP_TEX);
    d3d->pDevice->SetTexture(0, tex ? tex->pTexture2 : NULL);
    d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, MENU_FVF, back, 4, 0);

    const float hx0 = fw * 0.60000002f, hx1 = fw * 0.40000001f;
    const float hy0 = fw * 0.065624997f, hy1 = fw * 0.16562501f;
    D3DTLVERTEX head[4] = {
        tlv(hx0, hy0, 1.0f, 0.0f), tlv(hx0, hy1, 1.0f, 0.5f),
        tlv(hx1, hy0, 0.0f, 0.0f), tlv(hx1, hy1, 0.0f, 0.5f),
    };
    d3d->pDevice->SetTexture(0, g_videoTexture);
    d3d->pDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, MENU_FVF, head, 4, 0);

    const DWORD *col = (const DWORD *)((BYTE *)theme + 0x6f934);
    float rowY[13];
    for (int i = 0; i < 13; i++) {
        rowY[i] = fw * (0.171875f + 0.03125f * (float)i);
        const DWORD wr = mode_width(d3d);
        text->drawLeft(fw * 0.09375f, rowY[i],
                       (float)(wr * 12) * K640, (float)(wr * 14) * K640, 0.75f,
                       k_controls[i].label, d3d, 0, col[0], col[1]);
    }

    const float tx = (float)(w * 450) * K640;
    draw_label(d3d, theme, text, 180.0f, 0.578125f, "following camera", 0x6f93c);
    draw_widget(d3d, place(tx, fw * 0.59062499f),
                g->cameraTurnsWithPlayer() != 0 ? g_texOn : g_texOff);

    draw_label(d3d, theme, text, 180.0f, 0.609375f, "joystick deathzone", 0x6f93c);
    draw_widget(d3d, place(tx, fw * 0.62187499f), g_texKnobBase);
    draw_widget(d3d, knob(tx, fw * 0.62187499f, g->joyDeadzone()), g_texKnob);

    char buf[0x100];
    for (int i = 0; i < 13; i++) {
        ProgCtrl_GetBindingStr(GG_PROGCTRL, 1, k_controls[i].action, buf, sizeof(buf));
        const bool asking = g->rebindActive() != 0 && g->rebindCode() == k_controls[i].node;
        const DWORD wr = mode_width(d3d);
        text->drawRight(fw * 0.90625f, rowY[i],
                        (float)(wr * 12) * K640, (float)(wr * 14) * K640, 0.75f,
                        asking ? "???" : buf, d3d, 0, col[0], col[1]);
    }

    Menu_DrawControlsCursorMarkers(g, d3d, ms);
}

/* ── BuildMenuGeometry 0x0042d960 ──────────────────────────────────────────────
 * Called once when the device is set up (1 E8 site, 0x4260A0), cdecl
 * (Direct3D*, theme path prefix).  Fills every static quad the screens above
 * draw and loads the nine menu textures.  All in 640-space x w, with fw the
 * UNSIGNED width; every vertex z 0, rhw 10, white:
 *
 *   0x4e0580  panel quad   x 0.6..0.4  y 0.175..0.275  v 0..0.5 (top half)
 *   0x4e0600  list quad    the same rectangle          v 0.5..1 (bottom half)
 *   0x4e06c8  backdrop     x 0.7..0.3  y 0.175..0.575  v 0..1
 *   0x4e04e8  widget model the square (+-w/64, +-w/64, 0) -- 10 virtual
 *
 * Strip order in each quad: (x0,y0) (x0,y1) (x1,y0) (x1,y1), u 1 1 0 0.
 * Ghidra's decompile puts 0.7w into the list quad's last vertex -- wrong: the
 * listing writes that vertex from the scratch slot before the 0.7 is stored.
 *
 * Textures: "<prefix>\textures\<file>" imported into each object with
 * alpha flag 1, bpp 0, stage 0.  The path buffer is 260 bytes and unbounded,
 * as in the original (sprintf, no length). */
struct MenuTextureLoad { SceneTexture *obj; const char *file; };
static const MenuTextureLoad k_menuTextures[9] = {
    { (SceneTexture *)0x004e0520, "menu_1.tga" },
    { (SceneTexture *)0x004e0748, "menu_2.tga" },
    { (SceneTexture *)0x004e0768, "menu_3.tga" },
    { (SceneTexture *)0x004e04c8, "menu_4.tga" },
    { (SceneTexture *)0x004e0560, "selector.tga" },
    { (SceneTexture *)0x004e06a8, "knopf_ein.tga" },
    { (SceneTexture *)0x004e0788, "knopf_aus.tga" },
    { (SceneTexture *)0x004e0540, "drehknopf.tga" },
    { (SceneTexture *)0x004e0688, "scale.tga" },
};

static void fill_quad(D3DTLVERTEX *q, float x0, float x1, float y0, float y1,
                      float v0, float v1)
{
    q[0] = tlv(x0, y0, 1.0f, v0);
    q[1] = tlv(x0, y1, 1.0f, v1);
    q[2] = tlv(x1, y0, 0.0f, v0);
    q[3] = tlv(x1, y1, 0.0f, v1);
}

extern "C" __declspec(dllexport) void __cdecl
Menu_BuildMenuGeometry(Direct3D *d3d, const char *prefix)
{
    const float fw = (float)d3d->pSelectedMode->dwWidth;

    fill_quad((D3DTLVERTEX *)g_panelQuad, fw * 0.60000002f, fw * 0.40000001f,
              fw * 0.17499999f, fw * 0.27500001f, 0.0f, 0.5f);
    fill_quad((D3DTLVERTEX *)g_listQuad, fw * 0.60000002f, fw * 0.40000001f,
              fw * 0.17499999f, fw * 0.27500001f, 0.5f, 1.0f);
    fill_quad((D3DTLVERTEX *)g_backdropQuad, fw * 0.69999999f, fw * 0.30000001f,
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
        Texture_ImportSceneTextures(t.obj, d3d->pDD4, d3d->pDevice, path, 1, 0, 0);
    }
}

enum CreditAlign { CR_CENTRE, CR_RIGHT, CR_LEFT };

struct CreditRow {
    CreditAlign align;
    float       offset;   /* virtual pixels below the scroll origin */
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
    { CR_LEFT,   864.0f, "" },   /* 0x46c290: .data, empty, never written */
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

static float g_creditsScroll;     /* 0x004e0680 */
static DWORD g_creditsStartMs;    /* 0x004e0518 */

extern "C" __declspec(dllexport) void __cdecl
Menu_RenderCreditsScroll(Game *game, Direct3D *d3d, TextRenderer *text,
                         DWORD nowMs)
{
    const float scale = (float)d3d->pSelectedMode->dwWidth * (1.0f / 640.0f);

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
