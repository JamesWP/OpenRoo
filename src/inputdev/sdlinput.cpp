#include <SDL3/SDL.h>
#include <string.h>
#include "inputdev.h"

namespace inputdev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define ID_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

/* The game's scan codes (DirectInput's: AT set 1, 0x80 added for the
 * extended keys) to SDL's.  Anything not listed has no SDL scancode here and
 * never reads as held. */
static SDL_Scancode sdl_scancode(int dik)
{
    static SDL_Scancode table[256];
    static bool built = false;
    if (!built) {
        struct { int dik; SDL_Scancode sc; } map[] = {
            {0x01, SDL_SCANCODE_ESCAPE},   {0x02, SDL_SCANCODE_1},
            {0x03, SDL_SCANCODE_2},        {0x04, SDL_SCANCODE_3},
            {0x05, SDL_SCANCODE_4},        {0x06, SDL_SCANCODE_5},
            {0x07, SDL_SCANCODE_6},        {0x08, SDL_SCANCODE_7},
            {0x09, SDL_SCANCODE_8},        {0x0a, SDL_SCANCODE_9},
            {0x0b, SDL_SCANCODE_0},        {0x0c, SDL_SCANCODE_MINUS},
            {0x0d, SDL_SCANCODE_EQUALS},   {0x0e, SDL_SCANCODE_BACKSPACE},
            {0x0f, SDL_SCANCODE_TAB},      {0x10, SDL_SCANCODE_Q},
            {0x11, SDL_SCANCODE_W},        {0x12, SDL_SCANCODE_E},
            {0x13, SDL_SCANCODE_R},        {0x14, SDL_SCANCODE_T},
            {0x15, SDL_SCANCODE_Y},        {0x16, SDL_SCANCODE_U},
            {0x17, SDL_SCANCODE_I},        {0x18, SDL_SCANCODE_O},
            {0x19, SDL_SCANCODE_P},        {0x1a, SDL_SCANCODE_LEFTBRACKET},
            {0x1b, SDL_SCANCODE_RIGHTBRACKET}, {0x1c, SDL_SCANCODE_RETURN},
            {0x1d, SDL_SCANCODE_LCTRL},    {0x1e, SDL_SCANCODE_A},
            {0x1f, SDL_SCANCODE_S},        {0x20, SDL_SCANCODE_D},
            {0x21, SDL_SCANCODE_F},        {0x22, SDL_SCANCODE_G},
            {0x23, SDL_SCANCODE_H},        {0x24, SDL_SCANCODE_J},
            {0x25, SDL_SCANCODE_K},        {0x26, SDL_SCANCODE_L},
            {0x27, SDL_SCANCODE_SEMICOLON},{0x28, SDL_SCANCODE_APOSTROPHE},
            {0x29, SDL_SCANCODE_GRAVE},    {0x2a, SDL_SCANCODE_LSHIFT},
            {0x2b, SDL_SCANCODE_BACKSLASH},{0x2c, SDL_SCANCODE_Z},
            {0x2d, SDL_SCANCODE_X},        {0x2e, SDL_SCANCODE_C},
            {0x2f, SDL_SCANCODE_V},        {0x30, SDL_SCANCODE_B},
            {0x31, SDL_SCANCODE_N},        {0x32, SDL_SCANCODE_M},
            {0x33, SDL_SCANCODE_COMMA},    {0x34, SDL_SCANCODE_PERIOD},
            {0x35, SDL_SCANCODE_SLASH},    {0x36, SDL_SCANCODE_RSHIFT},
            {0x37, SDL_SCANCODE_KP_MULTIPLY}, {0x38, SDL_SCANCODE_LALT},
            {0x39, SDL_SCANCODE_SPACE},    {0x3a, SDL_SCANCODE_CAPSLOCK},
            {0x3b, SDL_SCANCODE_F1},       {0x3c, SDL_SCANCODE_F2},
            {0x3d, SDL_SCANCODE_F3},       {0x3e, SDL_SCANCODE_F4},
            {0x3f, SDL_SCANCODE_F5},       {0x40, SDL_SCANCODE_F6},
            {0x41, SDL_SCANCODE_F7},       {0x42, SDL_SCANCODE_F8},
            {0x43, SDL_SCANCODE_F9},       {0x44, SDL_SCANCODE_F10},
            {0x45, SDL_SCANCODE_NUMLOCKCLEAR}, {0x46, SDL_SCANCODE_SCROLLLOCK},
            {0x47, SDL_SCANCODE_KP_7},     {0x48, SDL_SCANCODE_KP_8},
            {0x49, SDL_SCANCODE_KP_9},     {0x4a, SDL_SCANCODE_KP_MINUS},
            {0x4b, SDL_SCANCODE_KP_4},     {0x4c, SDL_SCANCODE_KP_5},
            {0x4d, SDL_SCANCODE_KP_6},     {0x4e, SDL_SCANCODE_KP_PLUS},
            {0x4f, SDL_SCANCODE_KP_1},     {0x50, SDL_SCANCODE_KP_2},
            {0x51, SDL_SCANCODE_KP_3},     {0x52, SDL_SCANCODE_KP_0},
            {0x53, SDL_SCANCODE_KP_PERIOD},{0x56, SDL_SCANCODE_NONUSBACKSLASH},
            {0x57, SDL_SCANCODE_F11},      {0x58, SDL_SCANCODE_F12},
            {0x9c, SDL_SCANCODE_KP_ENTER}, {0x9d, SDL_SCANCODE_RCTRL},
            {0xb5, SDL_SCANCODE_KP_DIVIDE},{0xb7, SDL_SCANCODE_PRINTSCREEN},
            {0xb8, SDL_SCANCODE_RALT},     {0xc5, SDL_SCANCODE_PAUSE},
            {0xc7, SDL_SCANCODE_HOME},     {0xc8, SDL_SCANCODE_UP},
            {0xc9, SDL_SCANCODE_PAGEUP},   {0xcb, SDL_SCANCODE_LEFT},
            {0xcd, SDL_SCANCODE_RIGHT},    {0xcf, SDL_SCANCODE_END},
            {0xd0, SDL_SCANCODE_DOWN},     {0xd1, SDL_SCANCODE_PAGEDOWN},
            {0xd2, SDL_SCANCODE_INSERT},   {0xd3, SDL_SCANCODE_DELETE},
            {0xdb, SDL_SCANCODE_LGUI},     {0xdc, SDL_SCANCODE_RGUI},
            {0xdd, SDL_SCANCODE_APPLICATION},
        };
        for (const auto &m : map)
            table[m.dik] = m.sc;
        built = true;
    }
    return (dik >= 0 && dik < 256) ? table[dik] : SDL_SCANCODE_UNKNOWN;
}

/* A Windows virtual key to the SDL scancode(s) that hold it down. */
static bool virtual_key_down(const bool *keys, int vk)
{
    SDL_Scancode a = SDL_SCANCODE_UNKNOWN, b = SDL_SCANCODE_UNKNOWN;
    if (vk >= 'A' && vk <= 'Z')       a = (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
    else if (vk >= '1' && vk <= '9')  a = (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
    else if (vk == '0')               a = SDL_SCANCODE_0;
    else if (vk >= 0x70 && vk <= 0x7b) a = (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - 0x70));
    else switch (vk) {
    case 0x08: a = SDL_SCANCODE_BACKSPACE; break;
    case 0x09: a = SDL_SCANCODE_TAB; break;
    case 0x0d: a = SDL_SCANCODE_RETURN; b = SDL_SCANCODE_KP_ENTER; break;
    case 0x10: a = SDL_SCANCODE_LSHIFT; b = SDL_SCANCODE_RSHIFT; break;
    case 0x11: a = SDL_SCANCODE_LCTRL;  b = SDL_SCANCODE_RCTRL; break;
    case 0x12: a = SDL_SCANCODE_LALT;   b = SDL_SCANCODE_RALT; break;
    case 0x1b: a = SDL_SCANCODE_ESCAPE; break;
    case 0x20: a = SDL_SCANCODE_SPACE; break;
    case 0x21: a = SDL_SCANCODE_PAGEUP; break;
    case 0x22: a = SDL_SCANCODE_PAGEDOWN; break;
    case 0x23: a = SDL_SCANCODE_END; break;
    case 0x24: a = SDL_SCANCODE_HOME; break;
    case 0x25: a = SDL_SCANCODE_LEFT; break;
    case 0x26: a = SDL_SCANCODE_UP; break;
    case 0x27: a = SDL_SCANCODE_RIGHT; break;
    case 0x28: a = SDL_SCANCODE_DOWN; break;
    case 0x2d: a = SDL_SCANCODE_INSERT; break;
    case 0x2e: a = SDL_SCANCODE_DELETE; break;
    default: return false;
    }
    return keys[a] || (b != SDL_SCANCODE_UNKNOWN && keys[b]);
}

short asyncKeyState(int virtualKey)
{
    const bool *keys = SDL_GetKeyboardState(NULL);
    return virtual_key_down(keys, virtualKey) ? (short)0x8000 : 0;
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

bool Devices::readKeyboard(unsigned char keys[256])
{
    if (!state_->open) return false;
    const bool *down = SDL_GetKeyboardState(NULL);
    for (int i = 0; i < 256; i++) {
        SDL_Scancode sc = sdl_scancode(i);
        keys[i] = (sc != SDL_SCANCODE_UNKNOWN && down[sc]) ? 0x80 : 0;
    }
    return true;
}

bool Devices::keyName(int scanCode, char *buf, unsigned bufsz)
{
    if (!buf || bufsz == 0) return false;
    SDL_Scancode sc = sdl_scancode(scanCode);
    const char *name = sc == SDL_SCANCODE_UNKNOWN ? "" : SDL_GetScancodeName(sc);
    if (!*name) {
        ID_LOG("inputdev: no name for sc=0x%02X\n", scanCode);
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
