/* The game's keys: one snapshot of every key per tick, which everything that
 * reads a key reads.  The snapshot is made from the key sources the app has
 * added (keysource.h): the keyboard, a replay, anything else.
 *
 * Keys are input::Key scancodes.  Held and just-pressed are the only two
 * questions; nothing else debounces. */
#pragma once
#include "keysource.h"

namespace input {

/* Adds a source of keys, or an observer of them.  The app does this once, at
 * start up; the group that implements one never knows who reads it.  Nothing
 * is owned: each must outlive the game.  Null is ignored. */
void addSource(KeySource *source);
void addObserver(KeyObserver *observer);

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
