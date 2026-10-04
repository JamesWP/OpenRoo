/* The platform input layer: the only code that may touch the system keyboard,
 * mouse and controller APIs (SDL's keyboard state).  The header is free of
 * platform headers; the game's input logic (src/input) is written against it.
 *
 * A key is identified by its SDL scancode: the physical key, whatever the
 * layout.  Game code never includes SDL, so the scancodes it uses are named
 * here, with SDL's own values (checked against SDL in sdlinput.cpp).  They
 * are what the key bindings in openroo.ini are made from, by name. */
#pragma once

namespace inputdev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

/* Keys are 0 .. KEY_COUNT-1 (SDL_SCANCODE_COUNT). */
enum { KEY_COUNT = 512 };

enum Key {
    KEY_NONE = 0,
    KEY_A = 4, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,                    // A..Z = 4..29
    KEY_1 = 30, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_0 = 39,
    KEY_RETURN = 40, KEY_ESCAPE = 41, KEY_BACKSPACE = 42, KEY_TAB = 43,
    KEY_SPACE = 44,
    KEY_F1 = 58, KEY_F2, KEY_F3, KEY_F4,
    KEY_PAGEUP = 75, KEY_DELETE = 76, KEY_PAGEDOWN = 78,
    KEY_RIGHT = 79, KEY_LEFT = 80, KEY_DOWN = 81, KEY_UP = 82,
    KEY_LSHIFT = 225, KEY_RSHIFT = 229
};

/* Whether a key is down right now.  Reflects the events pumped so far
 * (windev::runMessageLoop), and only while the window has the focus. */
bool keyDown(int key);

/* The key with this name ("Up", "Page Up", "A"; any case), or KEY_NONE. */
int keyFromName(const char *name);

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

    /* The state of every key: bit 0x80 of each byte is set while the key is
     * held.  False if there is no keyboard or it cannot be read. */
    bool readKeyboard(unsigned char keys[KEY_COUNT]);

    /* The key's name, for menus and openroo.ini, into buf (bufsz bytes).
     * False if it has none. */
    bool keyName(int key, char *buf, unsigned bufsz);

    /* The controller: no controller is ever opened, so both succeed without
     * doing anything.  Axes are numbered by their offset (0, 4, 8). */
    bool setControllerRange(int axis, int low, int high);
    bool setControllerDeadzone(int axis, int zone);

private:
    DevicesState *state_;
};

}  // namespace inputdev
