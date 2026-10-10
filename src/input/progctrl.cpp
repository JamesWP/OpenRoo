#include <stdio.h>
#include <stdint.h>
#include <fstream>
#include <string>
#include <vector>
#include <new>
#include <string.h>
#include "progctrl.h"
#include "logger.h"
#include "gamestate.h"
#include "record.h"
#include "clock.h"
#include "gamestr.h"
#include "ini.h"
#include "sysdev.h"
#include <algorithm>
#include <iterator>



ActionEntry *ActionTable::find(const char *name)
{
    for (ActionEntry *e = head; e; e = e->chain)
        if (!sysdev::compareNoCase(e->name, name)) return e;
    return nullptr;
}

ActionEntry *ActionTable::getOrCreate(const char *name)
{
    ActionEntry **tail = &head;
    for (ActionEntry *e = head; e; e = e->chain) {
        if (!sysdev::compareNoCase(e->name, name)) return e;
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
        char keyname[64] = "?";
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

    uint8_t ks[inputdev::KEY_COUNT];
    if (record_replaying()) {
        // Replay supplies both the key array and the mode; the real keyboard
        // is not read.
        unsigned short recorded = game_state;
        if (!replay_keys(&recorded, ks)) return;
        game_state = recorded;
        if (game_state >= 5) return;
    } else {
        if (game_state >= 5 || !devices_.readKeyboard(ks)) return;
        record_keys(game_state, ks);
    }

    for (ActionEntry *e = action_tables[game_state].head; e; e = e->chain) {
        for (KeyBind *kb = e->kbd; kb; kb = kb->next) {
            if (kb->scancode >= 0 && kb->scancode < inputdev::KEY_COUNT && (ks[kb->scancode] & 0x80)) {
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

    uint8_t ks[inputdev::KEY_COUNT];
    if (!devices_.readKeyboard(ks)) return 0;

    for (int sc = 1; sc < inputdev::KEY_COUNT; sc++) {
        if (ks[sc] & 0x80)
            return bindKey((unsigned short)mode, name, sc, strength);
    }
    return 0;
}

/* FORMAT: the bindings are the [keys.<mode>] sections of openroo.ini, a line
 * per action:
 *     Move_Forward = Up | W
 *     Zoom_In = A@50
 * Keys are SDL's scancode names, joined with " | "; "@n" after a name is the
 * binding's strength (100 when absent).  An action missing from the file keeps
 * its default; one with nothing after the "=" has no key. */
static std::string section_name(int mode)
{
    return "keys." + std::to_string(mode);
}

/* Splits a binding list into (key name, strength) pairs. */
static std::vector<std::pair<std::string, int> > parse_binding_list(const std::string &text)
{
    std::vector<std::pair<std::string, int> > out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t bar = text.find('|', pos);
        std::string item = text.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
        pos = bar == std::string::npos ? text.size() + 1 : bar + 1;

        size_t a = item.find_first_not_of(" \t"), b = item.find_last_not_of(" \t");
        if (a == std::string::npos) continue;
        item = item.substr(a, b - a + 1);

        int strength = 100;
        size_t at = item.rfind('@');
        if (at != std::string::npos && at + 1 < item.size()
            && item.find_first_not_of("0123456789", at + 1) == std::string::npos) {
            strength = atoi(item.c_str() + at + 1);
            item = item.substr(0, at);
            while (!item.empty() && item.back() == ' ') item.pop_back();
        }
        out.push_back(std::make_pair(item, strength));
    }
    return out;
}

int ProgableControl::readBindings()
{
    g_logger.write("ProgCtrl::ReadBindings <- '%s'\n", GS_CFG_FILE);
    IniFile ini;
    if (!ini.load(GS_CFG_FILE)) {
        g_logger.write("ProgCtrl::ReadBindings: no settings file\n");
        return 0;
    }
    int found = 0;
    for (int m = 0; m < 5; m++) {
        const std::string section = section_name(m);
        for (const auto &kv : ini.entries(section.c_str())) {
            ActionEntry *e = action_tables[m].find(kv.first.c_str());
            if (!e) {
                g_logger.write("ProgCtrl::ReadBindings: mode %d: '%s' is not an action\n",
                          m, kv.first.c_str());
                continue;
            }
            found++;
            clearBindings((unsigned short)m, e->name);
            for (const auto &key : parse_binding_list(kv.second)) {
                int id = inputdev::keyFromName(key.first.c_str());
                if (id == inputdev::KEY_NONE) {
                    g_logger.write("ProgCtrl::ReadBindings: '%s': unknown key '%s'\n",
                              e->name, key.first.c_str());
                    continue;
                }
                bindKey((unsigned short)m, e->name, id, key.second);
            }
        }
    }
    g_logger.write("ProgCtrl::ReadBindings: %d action(s) set\n", found);
    return found > 0;
}

int ProgableControl::writeBindings()
{
    g_logger.write("ProgCtrl::WriteBindings -> '%s'\n", GS_CFG_FILE);
    IniFile ini;
    ini.load(GS_CFG_FILE);  // keep the settings; a missing file starts empty
    for (int m = 0; m < 5; m++) {
        const std::string section = section_name(m);
        ini.removeSection(section.c_str());
        for (ActionEntry *e = action_tables[m].head; e; e = e->chain) {
            // The chain is newest first; the file lists them oldest first.
            std::vector<KeyBind *> binds;
            for (KeyBind *kb = e->kbd; kb; kb = kb->next) binds.push_back(kb);
            std::string text;
            for (size_t i = binds.size(); i-- > 0;) {
                char name[64];
                if (!devices_.keyName(binds[i]->scancode, name, sizeof(name))) continue;
                if (!text.empty()) text += " | ";
                text += name;
                if (binds[i]->strength != 100) text += "@" + std::to_string(binds[i]->strength);
            }
            ini.set(section.c_str(), e->name, text);
        }
    }
    if (!ini.save(GS_CFG_FILE)) {
        g_logger.write("ProgCtrl::WriteBindings: write error\n");
        return 0;
    }
    g_logger.write("ProgCtrl::WriteBindings: ok\n");
    return 1;
}

ProgableControl g_progCtrl;
