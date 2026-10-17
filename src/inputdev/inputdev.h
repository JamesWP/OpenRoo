/* The platform input layer: the only code that may touch the system keyboard,
 * mouse and controller APIs (SDL's keyboard state).  The header is free of
 * platform headers; the game's input logic (src/input) is written against
 * the key interfaces (src/inputapi), of which the keyboard is one source. */
#pragma once
#include "keysource.h"

namespace inputdev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

/* The keyboard as a key source, for the app to add to input.  It holds the
 * keys down while the window has the focus, as of the events pumped so far
 * (windev::runMessageLoop). */
input::KeySource &keyboard();

/* The first gamepad as a key source: its buttons press the keys the default
 * bindings use (the table in sdlinput.cpp).  Holds nothing while no gamepad
 * is connected. */
input::KeySource &gamepad();

/* While set, the keyboard holds no key: something else (the debug UI's text
 * box) has it. */
void setKeyboardSuppressed(bool suppressed);

/* The key with this name ("Up", "Page Up", "A"; any case), or KEY_NONE. */
int keyFromName(const char *name);

/* The key's name, for menus and openroo.ini, into buf (bufsz bytes).  False
 * if it has none. */
bool keyName(int key, char *buf, unsigned bufsz);

}  // namespace inputdev
