#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <stddef.h>

typedef void (*ActionCallback)(int key_id, int strength, void *context);

/* Keyboard scancode binding */
struct KeyBind {
    int      scancode;
    int      strength;
    KeyBind *next;
};

/* One registered action entry */
struct ActionEntry {
    char           name[256];
    ActionCallback callback;
    void          *context;
    KeyBind       *kbd;
    ActionEntry   *chain;   /* next in table list */
};

/* Action table slot — must be exactly 16 bytes to match game struct layout */
struct ActionTable {
    ActionEntry *head;
    DWORD        entry_count;
    DWORD        _pad[2];
};
static_assert(sizeof(ActionTable) == 16, "ActionTable size");

/*
 * ProgableControl — 404 bytes (0x194).
 * Global singleton at ProgableControlGlobal @ 0x46c298.
 * All methods accessed only through our replacements; layout must match the
 * original struct exactly so game code that computes this+0x144 etc. works.
 */
#pragma pack(push, 1)
struct ProgableControl {
    void                  *vtable;              /* +0x000 */
    void                  *pLogger;             /* +0x004 */
    DWORD                  dwOwns_logger;       /* +0x008 */
    LPDIRECTINPUT8A        directinput;         /* +0x00C */
    LPDIRECTINPUTDEVICE8A  pKeyboard;           /* +0x010 */
    LPDIRECTINPUTDEVICE8A  pMouse;              /* +0x014 */
    LPDIRECTINPUTDEVICE8A  pJoystick;           /* +0x018 */
    char                   sep_or[50];          /* +0x01C */
    char                   prefix_joystick[50]; /* +0x04E */
    char                   suffix_positive[50]; /* +0x080 */
    char                   suffix_negative[50]; /* +0x0B2 */
    DWORD                  axis_midpoints[20];  /* +0x0E4 (80 bytes) */
    BYTE                   _joystick_list[16];  /* +0x134 (unused) */
    ActionTable            action_tables[5];    /* +0x144 (80 bytes) */
};
#pragma pack(pop)

static_assert(offsetof(ProgableControl, directinput)    == 0x00C, "directinput");
static_assert(offsetof(ProgableControl, pKeyboard)      == 0x010, "pKeyboard");
static_assert(offsetof(ProgableControl, sep_or)         == 0x01C, "sep_or");
static_assert(offsetof(ProgableControl, axis_midpoints) == 0x0E4, "axis_midpoints");
static_assert(offsetof(ProgableControl, action_tables)  == 0x144, "action_tables");
static_assert(sizeof(ProgableControl)                   == 0x194, "ProgableControl size");

/* Original vtable at PTR_ScalarDtorProgControl @ 0x0045efb0 */
static const void *const PROGCTRL_VTABLE = reinterpret_cast<const void*>(0x45efb0);

/* Exports of progctrl.cpp other files call (COHESION_PLAN.md template 10). */
extern "C" {
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_SetJoyDeadzone(ProgableControl *self, DWORD axis, int zone);
__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_Dispatch(ProgableControl *self, unsigned short game_state);
__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_ClearBindings(ProgableControl *self, unsigned short mode,
                       const char *name);
__declspec(dllexport) int  __attribute__((thiscall))
ProgCtrl_CaptureBinding(ProgableControl *self, unsigned int mode,
                        const char *name, int strength, int allow_axis,
                        int flags);
}
