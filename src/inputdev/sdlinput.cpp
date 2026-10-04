#include <SDL3/SDL.h>
#include <string.h>
#include "inputdev.h"

namespace inputdev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define ID_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

/* The Key enum is SDL's scancodes under other names. */
static_assert((int)KEY_COUNT == (int)SDL_SCANCODE_COUNT, "key count");
#define CHECK_KEY(k, sc) static_assert((int)(k) == (int)(sc), #k)
CHECK_KEY(KEY_A, SDL_SCANCODE_A);         CHECK_KEY(KEY_Z, SDL_SCANCODE_Z);
CHECK_KEY(KEY_1, SDL_SCANCODE_1);         CHECK_KEY(KEY_9, SDL_SCANCODE_9);
CHECK_KEY(KEY_0, SDL_SCANCODE_0);
CHECK_KEY(KEY_RETURN, SDL_SCANCODE_RETURN);
CHECK_KEY(KEY_ESCAPE, SDL_SCANCODE_ESCAPE);
CHECK_KEY(KEY_BACKSPACE, SDL_SCANCODE_BACKSPACE);
CHECK_KEY(KEY_TAB, SDL_SCANCODE_TAB);     CHECK_KEY(KEY_SPACE, SDL_SCANCODE_SPACE);
CHECK_KEY(KEY_F1, SDL_SCANCODE_F1);       CHECK_KEY(KEY_F4, SDL_SCANCODE_F4);
CHECK_KEY(KEY_PAGEUP, SDL_SCANCODE_PAGEUP);
CHECK_KEY(KEY_PAGEDOWN, SDL_SCANCODE_PAGEDOWN);
CHECK_KEY(KEY_DELETE, SDL_SCANCODE_DELETE);
CHECK_KEY(KEY_RIGHT, SDL_SCANCODE_RIGHT); CHECK_KEY(KEY_LEFT, SDL_SCANCODE_LEFT);
CHECK_KEY(KEY_DOWN, SDL_SCANCODE_DOWN);   CHECK_KEY(KEY_UP, SDL_SCANCODE_UP);
CHECK_KEY(KEY_LSHIFT, SDL_SCANCODE_LSHIFT);
CHECK_KEY(KEY_RSHIFT, SDL_SCANCODE_RSHIFT);

bool keyDown(int key)
{
    if (key <= 0 || key >= KEY_COUNT) return false;
    return SDL_GetKeyboardState(NULL)[key];
}

int keyFromName(const char *name)
{
    // SDL_GetScancodeFromName ignores case.
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    return sc == SDL_SCANCODE_UNKNOWN ? KEY_NONE : (int)sc;
}

struct DevicesState {
    bool open;
};

Devices::Devices() : state_(new DevicesState())
{
}

Devices::~Devices()
{
    destroy();
    delete state_;
}

void Devices::destroy()
{
    state_->open = false;
}

/* SDL's keyboard is part of its video system, which the window brought up
 * (a headless run has no keyboard to read, as before). */
bool Devices::create(void *)
{
    state_->open = true;
    return true;
}

bool Devices::acquire()
{
    return true;
}

void Devices::unacquire()
{
}

bool Devices::readKeyboard(unsigned char keys[KEY_COUNT])
{
    if (!state_->open) return false;
    const bool *down = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < KEY_COUNT; i++)
        keys[i] = down[i] ? 0x80 : 0;
    return true;
}

bool Devices::keyName(int key, char *buf, unsigned bufsz)
{
    if (!buf || bufsz == 0) return false;
    const char *name = key > 0 && key < KEY_COUNT
                     ? SDL_GetScancodeName((SDL_Scancode)key) : "";
    if (!*name) {
        ID_LOG("inputdev: no name for key %d\n", key);
        return false;
    }
    strncpy(buf, name, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return true;
}

bool Devices::setControllerRange(int, int, int)
{
    return true;
}

bool Devices::setControllerDeadzone(int, int)
{
    return true;
}

}  // namespace inputdev
