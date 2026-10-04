/* The platform windowing layer: the only code that may touch SDL's window,
 * OpenGL context, displays, event loop and message box, and the system dialog
 * APIs of the launcher.  The header is opaque and
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
#include <stdint.h>
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

/* What the OpenGL context must provide: a core profile of at least
 * major.minor, with a depth buffer of at least depthBits and a stencil buffer
 * of at least stencilBits, and no alpha channel in the back buffer. */
struct GLContextConfig {
    int major, minor;
    int depthBits, stencilBits;
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

    /* The native window handle (an HWND), for the input and video devices. */
    void *handle() const { return handle_; }

    // ── The OpenGL context, for the rendering backend (src/gl) ──

    /* Makes an OpenGL core-profile context of at least major.minor on this
     * window and makes it current.  The window was created with the
     * capability.  False on failure; the reason is in the log. */
    bool createGLContext(const GLContextConfig &config);
    void destroyGLContext();
    /* Whether the context exists: it ends with its window, which can close
     * before the game frees what it made on it. */
    bool hasGLContext() const;

    /* The entry point of an OpenGL function, or NULL; only after
     * createGLContext. */
    void *glProcAddress(const char *name) const;

    /* 0 presents at once, 1 waits for the display's refresh. */
    void setSwapInterval(int interval);

    /* Shows the back buffer. */
    void swapBuffers();

    /* Puts the window full-screen on `display` in the video mode closest to
     * width x height (see listDisplays).  False if the display has no such
     * mode. */
    bool enterFullscreen(unsigned display, unsigned width, unsigned height);

    /* The size of the window's drawable area in pixels. */
    void drawableSize(unsigned *width, unsigned *height) const;

private:
    void *handle_;
    void *sdl_;
};

/* One display, as listDisplays lists them. */
struct DisplayInfo {
    char     name[128];
    unsigned id;          // for listDisplayModes and Window::enterFullscreen
    bool     primary;
    uint8_t  stableId[16];  // the same display has the same value from run to run
};

/* The attached displays, the primary one first.  False if there are none. */
bool listDisplays(std::vector<DisplayInfo> &out);

/* The sizes the display can be set to, one entry per width and height. */
struct DisplaySize { unsigned width, height; };
bool listDisplayModes(unsigned display, std::vector<DisplaySize> &out);

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
