#include "keys.h"
#include <string.h>
#include <vector>

namespace input {

static std::vector<KeySource *>   g_sources;
static std::vector<KeyObserver *> g_observers;

static unsigned char g_now[KEY_COUNT];   // 0x80 while held
static unsigned char g_prev[KEY_COUNT];
static bool          g_waiting[KEY_COUNT];  // pressed, and not yet read
static bool          g_read[KEY_COUNT];     // read this tick
static bool          g_started;

void addSource(KeySource *source)       { if (source) g_sources.push_back(source); }
void addObserver(KeyObserver *observer) { if (observer) g_observers.push_back(observer); }

/* The sources' keys, merged: those of the exclusive sources if there are any,
 * else all. */
static void sample_sources()
{
    bool exclusive = false;
    for (KeySource *s : g_sources)
        exclusive = exclusive || s->exclusive();
    memset(g_now, 0, sizeof(g_now));
    for (KeySource *s : g_sources)
        if (!exclusive || s->exclusive())
            s->sample(g_now);
}

void update()
{
    memcpy(g_prev, g_now, sizeof(g_now));
    sample_sources();
    for (KeyObserver *o : g_observers)
        o->observe(g_now);

    // What is held as the game starts is not pressed.
    if (!g_started) {
        memcpy(g_prev, g_now, sizeof(g_now));
        g_started = true;
    }
    // A press waits while the key is held, until a tick has read it.
    for (int k = 0; k < KEY_COUNT; k++) {
        const bool held = g_now[k] & 0x80, was = g_prev[k] & 0x80;
        g_waiting[k] = held && (!was || (g_waiting[k] && !g_read[k]));
        g_read[k] = false;
    }
}

static bool valid(int key) { return key > 0 && key < KEY_COUNT; }

bool down(int key)
{
    return valid(key) && (g_now[key] & 0x80);
}

bool pressed(int key)
{
    if (!valid(key) || !g_waiting[key])
        return false;
    g_read[key] = true;
    return true;
}

void swallow(int key)
{
    if (valid(key)) g_waiting[key] = false;
}

int firstDown()
{
    for (int k = 1; k < KEY_COUNT; k++)
        if (g_now[k] & 0x80) return k;
    return 0;
}

}  // namespace input
