/* The programmable control: the tables that map keys to named game actions.
 *
 * Actions are registered per mode (0..4, the game state the dispatch is called
 * with); each has a callback, a context and a list of bound keys.  Once per
 * tick, dispatch calls the callback of every action with a key held
 * (keys.h).  Bindings are saved to and loaded from openroo.ini.
 * app owns the one instance and passes it down. */

#pragma once
#include <stdint.h>
#include <stddef.h>

/* Called with the key that fired, the binding's strength and the context
 * given at registration. */
typedef void (*ActionCallback)(int key_id, int strength, void *context);

/* One key bound to an action. */
struct KeyBind {
    /* Frees kb and every binding chained after it; NULL frees nothing. */
    static void freeChain(KeyBind *kb);

    int      scancode;  // an input::Key
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

/* The whole control state. */
class ProgableControl {
public:
    /* The destructor frees the action tables. */
    ProgableControl();
    virtual ~ProgableControl();
    ProgableControl(const ProgableControl &) = delete;
    ProgableControl &operator=(const ProgableControl &) = delete;

    /* Calls every action in this mode with a bound key held; the first held
     * key of each action wins.  Modes of 5 and above do nothing. */
    void dispatch(unsigned short game_state);
    void clearBindings(unsigned short mode, const char *name);

    /* The action's bound key names, joined with " or ", into buf (at most
     * bufsz bytes).  Empty if the mode or action is unknown. */
    void getBindingStr(int mode, const char *name, char *buf,
                       unsigned int bufsz);

    void registerAction(unsigned short mode, const char *name,
                        ActionCallback cb, void *ctx);

    /* Loads the bindings in openroo.ini over the registered actions: each
     * action the file lists gets exactly the keys listed, the others keep
     * theirs.  Returns 0 if the file has no bindings.  Unknown names and keys
     * are skipped. */
    int  readBindings();

    /* Adds a key to an action, or updates the strength of one already bound. */
    int  bindKey(unsigned short mode, const char *name, int sc, int strength);

    /* Saves the bindings. */
    int  writeBindings();

    /* Binds the first key currently held to the action.  Returns 1 if one
     * was. */
    int  captureBinding(unsigned int mode, const char *name, int strength);

    /* The registered actions of one mode (0..4). */
    const ActionTable &actionTable(int mode) const { return action_tables[mode]; }

private:
    char                   sep_or[50];  // joins key names in a binding description: " or "
    ActionTable            action_tables[5];    // one per mode
};
