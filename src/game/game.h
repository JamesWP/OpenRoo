/* Game: the one game object, holding the whole game's state as sub-objects
 * and fields (game.cpp builds it).  A field whose meaning is not decoded is
 * named field_<hex>, after where the original kept it.
 */
#pragma once
#include <stdint.h>
#include <string.h>

 
#include "frontend.h"
#include "scriptplayer.h"
#include "camerarig.h"
#include "gameaudio.h"
#include "config.h"
#include "themesounds.h"
#include "bomb.h"
#include "world.h"
#include "session.h"
#include "playstate.h"

class ProgableControl;

 
class Bomb;
class Foe;

class Game {
public:
     

    /* The one Game object, NULL until WinMain has built it.  A static
     * member, so it is outside the layout. */
    static inline Game *s_instance = nullptr;
    static Game *instance()           { return s_instance; }
    static void set_instance(Game* g) { s_instance = g; }

    /* ── quit ───────────────────────────────────────────────────────── */
    /* A fatal error in the simulation asks for the game to end; the driver
     * (core) passes it to the window layer.  If called more than once, the
     * last code wins. */
    void requestQuit(int exitCode)   { quitRequested_ = true; quitCode_ = exitCode; }
    bool quitRequested() const       { return quitRequested_; }
    int  quitCode() const            { return quitCode_; }
    void clearQuitRequest()          { quitRequested_ = false; }

    /* ── sound (gameaudio.h) ────────────────────────────────────────── */
    /* The sound manager, the fixed sounds, the theme's sound table and the CD
     * player (gameaudio.h). */
    GameAudio    *audio()             { return &audio_; }
    const GameAudio *audio() const    { return &audio_; }

    /* ── the level in play (world.h) ─────────────────────────────────── */
    /* The level in play: its map, objects and player (world.h). */
    World        *world()       { return &world_; }
    const World  *world() const { return &world_; }

    /* ── time ───────────────────────────────────────────────────────── */
    /* The `now` GameTick was last called with (double, ms); the menus
     * stamp their timers from it.  Its two halves are also copied dword by
     * dword into the menu's lock start (MenuTree::setLockStart). */
    double         lastTickTime() const              { return lastTickTime_; }
    void           setLastTickTime(double t)         { lastTickTime_ = t; }

    /* ── tiles ──────────────────────────────────────────────────────── */
    /* ── level-load flags ───────────────────────────────────────────── */
    /* 1 when the last level load read a different map name from the
     * one before it (both loaders, levelparse.cpp). */
    unsigned int   mapChanged() const                { return mapChanged_; }
    void           setMapChanged(unsigned int c)     { mapChanged_ = c; }
    /* GameTick latches it to 1 the first time the gems required for the exit
     * are collected in a level (and plays a crystal sound); the level builder
     * and Load clear it. */
    int            gemsGoalReached() const           { return gemsGoalReached_; }
    void           setGemsGoalReached(int v)         { gemsGoalReached_ = v; }

    /* The level builder sets it to 1 once a level is built; the next frame's
     * update (core/frame.h) then prepares the level's assets and clears it (skipping the frame
     * meanwhile), and GameTick loads the level sounds only while it and
     * levelSoundsReady() are both 0. */
    int            levelPrepPending() const          { return levelPrepPending_; }
    void           setLevelPrepPending(int v)        { levelPrepPending_ = v; }
    /* The game file's name (the .gam, the save-slot files' base and the
     * final directory's %s), 0x80 bytes. */
    const char    *gameFileName() const              { return gameFileName_; }
    /* The directory every game file is read from; fixed when the Game is made. */
    const char    *gameDir() const                   { return gameDir_; }
    /* The level the main menu shows behind it, which GameTick reloads on
     * returning there; 0x80 bytes. */
    const char    *menuLevelName() const             { return menuLevelName_; }
    /* The game state ESC interrupted (Playing or Flythrough); keypress.cpp
     * restores it when the menu is left. */
    GameState      stateBeforeMenu() const           { return stateBeforeMenu_; }
    void           setStateBeforeMenu(GameState s)   { stateBeforeMenu_ = s; }

    /* ── the front end (frontend.h) ─────────────────────────────────── */
    /* The menu, the cheat and name entries, the save slots, the high-score
     * table and the rebind state (frontend.h). */
    FrontEnd       *frontEnd()                       { return &frontEnd_; }
    const FrontEnd *frontEnd() const                 { return &frontEnd_; }

    /* ── the play-through (session.h) ───────────────────────────────── */
    /* The play-through's progress: the game file's level list and the level
     * being played, the restart count, the counters the level score is
     * computed from, the end-of-level tally and the total play time. */
    Session        *session()                        { return &session_; }
    const Session  *session() const                  { return &session_; }

    /* The settings object (config.h); openroo.ini holds it. */
    Config               *config()                   { return &config_; }
    /* The instruction-script player (scriptplayer.h). */
    ScriptPlayer   *scriptPlayer()                   { return &scriptPlayer_; }

    /* ── game state ─────────────────────────────────────────────────── */
    /* The top-level state GameTick and HandleKeypress switch on (GameState,
     * playstate.h).  NameEntry is set by GameTick after
     * InsertScoreIntoHighScoreTable; PauseMenu is the ESC menu. */
    GameState      state() const                     { return state_; }
    void           setState(GameState s)             { state_ = s; }
    /* ── camera and controls ────────────────────────────────────────── */
    /* The camera (camerarig.h): its intent, set by the simulation, menus,
     * script player and checkpoints, and its live pose. */
    CameraRig      *camera()                         { return &camera_; }
    const CameraRig *camera() const                  { return &camera_; }
    BombSounds bombSounds() const
    {
        return { audio_.themeSound(ThemeSound::MoveJumpPad), audio_.themeSound(ThemeSound::Teleporter),
                 audio_.themeSound(ThemeSound::ExplosionBomb), audio_.themeSound(ThemeSound::FallCatcher),
                 audio_.themeSound(ThemeSound::BombTick) };
    }

    bool initialised() const {return initialised_; }

    /* ── the lifecycle (game.cpp) ───────────────────────────────────────
     * The members build themselves.  The constructor then reads the .gam,
     * save slots, openroo.ini and high scores; check initialised() afterwards.
     * The destructor saves the scores and config, and the members tear
     * themselves down. */
    Game();
    virtual ~Game();
    /* Built and torn down by Game_Boot and Game_Shutdown (core/gameboot.h),
     * which set up and take down everything the Game holds. */
    friend void Game_Boot(Game *g, const char *gameName, const char *gameDir);
    friend void Game_Shutdown(Game *g);

    Game(const Game &) = delete;
    Game &operator=(const Game &) = delete;

private:
    void  releaseAllSounds();

private:
     

    unsigned int  mapChanged_{};
    int           initialised_{};                           /* 0 until Load completes */
    /* The game's sound (gameaudio.h): the sound manager, the fixed sounds,
     * the theme's sound table and the CD player.  Declared before the level,
     * whose entities hold sound handles, so the level is torn down before
     * it. */
    GameAudio     audio_;
    /* 256 level names, 0x100 each.  SetCurrentLevelName copies entry `levelNo & 0xff` into World's levelName_. */
    char          gameFileName_[0x80]{};
    char          gameDir_[64]{};
    GameState     stateBeforeMenu_{};
    char          menuLevelName_[0x80]{};
    /* The level in play (world.h).  Declared after the game's sound, so it
     * is torn down before it. */
    World         world_;
    /* The front end (frontend.h): the menu, the text entries, the save slots
     * and the high-score table.  Declared after World, so it is torn down
     * before it and the game's sound. */
    FrontEnd      frontEnd_;
    /* The play-through (session.h).  Declared after the front end, so it is
     * torn down before it and the game's sound. */
    Session       session_;
    double        lastTickTime_{};
    int           levelPrepPending_{};
    int           gemsGoalReached_{};
    ScriptPlayer  scriptPlayer_;
    CameraRig     camera_;
    /* The settings (config.h); music, volumes, 3D sound, the camera
     * option and the joystick deadzone live in its persisted blob. */
    Config        config_;
    GameState     state_{};
    /* The fatal-error quit requested by requestQuit, until the driver takes it. */
    bool          quitRequested_{};
    int           quitCode_{};
};

 