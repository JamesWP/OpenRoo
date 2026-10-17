#include <SDL3/SDL.h>
#include <string.h>
#include "inputdev.h"

namespace inputdev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define ID_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

/* The Key enum is SDL's scancodes under other names. */
static_assert((int)input::KEY_COUNT == (int)SDL_SCANCODE_COUNT, "key count");
#define CHECK_KEY(k, sc) static_assert((int)(input::k) == (int)(sc), #k)
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

static bool g_suppressed;
void setKeyboardSuppressed(bool suppressed) { g_suppressed = suppressed; }

namespace {
class Keyboard : public input::KeySource {
public:
    void sample(unsigned char keys[input::KEY_COUNT]) override
    {
        if (g_suppressed) return;
        const bool *down = SDL_GetKeyboardState(NULL);
        for (int i = 0; i < input::KEY_COUNT; i++)
            if (down[i]) keys[i] = 0x80;
    }
};
}  // namespace

input::KeySource &keyboard()
{
    static Keyboard k;
    return k;
}

namespace {
/* The gamepad's buttons, as the keys the default bindings
 * (sequencer/inputsetup.cpp) and the menus read.  SDL's buttons are
 * positional: EAST is the right-hand face button (A on the RG35XX and on a
 * Nintendo pad), SOUTH the bottom one. */
struct ButtonKey { SDL_GamepadButton button; int key; };
const ButtonKey kButtonKeys[] = {
    { SDL_GAMEPAD_BUTTON_DPAD_UP,        input::KEY_UP },
    { SDL_GAMEPAD_BUTTON_DPAD_DOWN,      input::KEY_DOWN },
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT,      input::KEY_LEFT },
    { SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     input::KEY_RIGHT },
    { SDL_GAMEPAD_BUTTON_EAST,           input::KEY_RETURN },    // confirm
    { SDL_GAMEPAD_BUTTON_SOUTH,          input::KEY_B },         // bomb
    { SDL_GAMEPAD_BUTTON_NORTH,          input::KEY_PAGEUP },    // tilt up
    { SDL_GAMEPAD_BUTTON_WEST,           input::KEY_PAGEDOWN },  // tilt down
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  input::KEY_X },         // rotate left
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, input::KEY_C },         // rotate right
    { SDL_GAMEPAD_BUTTON_BACK,           input::KEY_TAB },       // overview
    { SDL_GAMEPAD_BUTTON_START,          input::KEY_ESCAPE },    // pause, back
    { SDL_GAMEPAD_BUTTON_GUIDE,          input::KEY_DELETE },    // harakiri
};
/* The triggers (on the RG35XX, digital: 0 or full). */
struct AxisKey { SDL_GamepadAxis axis; int key; };
const AxisKey kAxisKeys[] = {
    { SDL_GAMEPAD_AXIS_LEFT_TRIGGER,  input::KEY_Z },  // zoom out
    { SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, input::KEY_A },  // zoom in
};
const Sint16 kTriggerDown = 16384;

class Gamepad : public input::KeySource {
public:
    void sample(unsigned char keys[input::KEY_COUNT]) override
    {
        if (!open()) return;
        for (const ButtonKey &b : kButtonKeys)
            if (SDL_GetGamepadButton(pad_, b.button)) keys[b.key] = 0x80;
        for (const AxisKey &a : kAxisKeys)
            if (SDL_GetGamepadAxis(pad_, a.axis) >= kTriggerDown) keys[a.key] = 0x80;
    }

private:
    /* The first gamepad, opened on first use and again after it is
     * unplugged.  The events that update its state are pumped by windev. */
    bool open()
    {
        if (pad_ && SDL_GamepadConnected(pad_)) return true;
        if (pad_) {
            SDL_CloseGamepad(pad_);
            pad_ = NULL;
        }
        if (!inited_) {
            inited_ = true;
            if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
                ID_LOG("inputdev: no gamepad support: %s\n", SDL_GetError());
                return false;
            }
            ok_ = true;
        }
        if (!ok_ || !SDL_HasGamepad()) return false;
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids && count > 0) {
            pad_ = SDL_OpenGamepad(ids[0]);
            if (pad_) ID_LOG("inputdev: gamepad '%s'\n", SDL_GetGamepadName(pad_));
        }
        SDL_free(ids);
        return pad_ != NULL;
    }

    SDL_Gamepad *pad_ = NULL;
    bool inited_ = false, ok_ = false;
};
}  // namespace

input::KeySource &gamepad()
{
    static Gamepad g;
    return g;
}

int keyFromName(const char *name)
{
    // SDL_GetScancodeFromName ignores case.
    SDL_Scancode sc = SDL_GetScancodeFromName(name);
    return sc == SDL_SCANCODE_UNKNOWN ? input::KEY_NONE : (int)sc;
}

bool keyName(int key, char *buf, unsigned bufsz)
{
    if (!buf || bufsz == 0) return false;
    const char *name = key > 0 && key < input::KEY_COUNT
                     ? SDL_GetScancodeName((SDL_Scancode)key) : "";
    if (!*name) {
        ID_LOG("inputdev: no name for key %d\n", key);
        return false;
    }
    strncpy(buf, name, bufsz - 1);
    buf[bufsz - 1] = '\0';
    return true;
}

}  // namespace inputdev
