/* The programmable control: the keyboard and mouse devices (inputdev.h), and the
 * tables that map keys to named game actions.
 *
 * Actions are registered per mode (0..4, the game state the dispatch is called
 * with); each has a callback, a context and a list of bound keys.  Once per
 * tick, Dispatch reads the keyboard and calls the callback of every action
 * with a key held.  Bindings are saved to and loaded from ProgableControl.sav.
 * There is one instance, g_progCtrl (gameglobals.h). */

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "inputdev.h"

/* Called with the scan code that fired, the binding's strength and the context
 * given at registration. */
typedef void (*ActionCallback)(int key_id, int strength, void *context);

/* One key bound to an action. */
struct KeyBind {
    /* Frees kb and every binding chained after it; NULL frees nothing. */
    static void freeChain(KeyBind *kb);

    int      scancode;  // inputdev scan code, 0..255
    int      strength;  // passed to the callback; 100 for a plain key
    KeyBind *next;
};

/* One registered action: a name unique within its mode, case-insensitive. */
struct ActionEntry {
    char           name[256];
    ActionCallback callback;
    void          *context;
    KeyBind       *kbd;
    ActionEntry   *chain;  // next action in the mode's list, in registration order
};

class ActionTable {
public:
    ActionEntry *first() const  { return head; }
    uint32_t        count() const  { return entry_count; }

    /* The action with this name (case-insensitive), or NULL. */
    ActionEntry *find(const char *name);
    /* find, or a new zeroed entry appended so the file keeps registration
     * order; NULL if the allocation fails. */
    ActionEntry *getOrCreate(const char *name);
    /* Frees every entry and its key bindings. */
    void         freeAll();

private:
    friend class ProgableControl;

    ActionEntry *head;
    uint32_t        entry_count;
};

/* The whole control state.  The controller is never set up: its range and
 * dead-zone calls succeed without doing anything. */
class ProgableControl {
public:
    /* The destructor releases the devices and frees the action tables. */
    ProgableControl();
    virtual ~ProgableControl();
    ProgableControl(const ProgableControl &) = delete;
    ProgableControl &operator=(const ProgableControl &) = delete;

    int  setJoyDeadzone(uint32_t axis, int zone);

    /* Reads the keyboard (or the replay, or the autoplay policy) and calls
     * every action in this mode with a bound key held; the first held key of
     * each action wins.  Modes of 5 and above do nothing. */
    void dispatch(unsigned short game_state);
    void clearBindings(unsigned short mode, const char *name);

    /* The action's bound key names, joined with " or ", into buf (at most
     * bufsz bytes).  Empty if the mode or action is unknown. */
    void getBindingStr(int mode, const char *name, char *buf,
                       unsigned int bufsz);

    /* The setup calls, in the order inputsetup.cpp makes them.  Each returns
     * 1 on success, 0 on failure. */
    int  setupDevices(void *window);
    int  setJoyRange(int axis, int lo, int hi);
    void registerAction(unsigned short mode, const char *name,
                        ActionCallback cb, void *ctx);

    /* Loads ProgableControl.sav into the registered actions; returns 0 if the
     * file is missing or short.  Bindings for unregistered names are
     * discarded. */
    int  readBindings();

    /* Adds a key to an action, or updates the strength of one already bound. */
    int  bindKey(unsigned short mode, const char *name, int sc, int strength);
    int  acquireAll();
    int  unacquireAll();

    /* Saves the bindings; releases every input device. */
    int  writeBindings();
    void shutdown();

    /* Binds the first key currently held to the action.  Returns 1 if one
     * was.  The axis and flags arguments are ignored. */
    int  captureBinding(unsigned int mode, const char *name, int strength,
                        int allow_axis, int flags);

    /* The registered actions of one mode (0..4). */
    const ActionTable &actionTable(int mode) const { return action_tables[mode]; }

private:
    int  readOrigEntryBindings(FILE *f, int mode, ActionEntry *e);
    int  readOrigFormat(FILE *f);
 

    inputdev::Devices      devices_;
    char                   sep_or[50];  // joins key names in a binding description: " or "
    ActionTable            action_tables[5];    // one per mode
};


