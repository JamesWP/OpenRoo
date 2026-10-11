#include "keys.h"
#include "record.h"
#include "inputdev.h"
#include <string.h>

namespace input {

static unsigned char g_now[inputdev::KEY_COUNT];   // 0x80 while held
static unsigned char g_prev[inputdev::KEY_COUNT];
static bool          g_swallowed[inputdev::KEY_COUNT];
static bool          g_started;

void update()
{
    memcpy(g_prev, g_now, sizeof(g_now));
    if (record_replaying())
        replay_keys(g_now);
    else
        inputdev::readKeyboard(g_now);
    record_keys(g_now);

    // What is held as the game starts, or is carried over from the previous
    // tick of a recording, is not pressed.
    if (!g_started) {
        memcpy(g_prev, g_now, sizeof(g_now));
        g_started = true;
    }
    memset(g_swallowed, 0, sizeof(g_swallowed));
}

static bool valid(int key) { return key > 0 && key < inputdev::KEY_COUNT; }

bool down(int key)
{
    return valid(key) && (g_now[key] & 0x80);
}

bool pressed(int key)
{
    return down(key) && !(g_prev[key] & 0x80) && !g_swallowed[key];
}

void swallow(int key)
{
    if (valid(key)) g_swallowed[key] = true;
}

int firstDown()
{
    for (int k = 1; k < inputdev::KEY_COUNT; k++)
        if (g_now[k] & 0x80) return k;
    return 0;
}

}  // namespace input
