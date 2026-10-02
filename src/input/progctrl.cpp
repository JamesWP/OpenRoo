#include <windows.h>
#include <stdint.h>
#include <errno.h>
#include <new>
#include <string.h>
#include <stdio.h>
#include "progctrl.h"
#include "logger.h"
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
    ActionEntry *e = new (std::nothrow) ActionEntry();
    if (!e) return nullptr;
    strncpy(e->name, name, 255);
    *tail = e;  // appended, so the file keeps registration order
    entry_count++;
    return e;
}

void KeyBind::freeChain(KeyBind *kb)
{
    while (kb) { KeyBind *n = kb->next; delete kb; kb = n; }
}

void ActionTable::freeAll()
{
    ActionEntry *e = head;
    while (e) {
        ActionEntry *n = e->chain;
        KeyBind::freeChain(e->kbd);
        delete e;
        e = n;
    }
    head        = nullptr;
    entry_count = 0;
}

ProgableControl::ProgableControl()
{
    strncpy(sep_or, " or ", sizeof(sep_or) - 1);
    for (int i = 0; i < 5; i++) {
        action_tables[i].head        = nullptr;
        action_tables[i].entry_count = 0;
    }
}

ProgableControl::~ProgableControl()
{
    for (int i = 0; i < 5; i++) action_tables[i].freeAll();
}

void ProgableControl::shutdown()
{
    devices_.destroy();
}

int ProgableControl::setupDevices(void *window)
{
    return devices_.create(window) ? 1 : 0;
}

int ProgableControl::setJoyRange(int axis, int lo, int hi)
{
    return devices_.setControllerRange(axis, lo, hi);
}

int ProgableControl::setJoyDeadzone(uint32_t axis, int zone)
{
    return devices_.setControllerDeadzone((int)axis, zone);
}

int ProgableControl::acquireAll()
{
    return devices_.acquire() ? 1 : 0;
}

int ProgableControl::unacquireAll()
{
    devices_.unacquire();
    return 1;
}

void ProgableControl::registerAction(unsigned short mode,
                                 const char *name, ActionCallback cb, void *ctx)
{
    if (mode >= 5) return;
    ActionEntry *e = action_tables[mode].getOrCreate(name);
    if (!e) {
        g_logger.write("ProgCtrl::RegisterAction: allocation failed for '%s'\n", name);
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
    KeyBind *kb = new (std::nothrow) KeyBind();
    if (!kb) { g_logger.write("ProgCtrl::BindKey: allocation failed\n"); return 0; }
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
        devices_.keyName(kb->scancode, keyname, sizeof(keyname));
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

    uint8_t ks[256];
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

        uint8_t human[256];
        memset(human, 0, sizeof(human));
        if (!devices_.readKeyboard(human)) memset(human, 0, sizeof(human));

        memset(ks, 0, sizeof(ks));
        if (!policy_keys(this, game_state, ks)) {
            memcpy(ks, human, sizeof(ks));
        } else {
            for (int i = 0; i < 256; i++) ks[i] |= human[i];
        }
        record_keys(game_state, ks);
    } else {
        if (game_state >= 5 || !devices_.readKeyboard(ks)) return;
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
    if (mode >= 5) return 0;

    uint8_t ks[256];
    if (!devices_.readKeyboard(ks)) return 0;

    for (int sc = 0; sc < 256; sc++) {
        if (ks[sc] & 0x80)
            return bindKey((unsigned short)mode, name, sc, strength);
    }
    return 0;
}

/* FORMAT: one action's bindings in ProgableControl.sav:
 *   uint32_t kbd_count;  kbd_count x (uint32_t scan code, uint32_t strength)
 *   uint32_t axis_count; axis_count x 12 bytes, skipped
 *   uint32_t btn_count;  btn_count x 12 bytes, skipped */
int ProgableControl::readOrigEntryBindings(FILE *f, int mode, ActionEntry *e)
{
    uint32_t kbd_count;
    if (fread(&kbd_count, 1, 4, f) != 4) return 0;
    g_logger.write("ProgCtrl::ReadBindings(orig):     kbd_count=%lu\n", kbd_count);
    for (uint32_t ki = 0; ki < kbd_count; ki++) {
        uint32_t key_id, strength;
        if (fread(&key_id, 1, 4, f) != 4) return 0;
        if (fread(&strength, 1, 4, f) != 4) return 0;
        g_logger.write("ProgCtrl::ReadBindings(orig):       key=0x%02lX strength=%lu -> %s\n",
                  key_id, strength, e ? "applied" : "skipped");
        if (e) bindKey((unsigned short)mode, e->name, (int)key_id, (int)strength);
    }

    uint32_t axis_count;
    if (fread(&axis_count, 1, 4, f) != 4) return 0;
    g_logger.write("ProgCtrl::ReadBindings(orig):     axis_count=%lu (skipped)\n", axis_count);
    for (uint32_t ai = 0; ai < axis_count; ai++) {
        uint8_t discard[12];
        if (fread(discard, 1, 12, f) != 12) return 0;
    }

    uint32_t btn_count;
    if (fread(&btn_count, 1, 4, f) != 4) return 0;
    g_logger.write("ProgCtrl::ReadBindings(orig):     btn_count=%lu (skipped)\n", btn_count);
    for (uint32_t bi = 0; bi < btn_count; bi++) {
        uint8_t discard[12];
        if (fread(discard, 1, 12, f) != 12) return 0;
    }

    return 1;
}

int ProgableControl::readOrigFormat(FILE *f)
{
    g_logger.write("ProgCtrl::ReadBindings: reading\n");
    for (int m = 0; m < 5; m++) {
        uint32_t cnt;
        if (fread(&cnt, 1, 4, f) != 4) {
            g_logger.write("ProgCtrl::ReadBindings(orig): read error on entry count for mode %d\n", m);
            return 0;
        }
        g_logger.write("ProgCtrl::ReadBindings(orig): mode %d: %lu entries\n", m, cnt);
        for (uint32_t ei = 0; ei < cnt; ei++) {
            char namebuf[256] = {};
            if (fread(namebuf, 1, 256, f) != 256) {
                g_logger.write("ProgCtrl::ReadBindings(orig): read error on name\n");
                return 0;
            }
            ActionEntry *e = action_tables[m].find(namebuf);
            g_logger.write("ProgCtrl::ReadBindings(orig):   '%s'%s\n",
                      namebuf, e ? "" : " (not registered, bindings discarded)");
            if (!readOrigEntryBindings(f, m, e)) {
                g_logger.write("ProgCtrl::ReadBindings(orig): read error in bindings for '%s'\n", namebuf);
                return 0;
            }
        }
    }
    g_logger.write("ProgCtrl::ReadBindings(orig): ok\n");
    return 1;
}

int ProgableControl::writeBindings()
{
    g_logger.write("ProgCtrl::WriteBindings(this=%p) -> '%s'\n", this, SAVE_FILE);
    FILE *f = fopen(SAVE_FILE, "wb");
    if (!f) {
        g_logger.write("ProgCtrl::WriteBindings: fopen FAILED errno=%d\n", errno);
        return 0;
    }
    // FORMAT: no header.  For each of the five modes:
    //   uint32_t entry_count
    //   per entry: char name[256], then the bindings as read above,
    //   with axis_count and btn_count always 0.
    const uint32_t zero = 0;
    for (int m = 0; m < 5; m++) {
        ActionTable *t = &action_tables[m];
        uint32_t cnt = t->entry_count;
        g_logger.write("ProgCtrl::WriteBindings: mode %d: %lu entries\n", m, cnt);
        if (fwrite(&cnt, 1, 4, f) != 4) goto fail;
        for (ActionEntry *e = t->head; e; e = e->chain) {
            char namebuf[256] = {};
            strncpy(namebuf, e->name, 255);
            if (fwrite(namebuf, 1, 256, f) != 256) goto fail;
            uint32_t kc = 0;
            for (KeyBind *kb = e->kbd; kb; kb = kb->next) kc++;
            if (fwrite(&kc, 1, 4, f) != 4) goto fail;
            g_logger.write("ProgCtrl::WriteBindings:   '%s': %lu binding(s)\n", namebuf, kc);
            for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
                uint32_t key_id   = (uint32_t)kb->scancode;
                uint32_t strength = (uint32_t)kb->strength;
                g_logger.write("ProgCtrl::WriteBindings:     sc=0x%02lX strength=%lu\n", key_id, strength);
                if (fwrite(&key_id, 1, 4, f) != 4) goto fail;
                if (fwrite(&strength, 1, 4, f) != 4) goto fail;
            }
            if (fwrite(&zero, 1, 4, f) != 4) goto fail;
            if (fwrite(&zero, 1, 4, f) != 4) goto fail;
        }
    }
    fclose(f);
    g_logger.write("ProgCtrl::WriteBindings: ok\n");
    return 1;
fail:
    g_logger.write("ProgCtrl::WriteBindings: write error\n");
    fclose(f);
    return 0;
}

int ProgableControl::readBindings()
{
    g_logger.write("ProgCtrl::ReadBindings(this=%p) <- '%s'\n", this, SAVE_FILE);
    FILE *f = fopen(SAVE_FILE, "rb");
    if (!f) {
        g_logger.write("ProgCtrl::ReadBindings: no save file (errno=%d)\n", errno);
        return 0;
    }
    int ok = readOrigFormat(f);
    fclose(f);
    if (ok) g_logger.write("ProgCtrl::ReadBindings: done\n");
    return ok;
}
