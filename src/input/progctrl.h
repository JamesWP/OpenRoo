/* The programmable control: DirectInput keyboard and mouse devices, and the
 * tables that map keys to named game actions.
 *
 * Actions are registered per mode (0..4, the game state the dispatch is called
 * with); each has a callback, a context and a list of bound keys.  Once per
 * tick, Dispatch reads the keyboard and calls the callback of every action
 * with a key held.  Bindings are saved to and loaded from ProgableControl.sav.
 * There is one instance, g_progCtrl (gameglobals.h). */

#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stddef.h>

/* Called with the scan code that fired, the binding's strength and the context
 * given at registration. */
typedef void (*ActionCallback)(int key_id, int strength, void *context);

/* One key bound to an action. */
struct KeyBind {
    int      scancode;  // DirectInput scan code, 0..255
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

struct ActionTable {
    ActionEntry *head;
    DWORD        entry_count;
    DWORD        _pad[2];  // unused
};
static_assert(sizeof(ActionTable) == 16, "ActionTable size");

/* The whole control state.  The joystick is never set up: its setup, range and
 * dead-zone calls succeed without doing anything. */
#pragma pack(push, 1)
struct ProgableControl {
    void                  *vtable;
    void                  *pLogger;
    DWORD                  dwOwns_logger;
    LPDIRECTINPUT8A        directinput;
    LPDIRECTINPUTDEVICE8A  pKeyboard;
    LPDIRECTINPUTDEVICE8A  pMouse;
    LPDIRECTINPUTDEVICE8A  pJoystick;
    char                   sep_or[50];  // joins key names in a binding description: " oder "
    char                   prefix_joystick[50];
    char                   suffix_positive[50];
    char                   suffix_negative[50];
    DWORD                  axis_midpoints[20];
    BYTE                   _joystick_list[16];  // unused
    ActionTable            action_tables[5];    // one per mode
};
#pragma pack(pop)

static_assert(offsetof(ProgableControl, directinput)    == 0x00C, "directinput");
static_assert(offsetof(ProgableControl, pKeyboard)      == 0x010, "pKeyboard");
static_assert(offsetof(ProgableControl, sep_or)         == 0x01C, "sep_or");
static_assert(offsetof(ProgableControl, axis_midpoints) == 0x0E4, "axis_midpoints");
static_assert(offsetof(ProgableControl, action_tables)  == 0x144, "action_tables");
static_assert(sizeof(ProgableControl)                   == 0x194, "ProgableControl size");

/* The one-slot vtable: the scalar deleting destructor. */
extern const void *const PROGCTRL_VTABLE;

extern "C" {
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_SetJoyDeadzone(ProgableControl *self, DWORD axis, int zone);

/* Reads the keyboard (or the replay, or the autoplay policy) and calls every
 * action in this mode with a bound key held; the first held key of each action
 * wins.  Modes of 5 and above do nothing. */
__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_Dispatch(ProgableControl *self, unsigned short game_state);
__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_ClearBindings(ProgableControl *self, unsigned short mode,
                       const char *name);

/* The action's bound key names, joined with " oder ", into buf (at most bufsz
 * bytes).  Empty if the mode or action is unknown. */
__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_GetBindingStr(ProgableControl *self, int mode, const char *name,
                       char *buf, unsigned int bufsz);

/* The setup calls, in the order inputsetup.cpp makes them.  Each returns 1 on
 * success, 0 on failure. */
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_InitDInput(ProgableControl *self, HINSTANCE hInstance);
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_SetupKbd(ProgableControl *self, HWND hwnd);
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_SetupMouse(ProgableControl *self, HWND hwnd);
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_SetupJoy(ProgableControl *self, HWND hwnd);
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_SetJoyRange(ProgableControl *self, int axis, int lo, int hi);
__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_RegisterAction(ProgableControl *self, unsigned short mode,
                        const char *name, ActionCallback cb, void *ctx);

/* Loads ProgableControl.sav into the registered actions; returns 0 if the file
 * is missing or short.  Bindings for unregistered names are discarded. */
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_ReadBindings(ProgableControl *self);

/* Adds a key to an action, or updates the strength of one already bound. */
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_BindKey(ProgableControl *self, unsigned short mode,
                 const char *name, int sc, int strength);
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_AcquireAll(ProgableControl *self);
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_UnacquireAll(ProgableControl *self);

/* Saves the bindings; releases every input device. */
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_WriteBindings(ProgableControl *self);
__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_Shutdown(ProgableControl *self);

/* Binds the first key currently held to the action.  Returns 1 if one was.
 * The axis and flags arguments are ignored. */
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_CaptureBinding(ProgableControl *self, unsigned int mode,
                        const char *name, int strength, int allow_axis,
                        int flags);
}

/* Construction and destruction of the one global instance, driven by
 * staticinit.cpp. */
extern "C" __declspec(dllexport) void *__attribute__((thiscall)) ProgCtrl_Setup(ProgableControl *s, int logger_or_0);
extern "C" __declspec(dllexport) void __attribute__((thiscall)) ProgCtrl_Teardown(ProgableControl *s);
