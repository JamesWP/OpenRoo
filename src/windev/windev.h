/* The platform windowing layer: the only code that may touch the system
 * window, message loop, message box and dialog APIs.  The header is opaque and
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
    /* Any key was released. */
    virtual void onKeyUp() {}
    /* The window was destroyed (closed). */
    virtual void onDestroyed() {}
    /* Every message, after the above; true if the handler consumed it.  The
     * platform libraries' own messages (music, movie) arrive here. */
    virtual bool onNativeMessage(unsigned msg, unsigned long wParam, long lParam)
    { (void)msg; (void)wParam; (void)lParam; return false; }
};

struct WindowConfig {
    const char *title;
    int         width, height;
    /* No visible window at all, only a handle and a message queue: for
     * headless runs. */
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
    bool create(void *instance, WindowHandler *handler, const WindowConfig &config);
    void destroy();
    void show(bool visible);

    /* The native window handle. */
    void *handle() const { return handle_; }

private:
    void *handle_;
};

/* Pumps messages, calling idle between them, until quit(); returns quit's
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
