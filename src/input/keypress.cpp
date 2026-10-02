/* Every key is read with hooks_GetAsyncKeyState, which the replay recorder
 * sees.
 *
 * DETERMINISM: the order and number of key polls is part of every recording.
 * Each key is polled before its debounce and state guards are tested, so it is
 * sampled even when the guard fails, and the polls whose results are discarded
 * are kept: they also clear the key's pressed-since-last-call bit.
 *
 * KAROO_SIM_FX=slotshift is a negative control: loading save slot k restores
 * slot k+1.  Every recording loads a slot through here, so it must fail them
 * all. */

#include <windows.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "windev.h"
#include "game.h"
#include "menunav.h"
#include "levelsetup.h"
#include "levelparse.h"
#include "gamereset.h"
#include "keypress.h"
#include "menutree.h"
#include "textentry.h"
#include "player.h"

#include "audiodev.h"
#include "voicepool.h"
#include "progctrl.h"
#include "cdm.h"

#include "soundmanager.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "levelselect.h"
#include "record.h"

#define KEY(k)  hooks_GetAsyncKeyState(k)

static int s_fx = -1;

/* The key-rebind prompts: copy the action name, arm key capture and pop back.
 * hl is the arrow key the rebind screen highlights, or -1 for none. */
static void rebind(Game *game, const char *name, unsigned char code, int hl)
{
    strcpy(game->rebindAction(), name);
    game->setRebindActive(1);
    game->setRebindCode(code);
    if (hl >= 0)
        game->menu()->setLastKey((unsigned char)hl);
    game->menu()->pop();
}

/* The tail shared by new game, the level select and loading a save: start the
 * level's intro flythrough. */
static void loaded_tail(Game *game)
{
    game->stateRef() = 4;
    game->setCameraDistance(7.0f);
    if (game->musicOn() != 0)
        g_cdAudio.stop();
    game->scriptPlayer()->setRunning(1);
    game->setCameraMode(1);
    game->menu()->rewind();
}

static void option_edit(Game *game, unsigned char key)
{
    switch (key) {
    case 0x22: {  // the "sfx %" page edits the joystick dead zone
        if (game->debounceRef() != 0x27 && KEY(0x27) != 0 && game->joyDeadzone() < 0x5a) {
            game->setJoyDeadzone((unsigned short)(game->joyDeadzone() + 10));
            g_progCtrl.setJoyDeadzone(0, game->joyDeadzone() * 100);
            g_progCtrl.setJoyDeadzone(4, game->joyDeadzone() * 100);
            game->debounceRef() = 0x27;
        }
        if (game->debounceRef() != 0x25 && KEY(0x25) != 0 && game->joyDeadzone() > 10) {
            game->setJoyDeadzone((unsigned short)(game->joyDeadzone() - 10));
            g_progCtrl.setJoyDeadzone(0, game->joyDeadzone() * 100);
            g_progCtrl.setJoyDeadzone(4, game->joyDeadzone() * 100);
            game->debounceRef() = 0x25;
        }
        break;
    }
    case 0x3e: {
        int changed = 0;
        g_cdAudio.getMixerDetails();  // PRESERVED: the result is discarded
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
        // PRESERVED: unsigned, and clamped after the store; 65536 is full
        // volume.
        {
            unsigned int v = ((unsigned int)game->cdVolume() * 65536u) / 100u;
            game->setCdMixerVolume(v);
            if (v > 65536u)
                game->setCdMixerVolume(65536u);
            g_cdAudio.setMixerVolume(game->cdMixerVolume());
        }
        break;
    }
    case 0x3f: {
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
        // The same percentage in both 16-bit halves: left and right channel.
        game->setWaveOutVolume((unsigned int)game->waveVolume() * 0x28f028fu);
        if (game->soundCreated() != 0)
            audiodev::setMasterVolume(game->waveOutVolume());
        break;
    }
    // The three 0..2 video-quality sliders: shadows, highlights and particles.
    // Reflection is a 0/1 toggle on its own node.
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

  void  
Sim_HandleKeypress(Game *self)
{

    if (s_fx < 0) {
        char e[32];
        DWORD n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "slotshift") == 0);
        if (s_fx)
            g_logger.write("keypress: KAROO_SIM_FX=slotshift -- load restores slot k+1\n");
    }

    int entry = 0;
    if (self->rebindActive() == 0 && self->textEntryActive() == 0) {
        if (KEY(0x1b) != 0 && self->debounceRef() != 0x1b && self->menu()->changed() != 0) {
            if (self->fixedSounds()->switchClick != NULL)
                (self->fixedSounds()->switchClick)->play(false);
            self->debounceRef() = 0x1b;
        }
        if (self->menu()->nodeRef() != 5 && self->menu()->nodeRef() != 3 && self->menu()->childCount(self->menu()->nodeRef()) > 1) {
            if (KEY(0x26) != 0 && self->debounceRef() != 0x26) {
                if (self->fixedSounds()->menuUpDown != NULL)
                    (self->fixedSounds()->menuUpDown)->cycle(0);
                self->debounceRef() = 0x26;
            }
            if (KEY(0x28) != 0 && self->debounceRef() != 0x28) {
                if (self->fixedSounds()->menuUpDown != NULL)
                    (self->fixedSounds()->menuUpDown)->cycle(0);
                self->debounceRef() = 0x28;
            }
        }
        if (KEY(0x0d) != 0 && self->debounceRef() != 0x0d && self->menu()->changed() != 0 &&
            (unsigned short)self->menu()->nodeRef() == self->menu()->lastNodeSeen()) {
            if (self->fixedSounds()->switchClick != NULL)
                (self->fixedSounds()->switchClick)->play(false);
            self->debounceRef() = 0x0d;
        }
        self->menu()->navigate((int)(long long)self->lastTickTime());
        self->menu()->setLastNodeSeen((unsigned short)self->menu()->nodeRef());
        entry = self->textEntryActive() != 0;
    } else {
        entry = self->textEntryActive() != 0;
    }

    if (entry) {  // save-name text entry
        self->nameEntry()->poll((unsigned int)(long long)*self->clock());
        if (self->nameEntry()->active() == 0) {
            if (self->nameEntry()->lastKey() == 0x0d)
                self->saveSlots()->writeAllSlotFiles(self->gameFileName(), 0x37);
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

    // DETERMINISM: the right and left polls here are discarded by the game's
    // own code.  The level select reads them instead of polling again, so the
    // recorded key stream is unchanged.  Its edges are tracked separately,
    // because the game's debounce belongs to the option pages on these keys.
    {
        static bool s_right, s_left;
        const bool right = KEY(0x27) != 0, left = KEY(0x25) != 0;
        if (self->rebindActive() == 0 && self->textEntryActive() == 0) {
            if (right && !s_right)
                LevelSelect_Turn(self, +1);
            if (left && !s_left)
                LevelSelect_Turn(self, -1);
        }
        s_right = right;
        s_left = left;
    }
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
            g_cdAudio.stop();
        self->debounceRef() = 0x0d;
        if (self->field_0c() == 0)
            windev::quit(1);
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
            g_cdAudio.stop();
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
            g_cdAudio.stop();
        self->debounceRef() = 0x0d;
        self->menu()->setLockStart(self->lastTickTime());
        self->setCameraMode(1);
        self->menu()->setLock(1);
        self->menu()->pop();
        g_logger.logMessage(1, "level completed - continue");
        if (self->fixedSounds()->levelCompleted != NULL)
            (self->fixedSounds()->levelCompleted)->stop();
        break;
    }
    case 0x22: case 0x32: case 0x3e: case 0x3f:
    case 0x48: case 0x49: case 0x4a: case 0x50:
        self->menu()->pop();
        break;
    default:
        break;
    }

    // The level select's start node: as New Game, at the chosen level.
    if (self->menu()->nodeRef() == LS_START) {
        int level = LevelSelect_Chosen(self);
        Sim_ClearGameState(self);
        self->setLevelIndex((unsigned char)level);
        Sim_OpenLevelFile(self, self->levelIndex());
        Sim_SetupLevelObjects(self);
        loaded_tail(self);
        self->debounceRef() = 0x0d;
    }

    // Load nodes, 200 .. 200+n-1.  The slot index is the node number wrapped
    // to a byte.
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

    // Save nodes, 200+n .. 200+2n-1: snapshot the game into the slot and start
    // name entry on it.
    {
        unsigned int n = self->saveSlots()->count();
        unsigned int node = self->menu()->nodeRef();
        if ((int)node >= (int)(n + 200) && (int)node < (int)(2 * n + 200)) {
            unsigned char slot = (unsigned char)(node - n + 0x38);
            SaveSlot *rec = self->saveSlots()->slot(slot);
            self->nameEntry()->setMaxLength(10);
            self->setTextEntryActive(1);
            self->nameEntry()->setActive(1);
            *self->saveSlots()->edit() = *rec;
            self->nameEntry()->setBuffer(rec->name);
            self->saveSlots()->setEditSlot(slot);
            self->nameEntry()->setCursor((unsigned char)strlen(rec->name));
            Sim_StoreGameStateIntoSaveSlot(self, slot);
            self->nameEntry()->setLastKey(0x0d);
            // PRESERVED: three discarded Backspace polls clear its pressed bit
            // before name entry starts.
            KEY(8);
            KEY(8);
            KEY(8);
            self->menu()->pop();
        }
    }

    // Key-rebind capture, once Enter is released.
    if (self->rebindActive() != 0 && KEY(0x0d) == 0) {
        g_progCtrl.clearBindings(1, self->rebindAction());
        if (g_progCtrl.captureBinding(1, self->rebindAction(),
                                    100, 10, 0) != 0)
            self->setRebindActive(0);
    }
}
