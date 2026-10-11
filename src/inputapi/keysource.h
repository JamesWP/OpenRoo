/* Where the game's keys come from.
 *
 * A group that supplies keys implements KeySource and the app hands it to
 * input::addSource (input/keys.h): the keyboard (inputdev), a replay
 * (testing), and any further one, a controller or a script, without the game
 * or the input group knowing it.  A group that wants to see the keys the game
 * is given, as the recorder does, implements KeyObserver.
 *
 * Each tick input::update asks the sources for their keys and merges them,
 * and tells the observers what the game is about to read. */
#pragma once
#include "keycodes.h"

namespace input {

class KeySource {
public:
    virtual ~KeySource() {}

    /* Marks the keys this source holds down: sets the byte of each to 0x80
     * and leaves the others alone.  Called once per tick, before the game
     * reads a key.  Keys are scancodes (keycodes.h); a source that has
     * buttons or axes maps them to the keys the bindings use. */
    virtual void sample(unsigned char keys[KEY_COUNT]) = 0;

    /* Whether this source is the only one the game takes keys from while it
     * is added.  A replay is: a real key must not change what it replays.
     * The other sources go on running for whatever else uses them, the debug
     * UI's text box for one.  Otherwise the sources' keys are merged. */
    virtual bool exclusive() const { return false; }
};

class KeyObserver {
public:
    virtual ~KeyObserver() {}

    /* The keys the game reads this tick, as input::update merged them. */
    virtual void observe(const unsigned char keys[KEY_COUNT]) = 0;
};

}  // namespace input
