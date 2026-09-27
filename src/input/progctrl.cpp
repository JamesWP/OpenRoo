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

ActionEntry *ActionTable::find(const char *name)
{
    for (ActionEntry *e = head; e; e = e->chain)
        if (!_stricmp(e->name, name)) return e;
    return nullptr;
}

ActionEntry *ActionTable::getOrCreate(const char *name)
{
    ActionEntry **tail = &head;
    for (ActionEntry *e = head; e; e = e->chain) {
        if (!_stricmp(e->name, name)) return e;
        tail = &e->chain;
    }
    ActionEntry *e = (ActionEntry *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (!e) return nullptr;
    strncpy(e->name, name, 255);
    *tail = e;  // appended, so the file keeps registration order
    entry_count++;
    return e;
}

void KeyBind::freeChain(KeyBind *kb)
{
    while (kb) { KeyBind *n = kb->next; HeapFree(GetProcessHeap(), 0, kb); kb = n; }
}

void ActionTable::freeAll()
{
    ActionEntry *e = head;
    while (e) {
        ActionEntry *n = e->chain;
        KeyBind::freeChain(e->kbd);
        HeapFree(GetProcessHeap(), 0, e);
        e = n;
    }
    head        = nullptr;
    entry_count = 0;
}

void ProgableControl::releaseDevices()
{
    if (pKeyboard) { pKeyboard->Unacquire(); pKeyboard->Release(); pKeyboard = nullptr; }
    if (pMouse)    { pMouse->Unacquire();    pMouse->Release();    pMouse    = nullptr; }
    if (pJoystick) { pJoystick->Unacquire(); pJoystick->Release(); pJoystick = nullptr; }
    if (directinput) { directinput->Release(); directinput = nullptr; }
}

void *ProgableControl::setup(int)
{
    vtable        = const_cast<void*>(PROGCTRL_VTABLE);
    pLogger       = nullptr;
    dwOwns_logger = 0;
    directinput   = nullptr;
    pKeyboard     = nullptr;
    pMouse        = nullptr;
    pJoystick     = nullptr;
    strncpy(sep_or,          " or ",    sizeof(sep_or)          - 1);
    strncpy(prefix_joystick, "JOYSTICK ", sizeof(prefix_joystick) - 1);
    strncpy(suffix_positive, " positive",  sizeof(suffix_positive) - 1);
    strncpy(suffix_negative, " negative",  sizeof(suffix_negative) - 1);
    memset(axis_midpoints, 0, sizeof(axis_midpoints));
    for (int i = 0; i < 5; i++) {
        action_tables[i].head        = nullptr;
        action_tables[i].entry_count = 0;
    }
    return this;
}

void ProgableControl::teardown()
{
    vtable = const_cast<void*>(PROGCTRL_VTABLE);
    releaseDevices();
    for (int i = 0; i < 5; i++) action_tables[i].freeAll();
}

void  
ProgableControl::scalarDtor(ProgableControl *s, int free_or_not)
{
    s->teardown();
    if (free_or_not & 1)
        HeapFree(GetProcessHeap(), 0, s);
}

void ProgableControl::shutdown()
{
    releaseDevices();
}

int ProgableControl::initDInput(HINSTANCE hInstance)
{
    HRESULT hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION,
                                    IID_IDirectInput8A,
                                    reinterpret_cast<void**>(&directinput), nullptr);
    if (FAILED(hr)) {
        log_write("ProgCtrl::InitDInput: DirectInput8Create FAILED hr=0x%08lx\n", hr);
        directinput = nullptr;
        return 0;
    }
    return 1;
}

int ProgableControl::setupKbd(HWND hwnd)
{
    if (!directinput) {
        log_write("ProgCtrl::SetupKbd: no directinput interface\n");
        return 0;
    }
    HRESULT hr = directinput->CreateDevice(GUID_SysKeyboard, &pKeyboard, nullptr);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: CreateDevice FAILED hr=0x%08lx\n", hr);
        return 0;
    }
    hr = pKeyboard->SetDataFormat(&c_dfDIKeyboard);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: SetDataFormat FAILED hr=0x%08lx\n", hr);
        pKeyboard->Release(); pKeyboard = nullptr; return 0;
    }
    hr = pKeyboard->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupKbd: SetCooperativeLevel FAILED hr=0x%08lx\n", hr);
        pKeyboard->Release(); pKeyboard = nullptr; return 0;
    }
    return 1;
}

int ProgableControl::setupMouse(HWND hwnd)
{
    if (!directinput) {
        log_write("ProgCtrl::SetupMouse: no directinput interface\n");
        return 0;
    }
    HRESULT hr = directinput->CreateDevice(GUID_SysMouse, &pMouse, nullptr);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: CreateDevice FAILED hr=0x%08lx\n", hr);
        return 0;
    }
    hr = pMouse->SetDataFormat(&c_dfDIMouse);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: SetDataFormat FAILED hr=0x%08lx\n", hr);
        pMouse->Release(); pMouse = nullptr; return 0;
    }
    hr = pMouse->SetCooperativeLevel(hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        log_write("ProgCtrl::SetupMouse: SetCooperativeLevel FAILED hr=0x%08lx\n", hr);
        pMouse->Release(); pMouse = nullptr; return 0;
    }
    return 1;
}

int ProgableControl::setupJoy(HWND)
{
    return 1;
}
int ProgableControl::setJoyRange(int, int, int)
{
    return 1;
}
int ProgableControl::setJoyDeadzone(DWORD, int)
{
    return 1;
}

int ProgableControl::acquireAll()
{
    if (pKeyboard) {
        HRESULT hr = pKeyboard->Acquire();
        if (FAILED(hr)) {
            log_write("ProgCtrl::AcquireAll: keyboard Acquire FAILED hr=0x%08lx\n", hr);
            return 0;
        }
    }
    if (pMouse) pMouse->Acquire();
    return 1;
}

int ProgableControl::unacquireAll()
{
    if (pKeyboard) pKeyboard->Unacquire();
    if (pMouse)    pMouse->Unacquire();
    if (pJoystick) pJoystick->Unacquire();
    return 1;
}

void ProgableControl::registerAction(unsigned short mode,
                                 const char *name, ActionCallback cb, void *ctx)
{
    if (mode >= 5) return;
    ActionEntry *e = action_tables[mode].getOrCreate(name);
    if (!e) {
        log_write("ProgCtrl::RegisterAction: HeapAlloc failed for '%s'\n", name);
        return;
    }
    e->callback = cb;
    e->context  = ctx;
}

int ProgableControl::bindKey(unsigned short mode,
                         const char *name, int sc, int strength)
{
    if (mode >= 5) return 0;
    ActionEntry *e = action_tables[mode].find(name);
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

void ProgableControl::clearBindings(unsigned short mode, const char *name)
{
    if (mode >= 5) return;
    ActionEntry *e = action_tables[mode].find(name);
    if (!e) return;
    KeyBind::freeChain(e->kbd);
    e->kbd = nullptr;
}

void ProgableControl::getBindingStr(int mode, const char *name,
                                char *buf, unsigned int bufsz)
{
    if (!buf || bufsz == 0) return;
    buf[0] = '\0';
    if (mode < 0 || mode >= 5) return;
    ActionEntry *e = action_tables[mode].find(name);
    if (!e) return;

    bool first = true;
    for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
        char keyname[MAX_PATH] = "?";
        if (pKeyboard) {
            DIDEVICEOBJECTINSTANCEA doi;
            doi.dwSize = sizeof(doi);
            HRESULT hr = pKeyboard->GetObjectInfo(&doi, (DWORD)kb->scancode, DIPH_BYOFFSET);
            if (SUCCEEDED(hr))
                strncpy(keyname, doi.tszName, sizeof(keyname) - 1);
            else
                log_write("ProgCtrl::GetBindingStr: GetObjectInfo sc=0x%02X FAILED hr=0x%08lx\n",
                          kb->scancode, hr);
        }
        if (!first) {
            size_t cur = strlen(buf), sep = strlen(sep_or);
            if (cur + sep + 1 <= bufsz)
                strncat(buf, sep_or, bufsz - cur - 1);
        }
        first = false;
        size_t cur = strlen(buf);
        if (cur + strlen(keyname) + 1 <= bufsz)
            strncat(buf, keyname, bufsz - cur - 1);
    }
}

void ProgableControl::dispatch(unsigned short game_state)
{
    gamestate_note_mode(game_state);  // before the early return, so paused and cutscene modes are seen

    BYTE ks[256];
    if (record_replaying() && !policy_in_control(clock_frame())) {
        // Replay supplies both the key array and the mode; the real keyboard
        // is not read.
        unsigned short recorded = game_state;
        if (!replay_keys(&recorded, ks)) return;
        game_state = recorded;
        if (game_state >= 5) return;
    } else if (policy_in_control(clock_frame())) {
        // The autoplay policy drives.  The watcher's own keys are merged on
        // top, so they can still steer; the recording sees the merged array,
        // so a recorded policy run replays exactly.
        if (game_state >= 5) return;

        BYTE human[256];
        memset(human, 0, sizeof(human));
        if (pKeyboard) {
            HRESULT hr = pKeyboard->GetDeviceState(256, human);
            if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
                pKeyboard->Acquire();
                hr = pKeyboard->GetDeviceState(256, human);
            }
            if (FAILED(hr)) memset(human, 0, sizeof(human));
        }

        memset(ks, 0, sizeof(ks));
        if (!policy_keys(this, game_state, ks)) {
            memcpy(ks, human, sizeof(ks));
        } else {
            for (int i = 0; i < 256; i++) ks[i] |= human[i];
        }
        record_keys(game_state, ks);
    } else {
        if (game_state >= 5 || !pKeyboard) return;

        HRESULT hr = pKeyboard->GetDeviceState(256, ks);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            pKeyboard->Acquire();
            hr = pKeyboard->GetDeviceState(256, ks);
        }
        if (FAILED(hr)) return;
        // The policy may overwrite the keys (it declines outside a level), and
        // runs before recording so a policy-driven run records like a
        // hand-played one.
        policy_keys(this, game_state, ks);
        record_keys(game_state, ks);
    }

    for (ActionEntry *e = action_tables[game_state].head; e; e = e->chain) {
        for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
            if (kb->scancode >= 0 && kb->scancode < 256 && (ks[kb->scancode] & 0x80)) {
                if (e->callback) e->callback(kb->scancode, kb->strength, e->context);
                break;
            }
        }
    }
}

int ProgableControl::captureBinding(unsigned int mode, const char *name,
                                int strength, int , int)
{
    if (mode >= 5 || !pKeyboard) return 0;

    BYTE ks[256];
    HRESULT hr = pKeyboard->GetDeviceState(256, ks);
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
        pKeyboard->Acquire();
        hr = pKeyboard->GetDeviceState(256, ks);
    }
    if (FAILED(hr)) return 0;

    for (int sc = 0; sc < 256; sc++) {
        if (ks[sc] & 0x80)
            return bindKey((unsigned short)mode, name, sc, strength);
    }
    return 0;
}

/* FORMAT: one action's bindings in ProgableControl.sav:
 *   DWORD kbd_count;  kbd_count x (DWORD scan code, DWORD strength)
 *   DWORD axis_count; axis_count x 12 bytes, skipped
 *   DWORD btn_count;  btn_count x 12 bytes, skipped */
int ProgableControl::readOrigEntryBindings(HANDLE f, int mode, ActionEntry *e)
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
        if (e) bindKey((unsigned short)mode, e->name, (int)key_id, (int)strength);
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

int ProgableControl::readOrigFormat(HANDLE f)
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
            ActionEntry *e = action_tables[m].find(namebuf);
            log_write("ProgCtrl::ReadBindings(orig):   '%s'%s\n",
                      namebuf, e ? "" : " (not registered, bindings discarded)");
            if (!readOrigEntryBindings(f, m, e)) {
                log_write("ProgCtrl::ReadBindings(orig): read error in bindings for '%s'\n", namebuf);
                return 0;
            }
        }
    }
    log_write("ProgCtrl::ReadBindings(orig): ok\n");
    return 1;
}

int ProgableControl::writeBindings()
{
    log_write("ProgCtrl::WriteBindings(this=%p) -> '%s'\n", this, SAVE_FILE);
    HANDLE f = CreateFileA(SAVE_FILE, GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("ProgCtrl::WriteBindings: CreateFile FAILED err=%lu\n", GetLastError());
        return 0;
    }
    DWORD n;
    // FORMAT: no header.  For each of the five modes:
    //   DWORD entry_count
    //   per entry: char name[256], then the bindings as read above,
    //   with axis_count and btn_count always 0.
    const DWORD zero = 0;
    for (int m = 0; m < 5; m++) {
        ActionTable *t = &action_tables[m];
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
            if (!WriteFile(f, &zero, 4, &n, nullptr)) goto fail;
            if (!WriteFile(f, &zero, 4, &n, nullptr)) goto fail;
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

int ProgableControl::readBindings()
{
    log_write("ProgCtrl::ReadBindings(this=%p) <- '%s'\n", this, SAVE_FILE);
    HANDLE f = CreateFileA(SAVE_FILE, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("ProgCtrl::ReadBindings: no save file (err=%lu)\n", GetLastError());
        return 0;
    }
    int ok = readOrigFormat(f);
    CloseHandle(f);
    if (ok) log_write("ProgCtrl::ReadBindings: done\n");
    return ok;
}

static void *const progctrl_vtable_slots[1] = {
    (void *)&ProgableControl::scalarDtor,
};
extern const void *const PROGCTRL_VTABLE = progctrl_vtable_slots;
