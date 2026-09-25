/* ProgableControl replacement — DirectInput 8, simple action/binding tables */
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <string.h>
#include <stdio.h>
#include "progctrl.h"
#include "log.h"
#include "gamestate.h"
#include "record.h"
#include "policy.h"
#include "clock.h"

static const char SAVE_FILE[] = "ProgableControl.sav";

/* ── action table helpers ─────────────────────────────────────────────────── */

static ActionEntry *find_entry(ActionTable *t, const char *name)
{
    for (ActionEntry *e = t->head; e; e = e->chain)
        if (!_stricmp(e->name, name)) return e;
    return nullptr;
}

static ActionEntry *get_or_create(ActionTable *t, const char *name)
{
    /* Walk to tail, checking for duplicates on the way. */
    ActionEntry **tail = &t->head;
    for (ActionEntry *e = t->head; e; e = e->chain) {
        if (!_stricmp(e->name, name)) return e;
        tail = &e->chain;
    }
    ActionEntry *e = (ActionEntry *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) return nullptr;
    strncpy(e->name, name, 255);
    *tail = e;  /* append — preserves registration order in the file */
    t->entry_count++;
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
    //log_write("ProgCtrl: release_devices(kbd=%p mouse=%p joy=%p di=%p)\n",
    //          s->pKeyboard, s->pMouse, s->pJoystick, s->directinput);
    if (s->pKeyboard) { s->pKeyboard->Unacquire(); s->pKeyboard->Release(); s->pKeyboard = nullptr; }
    if (s->pMouse)    { s->pMouse->Unacquire();    s->pMouse->Release();    s->pMouse    = nullptr; }
    if (s->pJoystick) { s->pJoystick->Unacquire(); s->pJoystick->Release(); s->pJoystick = nullptr; }
    if (s->directinput) { s->directinput->Release(); s->directinput = nullptr; }
}

/* ── method bodies ────────────────────────────────────────────────────────── */

static void *Setup_impl(ProgableControl *s, int /*logger_or_0*/)
{
    //log_write("ProgCtrl::Setup(this=%p logger_or_0=%d)\n", s, logger_or_0);
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
    //log_write("ProgCtrl::Teardown(this=%p)\n", s);
    s->vtable = const_cast<void*>(PROGCTRL_VTABLE);
    release_devices(s);
    for (int i = 0; i < 5; i++) free_table(&s->action_tables[i]);
}

static void ScalarDtor_impl(ProgableControl *s, int free_or_not)
{
    //log_write("ProgCtrl::ScalarDtor(this=%p free_or_not=%d)\n", s, free_or_not);
    Teardown_impl(s);
    if (free_or_not & 1)
        HeapFree(GetProcessHeap(), 0, s);
}

static void Shutdown_impl(ProgableControl *s)
{
    //log_write("ProgCtrl::Shutdown(this=%p)\n", s);
    release_devices(s);
}

static int InitDInput_impl(ProgableControl *s, HINSTANCE hInstance)
{
    //log_write("ProgCtrl::InitDInput(this=%p hInstance=%p)\n", s, hInstance);
    HRESULT hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION,
                                    IID_IDirectInput8A,
                                    reinterpret_cast<void**>(&s->directinput), nullptr);
    if (FAILED(hr)) {
        log_write("ProgCtrl::InitDInput: DirectInput8Create FAILED hr=0x%08lx\n", hr);
        s->directinput = nullptr;
        return 0;
    }
    return 1;
}

static int SetupKbd_impl(ProgableControl *s, HWND hwnd)
{
    //log_write("ProgCtrl::SetupKbd(this=%p hwnd=%p)\n", s, hwnd);
    if (!s->directinput) {
        log_write("ProgCtrl::SetupKbd: no directinput interface\n");
        return 0;
    }
    HRESULT hr = s->directinput->CreateDevice(GUID_SysKeyboard, &s->pKeyboard, nullptr);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: CreateDevice FAILED hr=0x%08lx\n", hr);
        return 0;
    }
    hr = s->pKeyboard->SetDataFormat(&c_dfDIKeyboard);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: SetDataFormat FAILED hr=0x%08lx\n", hr);
        s->pKeyboard->Release(); s->pKeyboard = nullptr; return 0;
    }
    hr = s->pKeyboard->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: SetCooperativeLevel FAILED hr=0x%08lx\n", hr);
        s->pKeyboard->Release(); s->pKeyboard = nullptr; return 0;
    }
    return 1;
}

static int SetupMouse_impl(ProgableControl *s, HWND hwnd)
{
    //log_write("ProgCtrl::SetupMouse(this=%p hwnd=%p)\n", s, hwnd);
    if (!s->directinput) {
        log_write("ProgCtrl::SetupMouse: no directinput interface\n");
        return 0;
    }
    HRESULT hr = s->directinput->CreateDevice(GUID_SysMouse, &s->pMouse, nullptr);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: CreateDevice FAILED hr=0x%08lx\n", hr);
        return 0;
    }
    hr = s->pMouse->SetDataFormat(&c_dfDIMouse);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: SetDataFormat FAILED hr=0x%08lx\n", hr);
        s->pMouse->Release(); s->pMouse = nullptr; return 0;
    }
    hr = s->pMouse->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: SetCooperativeLevel FAILED hr=0x%08lx\n", hr);
        s->pMouse->Release(); s->pMouse = nullptr; return 0;
    }
    return 1;
}

static int SetupJoy_impl(ProgableControl *, HWND)          { return 1; }
static int SetJoyRange_impl(ProgableControl *, int, int, int) { return 1; }
static int SetJoyDeadzone_impl(ProgableControl *, DWORD, int) { return 1; }

static int AcquireAll_impl(ProgableControl *s)
{
    if (s->pKeyboard) {
        HRESULT hr = s->pKeyboard->Acquire();
        if (FAILED(hr)) {
            log_write("ProgCtrl::AcquireAll: keyboard Acquire FAILED hr=0x%08lx\n", hr);
            return 0;
        }
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
    //log_write("ProgCtrl::RegisterAction(mode=%u name='%s' cb=%p ctx=%p)\n", mode, name, cb, ctx);
    if (mode >= 5) return;
    ActionEntry *e = get_or_create(&s->action_tables[mode], name);
    if (!e) {
        log_write("ProgCtrl::RegisterAction: HeapAlloc failed for '%s'\n", name);
        return;
    }
    e->callback = cb;
    e->context  = ctx;
}

static int BindKey_impl(ProgableControl *s, unsigned short mode,
                         const char *name, int sc, int strength)
{
    //log_write("ProgCtrl::BindKey(mode=%u name='%s' sc=0x%02X strength=%d)\n", mode, name, sc, strength);
    if (mode >= 5) return 0;
    ActionEntry *e = find_entry(&s->action_tables[mode], name);
    if (!e) return 0;
    for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
        if (kb->scancode == sc) { kb->strength = strength; return 1; }
    }
    KeyBind *kb = (KeyBind *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*kb));
    if (!kb) { log_write("ProgCtrl::BindKey: HeapAlloc failed\n"); return 0; }
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
            HRESULT hr = s->pKeyboard->GetObjectInfo(&doi, (DWORD)kb->scancode, DIPH_BYOFFSET);
            if (SUCCEEDED(hr))
                strncpy(keyname, doi.tszName, sizeof(keyname) - 1);
            else
                log_write("ProgCtrl::GetBindingStr: GetObjectInfo sc=0x%02X FAILED hr=0x%08lx\n",
                          kb->scancode, hr);
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
    /* Captured before the early return so paused/cutscene modes are visible. */
    gamestate_note_mode(game_state);

    BYTE ks[256];
    if (record_replaying() && !policy_in_control(clock_frame())) {
        /* Replay drives the scancode array and the mode from the recording,
         * so the real keyboard is not touched at all. */
        unsigned short recorded = game_state;
        if (!replay_keys(&recorded, ks)) return;
        game_state = recorded;
        if (game_state >= 5) return;
    } else if (policy_in_control(clock_frame())) {
        /* The policy owns the run from here: the recording (if any) only
         * existed to supply the menu prefix that got us into a level.  The
         * real keyboard is not read, so this is reproducible in the same way a
         * replay is. */
        if (game_state >= 5) return;

        /* Read the human's keys first, then merge the policy's on top.  The
         * policy drives, but whoever is watching can still steer, and — more
         * importantly — is not locked out of their own game.  Recording sees
         * the merged array, so a recorded policy run still replays exactly. */
        BYTE human[256];
        memset(human, 0, sizeof(human));
        if (s->pKeyboard) {
            HRESULT hr = s->pKeyboard->GetDeviceState(256, human);
            if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
                s->pKeyboard->Acquire();
                hr = s->pKeyboard->GetDeviceState(256, human);
            }
            if (FAILED(hr)) memset(human, 0, sizeof(human));
        }

        memset(ks, 0, sizeof(ks));
        if (!policy_keys(s, game_state, ks)) {
            memcpy(ks, human, sizeof(ks));
        } else {
            for (int i = 0; i < 256; i++) ks[i] |= human[i];
        }
        record_keys(game_state, ks);
    } else {
        if (game_state >= 5 || !s->pKeyboard) return;

        HRESULT hr = s->pKeyboard->GetDeviceState(256, ks);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            s->pKeyboard->Acquire();
            hr = s->pKeyboard->GetDeviceState(256, ks);
        }
        if (FAILED(hr)) return;
        /* Stage 4: the policy overwrites the buffer the game was about to be
         * given.  It runs *before* record_keys so a policy-driven run is
         * captured to a .rec exactly as a hand-played one is — replaying that
         * file back is the check that perception, decision and injection all
         * agree.  It declines outside a level, so menus stay hand-driven. */
        policy_keys(s, game_state, ks);
        record_keys(game_state, ks);
    }

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

/* ── save / load ──────────────────────────────────────────────────────────── */

/* Read one action entry's binding block in the original game format:
 *   DWORD kbd_count;  kbd_count × (DWORD key_id + DWORD strength)
 *   DWORD axis_count; axis_count × (DWORD axis_id + 8 bytes) — skipped
 *   DWORD btn_count;  btn_count  × (DWORD btn_id  + 8 bytes) — skipped
 */
static int read_orig_entry_bindings(HANDLE f, ProgableControl *s, int mode, ActionEntry *e)
{
    DWORD n;

    DWORD kbd_count;
    if (!ReadFile(f, &kbd_count, 4, &n, nullptr) || n != 4) return 0;
    log_write("ProgCtrl::ReadBindings(orig):     kbd_count=%lu\n", kbd_count);
    for (DWORD ki = 0; ki < kbd_count; ki++) {
        DWORD key_id, strength;
        if (!ReadFile(f, &key_id,   4, &n, nullptr) || n != 4) return 0;
        if (!ReadFile(f, &strength, 4, &n, nullptr) || n != 4) return 0;
        log_write("ProgCtrl::ReadBindings(orig):       key=0x%02lX strength=%lu -> %s\n",
                  key_id, strength, e ? "applied" : "skipped");
        if (e) BindKey_impl(s, (unsigned short)mode, e->name, (int)key_id, (int)strength);
    }

    DWORD axis_count;
    if (!ReadFile(f, &axis_count, 4, &n, nullptr) || n != 4) return 0;
    log_write("ProgCtrl::ReadBindings(orig):     axis_count=%lu (skipped)\n", axis_count);
    for (DWORD ai = 0; ai < axis_count; ai++) {
        BYTE discard[12];
        if (!ReadFile(f, discard, 12, &n, nullptr) || n != 12) return 0;
    }

    DWORD btn_count;
    if (!ReadFile(f, &btn_count, 4, &n, nullptr) || n != 4) return 0;
    log_write("ProgCtrl::ReadBindings(orig):     btn_count=%lu (skipped)\n", btn_count);
    for (DWORD bi = 0; bi < btn_count; bi++) {
        BYTE discard[12];
        if (!ReadFile(f, discard, 12, &n, nullptr) || n != 12) return 0;
    }

    return 1;
}

static int read_orig_format(HANDLE f, ProgableControl *s)
{
    DWORD n;
    log_write("ProgCtrl::ReadBindings: reading\n");
    for (int m = 0; m < 5; m++) {
        DWORD cnt;
        if (!ReadFile(f, &cnt, 4, &n, nullptr) || n != 4) {
            log_write("ProgCtrl::ReadBindings(orig): read error on entry count for mode %d\n", m);
            return 0;
        }
        log_write("ProgCtrl::ReadBindings(orig): mode %d: %lu entries\n", m, cnt);
        for (DWORD ei = 0; ei < cnt; ei++) {
            char namebuf[256] = {};
            if (!ReadFile(f, namebuf, 256, &n, nullptr) || n != 256) {
                log_write("ProgCtrl::ReadBindings(orig): read error on name\n");
                return 0;
            }
            ActionEntry *e = find_entry(&s->action_tables[m], namebuf);
            log_write("ProgCtrl::ReadBindings(orig):   '%s'%s\n",
                      namebuf, e ? "" : " (not registered, bindings discarded)");
            if (!read_orig_entry_bindings(f, s, m, e)) {
                log_write("ProgCtrl::ReadBindings(orig): read error in bindings for '%s'\n", namebuf);
                return 0;
            }
        }
    }
    log_write("ProgCtrl::ReadBindings(orig): ok\n");
    return 1;
}

static int WriteBindings_impl(ProgableControl *s)
{
    log_write("ProgCtrl::WriteBindings(this=%p) -> '%s'\n", s, SAVE_FILE);
    HANDLE f = CreateFileA(SAVE_FILE, GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("ProgCtrl::WriteBindings: CreateFile FAILED err=%lu\n", GetLastError());
        return 0;
    }
    DWORD n;
    /* Original format: no header.
     *   DWORD entry_count
     *   For each entry:
     *     char  name[256]
     *     DWORD kbd_count;  kbd_count × (DWORD key_id, DWORD strength)
     *     DWORD axis_count; (always 0)
     *     DWORD btn_count;  (always 0) */
    const DWORD zero = 0;
    for (int m = 0; m < 5; m++) {
        ActionTable *t = &s->action_tables[m];
        DWORD cnt = t->entry_count;
        log_write("ProgCtrl::WriteBindings: mode %d: %lu entries\n", m, cnt);
        if (!WriteFile(f, &cnt, 4, &n, nullptr)) goto fail;
        for (ActionEntry *e = t->head; e; e = e->chain) {
            char namebuf[256] = {};
            strncpy(namebuf, e->name, 255);
            if (!WriteFile(f, namebuf, 256, &n, nullptr)) goto fail;
            DWORD kc = 0;
            for (KeyBind *kb = e->kbd; kb; kb = kb->next) kc++;
            if (!WriteFile(f, &kc, 4, &n, nullptr)) goto fail;
            log_write("ProgCtrl::WriteBindings:   '%s': %lu binding(s)\n", namebuf, kc);
            for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
                DWORD key_id   = (DWORD)kb->scancode;
                DWORD strength = (DWORD)kb->strength;
                log_write("ProgCtrl::WriteBindings:     sc=0x%02lX strength=%lu\n", key_id, strength);
                if (!WriteFile(f, &key_id,   4, &n, nullptr)) goto fail;
                if (!WriteFile(f, &strength, 4, &n, nullptr)) goto fail;
            }
            if (!WriteFile(f, &zero, 4, &n, nullptr)) goto fail; /* axis_count = 0 */
            if (!WriteFile(f, &zero, 4, &n, nullptr)) goto fail; /* btn_count  = 0 */
        }
    }
    CloseHandle(f);
    log_write("ProgCtrl::WriteBindings: ok\n");
    return 1;
fail:
    log_write("ProgCtrl::WriteBindings: write error\n");
    CloseHandle(f);
    return 0;
}

static int ReadBindings_impl(ProgableControl *s)
{
    log_write("ProgCtrl::ReadBindings(this=%p) <- '%s'\n", s, SAVE_FILE);
    HANDLE f = CreateFileA(SAVE_FILE, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("ProgCtrl::ReadBindings: no save file (err=%lu)\n", GetLastError());
        return 0;
    }
    int ok = read_orig_format(f, s);
    CloseHandle(f);
    if (ok) log_write("ProgCtrl::ReadBindings: done\n");
    return ok;
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

/* PROGCTRL_VTABLE: our own 1-slot table (the game's was at 0x0045efb0, same slots). */
static void *const progctrl_vtable_slots[1] = {
    (void *)&ProgCtrl_ScalarDtor,
};
extern const void *const PROGCTRL_VTABLE = progctrl_vtable_slots;
