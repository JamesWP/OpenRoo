/* Game: the one game object, holding the whole game's state as sub-objects
 * and fields (game.cpp builds it).  A field whose meaning is not decoded is
 * named field_<hex>, after where the original kept it.
 */
#pragma once

#include "textentry.h"
#include "saveslots.h"
#include "menutree.h"
#include "scriptplayer.h"
#include "cdthemes.h"
#include "highscores.h"
#include "config.h"
#include "extraobjects.h"
#include "levelmap.h"
#include "switchcells.h"
#include "levelcensus.h"
#include "tickstep.h"
#include "soundmanager.h"
#include "player.h"

class SoundManager;
class LiftObject;
class SlideObject;
class BridgeObject;
class BreakableTile;

/* A sound asset's file name and, immediately after it, its enabled flag.
 * The spawn's unbounded strcpy of `name` relies on the flag to stop it. */
struct SoundAssetName {
    char name[256];
    int  enabled;
    DWORD unknown104;   /* ThemeSound_Add's arg4; the .thm path passes 1 */
    DWORD unknown108;   /* ThemeSound_Add's arg3; the .thm path passes 1 */
};

/* The theme sound table ("TSM" in its log line).  A .thm
 * `Sound <event> <wave>` line fills entries[id] through ThemeSound_Add
 * (theme.cpp); the id is RegisterThemeSound's event number, so e.g. entry 0
 * is movecatcher and entry 70 explosionbomb.  The event table's largest id is
 * 0x47, but the table holds 100 entries, and ReleaseAll clears all 100.
 * Lifecycle in
 * theme.cpp; the vtable is ours, one slot. */
#define THEME_SOUND_COUNT 100
struct ThemeSoundTable {
    void          *vtable;
    WORD           unknown8;     /* zeroed by the ctor, never read */
    SoundAssetName entries[THEME_SOUND_COUNT];
};

/* The end-of-level score tally.  Six rows, each
 * with a real COUNT and SCORE (CalculateLevelScore) and a SHOWN
 * count and score that AnimateScoreTallyStages counts up from zero
 * towards them.  The animation's stages visit FOES before TIME. */
enum TallyRow {
    TALLY_GEMS, TALLY_SURPLUS, TALLY_TIME, TALLY_FOES, TALLY_ALLITEMS,
    TALLY_VITALITY, TALLY_ROWS
};

struct ScoreTally {

    int           shownScore[TALLY_ROWS];
    int           shownBase;                /* running total before this level */
    int           shownCount[TALLY_ROWS];
    int           shownLevelTotal;          /* sum of shownScore */
    int           shownGrandTotal;          /* shownBase + shownLevelTotal */
    int           score[TALLY_ROWS];
    int           count[TALLY_ROWS];
    int           levelTotal;
    int           grandTotal;
    unsigned char stage;                    /* animation state, 0..6 */
    int           stageStart;               /* ms, the stage's start */

};

/* The fixed sounds AcquireFixedSoundBuffersAndMaybeReport loads once.  They
 * are the Game's, not the SoundManager's: only Game code writes them. */
struct CStaticSoundbuffer;
struct VoicePool;
struct FixedSounds {

    CStaticSoundbuffer *timeOut;
    CStaticSoundbuffer *switchClick;      /* menu select */
    VoicePool          *menuUpDown;       /* 5 voices */
    CStaticSoundbuffer *count;
    CStaticSoundbuffer *lastSeconds;      /* the countdown */
    CStaticSoundbuffer *levelCompleted;
    /* Three banks, 'A'..'C'; GameTick picks one by rand()%3 when the
     * crystals are complete. */
    CStaticSoundbuffer *crystalBank[3];
    /* Set once the load has run (or found no sound); it never runs again. */
    unsigned int        loaded;

};

class Bomb;
class Foe;
class Player;

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
    int  soundCreated() const        { return (int)soundManager_.dwCreated_; }
    FixedSounds  *fixedSounds()      { return &fixedSounds_; }

    /* ── time ───────────────────────────────────────────────────────── */
    /* The `now` GameTick was last called with (double, ms); the menus
     * stamp their timers from it.  Its two halves are also copied dword by
     * dword into the menu's lock start (MenuTree::setLockStart). */
    double         lastTickTime() const              { return lastTickTime_; }
    void           setLastTickTime(double t)         { lastTickTime_ = t; }
    /* The 8-byte clock accumulator.  Objects keep a pointer to it and
     * re-read it every tick. */
    double        *clock()           { return &clock_; }
    /* The tick step, dt; objects copy it to their own each tick. */
    TickStep      *tickStep()        { return &tickStep_; }

    /* ── tiles ──────────────────────────────────────────────────────── */
    /* ── level-load flags ───────────────────────────────────────────── */
    /* The level builder's missing-CD check fires only while it is
     * zero.  Not decoded. */
    int            field_0c() const                  { return field_0c_; }
    /* 1 when the last level load read a different map name from the
     * one before it (both loaders, levelparse.cpp). */
    unsigned int   mapChanged() const                { return mapChanged_; }
    void           setMapChanged(unsigned int c)     { mapChanged_ = c; }
    /* The NEXT level's bonus flag, which OpenLevelFile peeks by
     * loading that level's map first.  SetupLevelObjects reads it to give
     * the completed-level menu node (0x28) two children when it is 0 and
     * one otherwise -- the builder called it a "mode flag". */
    unsigned int   nextLevelBonus() const            { return nextLevelBonus_; }
    void           setNextLevelBonus(unsigned int b) { nextLevelBonus_ = b; }
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
    /* The level the main menu shows behind it, which GameTick reloads on
     * returning there; 0x80 bytes. */
    const char    *menuLevelName() const             { return menuLevelName_; }
    /* The game state ESC interrupted (1 playing or 4 loaded); keypress.cpp
     * restores it when the menu is left. */
    unsigned char  stateBeforeMenu() const           { return stateBeforeMenu_; }
    void           setStateBeforeMenu(unsigned char s) { stateBeforeMenu_ = s; }

    /* The level builder's census and spawn tables (levelcensus.h). */
    LevelCensus    *census()                    { return &census_; }
    TimedSpawner   *timedSpawner(unsigned i)    { return &timedSpawners_[i]; }
    FreeBomb       *freeBomb(unsigned i)        { return &freeBombs_[i]; }

    /* The cells each switch controls (switchcells.h), and the highest
     * switch index the level builder saw -- the list walks run to it. */
    SwitchCells    *switchCells()               { return &switchCells_; }
    unsigned char   switchMax() const           { return switchMax_; }
    void            setSwitchMax(unsigned char n) { switchMax_ = n; }

    /* The level's map (levelmap.h): its header and both tile grids. */
    LevelMap       *map()            { return &map_; }
    const LevelMap *map() const      { return &map_; }
    /* The base Tile::at() indexes from -- the grid's first cell.  Objects keep
     * their own copy. */
    Tile *tileBase()        { return map_.tileBase(); }
    /* Gems the level requires (Player::gemsCollected is the other side). */
    int            gemsRequired() const { return map_.gemsRequired(); }

    /* ── text entry ─────────────────────────────────────────────────── */
    /* The cheat-code entry (HandleTypedCheatCode polls it every tick) and
     * the name entry (save names and the high-score name). */
    TextEntry     *cheatEntry()                      { return &cheatEntry_; }
    TextEntry     *nameEntry()                       { return &nameEntry_; }
    /* The level's extra 3D objects, from its .leo (extraobjects.h). */
    ExtraObjects         *extraObjects()             { return &extraObjects_; }
    /* The settings object (config.h); Karoo.cfg is its persisted blob. */
    Config               *config()                   { return &config_; }
    /* The high-score table (highscores.h). */
    HighScoreTable       *highScores()               { return &highScores_; }
    const HighScoreTable *highScores() const         { return &highScores_; }
    /* The CD-music theme table (cdthemes.h). */
    CdThemes       *cdThemes()                       { return &cdThemes_; }
    /* The instruction-script player (scriptplayer.h). */
    ScriptPlayer   *scriptPlayer()                   { return &scriptPlayer_; }
    /* The menu (menutree.h). */
    MenuTree       *menu()                           { return &menu_; }
    const MenuTree *menu() const                     { return &menu_; }
    /* Game's own debounce key (distinct from MenuTree's): GameTick,
     * HandleKeypress, the tally and the cheats ignore a key equal to it
     * until it is released.  debounceRef() is the field as an lvalue. */
    unsigned char  debounce() const                  { return debounce_; }
    void           setDebounce(unsigned char k)      { debounce_ = k; }
    unsigned char &debounceRef()                     { return debounce_; }
    /* Key-rebind capture: the controls menu's rebind nodes name the action
     * and set the flag; HandleKeypress captures the next binding and clears
     * it.  RenderControlsRemap reads the flag. */
    int            rebindActive() const              { return rebindActive_; }
    void           setRebindActive(int a)            { rebindActive_ = a; }
    unsigned char  rebindCode() const                { return rebindCode_; }
    void           setRebindCode(unsigned char c)    { rebindCode_ = c; }
    char          *rebindAction()                    { return rebindAction_; }
    /* The save-slot table (saveslots.h). */
    SaveSlots     *saveSlots()                       { return &saveSlots_; }

    /* ── game state ─────────────────────────────────────────────────── */
    /* The top-level state GameTick and HandleKeypress switch on, 0..7.
     * menu.h's GAME_ST_* (decoded at runtime): 0 menu, 1 playing, 2 game
     * over, 3 level completed, 4 loaded (the flythrough and its "press
     * enter" screen), 7 quitting.  6 is high-score name entry (GameTick
     * sets it after InsertScoreIntoHighScoreTable); 5 is not decoded.
     * stateRef() is the field as an lvalue. */
    unsigned char  state() const                     { return state_; }
    void           setState(unsigned char s)         { state_ = s; }
    unsigned char &stateRef()                        { return state_; }
    /* GameTick runs InitLevelBasedSounds while this is 0, then sets it;
     * Load, SetupLevelObjects and the 3D-sound toggle clear it. */
    int            levelSoundsReady() const          { return levelSoundsReady_; }
    void           setLevelSoundsReady(int r)        { levelSoundsReady_ = r; }
    /* Save-name text entry in progress (nameEntry()):
     * HandleKeypress sets it on the save-slot nodes, routes keys to the
     * entry while it is set, and clears it when entry ends. */
    int            textEntryActive() const           { return textEntryActive_; }
    void           setTextEntryActive(int a)         { textEntryActive_ = a; }

    /* ── the level sequence ─────────────────────────────────────────── */
    /* The level being played, 0-based (StoreGameStateIntoSaveSlot saves it),
     * and how many levels the game file lists (LoadGameFile; zero when it
     * did not load).  index + 1 == count is the last level. */
    unsigned char  levelIndex() const                { return levelIndex_; }
    void           setLevelIndex(unsigned char i)    { levelIndex_ = i; }
    unsigned char  levelCount() const                { return levelCount_; }
    /* CD music on: every Sim_PlayCDStuf call is gated on it; HandleKeypress
     * toggles it, RenderSoundOptions shows it, sound setup clears it on
     * failure.  A dword. */
    int            musicOn() const                   { return config_.musicOn(); }
    void           setMusicOn(int on)                { config_.setMusicOn(on); }

    /* Total play time over the whole game, ms: each level's timeElapsed is
     * added as it ends; the save slot stores it, ClearGameState zeroes it. */
    double         totalPlayTime() const             { return totalPlayTime_; }
    void           setTotalPlayTime(double ms)       { totalPlayTime_ = ms; }
    /* ClearGameState zeroes the two halves as separate dword stores with
     * unrelated stores between them, so they cannot be merged into one
     * double store (template 3: preserve the original's shape).  These are
     * the only callers, and gamereset.cpp says why at each. */
    unsigned int  &totalPlayTimeLowDword()  { return ((unsigned int *)&totalPlayTime_)[0]; }
    unsigned int  &totalPlayTimeHighDword() { return ((unsigned int *)&totalPlayTime_)[1]; }

    /* SetCurrentLevelName copies entry `levelNo & 0xff` into levelName_. */
    char          *levelNameBuffer()                 { return levelName_; }
    char          *levelNameTableEntry(unsigned char i) { return levelNameTable_[i]; }

    /* The level-report tallies (reportwriter.cpp owns all of them). */
    unsigned short reportLevelsWithScript() const    { return reportLevelsWithScript_; }
    void        setReportLevelsWithScript(unsigned short n) { reportLevelsWithScript_ = n; }
    unsigned short reportLevelsWithBonus() const     { return reportLevelsWithBonus_; }
    void        setReportLevelsWithBonus(unsigned short n)  { reportLevelsWithBonus_ = n; }
    unsigned short reportLevelsWithLeo() const       { return reportLevelsWithLeo_; }
    void        setReportLevelsWithLeo(unsigned short n)    { reportLevelsWithLeo_ = n; }
    unsigned short reportTallyA() const              { return reportTallyA_; }
    void        setReportTallyA(unsigned short n)    { reportTallyA_ = n; }
    int            reportScoreTotal() const          { return reportScoreTotal_; }
    void        setReportScoreTotal(int n)           { reportScoreTotal_ = n; }

    /* ── video quality (the options menu; config.h has the derivation) ─ */
    unsigned char &videoShadows()                    { return config_.videoShadows(); }
    unsigned char &videoReflection()                 { return config_.videoReflection(); }
    unsigned char &videoHighlights()                 { return config_.videoHighlights(); }
    unsigned char &videoParticles()                  { return config_.videoParticles(); }

    /* ── volumes (the options menu, HandleKeypress 0x3e/0x3f) ────────── */
    /* Percent, steps of 10, shown by RenderSoundOptions; each has the
     * device value HandleKeypress derives from it beside it. */
    unsigned char  cdVolume() const                  { return config_.cdVolume(); }
    void           setCdVolume(unsigned char p)      { config_.setCdVolume(p); }
    /* CD mixer volume, 0..65536 (CDM_SetMixerVolume). */
    unsigned int   cdMixerVolume() const             { return config_.cdMixerVolume(); }
    void           setCdMixerVolume(unsigned int v)  { config_.setCdMixerVolume(v); }
    unsigned char  waveVolume() const                { return config_.waveVolume(); }
    void           setWaveVolume(unsigned char p)    { config_.setWaveVolume(p); }
    /* Both channels packed, for waveOutSetVolume. */
    unsigned int   waveOutVolume() const             { return config_.waveOutVolume(); }
    void           setWaveOutVolume(unsigned int v)  { config_.setWaveOutVolume(v); }

    /* ── camera and controls ────────────────────────────────────────── */
    /* 0 = follow the player; nonzero = view from the separate eye,
     * cameraEye() (FramePose_Player / UpdateViewTransform), and 2 also spins
     * the yaw -- the menus and the tally.  Checkpoints restore it. */
    unsigned char  cameraMode() const                { return cameraMode_; }
    void           setCameraMode(unsigned char m)    { cameraMode_ = m; }
    /* The distance the camera eases towards (UpdateViewTransform subtracts
     * the current eye distance and closes a dt-scaled share of the gap):
     * 7.0 by default, 40.0 in CameraOverview, animated by the sway. */
    float          cameraDistance() const            { return cameraDistance_; }
    void           setCameraDistance(float d)        { cameraDistance_ = d; }
    /* The controls menu's camera option: 1 = the camera turns with the
     * player (UpdateViewTransform, FramePose_Player).  GameTick forces it to 1
     * while the player is gliding, parking the choice +10 in
     * parkedCameraOption. */
    unsigned char  cameraTurnsWithPlayer() const     { return config_.cameraTurnsWithPlayer(); }
    void           setCameraTurnsWithPlayer(unsigned char on) { config_.setCameraTurnsWithPlayer(on); }
    /* Where GameTick parks cameraTurnsWithPlayer while gliding forces
     * it on: the player's choice + 10, so 0 means "nothing parked".  Load
     * zeroes it. */
    unsigned char  parkedCameraOption() const        { return parkedCameraOption_; }
    void           setParkedCameraOption(unsigned char v) { parkedCameraOption_ = v; }
    /* The options menu's 3D-sound switch: handed to the SoundManager's
     * setup, and InitLevelBasedSounds adds the extra-object sounds only
     * while it is on. */
    int            sound3D() const                   { return config_.sound3D(); }
    void           setSound3D(int on)                { config_.setSound3D(on); }
    /* Joystick deadzone in percent, steps of 10 (ProgCtrl gets it x100). */
    unsigned short joyDeadzone() const               { return config_.joyDeadzone(); }
    /* The separate eye the camera views from when cameraMode() is nonzero
     * (FramePose_Player makes it the camera's focus, z negated);
     * checkpoints restore it from the script player. */
    void           setCameraEye(int i, float v)      { cameraEye_[i] = v; }
    float          cameraEye(int i) const            { return cameraEye_[i]; }
    /* A float vector RenderGameFrame's scripted-camera branch (taken
     * instead of UpdateViewTransform while the script player's spline is active) reads beside
     * the eye; checkpoints restore it from the script player's spline point,
     * and the level builder seeds it {0, 1000, 0}.  Not decoded. */
    void           setField13cc94(int i, float v)    { field_13cc94_[i] = v; }
    float          field13cc94(int i) const          { return field_13cc94_[i]; }
    /* zoomDistance is the zoom distance the Zoom In/Out actions step (clamped
     * 2..20); GameTick eases cameraDistance towards it and copies it back
     * when the overview ends.  overviewActive is the overview flag the OverView
     * action raises (with cameraDistance 40); GameTick and the level
     * builder clear it.  field_13cc90 is set by both zoom actions and cleared
     * by the same two; PRESERVED: nothing reads it. */
    void           setField13cc90(int v)             { field_13cc90_ = v; }
    float          zoomDistance() const              { return zoomDistance_; }
    void           setZoomDistance(float d)          { zoomDistance_ = d; }
    int            overviewActive() const            { return overviewActive_; }
    void           setOverviewActive(int v)          { overviewActive_ = v; }
    /* keypress.cpp: field_13cc88 is a one-shot latch (cleared, then set on
     * the first press, which also sets field_13cc8c). */
    int            field_13cc88() const              { return field_13cc88_; }
    void           setField13cc88(int v)             { field_13cc88_ = v; }
    int            field_13cc8c() const              { return field_13cc8c_; }
    void           setField13cc8c(int v)             { field_13cc8c_ = v; }
    /* The buffer HandleTypedCheatCode matches typed cheats in. */
    unsigned char *cheatBuffer()                     { return cheatBuffer_; }
    void           setJoyDeadzone(unsigned short p)  { config_.setJoyDeadzone(p); }

    /* ── the tally's inputs (CalculateLevelScore) ────────────────────── */
    /* Death restarts on this level: GameTick adds one each time ENTER
     * restarts it after a death, and zeroes it when the level ends (as do
     * ClearGameState and the level-skip cheat).  While it is nonzero the
     * restart is a RETRY, not a fresh level: SetupLevelObjects keeps the
     * kill and item counters, OpenLevelFile skips the bonus peek, the
     * music and extra-object sounds are not reloaded, and
     * CalculateLevelScore denies the all-items bonus.  A byte; it wraps. */
    unsigned char  restartCount() const              { return restartCount_; }
    void           setRestartCount(unsigned char n)  { restartCount_ = n; }
    /* Items the level holds (SetupLevelObjects copies its census total);
     * the all-items bonus needs Player::itemsCollected to reach it. */
    unsigned short itemTotal() const                 { return itemTotal_; }
    void           setItemTotal(unsigned short n)    { itemTotal_ = n; }
    unsigned char  foesKilled() const                { return foesKilled_; }
    void           setFoesKilled(unsigned char n)    { foesKilled_ = n; }
    /* The level's time limit in seconds (SetupLevelObjects copies it from
     * the map file's) and the play time against it in milliseconds. */
    int            timeLimit() const                 { return map_.timeLimit(); }
    void           setTimeLimit(int s)               { map_.setTimeLimit(s); }
    unsigned int   timeElapsed() const               { return map_.timeElapsed(); }
    void           setTimeElapsed(unsigned int ms)   { map_.setTimeElapsed(ms); }

    /* ── vitality and the score tally ───────────────────────────────── */
    /* The tally's "vitality" row, which pays it 1:1:
     * Player::completionNumerator per second of field_170a65, times 25,
     * capped at 100 -- a RATE, not health (gamestate.cpp's note: it varies with movement).  Named for
     * the row it feeds, as determinism.cpp and the manifest already do. */
    unsigned char  vitalityPercent() const           { return vitalityPercent_; }
    void           setVitalityPercent(unsigned char p) { vitalityPercent_ = p; }
    /* The divisor: GameTick adds each tick's clock step to it and the
     * level builder zeroes it, so likely elapsed level time.  Unconfirmed. */
    unsigned int   field_170a65() const              { return field_170a65_; }
    void           setField170a65(unsigned int n)    { field_170a65_ = n; }
    /* The end-of-level tally (CalculateLevelScore fills it,
     * AnimateScoreTallyStages counts it up). */
    ScoreTally    *tally()                           { return &tally_; }
    const ScoreTally *tally() const                  { return &tally_; }
    /* Set to 1 when the tally has finished counting up (or ENTER skipped
     * it); GameTick only takes ENTER to the high-score table once it is. */
    int            tallyDone() const                 { return tallyDone_; }
    void           setTallyDone(int d)               { tallyDone_ = d; }

    /* ── lifts ──────────────────────────────────────────────────────── */
    unsigned char liftCount() const              { return liftCount_; }
    void          setLiftCount(unsigned char n)  { liftCount_ = n; }
    LiftObject   *liftSlot(unsigned int i) const { return liftSlots_[i]; }
    void          setLiftSlot(unsigned int i, LiftObject *p) { liftSlots_[i] = p; }

    /* ── slides ─────────────────────────────────────────────────────── */
    unsigned char slideCount() const               { return slideCount_; }
    void          setSlideCount(unsigned char n)   { slideCount_ = n; }
    SlideObject  *slideSlot(unsigned int i) const  { return slideSlots_[i]; }
    void          setSlideSlot(unsigned int i, SlideObject *p) { slideSlots_[i] = p; }

    /* ── bridges ────────────────────────────────────────────────────── */
    /* Indexed by the bridge's switch slot, not the count; the count is a
     * running total (see BridgeObject::spawn). */
    unsigned char bridgeCount() const              { return bridgeCount_; }
    void          setBridgeCount(unsigned char n)  { bridgeCount_ = n; }
    BridgeObject *bridgeSlot(unsigned int i) const { return bridgeSlots_[i]; }
    void          setBridgeSlot(unsigned int i, BridgeObject *p) { bridgeSlots_[i] = p; }

    /* ── breakable tiles ────────────────────────────────────────────── */
    unsigned char  breakableCount() const              { return breakableCount_; }
    void           setBreakableCount(unsigned char n)  { breakableCount_ = n; }
    BreakableTile *breakableSlot(unsigned int i) const { return breakableSlots_[i]; }
    void           setBreakableSlot(unsigned int i, BreakableTile *p) { breakableSlots_[i] = p; }

    /* ── bombs (the game's "enemy" table) ───────────────────────────── */
    /* Slots are indexed by ID; the ID list holds the live IDs, count long.
     * The *Ref accessors hand out addresses because the remove's tail
     * (objectremove.cpp) edits them in place, and the removes re-read the
     * slot through its address exactly as the original does. */
    unsigned char  bombCount() const               { return bombCount_; }
    void           setBombCount(unsigned char n)   { bombCount_ = n; }
    unsigned char *bombCountRef()   { return &bombCount_; }
    unsigned char  bombId(unsigned int i) const    { return bombIds_[i]; }
    unsigned char *bombIds()        { return bombIds_; }
    Bomb          *bombSlot(unsigned int id) const { return bombSlots_[id]; }
    Bomb         **bombSlotRef(unsigned int id)  { return &bombSlots_[id]; }

    /* ── foes ───────────────────────────────────────────────────────── */
    /* The same slot/count/ID-list shape as the bombs, and the same *Ref
     * accessors for the shared remove tail. */
    unsigned char  foeCount() const               { return foeCount_; }
    void           setFoeCount(unsigned char n)   { foeCount_ = n; }
    unsigned char *foeCountRef()    { return &foeCount_; }
    unsigned char  foeId(unsigned int i) const    { return foeIds_[i]; }
    unsigned char *foeIds()         { return foeIds_; }
    Foe           *foeSlot(unsigned int id) const { return foeSlots_[id]; }
    Foe          **foeSlotRef(unsigned int id)   { return &foeSlots_[id]; }
    /* A WORD the foe spawn bumps for type 0x0b; levelsetup.cpp calls it a
     * crystal count.  Not confirmed, so not named. */
    unsigned short field_42252() const             { return field_42252_; }
    void           setField42252(unsigned short n) { field_42252_ = n; }
    /* ── the player ─────────────────────────────────────────────────── */
    Player       *player()       { return &player_; }
    const Player *player() const { return &player_; }

    /* The current level's path; diagnostics only. */
    const char    *levelName() const               { return levelName_; }

    bool initialised() const {return initialised_; }

    /* The theme sound entries the spawns and level sounds acquire, named
     * by their old Game offsets; the index is the theme event id. */
    const SoundAssetName *soundAsset42262() const { return &themeSounds_.entries[0]; }
    const SoundAssetName *soundAsset4236e() const { return &themeSounds_.entries[1]; }
    const SoundAssetName *soundAsset4247a() const { return &themeSounds_.entries[2]; }
    const SoundAssetName *soundAsset42586() const { return &themeSounds_.entries[3]; }
    const SoundAssetName *soundAsset42692() const { return &themeSounds_.entries[4]; }
    const SoundAssetName *soundAsset4279e() const { return &themeSounds_.entries[5]; }
    const SoundAssetName *soundAsset428aa() const { return &themeSounds_.entries[6]; }
    const SoundAssetName *soundAsset429b6() const { return &themeSounds_.entries[7]; }
    const SoundAssetName *soundAsset42ac2() const { return &themeSounds_.entries[8]; }
    const SoundAssetName *soundAsset42bce() const { return &themeSounds_.entries[9]; }
    const SoundAssetName *soundAsset42cda() const { return &themeSounds_.entries[10]; }
    const SoundAssetName *soundAsset42de6() const { return &themeSounds_.entries[11]; }
    const SoundAssetName *soundAsset42ef2() const { return &themeSounds_.entries[12]; }
    const SoundAssetName *soundAsset42ffe() const { return &themeSounds_.entries[13]; }
    const SoundAssetName *soundAsset4310a() const { return &themeSounds_.entries[14]; }
    const SoundAssetName *soundAsset43216() const { return &themeSounds_.entries[15]; }
    const SoundAssetName *soundAsset441ca() const { return &themeSounds_.entries[30]; }
    const SoundAssetName *soundAsset44c42() const { return &themeSounds_.entries[40]; }
    const SoundAssetName *soundAsset44d4e() const { return &themeSounds_.entries[41]; }
    const SoundAssetName *soundAsset44e5a() const { return &themeSounds_.entries[42]; }
    const SoundAssetName *soundAsset456ba() const { return &themeSounds_.entries[50]; }
    const SoundAssetName *soundAsset457c6() const { return &themeSounds_.entries[51]; }
    const SoundAssetName *soundAsset458d2() const { return &themeSounds_.entries[52]; }
    const SoundAssetName *soundAsset46132() const { return &themeSounds_.entries[60]; }
    const SoundAssetName *soundAsset46baa() const { return &themeSounds_.entries[70]; }
    ThemeSoundTable      *themeSounds()             { return &themeSounds_; }

    /* ── the lifecycle (game.cpp) ───────────────────────────────────────
     * construct() builds every member, reads the .gam, save slots,
     * Karoo.cfg and high scores, then enters the first level.  destruct()
     * saves the scores and config and tears it all down.  The vtable has
     * one slot, the scalar deleting destructor. */
    Game *construct(const char *gameName);
    void  destruct();

private:
    int   loadGameFile(const char *name);   /* the .gam: the level names */
    void  releaseAllSounds();

private:
    Game() = delete;   /* game-owned; only ever reached by pointer */

    const void   *vtable_;                                /* ours, one slot */
    double        field_04_;                              /* Load sets 1.0; reader not decoded */
    int           field_0c_;
    unsigned int  mapChanged_;
    unsigned int  nextLevelBonus_;
    unsigned int  tickCount_;
    MenuTree      rootMenu_;                              /* constructed, never navigated */
    int           initialised_;                           /* 0 until Load completes */
    TimedSpawner  timedSpawners_[256];
    FreeBomb      freeBombs_[256];
    CdThemes      cdThemes_;
    unsigned char parkedCameraOption_;
    /* 256 level names, 0x100 each.  SetCurrentLevelName copies entry `levelNo & 0xff` into levelName_. */
    char          levelNameTable_[256][0x100];
    unsigned char levelCount_;
    char          gameFileName_[0x80];
    LevelCensus   census_;
    unsigned char restartCount_;
    /* The level-report tallies, zeroed and accumulated by
     * Report_WriteLevelReport (reportwriter.cpp) and by nothing else. */
    unsigned short reportLevelsWithScript_;
    int            reportScoreTotal_;
    unsigned short reportTallyA_;
    unsigned short reportLevelsWithBonus_;
    unsigned short reportLevelsWithLeo_;
    /* Foes killed this level; CalculateLevelScore pays 50 each. */
    unsigned char foesKilled_;
    unsigned short itemTotal_;
    unsigned short field_42252_;
    int           levelSoundsReady_;
    ThemeSoundTable themeSounds_;
    unsigned char switchMax_;
    unsigned char stateBeforeMenu_;
    unsigned int  field_48b14_;
    char          menuLevelName_[0x80];
    ExtraObjects  extraObjects_;
    SoundManager  soundManager_;
    FixedSounds   fixedSounds_;
    int           field_13cc84_;                          /* Load zeroes; reader not decoded */
    int           field_13cc88_;
    int           field_13cc8c_;
    int           field_13cc90_;
    float         field_13cc94_[3];
    float         zoomDistance_;
    int           overviewActive_;
    /* The typed-cheat buffer; declared up to the cheat entry that follows.
     * Its real length is not established. */
    unsigned char cheatBuffer_[0x100];
    TextEntry     cheatEntry_;
    HighScoreTable highScores_;
    ScoreTally    tally_;
    SwitchCells   switchCells_;
    BridgeObject *bridgeSlots_[256];
    unsigned char bridgeCount_;
    double        totalPlayTime_;
    double        lastTickTime_;
    double        clock_;
    TickStep      tickStep_;
    /* Recomputed by GameTick every tick; see vitalityPercent(). */
    unsigned char vitalityPercent_;
    unsigned int  field_170a65_;
    int           textEntryActive_;
    TextEntry     nameEntry_;
    SaveSlots     saveSlots_;
    /* As long as a level-name table entry. */
    char          levelName_[0x100];
    unsigned char levelIndex_;
    int           field_173584_;
    SlideObject  *slideSlots_[100];
    unsigned char slideCount_;
    LiftObject   *liftSlots_[256];
    unsigned char liftCount_;
    int           field_173b1a_;
    BreakableTile *breakableSlots_[200];
    unsigned char breakableCount_;
    Bomb         *bombSlots_[500];
    unsigned char bombCount_;
    unsigned char bombIds_[500];
    Foe          *foeSlots_[500];
    unsigned char foeCount_;
    unsigned char foeIds_[500];
    Player        player_;
    unsigned char rebindCode_;
    char          rebindAction_[0x100];
    int           rebindActive_;
    unsigned char debounce_;
    MenuTree      menu_;
    ScriptPlayer  scriptPlayer_;
    float         cameraDistance_;
    unsigned char cameraMode_;
    /* The settings (config.h); music, volumes, 3D sound, the camera
     * option and the joystick deadzone live in its persisted blob. */
    Config        config_;
    float         cameraEye_[3];
    unsigned char state_;
    /* The map: header, grid and snapshot grid.  Its header holds the time limit (GameTick times the
     * level out at timeLimit*1000 ms), the ms of play (CalculateLevelScore
     * pays the unused seconds) and the gem quota (CalculateLevelScore
     * pays 5 a gem up to it and 10 per gem beyond). */
    LevelMap      map_;
    int           tallyDone_;
};

/* The lifecycle (game.cpp): construct (returns self), destruct, and the
 * scalar deleting destructor, slot 0 of the vtable, which WinMain's
 * teardown calls. */
Game *Game_Construct(Game *self, const char *gameName);
void Game_Destruct(Game *self);
Game *Game_ScalarDestructor(Game *self, unsigned char flags);
