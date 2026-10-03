/* The platform input layer: the only code that may touch the system keyboard,
 * mouse and controller APIs (SDL's keyboard state).  The header is
 * opaque and free of platform headers; the game's input logic (src/input) is
 * written against it, so a port replaces the .cpp files beside it.
 *
 * Two key id spaces cross this boundary, both fixed by the game's saved files
 * and recordings:
 *   scan codes    0..255, the layout DirectInput used (an AT scan code,
 *                 with 0x80 added for the extended keys); saved in
 *                 ProgableControl.sav and used by the action bindings
 *   virtual keys  the Windows VK_* numbers the menus poll (Enter 0x0d, Escape
 *                 0x1b, arrows 0x25..0x28, letters as ASCII) */
#pragma once

namespace inputdev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

/* The scan codes the default key bindings use. */
enum Scan {
    SCAN_TAB = 0x0f, SCAN_A = 0x1e, SCAN_Z = 0x2c, SCAN_X = 0x2d,
    SCAN_C = 0x2e, SCAN_B = 0x30, SCAN_UP = 0xc8, SCAN_PAGE_UP = 0xc9,
    SCAN_LEFT = 0xcb, SCAN_RIGHT = 0xcd, SCAN_DOWN = 0xd0,
    SCAN_PAGE_DOWN = 0xd1, SCAN_DELETE = 0xd3
};

/* Whether a virtual key is down right now, in the old Windows format: the
 * high bit of the result is set while it is held.  Reflects the events
 * pumped so far (windev::runMessageLoop), and only while the window has the
 * focus. */
short asyncKeyState(int virtualKey);

struct DevicesState;

/* The keyboard and mouse bound to the game window, plus the controller. */
class Devices {
public:
    Devices();
    ~Devices();
    Devices(const Devices &) = delete;
    Devices &operator=(const Devices &) = delete;

    /* Brings up the input system and the keyboard and mouse for the native
     * window.  False if any fails, with nothing left open. */
    bool create(void *window);
    void destroy();

    /* Takes the devices when the window is active, and gives them back.
     * acquire is false if the keyboard cannot be taken. */
    bool acquire();
    void unacquire();

    /* The state of all 256 scan codes: bit 0x80 of each byte is set while
     * the key is held.  False if there is no keyboard or it cannot be read. */
    bool readKeyboard(unsigned char keys[256]);

    /* The key's name, for menus, into buf (bufsz bytes).  False if the system
     * has none. */
    bool keyName(int scanCode, char *buf, unsigned bufsz);

    /* The controller: no controller is ever opened, so both succeed without
     * doing anything.  Axes are numbered by their offset (0, 4, 8). */
    bool setControllerRange(int axis, int low, int high);
    bool setControllerDeadzone(int axis, int zone);

private:
    DevicesState *state_;
};

}  // namespace inputdev
