/* Every key is read with input_key_down, which the replay recorder
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

#include "inputdev.h"
#include <stdint.h>
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
#include "levelselect.h"
#include "record.h"


static int s_fx = -1;

/* The key-rebind prompts: copy the action name, arm key capture and pop back.
 * hl is the arrow key the rebind screen highlights, or -1 for none. */
static void rebind(Game *game, const char *name, unsigned char code, int hl)
{
    strcpy(game->frontEnd()->rebindAction(), name);
    game->frontEnd()->setRebindActive(1);
    game->frontEnd()->setRebindCode(code);
    if (hl >= 0)
        game->frontEnd()->menu()->setLastKey((unsigned char)hl);
    game->frontEnd()->menu()->pop();
}

/* The tail shared by new game, the level select and loading a save: start the
 * level's intro flythrough. */
static void loaded_tail(Game *game)
{
    game->setState(GameState::Flythrough);
    game->camera()->setCameraDistance(7.0f);
    if (game->musicOn() != 0)
        game->cdAudio()->stop();
    game->scriptPlayer()->setRunning(1);
    game->camera()->setCameraMode(1);
    game->frontEnd()->menu()->rewind();
}

static void option_edit(Game *game, unsigned char key, ProgableControl *progCtrl)
{
    switch (key) {
    case 0x22: {  // the "sfx %" page edits the joystick dead zone
        if (game->frontEnd()->debounceRef() != inputdev::KEY_RIGHT && input_key_down(inputdev::KEY_RIGHT) != 0 && game->joyDeadzone() < 0x5a) {
            game->setJoyDeadzone((unsigned short)(game->joyDeadzone() + 10));
            progCtrl->setJoyDeadzone(0, game->joyDeadzone() * 100);
            progCtrl->setJoyDeadzone(4, game->joyDeadzone() * 100);
            game->frontEnd()->debounceRef() = inputdev::KEY_RIGHT;
        }
        if (game->frontEnd()->debounceRef() != inputdev::KEY_LEFT && input_key_down(inputdev::KEY_LEFT) != 0 && game->joyDeadzone() > 10) {
            game->setJoyDeadzone((unsigned short)(game->joyDeadzone() - 10));
            progCtrl->setJoyDeadzone(0, game->joyDeadzone() * 100);
            progCtrl->setJoyDeadzone(4, game->joyDeadzone() * 100);
            game->frontEnd()->debounceRef() = inputdev::KEY_LEFT;
        }
        break;
    }
    case 0x3e: {
        int changed = 0;
        if (game->frontEnd()->debounceRef() != inputdev::KEY_RIGHT && input_key_down(inputdev::KEY_RIGHT) != 0 && game->cdVolume() < 100) {
            game->frontEnd()->debounceRef() = inputdev::KEY_RIGHT;
            game->setCdVolume((unsigned char)(game->cdVolume() + 10));
            changed = 1;
        }
        if (game->frontEnd()->debounceRef() != inputdev::KEY_LEFT && input_key_down(inputdev::KEY_LEFT) != 0 && game->cdVolume() != 0) {
            game->frontEnd()->debounceRef() = inputdev::KEY_LEFT;
            game->setCdVolume((unsigned char)(game->cdVolume() - 10));
        } else if (!changed) {
            break;
        }
        game->cdAudio()->setVolume(game->musicGain());
        break;
    }
    case 0x3f: {
        int changed = 0;
        if (game->frontEnd()->debounceRef() != inputdev::KEY_RIGHT && input_key_down(inputdev::KEY_RIGHT) != 0 && game->waveVolume() < 100) {
            game->frontEnd()->debounceRef() = inputdev::KEY_RIGHT;
            game->setWaveVolume((unsigned char)(game->waveVolume() + 10));
            changed = 1;
        }
        if (game->frontEnd()->debounceRef() != inputdev::KEY_LEFT && input_key_down(inputdev::KEY_LEFT) != 0 && game->waveVolume() != 0) {
            game->frontEnd()->debounceRef() = inputdev::KEY_LEFT;
            game->setWaveVolume((unsigned char)(game->waveVolume() - 10));
        } else if (!changed) {
            break;
        }
        if (game->soundCreated() != 0)
            audiodev::setEffectsVolume(game->effectsGain());
        break;
    }
    // The three 0..2 video-quality sliders: shadows, highlights and particles.
    // Reflection is a 0/1 toggle on its own node.
    case 0x48: case 0x49: case 0x4a: {
        unsigned char &opt = key == 0x48 ? game->videoShadows()
                           : key == 0x49 ? game->videoHighlights()
                                         : game->videoParticles();
        if (game->frontEnd()->debounceRef() != inputdev::KEY_RIGHT && input_key_down(inputdev::KEY_RIGHT) != 0 && opt < 2) {
            game->frontEnd()->debounceRef() = inputdev::KEY_RIGHT;
            opt = (unsigned char)(opt + 1);
        }
        if (game->frontEnd()->debounceRef() != inputdev::KEY_LEFT && input_key_down(inputdev::KEY_LEFT) != 0 && opt != 0) {
            opt = (unsigned char)(opt - 1);
            game->frontEnd()->debounceRef() = inputdev::KEY_LEFT;
        }
        break;
    }
    default:
        break;
    }
}

  void  
Flow_HandleKeypress(Game *self, ProgableControl *progCtrl)
{

    if (s_fx < 0) {
        char e[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", e, sizeof(e));
        s_fx = (n > 0 && n < sizeof(e) && strcmp(e, "slotshift") == 0);
        if (s_fx)
            g_logger.write("keypress: KAROO_SIM_FX=slotshift -- load restores slot k+1\n");
    }

    int entry = 0;
    if (self->frontEnd()->rebindActive() == 0 && self->frontEnd()->textEntryActive() == 0) {
        if (input_key_down(inputdev::KEY_ESCAPE) != 0 && self->frontEnd()->debounceRef() != inputdev::KEY_ESCAPE && self->frontEnd()->menu()->changed() != 0) {
            if (self->fixedSounds()->switchClick != NULL)
                (self->fixedSounds()->switchClick)->play(false);
            self->frontEnd()->debounceRef() = inputdev::KEY_ESCAPE;
        }
        if (self->frontEnd()->menu()->nodeRef() != 5 && self->frontEnd()->menu()->nodeRef() != 3 && self->frontEnd()->menu()->childCount(self->frontEnd()->menu()->nodeRef()) > 1) {
            if (input_key_down(inputdev::KEY_UP) != 0 && self->frontEnd()->debounceRef() != inputdev::KEY_UP) {
                if (self->fixedSounds()->menuUpDown != NULL)
                    (self->fixedSounds()->menuUpDown)->cycle(0);
                self->frontEnd()->debounceRef() = inputdev::KEY_UP;
            }
            if (input_key_down(inputdev::KEY_DOWN) != 0 && self->frontEnd()->debounceRef() != inputdev::KEY_DOWN) {
                if (self->fixedSounds()->menuUpDown != NULL)
                    (self->fixedSounds()->menuUpDown)->cycle(0);
                self->frontEnd()->debounceRef() = inputdev::KEY_DOWN;
            }
        }
        if (input_key_down(inputdev::KEY_RETURN) != 0 && self->frontEnd()->debounceRef() != inputdev::KEY_RETURN && self->frontEnd()->menu()->changed() != 0 &&
            (unsigned short)self->frontEnd()->menu()->nodeRef() == self->frontEnd()->menu()->lastNodeSeen()) {
            if (self->fixedSounds()->switchClick != NULL)
                (self->fixedSounds()->switchClick)->play(false);
            self->frontEnd()->debounceRef() = inputdev::KEY_RETURN;
        }
        self->frontEnd()->menu()->navigate((int)(long long)self->lastTickTime());
        self->frontEnd()->menu()->setLastNodeSeen((unsigned short)self->frontEnd()->menu()->nodeRef());
        entry = self->frontEnd()->textEntryActive() != 0;
    } else {
        entry = self->frontEnd()->textEntryActive() != 0;
    }

    if (entry) {  // save-name text entry
        self->frontEnd()->nameEntry()->poll((unsigned int)(long long)*self->world()->clock());
        if (self->frontEnd()->nameEntry()->active() == 0) {
            if (self->frontEnd()->nameEntry()->lastKey() == inputdev::KEY_RETURN)
                self->frontEnd()->saveSlots()->writeAllSlotFiles(self->gameDir(), self->gameFileName(), 0x37);
            else
                *self->frontEnd()->saveSlots()->slot((unsigned char)self->frontEnd()->saveSlots()->editSlot()) =
                    *self->frontEnd()->saveSlots()->edit();
            self->frontEnd()->setTextEntryActive(0);
            self->frontEnd()->menu()->pop();
            self->frontEnd()->menu()->setCursor(0);
        }
    }

    if (self->frontEnd()->menu()->nodeRef() == 0) {
        self->frontEnd()->setRebindActive(0);
        if (self->state() == GameState::PauseMenu && self->frontEnd()->menu()->leave() != 0) {
            self->setState(self->stateBeforeMenu());
            self->world()->player()->setPendingMove(0);
            self->frontEnd()->debounceRef() = inputdev::KEY_ESCAPE;
        }
    }

    {
        unsigned char key = self->frontEnd()->menu()->child(self->frontEnd()->menu()->nodeRef(), self->frontEnd()->menu()->cursor());
        if ((unsigned int)key - 0x22 <= 0x28)
            option_edit(self, key, progCtrl);
    }

    // DETERMINISM: the right and left polls here are discarded by the game's
    // own code.  The level select reads them instead of polling again, so the
    // recorded key stream is unchanged.  Its edges are tracked separately,
    // because the game's debounce belongs to the option pages on these keys.
    {
        static bool s_right, s_left;
        const bool right = input_key_down(inputdev::KEY_RIGHT) != 0, left = input_key_down(inputdev::KEY_LEFT) != 0;
        if (self->frontEnd()->rebindActive() == 0 && self->frontEnd()->textEntryActive() == 0) {
            if (right && !s_right)
                LevelSelect_Turn(self->frontEnd(), self->session(), +1);
            if (left && !s_left)
                LevelSelect_Turn(self->frontEnd(), self->session(), -1);
        }
        s_right = right;
        s_left = left;
    }
    if (self->frontEnd()->menu()->nodeRef() != 5 && self->frontEnd()->menu()->nodeRef() != 0x50)
        self->frontEnd()->setField13cc88(0);

    switch (self->frontEnd()->menu()->nodeRef()) {
    case 1:
        Sim_ClearGameState(self);
        Sim_OpenLevelFile(self, self->session()->levelIndex());
        Sim_SetupLevelObjects(self);
        loaded_tail(self);
        self->frontEnd()->debounceRef() = inputdev::KEY_RETURN;
        break;
    case 5:
        if (self->frontEnd()->field_13cc88() == 0) {
            self->frontEnd()->setField13cc8c(1);
            self->frontEnd()->setField13cc88(1);
        }
        break;
    case 6:
        self->frontEnd()->menu()->pop();
        self->setState(GameState::Quitting);
        if (self->musicOn() != 0)
            self->cdAudio()->stop();
        self->frontEnd()->debounceRef() = inputdev::KEY_RETURN;
        if (self->field_0c() == 0)
            windev::quit(1);
        break;
    case 0x14: rebind(self, GS_KEY_MOVE_FORWARD, 0x14, inputdev::KEY_UP); break;
    case 0x15: rebind(self, GS_KEY_MOVE_BACK, 0x15, inputdev::KEY_DOWN); break;
    case 0x16: rebind(self, GS_KEY_TURN_RIGHT, 0x16, inputdev::KEY_RIGHT); break;
    case 0x17: rebind(self, GS_KEY_TURN_LEFT, 0x17, inputdev::KEY_LEFT); break;
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
        if (self->world()->player()->gliding() == 0)
            self->setCameraTurnsWithPlayer(self->cameraTurnsWithPlayer() == 0);
        self->frontEnd()->menu()->pop();
        break;
    case 0x47:
        self->videoReflection() = (self->videoReflection() == 0);
        self->frontEnd()->menu()->pop();
        break;
    case 0x3c:
        self->setSound3D(self->sound3D() == 0);
        self->soundManager()->setup(self->sound3D());
        self->setLevelSoundsReady(0);
        self->frontEnd()->menu()->pop();
        break;
    case 0x3d:
        if (self->musicOn() != 0) {
            self->setMusicOn(0);
            self->cdAudio()->stop();
            self->frontEnd()->menu()->pop();
            break;
        }
        self->cdThemes()->replay();
        self->setMusicOn(1);
        self->frontEnd()->menu()->pop();
        break;
    case 0x29: {
        unsigned char lvl = (unsigned char)(self->session()->levelIndex() + 1);
        self->world()->player()->setGemsCollected(0);
        self->session()->setLevelIndex(lvl);
        Sim_OpenLevelFile(self, lvl);
        Sim_SetupLevelObjects(self);
        self->setState(GameState::Flythrough);
        self->scriptPlayer()->setRunning(1);
        if (self->musicOn() != 0)
            self->cdAudio()->stop();
        self->frontEnd()->debounceRef() = inputdev::KEY_RETURN;
        self->frontEnd()->menu()->setLockStart(self->lastTickTime());
        self->camera()->setCameraMode(1);
        self->frontEnd()->menu()->setLock(1);
        self->frontEnd()->menu()->pop();
        g_logger.logMessage(1, "level completed - continue");
        if (self->fixedSounds()->levelCompleted != NULL)
            (self->fixedSounds()->levelCompleted)->stop();
        break;
    }
    case 0x22: case 0x32: case 0x3e: case 0x3f:
    case 0x48: case 0x49: case 0x4a: case 0x50:
        self->frontEnd()->menu()->pop();
        break;
    default:
        break;
    }

    // The level select's start node: as New Game, at the chosen level.
    if (self->frontEnd()->menu()->nodeRef() == LS_START) {
        int level = LevelSelect_Chosen(self->frontEnd(), self->session());
        Sim_ClearGameState(self);
        self->session()->setLevelIndex((unsigned char)level);
        Sim_OpenLevelFile(self, self->session()->levelIndex());
        Sim_SetupLevelObjects(self);
        loaded_tail(self);
        self->frontEnd()->debounceRef() = inputdev::KEY_RETURN;
    }

    // Load nodes, 200 .. 200+n-1.  The slot index is the node number wrapped
    // to a byte.
    {
        unsigned int n = self->frontEnd()->saveSlots()->count();
        unsigned int node = self->frontEnd()->menu()->nodeRef();
        if (node >= 200 && (int)node < (int)(n + 200)) {
            unsigned char slot = (unsigned char)(node + 0x38);
            if (s_fx)
                slot = (unsigned char)(slot + 1);
            if (self->frontEnd()->saveSlots()->slot(slot)->inUse != 0) {
                Sim_ClearGameState(self);
                Menu_RestoreGameStateFromSaveSlot(self->frontEnd(), self->session(), self->world(), slot);
                Sim_OpenLevelFile(self, self->session()->levelIndex());
                Sim_SetupLevelObjects(self);
                loaded_tail(self);
            }
            self->frontEnd()->debounceRef() = inputdev::KEY_RETURN;
            self->frontEnd()->menu()->pop();
        }
    }

    // Save nodes, 200+n .. 200+2n-1: snapshot the game into the slot and start
    // name entry on it.
    {
        unsigned int n = self->frontEnd()->saveSlots()->count();
        unsigned int node = self->frontEnd()->menu()->nodeRef();
        if ((int)node >= (int)(n + 200) && (int)node < (int)(2 * n + 200)) {
            unsigned char slot = (unsigned char)(node - n + 0x38);
            SaveSlot *rec = self->frontEnd()->saveSlots()->slot(slot);
            self->frontEnd()->nameEntry()->setMaxLength(10);
            self->frontEnd()->setTextEntryActive(1);
            self->frontEnd()->nameEntry()->setActive(1);
            *self->frontEnd()->saveSlots()->edit() = *rec;
            self->frontEnd()->nameEntry()->setBuffer(rec->name);
            self->frontEnd()->saveSlots()->setEditSlot(slot);
            self->frontEnd()->nameEntry()->setCursor((unsigned char)strlen(rec->name));
            Menu_StoreGameStateIntoSaveSlot(self->frontEnd(), self->session(), self->world(), slot);
            self->frontEnd()->nameEntry()->setLastKey(inputdev::KEY_RETURN);
            // PRESERVED: three discarded Backspace polls clear its pressed bit
            // before name entry starts.
            input_key_down(inputdev::KEY_BACKSPACE);
            input_key_down(inputdev::KEY_BACKSPACE);
            input_key_down(inputdev::KEY_BACKSPACE);
            self->frontEnd()->menu()->pop();
        }
    }

    // Key-rebind capture, once Enter is released.
    if (self->frontEnd()->rebindActive() != 0 && input_key_down(inputdev::KEY_RETURN) == 0) {
        progCtrl->clearBindings(1, self->frontEnd()->rebindAction());
        if (progCtrl->captureBinding(1, self->frontEnd()->rebindAction(),
                                    100, 10, 0) != 0)
            self->frontEnd()->setRebindActive(0);
    }
}
