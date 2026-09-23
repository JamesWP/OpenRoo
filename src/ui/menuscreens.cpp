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
 * The vertex arrays and textures these read by address (0x4e06c8, 0x4e0580,
 * 0x4e0538, 0x4e0578) are .bss the menu geometry builder fills (writers at
 * 0x42d98c and 0x42dc31); they stay game-owned until that builder is
 * replaced.
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
#include <math.h>

#define K640          (1.0f / 640.0f)
#define MENU_FVF      0x1c4        /* XYZRHW | DIFFUSE | SPECULAR | TEX1 */
#define g_backdropQuad ((void *)0x004e06c8)
#define g_panelQuad    ((void *)0x004e0580)
#define g_panelTexture (*(IDirect3DTexture2 **)0x004e0538)
#define g_markerTexture (*(IDirect3DTexture2 **)0x004e0578)
#define g_listQuad     ((void *)0x004e0600)
#define g_optionsTexture (*(IDirect3DTexture2 **)0x004e0760)
#define g_saveTexture  (*(IDirect3DTexture2 **)0x004e04e0)
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

extern "C" __declspec(dllexport) void __cdecl
Menu_DrawCursorMarkers(Game *g, Direct3D *d3d, DWORD ms, float rowOffset)
{
    const DWORD w  = mode_width(d3d);
    const float fw = (float)w;
    const float y0 = (float)(w * 172) * K640
                   + ((float)g->menu()->cursor() * 0.05f + rowOffset * K640) * fw;
    const float y1 = (float)(w << 5) * K640 + y0;

    const double t = (double)ms * 0.01;
    const float left  = (float)(sin(t) * 4.0 + 244.0);
    const float right = (float)(sin(t + 3.14159274101257) * 4.0 + 396.0);

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
