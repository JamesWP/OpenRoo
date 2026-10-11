/* The keys: a key is identified by its SDL scancode, the physical key
 * whatever the layout.  Game code never includes SDL, so the scancodes it
 * uses are named here, with SDL's own values (checked against SDL in
 * inputdev/sdlinput.cpp).  They are what the key bindings in openroo.ini are
 * made from, by name. */
#pragma once

namespace input {

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

}  // namespace input
