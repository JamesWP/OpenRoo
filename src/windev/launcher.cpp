/* The launcher window (a frameless rectangle painted from our own bitmaps,
 * with bitmap buttons: play, setup, quit) and the display device panel it
 * opens.  What they list and change comes from the game's LauncherModel
 * (windev.h).  Drawn with SDL's software renderer: no widgets, no resources.
 *
 * Neither test gate reaches this code: --skip-launcher and --headless both
 * skip the launcher (app/launcher.cpp).  It is checked by hand.
 *
 * Why not SDL_CreatePopupWindow: popups are for menus and tooltips.  They need
 * a visible parent window (ours is hidden until the game starts), sit at an
 * offset from it, and are dismissed when the pointer or focus leaves them,
 * which a start-up dialog must not be.  This is a window of its own. */

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "windev.h"
#include "launcher_layout.h"
#include "buildinfo.h"

using namespace windev;

namespace {

enum Button { PLAY, SETUP, QUIT, BUTTONS };

const char LINK_URL[] = "https://github.com/jameswp/OpenRoo";

struct Art {
    SDL_Texture *bg = NULL;
    SDL_Texture *off[BUTTONS] = {}, *foc[BUTTONS] = {};
};

SDL_Texture *texture(SDL_Renderer *r, const unsigned char *bmp, size_t size)
{
    SDL_Surface *s = SDL_LoadBMP_IO(SDL_IOFromConstMem(bmp, size), true);
    if (!s) return NULL;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    return t;
}

bool in_rect(float x, float y, const SDL_Rect &r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

const SDL_Rect BUTTON_RECT[BUTTONS] = {
    { LAUNCHER_PLAY_RECT }, { LAUNCHER_SETUP_RECT }, { LAUNCHER_QUIT_RECT } };
const SDL_Rect MIN_RECT   = { LAUNCHER_MIN_RECT };
const SDL_Rect CLOSE_RECT = { LAUNCHER_CLOSE_RECT };

/* The title bar drags the window; its minimise and close boxes do not. */
SDL_HitTestResult SDLCALL hit_test(SDL_Window *, const SDL_Point *p, void *)
{
    if (p->y < LAUNCHER_TITLE_H && !in_rect((float)p->x, (float)p->y, MIN_RECT)
        && !in_rect((float)p->x, (float)p->y, CLOSE_RECT))
        return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

/* The corner text: a 3x5 pixel font, each glyph 5 rows of 3 bits (high bit
 * leftmost).  Lower case draws as upper; anything unknown as a blank. */
const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-'";
const unsigned char FONT[][5] = {
    {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},
    {3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
    {5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},
    {3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
    {5,5,2,2,2},{7,1,2,4,7},
    {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},{5,5,7,1,1},{7,4,6,1,6},
    {3,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,6},
    {0,0,0,0,2},{0,0,7,0,0},{2,2,0,0,0},
};
const int FONT_SCALE = 2;  // screen pixels per font pixel

void draw_text_px(SDL_Renderer *r, int x, int y, const char *text)
{
    for (; *text; ++text, x += 4 * FONT_SCALE) {
        const char *at = strchr(FONT_CHARS, toupper((unsigned char)*text));
        if (!at) continue;
        const unsigned char *g = FONT[at - FONT_CHARS];
        for (int row = 0; row < 5; ++row)
            for (int col = 0; col < 3; ++col)
                if (g[row] & (4 >> col)) {
                    SDL_FRect px = { (float)(x + col * FONT_SCALE),
                                     (float)(y + row * FONT_SCALE),
                                     (float)FONT_SCALE, (float)FONT_SCALE };
                    SDL_RenderFillRect(r, &px);
                }
    }
}

/* The corner text's extent, shadow included; clicking it opens the project. */
SDL_Rect g_link;

/* "<name> <version> <git sha>" at the bottom left, with a one-pixel shadow. */
void draw_build_text(SDL_Renderer *r)
{
    char text[160];
    snprintf(text, sizeof(text), "%s %s %s", OPENROO_NAME, OPENROO_VERSION,
             BUILD_GIT_SHA);
    int x = 11, y = LAUNCHER_H - 11 - 5 * FONT_SCALE;
    g_link = { x, y, (int)strlen(text) * 4 * FONT_SCALE, 6 * FONT_SCALE };
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    draw_text_px(r, x + FONT_SCALE, y + FONT_SCALE, text);
    SDL_SetRenderDrawColor(r, 255, 236, 200, 255);
    draw_text_px(r, x, y, text);
}

/* ── The device panel ────────────────────────────────────────────────── */

const SDL_Rect PANEL     = { 90, 60, 420, 280 };
const SDL_Rect ADAPTERS  = { 110, 104, 380, 56 };    // 4 rows of 14
const SDL_Rect MODES     = { 110, 190, 380, 84 };    // 6 rows of 14
const SDL_Rect OK_RECT   = { 300, 304, 90, 24 };
const SDL_Rect CANCEL_RECT = { 400, 304, 90, 24 };
const int ROW = 14;

struct List {
    std::vector<std::string> items;
    int selected = 0, top = 0;
    int visible = 0;

    void select(int i)
    {
        if (items.empty()) return;
        selected = i < 0 ? 0 : i >= (int)items.size() ? (int)items.size() - 1 : i;
        if (selected < top) top = selected;
        if (selected >= top + visible) top = selected - visible + 1;
    }
};

struct DevicePanel {
    LauncherModel *model;
    List adapters, modes;
    bool modeFocus = true;  // keys act on the modes list, else the adapters

    bool init(LauncherModel *m)
    {
        model = m;
        adapters.visible = ADAPTERS.h / ROW;
        modes.visible = MODES.h / ROW;
        if (!m->listAdapters(adapters.items)) return false;
        adapters.select(m->configuredAdapter() < 0 ? 0 : m->configuredAdapter());
        if (!fillModes()) return false;
        modes.select(m->configuredMode());
        return true;
    }

    bool fillModes()
    {
        modes.items.clear();
        modes.top = 0;
        return model->listModes(adapters.selected, modes.items);
    }

    void draw(SDL_Renderer *r) const
    {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 10, 10, 10, 235);
        SDL_FRect p = { (float)PANEL.x, (float)PANEL.y, (float)PANEL.w, (float)PANEL.h };
        SDL_RenderFillRect(r, &p);
        SDL_SetRenderDrawColor(r, 255, 236, 200, 255);
        SDL_RenderRect(r, &p);
        SDL_RenderDebugText(r, 110, 76, "Setup");
        SDL_RenderDebugText(r, 110, 92, "Device:");
        drawList(r, adapters, ADAPTERS, !modeFocus);
        SDL_RenderDebugText(r, 110, 178, "Mode:");
        drawList(r, modes, MODES, modeFocus);
        SDL_RenderDebugText(r, 110, 282, "Shadows need a 24- or 32-bit colour mode.");
        drawButton(r, OK_RECT, "OK");
        drawButton(r, CANCEL_RECT, "Cancel");
    }

    static void drawList(SDL_Renderer *r, const List &l, const SDL_Rect &box, bool focus)
    {
        SDL_FRect f = { (float)box.x, (float)box.y, (float)box.w, (float)box.h };
        SDL_SetRenderDrawColor(r, 255, 236, 200, focus ? 255 : 110);
        SDL_RenderRect(r, &f);
        for (int i = 0; i < l.visible && l.top + i < (int)l.items.size(); i++) {
            int idx = l.top + i;
            if (idx == l.selected) {
                SDL_FRect row = { (float)box.x + 1, (float)(box.y + i * ROW),
                                  (float)box.w - 2, (float)ROW };
                SDL_SetRenderDrawColor(r, 90, 70, 30, 255);
                SDL_RenderFillRect(r, &row);
            }
            SDL_SetRenderDrawColor(r, 255, 236, 200, 255);
            std::string s = l.items[idx].substr(0, (box.w - 8) / 8);
            SDL_RenderDebugText(r, (float)box.x + 4, (float)(box.y + i * ROW + 3), s.c_str());
        }
    }

    static void drawButton(SDL_Renderer *r, const SDL_Rect &b, const char *label)
    {
        SDL_FRect f = { (float)b.x, (float)b.y, (float)b.w, (float)b.h };
        SDL_SetRenderDrawColor(r, 255, 236, 200, 255);
        SDL_RenderRect(r, &f);
        SDL_RenderDebugText(r, (float)b.x + (b.w - 8 * (int)strlen(label)) / 2.0f,
                            (float)b.y + 8, label);
    }

    /* A click in a list selects the row under it; true if it was in one. */
    static bool pick(List &l, const SDL_Rect &box, float x, float y)
    {
        if (!in_rect(x, y, box)) return false;
        int row = l.top + ((int)y - box.y) / ROW;
        if (row < (int)l.items.size()) l.select(row);
        return true;
    }

    /* Returns 1 to confirm, 0 to cancel, -1 to stay. */
    int event(const SDL_Event &e)
    {
        switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (in_rect(e.button.x, e.button.y, OK_RECT)) return 1;
            if (in_rect(e.button.x, e.button.y, CANCEL_RECT)) return 0;
            if (pick(adapters, ADAPTERS, e.button.x, e.button.y)) {
                modeFocus = false;
                if (!fillModes()) return 0;
                modes.select(0);
            } else if (pick(modes, MODES, e.button.x, e.button.y)) {
                modeFocus = true;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            List &l = in_rect(mx, my, ADAPTERS) ? adapters : modes;
            int top = l.top - (int)e.wheel.y;
            int max = (int)l.items.size() - l.visible;
            l.top = top < 0 ? 0 : top > max ? (max < 0 ? 0 : max) : top;
            break;
        }
        case SDL_EVENT_KEY_DOWN:
            switch (e.key.key) {
            case SDLK_ESCAPE: return 0;
            case SDLK_RETURN: case SDLK_KP_ENTER: return 1;
            case SDLK_TAB: modeFocus = !modeFocus; break;
            case SDLK_UP: case SDLK_DOWN: {
                int d = e.key.key == SDLK_UP ? -1 : 1;
                if (modeFocus) {
                    modes.select(modes.selected + d);
                } else {
                    adapters.select(adapters.selected + d);
                    if (!fillModes()) return 0;
                    modes.select(0);
                }
                break;
            }
            }
            break;
        }
        return -1;
    }
};

/* ── The window ──────────────────────────────────────────────────────── */

struct Launcher {
    LauncherModel *model;
    SDL_Window    *window = NULL;
    SDL_Renderer  *renderer = NULL;
    Art            art;
    int            focus = PLAY;
    int            pressed = -1;
    bool           showPanel = false;
    DevicePanel    panel;

    bool open()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return false;
        window = SDL_CreateWindow(OPENROO_NAME, LAUNCHER_W, LAUNCHER_H,
                                  SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
        if (!window) return false;
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_SetWindowHitTest(window, hit_test, NULL);
        SDL_Surface *icon = SDL_CreateSurfaceFrom(LAUNCHER_ICON_SIZE, LAUNCHER_ICON_SIZE,
                                                  SDL_PIXELFORMAT_RGBA32,
                                                  (void *)launcher_icon_rgba,
                                                  LAUNCHER_ICON_SIZE * 4);
        if (icon) { SDL_SetWindowIcon(window, icon); SDL_DestroySurface(icon); }

        renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
        if (!renderer) return false;
        art.bg = texture(renderer, launcher_bg_bmp, sizeof(launcher_bg_bmp));
        const unsigned char *off[BUTTONS] = { launcher_play_off_bmp, launcher_setup_off_bmp,
                                              launcher_quit_off_bmp };
        const unsigned char *foc[BUTTONS] = { launcher_play_foc_bmp, launcher_setup_foc_bmp,
                                              launcher_quit_foc_bmp };
        for (int i = 0; i < BUTTONS; i++) {
            art.off[i] = texture(renderer, off[i], sizeof(launcher_play_off_bmp));
            art.foc[i] = texture(renderer, foc[i], sizeof(launcher_play_foc_bmp));
        }
        return art.bg != NULL;
    }

    ~Launcher()
    {
        // Textures go with the renderer.
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }

    void draw()
    {
        SDL_RenderTexture(renderer, art.bg, NULL, NULL);
        for (int i = 0; i < BUTTONS; i++) {
            SDL_Texture *t = (focus == i || pressed == i) ? art.foc[i] : art.off[i];
            if (!t) continue;
            const SDL_Rect &b = BUTTON_RECT[i];
            SDL_FRect d = { (float)b.x, (float)b.y, (float)b.w, (float)b.h };
            SDL_RenderTexture(renderer, t, NULL, &d);
        }
        draw_build_text(renderer);
        if (showPanel) panel.draw(renderer);
        SDL_RenderPresent(renderer);
    }

    void moveFocus(int to)
    {
        to = (to + BUTTONS) % BUTTONS;
        if (to == focus) return;
        focus = to;
        model->playSound(LauncherModel::Sound::Switch);
    }

    /* Returns 1 to play, 0 to quit, -1 to carry on. */
    int activate(int button)
    {
        if (button == PLAY) {
            model->playSound(LauncherModel::Sound::Impact);
            return 1;
        }
        if (button == QUIT) {
            model->playSound(LauncherModel::Sound::Ugh);
            return 0;
        }
        model->playSound(LauncherModel::Sound::Impact);
        showPanel = panel.init(model);
        return -1;
    }

    int event(const SDL_Event &e)
    {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            return 0;

        if (showPanel) {
            int r = panel.event(e);
            if (r == 1) model->choose(panel.adapters.selected, panel.modes.selected);
            if (r >= 0) showPanel = false;
            return -1;
        }

        int over = -1;
        if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN
            || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            float x = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
            float y = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
            for (int i = 0; i < BUTTONS; i++)
                if (in_rect(x, y, BUTTON_RECT[i])) over = i;
            if (e.type == SDL_EVENT_MOUSE_MOTION && over >= 0)
                moveFocus(over);
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                pressed = over;
                if (over >= 0) moveFocus(over);
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                int was = pressed;
                pressed = -1;
                if (was >= 0 && was == over)
                    return activate(was);
                if (in_rect(x, y, g_link)) {
                    SDL_OpenURL(LINK_URL);
                } else if (in_rect(x, y, MIN_RECT)) {
                    SDL_MinimizeWindow(window);
                } else if (in_rect(x, y, CLOSE_RECT)) {
                    model->playSound(LauncherModel::Sound::Ugh);
                    return 0;
                }
            }
        } else if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.key) {
            case SDLK_ESCAPE: return 0;
            case SDLK_TAB: moveFocus(focus + ((e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1)); break;
            case SDLK_DOWN: moveFocus(focus + 1); break;
            case SDLK_UP:   moveFocus(focus - 1); break;
            case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                return activate(focus);
            }
        }
        return -1;
    }
};

}  // namespace

namespace windev {

bool showLauncher(void *, LauncherModel &model)
{
    Launcher l;
    l.model = &model;
    if (!l.open())
        return true;  // no launcher to show: carry on with the saved settings

    int result = -1;
    SDL_Event e;
    while (result < 0) {
        l.draw();
        if (!SDL_WaitEvent(&e))
            break;
        result = l.event(e);
    }
    return result != 0;
}

}  // namespace windev
