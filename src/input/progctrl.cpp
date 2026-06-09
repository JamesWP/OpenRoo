/* ProgableControl replacement — DirectInput 8, simple action/binding tables */
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <string.h>
#include <stdio.h>
#include "progctrl.h"
#include "log.h"

static const char  SAVE_FILE[]  = "ProgableControl.sav";
static const DWORD SAVE_MAGIC   = 0x43544C50u; /* 'PLTC' */
static const DWORD SAVE_VERSION = 1;

/* ── action table helpers ─────────────────────────────────────────────────── */

static ActionEntry *find_entry(ActionTable *t, const char *name)
{
    for (ActionEntry *e = t->head; e; e = e->chain)
        if (!_stricmp(e->name, name)) return e;
    return nullptr;
}

static ActionEntry *get_or_create(ActionTable *t, const char *name)
{
    ActionEntry *e = find_entry(t, name);
    if (!e) {
        e = (ActionEntry *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
        if (!e) return nullptr;
        strncpy(e->name, name, 255);
        e->chain = t->head;
        t->head  = e;
        t->entry_count++;
    }
    return e;
}

static void free_keybinds(KeyBind *kb)
{
    while (kb) { KeyBind *n = kb->next; HeapFree(GetProcessHeap(), 0, kb); kb = n; }
}

static void free_table(ActionTable *t)
{
    ActionEntry *e = t->head;
    while (e) {
        ActionEntry *n = e->chain;
        free_keybinds(e->kbd);
        HeapFree(GetProcessHeap(), 0, e);
        e = n;
    }
    t->head        = nullptr;
    t->entry_count = 0;
}

static void release_devices(ProgableControl *s)
{
    if (s->pKeyboard) { s->pKeyboard->Unacquire(); s->pKeyboard->Release(); s->pKeyboard = nullptr; }
    if (s->pMouse)    { s->pMouse->Unacquire();    s->pMouse->Release();    s->pMouse    = nullptr; }
    if (s->pJoystick) { s->pJoystick->Unacquire(); s->pJoystick->Release(); s->pJoystick = nullptr; }
    if (s->directinput) { s->directinput->Release(); s->directinput = nullptr; }
}

/* ── method bodies ────────────────────────────────────────────────────────── */

static void *Setup_impl(ProgableControl *s, int logger_or_0)
{
    log_write("ProgCtrl::Setup(logger=%d)\n", logger_or_0);
    s->vtable        = const_cast<void*>(PROGCTRL_VTABLE);
    s->pLogger       = nullptr;
    s->dwOwns_logger = 0;
    s->directinput   = nullptr;
    s->pKeyboard     = nullptr;
    s->pMouse        = nullptr;
    s->pJoystick     = nullptr;
    strncpy(s->sep_or,          " oder ",    sizeof(s->sep_or)          - 1);
    strncpy(s->prefix_joystick, "JOYSTICK ", sizeof(s->prefix_joystick) - 1);
    strncpy(s->suffix_positive, " Positiv",  sizeof(s->suffix_positive) - 1);
    strncpy(s->suffix_negative, " Negativ",  sizeof(s->suffix_negative) - 1);
    memset(s->axis_midpoints, 0, sizeof(s->axis_midpoints));
    memset(s->_joystick_list, 0, sizeof(s->_joystick_list));
    for (int i = 0; i < 5; i++) {
        s->action_tables[i].head        = nullptr;
        s->action_tables[i].entry_count = 0;
        s->action_tables[i]._pad[0]     = 0;
        s->action_tables[i]._pad[1]     = 0;
    }
    return s;
}

static void Teardown_impl(ProgableControl *s)
{
    log_write("ProgCtrl::Teardown\n");
    s->vtable = const_cast<void*>(PROGCTRL_VTABLE);
    release_devices(s);
    for (int i = 0; i < 5; i++) free_table(&s->action_tables[i]);
}

static void ScalarDtor_impl(ProgableControl *s, int free_or_not)
{
    log_write("ProgCtrl::ScalarDtor(free=%d)\n", free_or_not);
    Teardown_impl(s);
    if (free_or_not & 1)
        HeapFree(GetProcessHeap(), 0, s);
}

static void Shutdown_impl(ProgableControl *s)
{
    log_write("ProgCtrl::Shutdown\n");
    release_devices(s);
}

static int InitDInput_impl(ProgableControl *s, HINSTANCE hInstance)
{
    log_write("ProgCtrl::InitDInput(hInstance=%p)\n", hInstance);
    HRESULT hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION,
                                    IID_IDirectInput8A,
                                    reinterpret_cast<void**>(&s->directinput), nullptr);
    if (FAILED(hr)) {
        log_write("ProgCtrl::InitDInput: failed hr=0x%lx\n", hr);
        s->directinput = nullptr;
        return 0;
    }
    return 1;
}

static int SetupKbd_impl(ProgableControl *s, HWND hwnd)
{
    log_write("ProgCtrl::SetupKbd(hwnd=%p)\n", hwnd);
    if (!s->directinput) return 0;

    HRESULT hr = s->directinput->CreateDevice(GUID_SysKeyboard, &s->pKeyboard, nullptr);
    if (FAILED(hr)) { log_write("ProgCtrl::SetupKbd: CreateDevice hr=0x%lx\n", hr); return 0; }

    hr = s->pKeyboard->SetDataFormat(&c_dfDIKeyboard);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: SetDataFormat hr=0x%lx\n", hr);
        s->pKeyboard->Release(); s->pKeyboard = nullptr; return 0;
    }

    hr = s->pKeyboard->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: SetCooperativeLevel hr=0x%lx\n", hr);
        s->pKeyboard->Release(); s->pKeyboard = nullptr; return 0;
    }
    return 1;
}

static int SetupMouse_impl(ProgableControl *s, HWND hwnd)
{
    log_write("ProgCtrl::SetupMouse(hwnd=%p)\n", hwnd);
    if (!s->directinput) return 0;

    HRESULT hr = s->directinput->CreateDevice(GUID_SysMouse, &s->pMouse, nullptr);
    if (FAILED(hr)) { log_write("ProgCtrl::SetupMouse: CreateDevice hr=0x%lx\n", hr); return 0; }

    hr = s->pMouse->SetDataFormat(&c_dfDIMouse);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: SetDataFormat hr=0x%lx\n", hr);
        s->pMouse->Release(); s->pMouse = nullptr; return 0;
    }

    hr = s->pMouse->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: SetCooperativeLevel hr=0x%lx\n", hr);
        s->pMouse->Release(); s->pMouse = nullptr; return 0;
    }
    return 1;
}

/* Joystick stubs — no hardware support needed for this title */
static int SetupJoy_impl(ProgableControl *, HWND)          { return 1; }
static int SetJoyRange_impl(ProgableControl *, int, int, int) { return 1; }
static int SetJoyDeadzone_impl(ProgableControl *, DWORD, int) { return 1; }

static int AcquireAll_impl(ProgableControl *s)
{
    log_write("ProgCtrl::AcquireAll\n");
    if (s->pKeyboard) {
        HRESULT hr = s->pKeyboard->Acquire();
        if (FAILED(hr)) { log_write("ProgCtrl::AcquireAll: kbd hr=0x%lx\n", hr); return 0; }
    }
    if (s->pMouse) s->pMouse->Acquire();
    return 1;
}

static int UnacquireAll_impl(ProgableControl *s)
{
    if (s->pKeyboard) s->pKeyboard->Unacquire();
    if (s->pMouse)    s->pMouse->Unacquire();
    if (s->pJoystick) s->pJoystick->Unacquire();
    return 1;
}

static void RegisterAction_impl(ProgableControl *s, unsigned short mode,
                                 const char *name, ActionCallback cb, void *ctx)
{
    log_write("ProgCtrl::RegisterAction(mode=%u name='%s')\n", mode, name);
    if (mode >= 5) return;
    ActionEntry *e = get_or_create(&s->action_tables[mode], name);
    if (!e) return;
    e->callback = cb;
    e->context  = ctx;
}

static int BindKey_impl(ProgableControl *s, unsigned short mode,
                         const char *name, int sc, int strength)
{
    if (mode >= 5) return 0;
    ActionEntry *e = find_entry(&s->action_tables[mode], name);
    if (!e) return 0;
    for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
        if (kb->scancode == sc) { kb->strength = strength; return 1; }
    }
    KeyBind *kb = (KeyBind *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*kb));
    if (!kb) return 0;
    kb->scancode = sc;
    kb->strength = strength;
    kb->next     = e->kbd;
    e->kbd       = kb;
    return 1;
}

static void ClearBindings_impl(ProgableControl *s, unsigned short mode, const char *name)
{
    if (mode >= 5) return;
    ActionEntry *e = find_entry(&s->action_tables[mode], name);
    if (!e) return;
    free_keybinds(e->kbd);
    e->kbd = nullptr;
}

static void GetBindingStr_impl(ProgableControl *s, int mode, const char *name,
                                char *buf, unsigned int bufsz)
{
    if (!buf || bufsz == 0) return;
    buf[0] = '\0';
    if (mode < 0 || mode >= 5) return;
    ActionEntry *e = find_entry(&s->action_tables[mode], name);
    if (!e) return;

    bool first = true;
    for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
        char keyname[MAX_PATH] = "?";
        if (s->pKeyboard) {
            DIDEVICEOBJECTINSTANCEA doi;
            doi.dwSize = sizeof(doi);
            if (SUCCEEDED(s->pKeyboard->GetObjectInfo(&doi, (DWORD)kb->scancode, DIPH_BYOFFSET)))
                strncpy(keyname, doi.tszName, sizeof(keyname) - 1);
        }
        if (!first) {
            size_t cur = strlen(buf), sep = strlen(s->sep_or);
            if (cur + sep + 1 <= bufsz)
                strncat(buf, s->sep_or, bufsz - cur - 1);
        }
        first = false;
        size_t cur = strlen(buf);
        if (cur + strlen(keyname) + 1 <= bufsz)
            strncat(buf, keyname, bufsz - cur - 1);
    }
}

static void Dispatch_impl(ProgableControl *s, unsigned short game_state)
{
    if (game_state >= 5 || !s->pKeyboard) return;

    BYTE ks[256];
    HRESULT hr = s->pKeyboard->GetDeviceState(256, ks);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        s->pKeyboard->Acquire();
        hr = s->pKeyboard->GetDeviceState(256, ks);
    }
    if (FAILED(hr)) return;

    for (ActionEntry *e = s->action_tables[game_state].head; e; e = e->chain) {
        for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
            if (kb->scancode >= 0 && kb->scancode < 256 && (ks[kb->scancode] & 0x80)) {
                if (e->callback) e->callback(kb->scancode, kb->strength, e->context);
                break;
            }
        }
    }
}

static int CaptureBinding_impl(ProgableControl *s, unsigned int mode, const char *name,
                                int strength, int /*allow_axis*/, int /*flags*/)
{
    if (mode >= 5 || !s->pKeyboard) return 0;

    BYTE ks[256];
    HRESULT hr = s->pKeyboard->GetDeviceState(256, ks);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        s->pKeyboard->Acquire();
        hr = s->pKeyboard->GetDeviceState(256, ks);
    }
    if (FAILED(hr)) return 0;

    for (int sc = 0; sc < 256; sc++) {
        if (ks[sc] & 0x80)
            return BindKey_impl(s, (unsigned short)mode, name, sc, strength);
    }
    return 0;
}

static int WriteBindings_impl(ProgableControl *s)
{
    log_write("ProgCtrl::WriteBindings\n");
    HANDLE f = CreateFileA(SAVE_FILE, GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("ProgCtrl::WriteBindings: can't create file err=%lu\n", GetLastError());
        return 0;
    }
    DWORD n;
    DWORD magic = SAVE_MAGIC, ver = SAVE_VERSION;
    if (!WriteFile(f, &magic, 4, &n, nullptr) || !WriteFile(f, &ver, 4, &n, nullptr))
        goto fail;
    for (int m = 0; m < 5; m++) {
        ActionTable *t = &s->action_tables[m];
        DWORD cnt = t->entry_count;
        if (!WriteFile(f, &cnt, 4, &n, nullptr)) goto fail;
        for (ActionEntry *e = t->head; e; e = e->chain) {
            char namebuf[256] = {};
            strncpy(namebuf, e->name, 255);
            if (!WriteFile(f, namebuf, 256, &n, nullptr)) goto fail;
            DWORD kc = 0;
            for (KeyBind *kb = e->kbd; kb; kb = kb->next) kc++;
            if (!WriteFile(f, &kc, 4, &n, nullptr)) goto fail;
            for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
                if (!WriteFile(f, &kb->scancode, 4, &n, nullptr)) goto fail;
                if (!WriteFile(f, &kb->strength, 4, &n, nullptr)) goto fail;
            }
        }
    }
    CloseHandle(f);
    return 1;
fail:
    log_write("ProgCtrl::WriteBindings: write error\n");
    CloseHandle(f);
    return 0;
}

static int ReadBindings_impl(ProgableControl *s)
{
    log_write("ProgCtrl::ReadBindings\n");
    HANDLE f = CreateFileA(SAVE_FILE, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("ProgCtrl::ReadBindings: no save file\n");
        return 0;
    }
    DWORD n, magic, ver;
    if (!ReadFile(f, &magic, 4, &n, nullptr) || n != 4 || magic != SAVE_MAGIC ||
        !ReadFile(f, &ver,   4, &n, nullptr) || n != 4 || ver   != SAVE_VERSION) {
        log_write("ProgCtrl::ReadBindings: bad header\n");
        CloseHandle(f); return 0;
    }
    for (int m = 0; m < 5; m++) {
        DWORD cnt;
        if (!ReadFile(f, &cnt, 4, &n, nullptr) || n != 4) goto fail;
        for (DWORD ei = 0; ei < cnt; ei++) {
            char namebuf[256] = {};
            if (!ReadFile(f, namebuf, 256, &n, nullptr) || n != 256) goto fail;
            DWORD kc;
            if (!ReadFile(f, &kc, 4, &n, nullptr) || n != 4) goto fail;
            ActionEntry *e = find_entry(&s->action_tables[m], namebuf);
            for (DWORD ki = 0; ki < kc; ki++) {
                int sc, st;
                if (!ReadFile(f, &sc, 4, &n, nullptr) || n != 4) goto fail;
                if (!ReadFile(f, &st, 4, &n, nullptr) || n != 4) goto fail;
                if (e) BindKey_impl(s, (unsigned short)m, e->name, sc, st);
            }
        }
    }
    CloseHandle(f);
    log_write("ProgCtrl::ReadBindings: OK\n");
    return 1;
fail:
    log_write("ProgCtrl::ReadBindings: read error\n");
    CloseHandle(f); return 0;
}

/* ── exports ─────────────────────────────────────────────────────────────── */

extern "C" {

__declspec(dllexport) void * __attribute__((thiscall))
ProgCtrl_Setup(ProgableControl *s, int logger_or_0)
    { return Setup_impl(s, logger_or_0); }

__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_ScalarDtor(ProgableControl *s, int free_or_not)
    { ScalarDtor_impl(s, free_or_not); }

__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_Teardown(ProgableControl *s)
    { Teardown_impl(s); }

__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_Shutdown(ProgableControl *s)
    { Shutdown_impl(s); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_InitDInput(ProgableControl *s, HINSTANCE hInstance)
    { return InitDInput_impl(s, hInstance); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_SetupKbd(ProgableControl *s, HWND hwnd)
    { return SetupKbd_impl(s, hwnd); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_SetupMouse(ProgableControl *s, HWND hwnd)
    { return SetupMouse_impl(s, hwnd); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_SetupJoy(ProgableControl *s, HWND hwnd)
    { return SetupJoy_impl(s, hwnd); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_SetJoyRange(ProgableControl *s, int axis, int lo, int hi)
    { return SetJoyRange_impl(s, axis, lo, hi); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_SetJoyDeadzone(ProgableControl *s, DWORD axis, int zone)
    { return SetJoyDeadzone_impl(s, axis, zone); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_AcquireAll(ProgableControl *s)
    { return AcquireAll_impl(s); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_UnacquireAll(ProgableControl *s)
    { return UnacquireAll_impl(s); }

__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_Dispatch(ProgableControl *s, unsigned short game_state)
    { Dispatch_impl(s, game_state); }

__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_RegisterAction(ProgableControl *s, unsigned short mode, const char *name,
                        ActionCallback cb, void *ctx)
    { RegisterAction_impl(s, mode, name, cb, ctx); }

__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_ClearBindings(ProgableControl *s, unsigned short mode, const char *name)
    { ClearBindings_impl(s, mode, name); }

__declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_GetBindingStr(ProgableControl *s, int mode, const char *name,
                       char *buf, unsigned int bufsz)
    { GetBindingStr_impl(s, mode, name, buf, bufsz); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_BindKey(ProgableControl *s, unsigned short mode, const char *name,
                 int sc, int strength)
    { return BindKey_impl(s, mode, name, sc, strength); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_CaptureBinding(ProgableControl *s, unsigned int mode, const char *name,
                        int strength, int allow_axis, int flags)
    { return CaptureBinding_impl(s, mode, name, strength, allow_axis, flags); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_WriteBindings(ProgableControl *s)
    { return WriteBindings_impl(s); }

__declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_ReadBindings(ProgableControl *s)
    { return ReadBindings_impl(s); }

} /* extern "C" */
