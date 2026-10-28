#include "keycodes.h"
#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "windev.h"
#include "game.h"
#include "savegame.h"
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
#include "levelselect.h"
#include "levelthemes.h"
#include "keys.h"



/* The key-rebind prompts: copy the action name, arm key capture and pop back. */
static void rebind(Game *game, const char *name, unsigned char code)
{
    strcpy(game->frontEnd()->rebindAction(), name);
    game->frontEnd()->setRebindActive(1);
    game->frontEnd()->setRebindCode(code);
    game->frontEnd()->menu()->pop();
}

/* The tail shared by new game, the level select and loading a save: start the
 * level's intro flythrough. */
static void loaded_tail(Game *game)
{
    game->setState(GameState::Flythrough);
    game->camera()->setDistance(7.0f);
    if (game->config()->musicOn() != 0)
        game->audio()->cdAudio()->stop();
    game->scriptPlayer()->setRunning(1);
    game->camera()->setMode(1);
    game->frontEnd()->menu()->rewind();
}

void Flow_StartFreshGame(Game *self, int level)
{
    Sim_ClearGameState(self);
    self->session()->setLevelIndex((unsigned char)level);
    Sim_OpenLevelFile(self, self->session()->levelIndex());
    Sim_SetupLevelObjects(self);
    loaded_tail(self);
}

/* Right is +1, Left -1, on the press. */
static int arrow_step()
{
    return input::pressed(input::KEY_RIGHT) ? 1 : input::pressed(input::KEY_LEFT) ? -1 : 0;
}

/* The volume pages step by 10 in 0..100; true if it moved. */
static bool step_volume(unsigned char vol, int step, unsigned char *out)
{
    if ((step > 0 && vol >= 100) || (step < 0 && vol == 0) || step == 0)
        return false;
    *out = (unsigned char)(vol + 10 * step);
    return true;
}

static void option_edit(Game *game, unsigned char key)
{
    const int step = arrow_step();
    switch (key) {
    case 0x22: {  // the "sfx %" page edits the joystick dead zone
        const unsigned short dz = game->config()->joyDeadzone();
        if ((step > 0 && dz < 0x5a) || (step < 0 && dz > 10))
            game->config()->setJoyDeadzone((unsigned short)(dz + 10 * step));
        break;
    }
    case 0x3e: {
        unsigned char v;
        if (step_volume(game->config()->cdVolume(), step, &v)) {
            game->config()->setCdVolume(v);
            game->audio()->cdAudio()->setVolume(game->config()->musicGain());
        }
        break;
    }
    case 0x3f: {
        unsigned char v;
        if (step_volume(game->config()->waveVolume(), step, &v)) {
            game->config()->setWaveVolume(v);
            if (game->audio()->soundCreated() != 0)
                audiodev::setEffectsVolume(game->config()->effectsGain());
        }
        break;
    }
    // The three 0..2 video-quality sliders: shadows, highlights and particles.
    // Reflection is a 0/1 toggle on its own node.
    case 0x48: case 0x49: case 0x4a: {
        unsigned char &opt = key == 0x48 ? game->config()->videoShadows()
                           : key == 0x49 ? game->config()->videoHighlights()
                                         : game->config()->videoParticles();
        if ((step > 0 && opt < 2) || (step < 0 && opt != 0))
            opt = (unsigned char)(opt + step);
        break;
    }
    default:
        break;
    }
}

  void  
Flow_HandleKeypress(Game *self, ProgableControl *progCtrl)
{

    int entry = 0;
    if (self->frontEnd()->rebindActive() == 0 && self->frontEnd()->textEntryActive() == 0) {
        const unsigned char cursorWas = self->frontEnd()->menu()->cursor();
        self->frontEnd()->menu()->navigate((int)(long long)self->lastTickTime());
        if (self->frontEnd()->menu()->changed() != 0 && self->audio()->fixedSounds()->switchClick != NULL)
            (self->audio()->fixedSounds()->switchClick)->play(false);
        const unsigned char node = self->frontEnd()->menu()->nodeRef();
        if (self->frontEnd()->menu()->changed() == 0 && self->frontEnd()->menu()->cursor() != cursorWas &&
            node != 5 && node != 3 &&
            self->audio()->fixedSounds()->menuUpDown != NULL)
            (self->audio()->fixedSounds()->menuUpDown)->cycle(0);
        self->frontEnd()->menu()->setLastNodeSeen((unsigned short)self->frontEnd()->menu()->nodeRef());
        entry = self->frontEnd()->textEntryActive() != 0;
    } else {
        entry = self->frontEnd()->textEntryActive() != 0;
    }

    if (entry) {  // save-name text entry
        self->frontEnd()->nameEntry()->poll((unsigned int)(long long)self->world()->now());
        if (self->frontEnd()->nameEntry()->active() == 0) {
            if (self->frontEnd()->nameEntry()->endKey() == input::KEY_RETURN)
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
            input::swallow(input::KEY_ESCAPE);
        }
    }

    {
        unsigned char key = self->frontEnd()->menu()->child(self->frontEnd()->menu()->nodeRef(), self->frontEnd()->menu()->cursor());
        if ((unsigned int)key - 0x22 <= 0x28)
            option_edit(self, key);
    }

    if (self->frontEnd()->rebindActive() == 0 && self->frontEnd()->textEntryActive() == 0) {
        if (input::pressed(input::KEY_RIGHT))
            LevelSelect_Turn(self->frontEnd(), self->session(), +1);
        if (input::pressed(input::KEY_LEFT))
            LevelSelect_Turn(self->frontEnd(), self->session(), -1);
    }
    if (self->frontEnd()->menu()->nodeRef() != 5 && self->frontEnd()->menu()->nodeRef() != 0x50)
        self->frontEnd()->setCreditsLatch(0);

    switch (self->frontEnd()->menu()->nodeRef()) {
    case 1:
        Sim_ClearGameState(self);
        Sim_OpenLevelFile(self, self->session()->levelIndex());
        Sim_SetupLevelObjects(self);
        loaded_tail(self);
        input::swallow(input::KEY_RETURN);
        break;
    case 5:
        if (self->frontEnd()->creditsLatch() == 0) {
            self->frontEnd()->setCreditsRestart(1);
            self->frontEnd()->setCreditsLatch(1);
        }
        break;
    case 6:
        self->frontEnd()->menu()->pop();
        self->setState(GameState::Quitting);
        if (self->config()->musicOn() != 0)
            self->audio()->cdAudio()->stop();
        input::swallow(input::KEY_RETURN);
        windev::quit(1);
        break;
    case 0x14: rebind(self, "John_Move_Forward", 0x14); break;
    case 0x15: rebind(self, "John_Move_Back", 0x15); break;
    case 0x16: rebind(self, "John_Turn_Right", 0x16); break;
    case 0x17: rebind(self, "John_Turn_Left", 0x17); break;
    case 0x18: rebind(self, "John_Zoom_In", 0x18); break;
    case 0x19: rebind(self, "John_Zoom_Out", 0x19); break;
    case 0x1a: rebind(self, "John_Release_Bomb", 0x1a); break;
    case 0x1b: rebind(self, "John_Harakiri", 0x1b); break;
    case 0x1c: rebind(self, "John_OverView", 0x1c); break;
    case 0x1d: rebind(self, "CamModeLeft", 0x1d); break;
    case 0x1e: rebind(self, "CamModeRight", 0x1e); break;
    case 0x1f: rebind(self, "CamModeUp", 0x1f); break;
    case 0x20: rebind(self, "CamModeDown", 0x20); break;
    case 0x21:
        if (self->world()->player()->gliding() == 0)
            self->config()->setCameraTurnsWithPlayer(self->config()->cameraTurnsWithPlayer() == 0);
        self->frontEnd()->menu()->pop();
        break;
    case 0x47:
        self->config()->videoReflection() = (self->config()->videoReflection() == 0);
        self->frontEnd()->menu()->pop();
        break;
    case 0x3c:
        self->config()->setSound3D(self->config()->sound3D() == 0);
        self->audio()->soundManager()->setup(self->config()->sound3D());
        self->audio()->setLevelSoundsReady(0);
        self->frontEnd()->menu()->pop();
        break;
    case 0x3d:
        if (self->config()->musicOn() != 0) {
            self->config()->setMusicOn(0);
            self->audio()->cdAudio()->stop();
            self->frontEnd()->menu()->pop();
            break;
        }
        self->audio()->cdThemes()->replay();
        self->config()->setMusicOn(1);
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
        if (self->config()->musicOn() != 0)
            self->audio()->cdAudio()->stop();
        input::swallow(input::KEY_RETURN);
        self->frontEnd()->menu()->setLockStart(self->lastTickTime());
        self->camera()->setMode(1);
        self->frontEnd()->menu()->setLock(1);
        self->frontEnd()->menu()->pop();
        g_logger.logMessage(1, "level completed - continue");
        self->soundEvents()->soundStop({SoundSource::Level, 0, Sfx::LevelComplete});
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
        Flow_StartFreshGame(self, LevelSelect_Chosen(self->frontEnd(), self->session()));
        input::swallow(input::KEY_RETURN);
    }

    // Load nodes, 200 .. 200+n-1.  The slot index is the node number wrapped
    // to a byte.
    {
        unsigned int n = self->frontEnd()->saveSlots()->count();
        unsigned int node = self->frontEnd()->menu()->nodeRef();
        if (node >= 200 && (int)node < (int)(n + 200)) {
            unsigned char slot = (unsigned char)(node + 0x38);
            if (self->frontEnd()->saveSlots()->slot(slot)->inUse != 0) {
                Sim_ClearGameState(self);
                SaveGame_RestoreFromSaveSlot(self->frontEnd(), self->session(), self->world(), slot);
                Sim_OpenLevelFile(self, self->session()->levelIndex());
                Sim_SetupLevelObjects(self);
                loaded_tail(self);
            }
            input::swallow(input::KEY_RETURN);
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
            SaveGame_StoreIntoSaveSlot(self->frontEnd(), self->session(), self->world(), slot);
            self->frontEnd()->menu()->pop();
        }
    }

    // Key-rebind capture, once Enter is released.
    if (self->frontEnd()->rebindActive() != 0 && !input::down(input::KEY_RETURN)) {
        progCtrl->clearBindings(1, self->frontEnd()->rebindAction());
        if (progCtrl->captureBinding(1, self->frontEnd()->rebindAction(), 100) != 0)
            self->frontEnd()->setRebindActive(0);
    }
}
