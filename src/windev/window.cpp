#include <SDL3/SDL.h>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windev.h"
#include "launcher_layout.h"

namespace windev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define WD_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

static WindowHandler *g_handler;
static SDL_Window *g_sdlWindow;
static void *g_glContext;
static bool g_closed;
static std::atomic<bool> g_quit(false);
static std::atomic<int>  g_exitCode(0);

bool messageBox(void *parent, const char *text, const char *title,
                Buttons buttons, Icon icon)
{
    SDL_Window *owner = parent ? g_sdlWindow : NULL;
    SDL_MessageBoxFlags flags = icon == Icon::Error ? SDL_MESSAGEBOX_ERROR
                                                    : SDL_MESSAGEBOX_INFORMATION;
    if (buttons == Buttons::Ok) {
        SDL_ShowSimpleMessageBox(flags, title, text, owner);
        return true;
    }

    SDL_MessageBoxButtonData btn[2] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
    };
    SDL_MessageBoxData box = { flags, owner, title, text, 2, btn, NULL };
    int pressed = 0;
    return SDL_ShowMessageBox(&box, &pressed) && pressed == 1;
}

/* The window is closed once: by its close button, a quit signal, or
 * requestClose. */
static void close_window()
{
    if (g_closed)
        return;
    g_closed = true;
    if (g_glContext) {
        SDL_GL_DestroyContext((SDL_GLContext)g_glContext);
        g_glContext = NULL;
    }
    if (g_sdlWindow) {
        SDL_DestroyWindow(g_sdlWindow);
        g_sdlWindow = NULL;
    }
    if (g_handler)
        g_handler->onDestroyed();
}

static void dispatch(const SDL_Event &e)
{
    switch (e.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (g_handler) g_handler->onActivate(true);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (g_handler) g_handler->onActivate(false);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (g_handler && !e.key.repeat) g_handler->onKeyDown();
        break;
    case SDL_EVENT_KEY_UP:
        if (g_handler) g_handler->onKeyUp();
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_QUIT:
        close_window();
        break;
    case SDL_EVENT_USER:
        if (g_handler)
            g_handler->onNativeMessage((unsigned)e.user.code, 0, 0);
        break;
    }
}

Window::Window() : handle_(NULL), sdl_(NULL)
{
}

Window::~Window()
{
}

bool Window::create(WindowHandler *handler,
                    const WindowConfig &config)
{
    g_handler = handler;
    g_closed  = false;
    // Events are enough for a run with no display: the video system is what
    // needs a display server.
    if (!SDL_Init(config.messageOnly ? SDL_INIT_EVENTS : SDL_INIT_VIDEO)) {
        WD_LOG("windev: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    // Closing the window is the game's to act on (onDestroyed), not SDL's.
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");

    if (config.messageOnly) {
        WD_LOG("windev: no window (headless)\n");
        return true;
    }

    SDL_Window *win = SDL_CreateWindow(config.title, config.width, config.height,
                                       SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (!win) {
        WD_LOG("windev: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(win, 0, 0);
    SDL_Surface *icon = SDL_CreateSurfaceFrom(LAUNCHER_ICON_SIZE, LAUNCHER_ICON_SIZE,
                                              SDL_PIXELFORMAT_RGBA32,
                                              (void *)launcher_icon_rgba,
                                              LAUNCHER_ICON_SIZE * 4);
    if (icon) {
        SDL_SetWindowIcon(win, icon);
        SDL_DestroySurface(icon);
    }
    g_sdlWindow = win;
    sdl_        = win;
    handle_     = SDL_GetPointerProperty(SDL_GetWindowProperties(win),
                                         SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    return true;
}

void Window::destroy()
{
    destroyGLContext();
    if (g_sdlWindow)
        SDL_DestroyWindow(g_sdlWindow);
    g_sdlWindow = NULL;
    handle_ = sdl_ = NULL;
}

bool Window::createGLContext(const GLContextConfig &c)
{
    if (!g_sdlWindow)
        return false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, c.major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, c.minor);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, c.depthBits);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, c.stencilBits);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GLContext ctx = SDL_GL_CreateContext(g_sdlWindow);
    if (!ctx) {
        WD_LOG("windev: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    g_glContext = ctx;
    return true;
}

void Window::destroyGLContext()
{
    if (g_glContext) {
        SDL_GL_DestroyContext((SDL_GLContext)g_glContext);
        g_glContext = NULL;
    }
}

bool Window::hasGLContext() const
{
    return g_glContext != NULL;
}

void *Window::glProcAddress(const char *name) const
{
    return (void *)SDL_GL_GetProcAddress(name);
}

void Window::setSwapInterval(int interval)
{
    SDL_GL_SetSwapInterval(interval);
}

void Window::swapBuffers()
{
    if (g_sdlWindow)
        SDL_GL_SwapWindow(g_sdlWindow);
}

bool Window::enterFullscreen(unsigned display, unsigned width, unsigned height)
{
    if (!g_sdlWindow)
        return false;
    SDL_DisplayMode mode;
    if (!SDL_GetClosestFullscreenDisplayMode((SDL_DisplayID)display, (int)width, (int)height,
                                             0.0f, false, &mode)) {
        WD_LOG("windev: no %ux%u mode: %s\n", width, height, SDL_GetError());
        return false;
    }
    SDL_SetWindowFullscreenMode(g_sdlWindow, &mode);
    // A hidden window would only go full-screen once shown.
    SDL_ShowWindow(g_sdlWindow);
    if (!SDL_SetWindowFullscreen(g_sdlWindow, true)) {
        WD_LOG("windev: full-screen failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SyncWindow(g_sdlWindow);
    return true;
}

void Window::drawableSize(unsigned *width, unsigned *height) const
{
    int w = 0, h = 0;
    if (g_sdlWindow)
        SDL_GetWindowSizeInPixels(g_sdlWindow, &w, &h);
    *width  = (unsigned)w;
    *height = (unsigned)h;
}

void Window::show(bool visible)
{
    if (!g_sdlWindow)
        return;
    if (visible)
        SDL_ShowWindow(g_sdlWindow);
    else
        SDL_HideWindow(g_sdlWindow);
}

// ── Displays ──

/* FNV-1a over the name, with two seeds, as a 16-byte id. */
static void display_stable_id(const char *name, uint8_t out[16])
{
    for (int half = 0; half < 2; half++) {
        uint64_t h = half ? 0x9E3779B97F4A7C15ull : 1469598103934665603ull;
        for (const char *p = name; *p; p++)
            h = (h ^ (uint8_t)*p) * 1099511628211ull;
        memcpy(out + half * 8, &h, 8);
    }
}

bool listDisplays(std::vector<DisplayInfo> &out)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return false;
    int count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int pass = 0; pass < 2; pass++)  // the primary display first
        for (int i = 0; ids && i < count; i++) {
            if ((ids[i] == primary) != (pass == 0))
                continue;
            DisplayInfo d = {};
            const char *name = SDL_GetDisplayName(ids[i]);
            snprintf(d.name, sizeof(d.name), "%s", name ? name : "Display");
            d.id      = (unsigned)ids[i];
            d.primary = ids[i] == primary;
            display_stable_id(d.name, d.stableId);
            out.push_back(d);
        }
    SDL_free(ids);
    return !out.empty();
}

bool listDisplayModes(unsigned display, std::vector<DisplaySize> &out)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return false;
    int count = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes((SDL_DisplayID)display, &count);
    for (int i = 0; modes && i < count; i++) {
        const DisplaySize s = { (unsigned)modes[i]->w, (unsigned)modes[i]->h };
        bool seen = false;
        for (const DisplaySize &o : out)
            seen = seen || (o.width == s.width && o.height == s.height);
        if (!seen)
            out.push_back(s);
    }
    SDL_free(modes);
    return !out.empty();
}

/* Every pending event is handled before each idle() call, and a quit is
 * acted on once the queue is empty, so the frame count of a run does not
 * depend on what else happened to be queued. */
int runMessageLoop(void (*idle)())
{
    SDL_Event e;
    for (;;) {
        while (SDL_PollEvent(&e))
            dispatch(e);
        if (g_quit)
            return g_exitCode;
        idle();
    }
}

void quit(int exitCode)
{
    g_exitCode = exitCode;
    g_quit = true;
}

void requestClose()
{
    SDL_Window *win = g_sdlWindow;
    if (win && !(SDL_GetWindowFlags(win) & SDL_WINDOW_HIDDEN)) {
        WD_LOG("windev: posting a close request\n");
        SDL_Event e = {};
        e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        e.window.windowID = SDL_GetWindowID(win);
        SDL_PushEvent(&e);
    } else {
        WD_LOG("windev: no main window found, exiting\n");
        _Exit(0);
    }
}

static Uint32 SDLCALL close_timer(void *, SDL_TimerID, Uint32)
{
    requestClose();
    return 0;
}

void requestCloseAfter(unsigned milliseconds)
{
    SDL_AddTimer(milliseconds, close_timer, NULL);
}

}  // namespace windev
