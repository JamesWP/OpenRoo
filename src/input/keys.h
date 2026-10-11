/* The game's keyboard: one snapshot of every key per tick, which everything
 * that reads a key reads.  The recording and replay (testing/record.h) sit
 * behind it, so a replay supplies the snapshot in place of the keyboard.
 *
 * Keys are inputdev::Key scancodes.  Held and just-pressed are the only two
 * questions; nothing else debounces. */
#pragma once

namespace input {

/* Takes the tick's snapshot.  Once per tick, before anything reads a key. */
void update();

/* Whether the key is held. */
bool down(int key);

/* Whether the key went down since the last tick.  A key held through a screen
 * change is not pressed again; a key held when the game starts is not pressed
 * at all. */
bool pressed(int key);

/* Treats this tick's press of the key as used, for the rest of the tick:
 * the screen that took it has handed over to another that reads the same key
 * (Enter starts the level, which Enter would skip the intro of). */
void swallow(int key);

/* The lowest key held, or 0 if none: the key a rebind captures. */
int firstDown();

}  // namespace input
