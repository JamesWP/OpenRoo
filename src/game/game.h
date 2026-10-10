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
#include "cdm.h"
#include "cdthemes.h"
#include "config.h"
#include "themesounds.h"
#include "bomb.h"
#include "soundmanager.h"
#include "world.h"
#include "session.h"
#include "playstate.h"

class SoundManager;


/* The fixed sounds AcquireFixedSoundBuffersAndMaybeReport loads once.  They
 * are the Game's, not the SoundManager's: only Game code writes them. */
namespace audiodev { class Buffer; }
class VoicePool;
struct FixedSounds {
    audiodev::Buffer *timeOut;
    audiodev::Buffer *switchClick;      /* menu select */
    VoicePool          *menuUpDown;       /* 5 voices */
    audiodev::Buffer *count;
    audiodev::Buffer *lastSeconds;      /* the countdown */
    audiodev::Buffer *levelCompleted;
    /* Three banks, 'A'..'C'; GameTick picks one by rand()%3 when the
     * crystals are complete. */
    audiodev::Buffer *crystalBank[3];
    /* Set once the load has run (or found no sound); it never runs again. */
    unsigned int        loaded;
};

 
class Bomb;
class Foe;

class Game {
public:
     

    /* The one Game object, NULL until WinMain has built it.  A static
     * member, so it is outside the layout. */
    static inline Game *s_instance = nullptr;
    static Game *instance()           { return s_instance; }
    static void set_instance(Game* g) { s_instance = g; }

    /* ── sound ──────────────────────────────────────────────────────── */
    SoundManager *soundManager()     { return &soundManager_; }
    /* Nonzero once sound is up. */
    int  soundCreated() const        { return (int)soundManager_.created(); }
    FixedSounds  *fixedSounds()      { return &fixedSounds_; }

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
    /* The level builder's missing-CD check fires only while it is
     * zero.  Not decoded. */
    int            field_0c() const                  { return field_0c_; }
    /* 1 when the last level load read a different map name from the
     * one before it (both loaders, levelparse.cpp). */
    unsigned int   mapChanged() const                { return mapChanged_; }
    void           setMapChanged(unsigned int c)     { mapChanged_ = c; }
    /* GameTick latches it to 1 the first time the exit
     * condition is met in a level (and plays a crystal sound); the level
     * builder clears it.  Named by offset. */
    int            field_173b1a() const              { return field_173b1a_; }
    void           setField173b1a(int v)             { field_173b1a_ = v; }

    /* The level builder sets it to 1; GameTick loads the level
     * sounds only while it and levelSoundsReady() are both 0.  Named by
     * offset. */
    int            field_173584() const              { return field_173584_; }
    void           setField173584(int v)             { field_173584_ = v; }
    /* GameTick adds one per tick and returns it with the low byte
     * masked off; field_48b14 is bumped beside it.  Neither is read elsewhere
     * in our code. */
    unsigned int   tickCount() const                 { return tickCount_; }
    void           setTickCount(unsigned int n)      { tickCount_ = n; }
    unsigned int   field_48b14() const               { return field_48b14_; }
    void           setField48b14(unsigned int n)     { field_48b14_ = n; }
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
     * table and the rebind and debounce state (frontend.h). */
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
    /* The CD audio device (cdm.h); the CD-music theme table plays on it. */
    CDM            *cdAudio()                        { return &cdAudio_; }
    /* The CD-music theme table (cdthemes.h). */
    CdThemes       *cdThemes()                       { return &cdThemes_; }
    /* The instruction-script player (scriptplayer.h). */
    ScriptPlayer   *scriptPlayer()                   { return &scriptPlayer_; }

    /* ── game state ─────────────────────────────────────────────────── */
    /* The top-level state GameTick and HandleKeypress switch on (GameState,
     * playstate.h).  NameEntry is set by GameTick after
     * InsertScoreIntoHighScoreTable; PauseMenu is the ESC menu. */
    GameState      state() const                     { return state_; }
    void           setState(GameState s)             { state_ = s; }
    /* GameTick runs InitLevelBasedSounds while this is 0, then sets it;
     * Load, SetupLevelObjects and the 3D-sound toggle clear it. */
    int            levelSoundsReady() const          { return levelSoundsReady_; }
    void           setLevelSoundsReady(int r)        { levelSoundsReady_ = r; }

    /* CD music on: every CdThemes::play call is gated on it; HandleKeypress
     * toggles it, RenderSoundOptions shows it, sound setup clears it on
     * failure.  A dword. */
    int            musicOn() const                   { return config_.musicOn(); }
    void           setMusicOn(int on)                { config_.setMusicOn(on); }

    /* ── video quality (the options menu; config.h has the derivation) ─ */
    unsigned char &videoShadows()                    { return config_.videoShadows(); }
    unsigned char &videoReflection()                 { return config_.videoReflection(); }
    unsigned char &videoHighlights()                 { return config_.videoHighlights(); }
    unsigned char &videoParticles()                  { return config_.videoParticles(); }

    /* ── volumes (the options menu, HandleKeypress 0x3e/0x3f) ────────── */
    /* Percent, steps of 10, shown by RenderSoundOptions: the music's and the
     * sound effects'. */
    unsigned char  cdVolume() const                  { return config_.cdVolume(); }
    void           setCdVolume(unsigned char p)      { config_.setCdVolume(p); }
    unsigned char  waveVolume() const                { return config_.waveVolume(); }
    void           setWaveVolume(unsigned char p)    { config_.setWaveVolume(p); }
    /* The same as gains, 0..1, for the audio layer. */
    float          musicGain() const                 { return (float)config_.cdVolume() / 100.0f; }
    float          effectsGain() const               { return (float)config_.waveVolume() / 100.0f; }

    /* ── camera and controls ────────────────────────────────────────── */
    /* The camera (camerarig.h): its intent, set by the simulation, menus,
     * script player and checkpoints, and its live pose. */
    CameraRig      *camera()                         { return &camera_; }
    const CameraRig *camera() const                  { return &camera_; }
    /* The controls menu's camera option: 1 = the camera turns with the
     * player (UpdateViewTransform, FramePose_Player).  GameTick forces it to 1
     * while the player is gliding, parking the choice +10 in
     * parkedCameraOption. */
    unsigned char  cameraTurnsWithPlayer() const     { return config_.cameraTurnsWithPlayer(); }
    void           setCameraTurnsWithPlayer(unsigned char on) { config_.setCameraTurnsWithPlayer(on); }
    /* The options menu's 3D-sound switch: handed to the SoundManager's
     * setup, and InitLevelBasedSounds adds the extra-object sounds only
     * while it is on. */
    int            sound3D() const                   { return config_.sound3D(); }
    void           setSound3D(int on)                { config_.setSound3D(on); }
    /* Joystick deadzone in percent, steps of 10 (ProgCtrl gets it x100). */
    unsigned short joyDeadzone() const               { return config_.joyDeadzone(); }
    void           setJoyDeadzone(unsigned short p)  { config_.setJoyDeadzone(p); }

    BombSounds bombSounds() const
    {
        return { soundAsset429b6(), soundAsset42ac2(), soundAsset46baa(),
                 soundAsset457c6(), soundAsset46132() };
    }

    bool initialised() const {return initialised_; }

    /* The theme sound entries the spawns and level sounds acquire, named
     * by their old Game offsets; the index is the theme event id. */
    const SoundAssetName *soundAsset42262() const { return themeSounds_.entry(0); }
    const SoundAssetName *soundAsset4236e() const { return themeSounds_.entry(1); }
    const SoundAssetName *soundAsset4247a() const { return themeSounds_.entry(2); }
    const SoundAssetName *soundAsset42586() const { return themeSounds_.entry(3); }
    const SoundAssetName *soundAsset42692() const { return themeSounds_.entry(4); }
    const SoundAssetName *soundAsset4279e() const { return themeSounds_.entry(5); }
    const SoundAssetName *soundAsset428aa() const { return themeSounds_.entry(6); }
    const SoundAssetName *soundAsset429b6() const { return themeSounds_.entry(7); }
    const SoundAssetName *soundAsset42ac2() const { return themeSounds_.entry(8); }
    const SoundAssetName *soundAsset42bce() const { return themeSounds_.entry(9); }
    const SoundAssetName *soundAsset42cda() const { return themeSounds_.entry(10); }
    const SoundAssetName *soundAsset42de6() const { return themeSounds_.entry(11); }
    const SoundAssetName *soundAsset42ef2() const { return themeSounds_.entry(12); }
    const SoundAssetName *soundAsset42ffe() const { return themeSounds_.entry(13); }
    const SoundAssetName *soundAsset4310a() const { return themeSounds_.entry(14); }
    const SoundAssetName *soundAsset43216() const { return themeSounds_.entry(15); }
    const SoundAssetName *soundAsset441ca() const { return themeSounds_.entry(30); }
    const SoundAssetName *soundAsset44c42() const { return themeSounds_.entry(40); }
    const SoundAssetName *soundAsset44d4e() const { return themeSounds_.entry(41); }
    const SoundAssetName *soundAsset44e5a() const { return themeSounds_.entry(42); }
    const SoundAssetName *soundAsset456ba() const { return themeSounds_.entry(50); }
    const SoundAssetName *soundAsset457c6() const { return themeSounds_.entry(51); }
    const SoundAssetName *soundAsset458d2() const { return themeSounds_.entry(52); }
    const SoundAssetName *soundAsset46132() const { return themeSounds_.entry(60); }
    const SoundAssetName *soundAsset46baa() const { return themeSounds_.entry(70); }
    ThemeSoundTable      *themeSounds()             { return &themeSounds_; }

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
     

    double        field_04_{};                              /* Load sets 1.0; reader not decoded */
    int           field_0c_{};
    unsigned int  mapChanged_{};
    unsigned int  tickCount_{};
    int           initialised_{};                           /* 0 until Load completes */
    /* The CD audio device (cdm.h).  Declared before the theme table, which
     * plays on it, so it is torn down after the table. */
    CDM           cdAudio_;
    CdThemes      cdThemes_;
    /* 256 level names, 0x100 each.  SetCurrentLevelName copies entry `levelNo & 0xff` into World's levelName_. */
    char          gameFileName_[0x80]{};
    char          gameDir_[64]{};
    int           levelSoundsReady_{};
    ThemeSoundTable themeSounds_;
    GameState     stateBeforeMenu_{};
    unsigned int  field_48b14_{};
    char          menuLevelName_[0x80]{};
    SoundManager  soundManager_;
    FixedSounds   fixedSounds_{};
    /* The level in play (world.h).  Declared after the sound manager and the
     * fixed sounds, so it is torn down before them. */
    World         world_;
    /* The front end (frontend.h): the menu, the text entries, the save slots
     * and the high-score table.  Declared after World, as the moved members
     * were after the sound manager, so it is torn down before them. */
    FrontEnd      frontEnd_;
    /* The play-through (session.h).  Declared after the front end, as the
     * moved members were after the sound manager, so it is torn down before
     * them. */
    Session       session_;
    int           field_13cc84_{};                          /* Load zeroes; reader not decoded */
    double        lastTickTime_{};
    int           field_173584_{};
    int           field_173b1a_{};
    ScriptPlayer  scriptPlayer_;
    CameraRig     camera_;
    /* The settings (config.h); music, volumes, 3D sound, the camera
     * option and the joystick deadzone live in its persisted blob. */
    Config        config_;
    GameState     state_{};
};

 