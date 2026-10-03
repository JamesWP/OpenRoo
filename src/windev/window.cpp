#include <SDL3/SDL.h>
#include <atomic>
#include <stdlib.h>
#include "windev.h"

namespace windev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define WD_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

static WindowHandler *g_handler;
static SDL_Window *g_sdlWindow;
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
                                       SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
    if (!win) {
        WD_LOG("windev: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(win, 0, 0);
    g_sdlWindow = win;
    sdl_        = win;
    handle_     = SDL_GetPointerProperty(SDL_GetWindowProperties(win),
                                         SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    return true;
}

void Window::destroy()
{
    if (g_sdlWindow)
        SDL_DestroyWindow(g_sdlWindow);
    g_sdlWindow = NULL;
    handle_ = sdl_ = NULL;
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
