/* The platform windowing layer: the only code that may touch SDL's window,
 * event loop and message box, and the system dialog APIs of the launcher.  The header is opaque and
 * free of platform headers; the game (src/app) is written against it, so a
 * port replaces the .cpp files beside it.
 *
 *   Window        the game's one window, with its events delivered to a handler
 *   messageBox    a modal notice
 *   runMessageLoop / quit / requestClose   the run's life cycle
 *   showLauncher  the start-up dialog, driven through a LauncherModel
 *
 * Native handles cross as void*. */
#pragma once
#include <string>
#include <vector>

namespace windev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

/* A modal notice; false only if an OK/Cancel box was cancelled. */
enum class Buttons { Ok, OkCancel };
enum class Icon    { None, Error };
bool messageBox(void *parent, const char *text, const char *title,
                Buttons buttons = Buttons::Ok, Icon icon = Icon::None);

/* What the window tells the game.  Every method has a do-nothing default. */
class WindowHandler {
public:
    virtual ~WindowHandler() {}
    /* The window gained or lost the focus. */
    virtual void onActivate(bool active) { (void)active; }
    /* A key went down (not an auto-repeat), and any key was released. */
    virtual void onKeyDown() {}
    virtual void onKeyUp() {}
    /* The window was destroyed (closed). */
    virtual void onDestroyed() {}
    /* Every message, after the above; true if the handler consumed it.  The
     * platform libraries' own messages (the movie ending) arrive here, as SDL
     * user events: msg is the event's code, wParam and lParam are 0. */
    virtual bool onNativeMessage(unsigned msg, unsigned long wParam, long lParam)
    { (void)msg; (void)wParam; (void)lParam; return false; }
};

struct WindowConfig {
    const char *title;
    int         width, height;
    /* No visible window at all, only a handle and a message queue: for
     * headless runs.  There is then no window at all (handle() is NULL), only
     * the event queue. */
    bool        messageOnly;
};

class Window {
public:
    Window();
    ~Window();
    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    /* Registers and creates the window; its events go to handler.  False on
     * failure. */
    bool create(WindowHandler *handler, const WindowConfig &config);
    void destroy();
    void show(bool visible);

    /* The native window handle (an HWND), for the Direct3D backend. */
    void *handle() const { return handle_; }

    /* The SDL_Window*, for a backend that creates its own surface on it. */
    void *sdlWindow() const { return sdl_; }

private:
    void *handle_;
    void *sdl_;
};

/* Pumps events, calling idle between them, until quit(); returns quit's
 * code. */
int  runMessageLoop(void (*idle)());
void quit(int exitCode);

/* Asks the main window to close so the game's own shutdown runs, or ends the
 * process if there is no window; requestCloseAfter does so after a delay, from
 * another thread. */
void requestClose();
void requestCloseAfter(unsigned milliseconds);

/* The data the launcher shows and changes: the display adapters and the modes
 * of one adapter.  The game implements it. */
class LauncherModel {
public:
    virtual ~LauncherModel() {}
    /* The adapter names; false if they cannot be listed. */
    virtual bool listAdapters(std::vector<std::string> &names) = 0;
    /* The configured adapter's index in that list, or -1. */
    virtual int  configuredAdapter() = 0;
    /* The mode names of one adapter; false if they cannot be listed. */
    virtual bool listModes(int adapter, std::vector<std::string> &names) = 0;
    virtual int  configuredMode() = 0;
    /* The setup dialog was confirmed. */
    virtual void choose(int adapter, int mode) = 0;
    /* A sound for the buttons. */
    enum class Sound { Switch, Impact, Ugh };
    virtual void playSound(Sound sound) = 0;
};

/* The start-up dialog.  True to play, false to quit. */
bool showLauncher(void *parent, LauncherModel &model);

}  // namespace windev
