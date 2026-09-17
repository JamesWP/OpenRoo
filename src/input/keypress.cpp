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

#include "static.h"
#include "voicepool.h"
#include "progctrl.h"
#include "cdm.h"


#include "soundmanager.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "record.h"


#define KEY(k)  hooks_GetAsyncKeyState(k)


static int s_fx = -1;

/* The rebind prompts 0x14..0x20: copy the action name, arm capture, pop.
 * Four of them also set +0x175534 (the key to highlight). */
static void rebind(Game *game, const char *name, unsigned char code, int hl)
{
    strcpy(game->rebindAction(), name);
    game->setRebindActive(1);
    game->setRebindCode(code);
    if (hl >= 0)
        game->menu()->setLastKey((unsigned char)hl);
    game->menu()->pop();
}

/* The "level loaded, flythrough armed" tail shared by new-game and load. */
static void loaded_tail(Game *game)
{
    game->stateRef() = 4;
    game->setCameraDistance(7.0f);
    if (game->musicOn() != 0)
        CDM_StopTrack(GG_CDAUDIO);
    game->scriptPlayer()->setRunning(1);
    game->setCameraMode(1);
    game->menu()->rewind();
}

static void option_edit(Game *game, unsigned char key)
{
    switch (key) {
    case 0x22: {                               /* sfx % -> joystick deadzone */
        if (game->debounceRef() != 0x27 && KEY(0x27) != 0 && game->joyDeadzone() < 0x5a) {
            game->setJoyDeadzone((unsigned short)(game->joyDeadzone() + 10));
            ProgCtrl_SetJoyDeadzone(GG_PROGCTRL, 0, game->joyDeadzone() * 100);
            ProgCtrl_SetJoyDeadzone(GG_PROGCTRL, 4, game->joyDeadzone() * 100);
            game->debounceRef() = 0x27;
        }
        if (game->debounceRef() != 0x25 && KEY(0x25) != 0 && game->joyDeadzone() > 10) {
            game->setJoyDeadzone((unsigned short)(game->joyDeadzone() - 10));
            ProgCtrl_SetJoyDeadzone(GG_PROGCTRL, 0, game->joyDeadzone() * 100);
            ProgCtrl_SetJoyDeadzone(GG_PROGCTRL, 4, game->joyDeadzone() * 100);
            game->debounceRef() = 0x25;
        }
        break;
    }
    case 0x3e: {                               /* CD volume */
        int changed = 0;
        CDM_GetMixerDetails(GG_CDAUDIO);          /* result discarded, as shipped */
        if (game->debounceRef() != 0x27 && KEY(0x27) != 0 && game->cdVolume() < 100) {
            game->debounceRef() = 0x27;
            game->setCdVolume((unsigned char)(game->cdVolume() + 10));
            changed = 1;
        }
        if (game->debounceRef() != 0x25 && KEY(0x25) != 0 && game->cdVolume() != 0) {
            game->debounceRef() = 0x25;
            game->setCdVolume((unsigned char)(game->cdVolume() - 10));
        } else if (!changed) {
            break;
        }
        {
            unsigned int v = ((unsigned int)game->cdVolume() * 65536u) / 100u;
            game->setCdMixerVolume(v);
            if (v > 65536u)
                game->setCdMixerVolume(65536u);
            CDM_SetMixerVolume(GG_CDAUDIO, game->cdMixerVolume());
        }
        break;
    }
    case 0x3f: {                               /* wave volume */
        int changed = 0;
        if (game->debounceRef() != 0x27 && KEY(0x27) != 0 && game->waveVolume() < 100) {
            game->debounceRef() = 0x27;
            game->setWaveVolume((unsigned char)(game->waveVolume() + 10));
            changed = 1;
        }
        if (game->debounceRef() != 0x25 && KEY(0x25) != 0 && game->waveVolume() != 0) {
            game->debounceRef() = 0x25;
            game->setWaveVolume((unsigned char)(game->waveVolume() - 10));
        } else if (!changed) {
            break;
        }
        game->setWaveOutVolume((unsigned int)game->waveVolume() * 0x28f028fu);
        if (game->soundCreated() != 0)
            waveOutSetVolume((HWAVEOUT)0, game->waveOutVolume());
        break;
    }
    /* The three 0..2 video-quality sliders.  The original picks the byte
     * with a computed offset (0x2aa136 / 0x2aa138 / 0x2aa139) and shares one
     * body; the key already distinguishes them, so the choice is three named
     * options instead -- same three bytes, same order.  The skipped
     * +0x2aa137 is Reflection, a 0/1 toggle on its own key (below). */
    case 0x48: case 0x49: case 0x4a: {
        unsigned char &opt = key == 0x48 ? game->videoShadows()
                           : key == 0x49 ? game->videoHighlights()
                                         : game->videoParticles();
        if (game->debounceRef() != 0x27 && KEY(0x27) != 0 && opt < 2) {
            game->debounceRef() = 0x27;
            opt = (unsigned char)(opt + 1);
        }
        if (game->debounceRef() != 0x25 && KEY(0x25) != 0 && opt != 0) {
            opt = (unsigned char)(opt - 1);
            game->debounceRef() = 0x25;
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

    if (s_fx < 0) {
        char e[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "slotshift") == 0);
        if (s_fx)
            log_write("keypress: KAROO_SIM_FX=slotshift -- load restores slot k+1\n");
    }

    int entry = 0;
    if (self->rebindActive() == 0 && self->textEntryActive() == 0) {
        if (KEY(0x1b) != 0 && self->debounceRef() != 0x1b && self->menu()->changed() != 0) {
            if (self->fixedSounds()->switchClick != NULL)
                CStatic_TriggerPlayback(self->fixedSounds()->switchClick, 0);
            self->debounceRef() = 0x1b;
        }
        if (self->menu()->nodeRef() != 5 && self->menu()->nodeRef() != 3 && self->menu()->childCount(self->menu()->nodeRef()) > 1) {
            if (KEY(0x26) != 0 && self->debounceRef() != 0x26) {
                if (self->fixedSounds()->menuUpDown != NULL)
                    Sim_VoicePoolCycle(self->fixedSounds()->menuUpDown, 0);
                self->debounceRef() = 0x26;
            }
            if (KEY(0x28) != 0 && self->debounceRef() != 0x28) {
                if (self->fixedSounds()->menuUpDown != NULL)
                    Sim_VoicePoolCycle(self->fixedSounds()->menuUpDown, 0);
                self->debounceRef() = 0x28;
            }
        }
        if (KEY(0x0d) != 0 && self->debounceRef() != 0x0d && self->menu()->changed() != 0 &&
            (unsigned short)self->menu()->nodeRef() == self->menu()->lastNodeSeen()) {
            if (self->fixedSounds()->switchClick != NULL)
                CStatic_TriggerPlayback(self->fixedSounds()->switchClick, 0);
            self->debounceRef() = 0x0d;
        }
        self->menu()->navigate((int)(long long)self->lastTickTime());
        self->menu()->setLastNodeSeen((unsigned short)self->menu()->nodeRef());
        entry = self->textEntryActive() != 0;
    } else {
        entry = self->textEntryActive() != 0;
    }

    if (entry) {                               /* save-name text entry */
        self->nameEntry()->poll((unsigned int)(long long)*self->clock());
        if (self->nameEntry()->active() == 0) {
            if (self->nameEntry()->lastKey() == 0x0d)
                Save_WriteAllSlotFiles(self->saveSlots(), self->gameFileName(), 0x37);
            else
                memcpy(self->saveSlots()->slot((unsigned char)self->saveSlots()->editSlot()),
                       self->saveSlots()->edit(), sizeof(SaveSlot));
            self->setTextEntryActive(0);
            self->menu()->pop();
            self->menu()->setCursor(0);
        }
    }

    if (self->menu()->nodeRef() == 0) {
        self->setRebindActive(0);
        if (self->stateRef() == 5 && self->menu()->leave() != 0) {
            self->stateRef() = self->stateBeforeMenu();
            self->player()->setPendingMove(0);
            self->debounceRef() = 0x1b;
        }
    }

    {
        unsigned char key = self->menu()->child(self->menu()->nodeRef(), self->menu()->cursor());
        if ((unsigned int)key - 0x22 <= 0x28)
            option_edit(self, key);
    }

    KEY(0x27);
    KEY(0x25);
    if (self->menu()->nodeRef() != 5 && self->menu()->nodeRef() != 0x50)
        self->setField13cc88(0);

    switch (self->menu()->nodeRef()) {
    case 1:
        Sim_ClearGameState(self);
        Sim_OpenLevelFile(self, self->levelIndex());
        Sim_SetupLevelObjects(self);
        loaded_tail(self);
        self->debounceRef() = 0x0d;
        break;
    case 5:
        if (self->field_13cc88() == 0) {
            self->setField13cc8c(1);
            self->setField13cc88(1);
        }
        break;
    case 6:
        self->menu()->pop();
        self->stateRef() = 7;
        if (self->musicOn() != 0)
            CDM_StopTrack(GG_CDAUDIO);
        self->debounceRef() = 0x0d;
        if (self->field_0c() == 0)
            PostQuitMessage(1);
        break;
    case 0x14: rebind(self, GS_KEY_MOVE_FORWARD, 0x14, 0x26); break;
    case 0x15: rebind(self, GS_KEY_MOVE_BACK, 0x15, 0x28); break;
    case 0x16: rebind(self, GS_KEY_TURN_RIGHT, 0x16, 0x27); break;
    case 0x17: rebind(self, GS_KEY_TURN_LEFT, 0x17, 0x25); break;
    case 0x18: rebind(self, GS_KEY_ZOOM_IN, 0x18, -1); break;
    case 0x19: rebind(self, GS_KEY_ZOOM_OUT, 0x19, -1); break;
    case 0x1a: rebind(self, GS_KEY_RELEASE_BOMB, 0x1a, -1); break;
    case 0x1b: rebind(self, GS_KEY_HARAKIRI, 0x1b, -1); break;
    case 0x1c: rebind(self, GS_KEY_OVERVIEW, 0x1c, -1); break;
    case 0x1d: rebind(self, GS_KEY_CAM_MODE_LEFT, 0x1d, -1); break;
    case 0x1e: rebind(self, GS_KEY_CAM_MODE_RIGHT, 0x1e, -1); break;
    case 0x1f: rebind(self, GS_KEY_CAM_MODE_UP, 0x1f, -1); break;
    case 0x20: rebind(self, GS_KEY_CAM_MODE_DOWN, 0x20, -1); break;
    case 0x21:
        if (self->player()->gliding() == 0)
            self->setCameraTurnsWithPlayer(self->cameraTurnsWithPlayer() == 0);
        self->menu()->pop();
        break;
    case 0x47:
        self->videoReflection() = (self->videoReflection() == 0);
        self->menu()->pop();
        break;
    case 0x3c:
        self->setSound3D(self->sound3D() == 0);
        self->soundManager()->setup(self->sound3D());
        self->setLevelSoundsReady(0);
        self->menu()->pop();
        break;
    case 0x3d:
        if (self->musicOn() != 0) {
            self->setMusicOn(0);
            CDM_StopTrack(GG_CDAUDIO);
            self->menu()->pop();
            break;
        }
        self->cdThemes()->replay();
        self->setMusicOn(1);
        self->menu()->pop();
        break;
    case 0x29: {
        unsigned char lvl = (unsigned char)(self->levelIndex() + 1);
        self->player()->setGemsCollected(0);
        self->setLevelIndex(lvl);
        Sim_OpenLevelFile(self, lvl);
        Sim_SetupLevelObjects(self);
        self->stateRef() = 4;
        self->scriptPlayer()->setRunning(1);
        if (self->musicOn() != 0)
            CDM_StopTrack(GG_CDAUDIO);
        self->debounceRef() = 0x0d;
        self->menu()->setLockStart(self->lastTickTime());
        self->setCameraMode(1);
        self->menu()->setLock(1);
        self->menu()->pop();
        GameLog_LogMessage(GG_LOGGER, 1, GS_GAME_LEVEL_DONE_CONTINUE);
        if (self->fixedSounds()->levelCompleted != NULL)
            CStatic_HaltPlayback(self->fixedSounds()->levelCompleted);
        break;
    }
    case 0x22: case 0x32: case 0x3e: case 0x3f:
    case 0x48: case 0x49: case 0x4a: case 0x50:
        self->menu()->pop();
        break;
    default:
        break;
    }

    /* save-slot LOAD nodes 200 .. 200+n-1 */
    {
        unsigned int n = self->saveSlots()->count();
        unsigned int node = self->menu()->nodeRef();
        if (node >= 200 && (int)node < (int)(n + 200)) {
            unsigned char slot = (unsigned char)(node + 0x38);
            if (s_fx)
                slot = (unsigned char)(slot + 1);
            if (self->saveSlots()->slot(slot)->inUse != 0) {
                Sim_ClearGameState(self);
                Sim_RestoreGameStateFromSaveSlot(self, slot);
                Sim_OpenLevelFile(self, self->levelIndex());
                Sim_SetupLevelObjects(self);
                loaded_tail(self);
            }
            self->debounceRef() = 0x0d;
            self->menu()->pop();
        }
    }

    /* save-slot SAVE nodes 200+n .. 200+2n-1 */
    {
        unsigned int n = self->saveSlots()->count();
        unsigned int node = self->menu()->nodeRef();
        if ((int)node >= (int)(n + 200) && (int)node < (int)(2 * n + 200)) {
            unsigned char slot = (unsigned char)(node - n + 0x38);
            unsigned char *rec = (unsigned char *)self->saveSlots()->slot(slot);
            self->nameEntry()->setMaxLength(10);
            self->setTextEntryActive(1);
            self->nameEntry()->setActive(1);
            memcpy(self->saveSlots()->edit(), rec, sizeof(SaveSlot));
            self->nameEntry()->setBuffer((char *)rec);
            self->saveSlots()->setEditSlot(slot);
            self->nameEntry()->setCursor((unsigned char)strlen((const char *)rec));
            Sim_StoreGameStateIntoSaveSlot(self, slot);
            self->nameEntry()->setLastKey(0x0d);
            KEY(8);
            KEY(8);
            KEY(8);
            self->menu()->pop();
        }
    }

    /* key-rebind capture */
    if (self->rebindActive() != 0 && KEY(0x0d) == 0) {
        ProgCtrl_ClearBindings(GG_PROGCTRL, 1, self->rebindAction());
        if (ProgCtrl_CaptureBinding(GG_PROGCTRL, 1, self->rebindAction(),
                                    100, 10, 0) != 0)
            self->setRebindActive(0);
    }
}
