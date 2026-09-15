/* GAMETICK_PLAN.md Band B reopened — the menu/keypress handler.
 *
 *   Game::HandleKeypress  0x00418d20   2 E8 sites (0x00414EF6, 0x0041500A,
 *                                      both GameTick)
 *
 * __thiscall(Game*), bare RET.  Transcribed from the LISTING, including the
 * two jump tables, which were decoded from the file rather than taken from
 * the decompile's switch:
 *
 *   option-edit switch on children[node][cursor] - 0x22 (table 0x419b54 /
 *   0x419b38): 0x22 sfx %, 0x3e CD volume, 0x3f wave volume, 0x48/0x49/0x4a
 *   three 0..2 byte options; everything else falls to the common tail.
 *
 *   action switch on node - 1 (table 0x419bdc / 0x419b80): 1 new game,
 *   5 sound-flag latch, 6 quit, 0x14..0x20 key-rebind prompts, 0x21 and 0x47
 *   toggles, 0x29 continue, 0x3c 3D-sound toggle, 0x3d CD toggle (falls into
 *   the pop), and pop-only for 0x22 0x32 0x3e 0x3f 0x48 0x49 0x4a 0x50.
 *
 * Every key poll goes through hooks_GetAsyncKeyState (the original hoists
 * the IAT pointer into ESI; patch.py redirects both forms), and the POLL
 * ORDER is the listing's: each key is read BEFORE its debounce and state
 * guards are tested, so a key is sampled even when the guard fails.  The
 * trailing "discarded" polls (RIGHT, LEFT; BACKSPACE x3) are kept -- they
 * clear GetAsyncKeyState's pressed-since-last-call bit.
 *
 * The CD-volume arm's pow is _CIpow(2.0, 16.0) = 65536.0, exact in any CRT,
 * so the volume is (pct * 65536) / 100 (unsigned), clamped to 65536.  The
 * wave arm's scale is pct * 0x28f028f (655 in both 16-bit halves).
 *
 * Callbacks kept: SoundManager::SoundSetup 0x004439d0 (the sound manager is
 * not ours) and user32 PostQuitMessage (an import, not game code).
 *
 * Control: KAROO_SIM_FX=slotshift -- loading save slot k restores slot k+1.
 * Every recording loads a slot through this path, so it must fail them all
 * with real field diffs -- the same shape as levelshift.
 */
#include <windows.h>
#include <string.h>
#include "log.h"
#include "game.h"
#include "gamelog.h"
#include "menunav.h"
#include "levelsetup.h"
#include "levelparse.h"
#include "gamereset.h"
#include "keypress.h"
#include "menutree.h"
#include "textentry.h"
#include "player.h"

struct CStaticSoundbuffer;
struct VoicePool;
struct ProgableControl;
struct CDM;

extern "C" __declspec(dllexport) SHORT WINAPI hooks_GetAsyncKeyState(int vKey);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_HaltPlayback(CStaticSoundbuffer *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolCycle(VoicePool *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_SetJoyDeadzone(ProgableControl *s, DWORD axis, int zone);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
ProgCtrl_ClearBindings(ProgableControl *s, unsigned short mode, const char *name);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
ProgCtrl_CaptureBinding(ProgableControl *s, unsigned int mode, const char *name,
                        int strength, int allow_axis, int flags);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
CDM_GetMixerDetails(CDM *self);   /* bare RET */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CDM_SetMixerVolume(CDM *self, DWORD level);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CDM_StopTrack(CDM *self);

typedef void (__attribute__((thiscall)) *sound_setup_fn)(void *sm, int mode3d);
#define ORIG_SOUND_SETUP ((sound_setup_fn)0x004439d0)   /* named callback */

#define PROGCTRL    ((ProgableControl *)0x0046c298)
#define CDAUDIO     ((CDM *)0x004dc640)
#define GAMELOGGER  ((GameLogger *)0x0046c4c0)
#define F_CONTINUE  ((const char *)0x00465a3c)   /* "level completed - continue" */

#define G8(o)   (*(unsigned char *)(B + (o)))
#define G16(o)  (*(unsigned short *)(B + (o)))
#define G32(o)  (*(unsigned int *)(B + (o)))
#define GP(o)   (*(void **)(B + (o)))
#define GD(o)   (*(double *)(B + (o)))
#define KEY(k)  hooks_GetAsyncKeyState(k)

#define MENU    (((Game *)B)->menu())
#define DEB     (((Game *)B)->debounceRef())
#define NODE    (((Game *)B)->menu()->nodeRef())
#define STATE   (((Game *)B)->stateRef())

static int s_fx = -1;

/* The rebind prompts 0x14..0x20: copy the action name, arm capture, pop.
 * Four of them also set +0x175534 (the key to highlight). */
static void rebind(unsigned char *B, const char *name, unsigned char code, int hl)
{
    strcpy(((Game *)B)->rebindAction(), name);
    ((Game *)B)->setRebindActive(1);
    ((Game *)B)->setRebindCode(code);
    if (hl >= 0)
        ((Game *)B)->menu()->setLastKey((unsigned char)hl);
    Sim_PopMenuNodeFromStack(MENU);
}

/* The "level loaded, flythrough armed" tail shared by new-game and load. */
static void loaded_tail(unsigned char *B)
{
    STATE = 4;
    ((Game *)B)->setCameraDistance(7.0f);
    if (((Game *)B)->musicOn() != 0)
        CDM_StopTrack(CDAUDIO);
    ((Game *)B)->scriptPlayer()->setRunning(1);
    ((Game *)B)->setCameraMode(1);
    Sim_RewindMenuStackToRootNode(MENU);
}

static void option_edit(unsigned char *B, unsigned char key)
{
    switch (key) {
    case 0x22: {                               /* sfx % -> joystick deadzone */
        if (DEB != 0x27 && KEY(0x27) != 0 && ((Game *)B)->joyDeadzone() < 0x5a) {
            ((Game *)B)->setJoyDeadzone((unsigned short)(((Game *)B)->joyDeadzone() + 10));
            ProgCtrl_SetJoyDeadzone(PROGCTRL, 0, ((Game *)B)->joyDeadzone() * 100);
            ProgCtrl_SetJoyDeadzone(PROGCTRL, 4, ((Game *)B)->joyDeadzone() * 100);
            DEB = 0x27;
        }
        if (DEB != 0x25 && KEY(0x25) != 0 && ((Game *)B)->joyDeadzone() > 10) {
            ((Game *)B)->setJoyDeadzone((unsigned short)(((Game *)B)->joyDeadzone() - 10));
            ProgCtrl_SetJoyDeadzone(PROGCTRL, 0, ((Game *)B)->joyDeadzone() * 100);
            ProgCtrl_SetJoyDeadzone(PROGCTRL, 4, ((Game *)B)->joyDeadzone() * 100);
            DEB = 0x25;
        }
        break;
    }
    case 0x3e: {                               /* CD volume */
        int changed = 0;
        CDM_GetMixerDetails(CDAUDIO);          /* result discarded, as shipped */
        if (DEB != 0x27 && KEY(0x27) != 0 && ((Game *)B)->cdVolume() < 100) {
            DEB = 0x27;
            ((Game *)B)->setCdVolume((unsigned char)(((Game *)B)->cdVolume() + 10));
            changed = 1;
        }
        if (DEB != 0x25 && KEY(0x25) != 0 && ((Game *)B)->cdVolume() != 0) {
            DEB = 0x25;
            ((Game *)B)->setCdVolume((unsigned char)(((Game *)B)->cdVolume() - 10));
        } else if (!changed) {
            break;
        }
        {
            unsigned int v = ((unsigned int)((Game *)B)->cdVolume() * 65536u) / 100u;
            ((Game *)B)->setCdMixerVolume(v);
            if (v > 65536u)
                ((Game *)B)->setCdMixerVolume(65536u);
            CDM_SetMixerVolume(CDAUDIO, ((Game *)B)->cdMixerVolume());
        }
        break;
    }
    case 0x3f: {                               /* wave volume */
        int changed = 0;
        if (DEB != 0x27 && KEY(0x27) != 0 && ((Game *)B)->waveVolume() < 100) {
            DEB = 0x27;
            ((Game *)B)->setWaveVolume((unsigned char)(((Game *)B)->waveVolume() + 10));
            changed = 1;
        }
        if (DEB != 0x25 && KEY(0x25) != 0 && ((Game *)B)->waveVolume() != 0) {
            DEB = 0x25;
            ((Game *)B)->setWaveVolume((unsigned char)(((Game *)B)->waveVolume() - 10));
        } else if (!changed) {
            break;
        }
        ((Game *)B)->setWaveOutVolume((unsigned int)((Game *)B)->waveVolume() * 0x28f028fu);
        if (((Game *)B)->soundCreated() != 0)
            waveOutSetVolume((HWAVEOUT)0, ((Game *)B)->waveOutVolume());
        break;
    }
    case 0x48: case 0x49: case 0x4a: {         /* three 0..2 byte options */
        unsigned int f = key == 0x48 ? 0x2aa136 : key == 0x49 ? 0x2aa138 : 0x2aa139;
        if (DEB != 0x27 && KEY(0x27) != 0 && G8(f) < 2) {
            DEB = 0x27;
            G8(f) = (unsigned char)(G8(f) + 1);
        }
        if (DEB != 0x25 && KEY(0x25) != 0 && G8(f) != 0) {
            G8(f) = (unsigned char)(G8(f) - 1);
            DEB = 0x25;
        }
        break;
    }
    default:
        break;
    }
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_HandleKeypress(Game *self)
{
    unsigned char *B = (unsigned char *)self;

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "slotshift") == 0);
        if (s_fx)
            log_write("keypress: KAROO_SIM_FX=slotshift -- load restores slot k+1\n");
    }

    int entry = 0;
    if (((Game *)B)->rebindActive() == 0 && ((Game *)B)->textEntryActive() == 0) {
        if (KEY(0x1b) != 0 && DEB != 0x1b && ((Game *)B)->menu()->changed() != 0) {
            if (((Game *)B)->fixedSounds()->switchClick != NULL)
                CStatic_TriggerPlayback(((Game *)B)->fixedSounds()->switchClick, 0);
            DEB = 0x1b;
        }
        if (NODE != 5 && NODE != 3 && ((Game *)B)->menu()->childCount(NODE) > 1) {
            if (KEY(0x26) != 0 && DEB != 0x26) {
                if (((Game *)B)->fixedSounds()->menuUpDown != NULL)
                    Sim_VoicePoolCycle(((Game *)B)->fixedSounds()->menuUpDown, 0);
                DEB = 0x26;
            }
            if (KEY(0x28) != 0 && DEB != 0x28) {
                if (((Game *)B)->fixedSounds()->menuUpDown != NULL)
                    Sim_VoicePoolCycle(((Game *)B)->fixedSounds()->menuUpDown, 0);
                DEB = 0x28;
            }
        }
        if (KEY(0x0d) != 0 && DEB != 0x0d && ((Game *)B)->menu()->changed() != 0 &&
            (unsigned short)NODE == ((Game *)B)->menu()->lastNodeSeen()) {
            if (((Game *)B)->fixedSounds()->switchClick != NULL)
                CStatic_TriggerPlayback(((Game *)B)->fixedSounds()->switchClick, 0);
            DEB = 0x0d;
        }
        Sim_NavigateMenuTree(MENU, (int)(long long)((Game *)B)->lastTickTime());
        ((Game *)B)->menu()->setLastNodeSeen((unsigned short)NODE);
        entry = ((Game *)B)->textEntryActive() != 0;
    } else {
        entry = ((Game *)B)->textEntryActive() != 0;
    }

    if (entry) {                               /* save-name text entry */
        ((Game *)B)->nameEntry()->poll((unsigned int)(long long)*((Game *)B)->clock());
        if (((Game *)B)->nameEntry()->active() == 0) {
            if (((Game *)B)->nameEntry()->lastKey() == 0x0d)
                Save_WriteAllSlotFiles(((Game *)B)->saveSlots(), (const char *)(B + 0x4215f), 0x37);
            else
                memcpy(((Game *)B)->saveSlots()->slot((unsigned char)((Game *)B)->saveSlots()->editSlot()),
                       ((Game *)B)->saveSlots()->edit(), sizeof(SaveSlot));
            ((Game *)B)->setTextEntryActive(0);
            Sim_PopMenuNodeFromStack(MENU);
            ((Game *)B)->menu()->setCursor(0);
        }
    }

    if (NODE == 0) {
        ((Game *)B)->setRebindActive(0);
        if (STATE == 5 && ((Game *)B)->menu()->leave() != 0) {
            STATE = G8(0x48b13);
            ((Game *)B)->player()->setPendingMove(0);
            DEB = 0x1b;
        }
    }

    {
        unsigned char key = ((Game *)B)->menu()->child(NODE, ((Game *)B)->menu()->cursor());
        if ((unsigned int)key - 0x22 <= 0x28)
            option_edit(B, key);
    }

    KEY(0x27);
    KEY(0x25);
    if (NODE != 5 && NODE != 0x50)
        G32(0x13cc88) = 0;

    switch (NODE) {
    case 1:
        Sim_ClearGameState((Game *)B);
        Sim_OpenLevelFile((Game *)B, ((Game *)B)->levelIndex());
        Sim_SetupLevelObjects((Game *)B);
        loaded_tail(B);
        DEB = 0x0d;
        break;
    case 5:
        if (G32(0x13cc88) == 0) {
            G32(0x13cc8c) = 1;
            G32(0x13cc88) = 1;
        }
        break;
    case 6:
        Sim_PopMenuNodeFromStack(MENU);
        STATE = 7;
        if (((Game *)B)->musicOn() != 0)
            CDM_StopTrack(CDAUDIO);
        DEB = 0x0d;
        if (G32(0x0c) == 0)
            PostQuitMessage(1);
        break;
    case 0x14: rebind(B, (const char *)0x0046454c, 0x14, 0x26); break;
    case 0x15: rebind(B, (const char *)0x0046453c, 0x15, 0x28); break;
    case 0x16: rebind(B, (const char *)0x00464560, 0x16, 0x27); break;
    case 0x17: rebind(B, (const char *)0x00464570, 0x17, 0x25); break;
    case 0x18: rebind(B, (const char *)0x0046452c, 0x18, -1); break;
    case 0x19: rebind(B, (const char *)0x0046451c, 0x19, -1); break;
    case 0x1a: rebind(B, (const char *)0x00464508, 0x1a, -1); break;
    case 0x1b: rebind(B, (const char *)0x004644f8, 0x1b, -1); break;
    case 0x1c: rebind(B, (const char *)0x004644e8, 0x1c, -1); break;
    case 0x1d: rebind(B, (const char *)0x004644cc, 0x1d, -1); break;
    case 0x1e: rebind(B, (const char *)0x004644d8, 0x1e, -1); break;
    case 0x1f: rebind(B, (const char *)0x004644c0, 0x1f, -1); break;
    case 0x20: rebind(B, (const char *)0x004644b4, 0x20, -1); break;
    case 0x21:
        if (((Game *)B)->player()->fieldEa() == 0)
            ((Game *)B)->setCameraTurnsWithPlayer(((Game *)B)->cameraTurnsWithPlayer() == 0);
        Sim_PopMenuNodeFromStack(MENU);
        break;
    case 0x47:
        ((Game *)B)->config()->setField1f609(((Game *)B)->config()->field1f609() == 0);
        Sim_PopMenuNodeFromStack(MENU);
        break;
    case 0x3c:
        ((Game *)B)->setSound3D(((Game *)B)->sound3D() == 0);
        ORIG_SOUND_SETUP(B + 0x13cba8, ((Game *)B)->sound3D());
        ((Game *)B)->setLevelSoundsReady(0);
        Sim_PopMenuNodeFromStack(MENU);
        break;
    case 0x3d:
        if (((Game *)B)->musicOn() != 0) {
            ((Game *)B)->setMusicOn(0);
            CDM_StopTrack(CDAUDIO);
            Sim_PopMenuNodeFromStack(MENU);
            break;
        }
        ((Game *)B)->cdThemes()->replay();
        ((Game *)B)->setMusicOn(1);
        Sim_PopMenuNodeFromStack(MENU);
        break;
    case 0x29: {
        unsigned char lvl = (unsigned char)(((Game *)B)->levelIndex() + 1);
        ((Game *)B)->player()->setGemsCollected(0);
        ((Game *)B)->setLevelIndex(lvl);
        Sim_OpenLevelFile((Game *)B, lvl);
        Sim_SetupLevelObjects((Game *)B);
        STATE = 4;
        ((Game *)B)->scriptPlayer()->setRunning(1);
        if (((Game *)B)->musicOn() != 0)
            CDM_StopTrack(CDAUDIO);
        DEB = 0x0d;
        ((Game *)B)->menu()->setLockStart(((Game *)B)->lastTickTime());
        ((Game *)B)->setCameraMode(1);
        ((Game *)B)->menu()->setLock(1);
        Sim_PopMenuNodeFromStack(MENU);
        GameLog_LogMessage(GAMELOGGER, 1, F_CONTINUE);
        if (((Game *)B)->fixedSounds()->levelCompleted != NULL)
            CStatic_HaltPlayback(((Game *)B)->fixedSounds()->levelCompleted);
        break;
    }
    case 0x22: case 0x32: case 0x3e: case 0x3f:
    case 0x48: case 0x49: case 0x4a: case 0x50:
        Sim_PopMenuNodeFromStack(MENU);
        break;
    default:
        break;
    }

    /* save-slot LOAD nodes 200 .. 200+n-1 */
    {
        unsigned int n = ((Game *)B)->saveSlots()->count();
        unsigned int node = NODE;
        if (node >= 200 && (int)node < (int)(n + 200)) {
            unsigned char slot = (unsigned char)(node + 0x38);
            if (s_fx)
                slot = (unsigned char)(slot + 1);
            if (((Game *)B)->saveSlots()->slot(slot)->inUse != 0) {
                Sim_ClearGameState((Game *)B);
                Sim_RestoreGameStateFromSaveSlot((Game *)B, slot);
                Sim_OpenLevelFile((Game *)B, ((Game *)B)->levelIndex());
                Sim_SetupLevelObjects((Game *)B);
                loaded_tail(B);
            }
            DEB = 0x0d;
            Sim_PopMenuNodeFromStack(MENU);
        }
    }

    /* save-slot SAVE nodes 200+n .. 200+2n-1 */
    {
        unsigned int n = ((Game *)B)->saveSlots()->count();
        unsigned int node = NODE;
        if ((int)node >= (int)(n + 200) && (int)node < (int)(2 * n + 200)) {
            unsigned char slot = (unsigned char)(node - n + 0x38);
            unsigned char *rec = (unsigned char *)((Game *)B)->saveSlots()->slot(slot);
            ((Game *)B)->nameEntry()->setMaxLength(10);
            ((Game *)B)->setTextEntryActive(1);
            ((Game *)B)->nameEntry()->setActive(1);
            memcpy(((Game *)B)->saveSlots()->edit(), rec, sizeof(SaveSlot));
            ((Game *)B)->nameEntry()->setBuffer((char *)rec);
            ((Game *)B)->saveSlots()->setEditSlot(slot);
            ((Game *)B)->nameEntry()->setCursor((unsigned char)strlen((const char *)rec));
            Sim_StoreGameStateIntoSaveSlot((Game *)B, slot);
            ((Game *)B)->nameEntry()->setLastKey(0x0d);
            KEY(8);
            KEY(8);
            KEY(8);
            Sim_PopMenuNodeFromStack(MENU);
        }
    }

    /* key-rebind capture */
    if (((Game *)B)->rebindActive() != 0 && KEY(0x0d) == 0) {
        ProgCtrl_ClearBindings(PROGCTRL, 1, ((Game *)B)->rebindAction());
        if (ProgCtrl_CaptureBinding(PROGCTRL, 1, ((Game *)B)->rebindAction(),
                                    100, 10, 0) != 0)
            ((Game *)B)->setRebindActive(0);
    }
}
