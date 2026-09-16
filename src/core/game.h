/* Game -- the one global game object (COHESION_PLAN.md Band 3).
 *
 * SKETCH.  Band 3 is last in the plan; until then this class grows one
 * field at a time, as each object class needs one.  Files not yet converted
 * still use their own G_* defines -- when one is converted, its defines are
 * deleted in favour of these accessors.
 *
 * Real fields at the game's offsets, packed; everything unknown is a gap
 * whose size is written as END - START of its neighbours.  Only the offsets
 * are asserted (layout.h), never the gaps, so splitting a gap for a new
 * field changes no existing assertion.  Unknown-meaning fields are named
 * field_<offset>; naming is RE work and happens in Ghidra first.
 */
#pragma once

#include "layout.h"
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

class SoundManager;
class LiftObject;
class SlideObject;
class BridgeObject;
class BreakableTile;

/* The tick step at Game+0x170a5c: GameTick's `dt` argument, a double
 * (GameTick 0x41567c and the six Camera* handlers FLD it as one; GameTick
 * zeroes it while paused, state 5).  Every level object keeps a pointer to
 * it and copies it to its own +0x15 each tick.  Wrapped in a packed
 * struct so that it is 1-aligned: objects hold it at odd offsets and pass
 * its address to byte-copy helpers, which a bare double would warn on. */
struct __attribute__((packed)) TickStep {
    double value;
};

/* A sound asset's file name and, immediately after it, its enabled flag.
 * The spawn's unbounded strcpy of `name` relies on the flag to stop it. */
struct __attribute__((packed)) SoundAssetName {
    char name[256];
    int  enabled;
};

/* The end-of-level score tally, Game+0x1404c1..+0x140543.  Six rows, each
 * with a real COUNT and SCORE (CalculateLevelScore 0x41a760) and a SHOWN
 * count and score that AnimateScoreTallyStages 0x41a970 counts up from zero
 * towards them.  The arrays are in offset order; the animation's stages
 * visit FOES before TIME.  The two gaps are never touched by either. */
enum TallyRow {
    TALLY_GEMS, TALLY_SURPLUS, TALLY_TIME, TALLY_FOES, TALLY_ALLITEMS,
    TALLY_VITALITY, TALLY_ROWS
};

struct __attribute__((packed)) ScoreTally {
    static const int ORIGIN = 0;

    int           shownScore[TALLY_ROWS];   /* +0x00  0x1404c1 */
    int           shownBase;                /* +0x18  running total before this level */
    int           shownCount[TALLY_ROWS];   /* +0x1c  0x1404dd */
    int           shownLevelTotal;          /* +0x34  sum of shownScore */
    int           shownGrandTotal;          /* +0x38  shownBase + shownLevelTotal */
    unsigned char gap_3c[0x41 - 0x3c];
    int           score[TALLY_ROWS];        /* +0x41  0x140502 */
    unsigned char gap_59[0x5d - 0x59];
    int           count[TALLY_ROWS];        /* +0x5d  0x14051e */
    int           levelTotal;               /* +0x75  0x140536 */
    int           grandTotal;               /* +0x79  0x14053a */
    unsigned char stage;                    /* +0x7d  animation state, 0..6 */
    int           stageStart;               /* +0x7e  ms, the stage's start */

    KAROO_LAYOUT_REGISTER(ScoreTally);
};

KAROO_LAYOUT_CHECKS(ScoreTally)
{
    KAROO_LAYOUT_AT(shownScore,      0x00);
    KAROO_LAYOUT_AT(shownBase,       0x18);
    KAROO_LAYOUT_AT(shownCount,      0x1c);
    KAROO_LAYOUT_AT(shownLevelTotal, 0x34);
    KAROO_LAYOUT_AT(shownGrandTotal, 0x38);
    KAROO_LAYOUT_AT(score,           0x41);
    KAROO_LAYOUT_AT(count,           0x5d);
    KAROO_LAYOUT_AT(levelTotal,      0x75);
    KAROO_LAYOUT_AT(grandTotal,      0x79);
    KAROO_LAYOUT_AT(stage,           0x7d);
    KAROO_LAYOUT_AT(stageStart,      0x7e);
    /* Tiles its span, 0x1404c1..0x140543, with no field missing. */
    KAROO_LAYOUT_SIZE(0x140543 - 0x1404c1);
}

/* The fixed sounds AcquireFixedSoundBuffersAndMaybeReport 0x41a280 loads
 * once, Game+0x13cc5c..+0x13cc84.  They are GAME's: the SoundManager is
 * 0xb4 bytes and ends exactly here (none of its ctor 0x4430e0, dtor
 * 0x443180, purge 0x443520, init 0x4431f0 or setup 0x4439d0 touches past
 * its second list at +0xa4), and only Game code writes them.  Ghidra's
 * 288-byte SoundManager struct over-reaches into them. */
struct CStaticSoundbuffer;
struct VoicePool;
struct __attribute__((packed)) FixedSounds {
    static const int ORIGIN = 0;

    CStaticSoundbuffer *timeOut;          /* +0x00  0x13cc5c */
    CStaticSoundbuffer *switchClick;      /* +0x04  menu select */
    VoicePool          *menuUpDown;       /* +0x08  5 voices */
    CStaticSoundbuffer *count;            /* +0x0c */
    CStaticSoundbuffer *lastSeconds;      /* +0x10  the countdown */
    CStaticSoundbuffer *levelCompleted;   /* +0x14 */
    /* Three banks, 'A'..'C'; GameTick picks one by rand()%3 when the
     * crystals are complete. */
    CStaticSoundbuffer *crystalBank[3];   /* +0x18 */
    /* Set once the load has run (or found no sound); it never runs again. */
    unsigned int        loaded;           /* +0x24  0x13cc80 */

    KAROO_LAYOUT_REGISTER(FixedSounds);
};

KAROO_LAYOUT_CHECKS(FixedSounds)
{
    KAROO_LAYOUT_AT(switchClick,    0x04);
    KAROO_LAYOUT_AT(menuUpDown,     0x08);
    KAROO_LAYOUT_AT(count,          0x0c);
    KAROO_LAYOUT_AT(lastSeconds,    0x10);
    KAROO_LAYOUT_AT(levelCompleted, 0x14);
    KAROO_LAYOUT_AT(crystalBank,    0x18);
    KAROO_LAYOUT_AT(loaded,         0x24);
    KAROO_LAYOUT_SIZE(0x28);
}

class Bomb;
class Foe;
class Player;

class __attribute__((packed)) Game {
public:
    static const int ORIGIN = 0;

    /* The game's one Game object, through its global pointer 0x0046c498
     * (NULL until the game has built it).  The only place that address is
     * named. */
    static Game *instance()          { return *(Game **)0x0046c498; }

    /* ── sound ──────────────────────────────────────────────────────── */
    SoundManager *soundManager()     { return (SoundManager *)soundManagerHead_; }
    /* Nonzero once sound is up. */
    int  soundCreated() const        { return soundCreated_; }
    FixedSounds  *fixedSounds()      { return &fixedSounds_; }

    /* ── time ───────────────────────────────────────────────────────── */
    /* The `now` GameTick was last called with (double, ms); the menus
     * stamp their timers from it.  Its two halves are also copied dword by
     * dword into the menu's lock start (MenuTree::setLockStart). */
    double         lastTickTime() const              { return lastTickTime_; }
    void           setLastTickTime(double t)         { lastTickTime_ = t; }
    /* The 8-byte clock accumulator.  Objects keep a pointer to it and
     * re-read it every tick.  Addressed via offsetof rather than &clock_,
     * which GCC flags for a packed member (-Waddress-of-packed-member); the
     * offset still comes from the field, and 0x170a54 is 4-aligned. */
    double        *clock()
    {
        return (double *)((unsigned char *)this + offsetof(Game, clock_));
    }
    /* The tick step, dt; objects copy it to their +0x15 each tick. */
    TickStep      *tickStep()        { return &tickStep_; }

    /* ── tiles ──────────────────────────────────────────────────────── */
    /* ── level-load flags ───────────────────────────────────────────── */
    /* +0x0c: the level builder's missing-CD check fires only while it is
     * zero.  Not decoded. */
    int            field_0c() const                  { return field_0c_; }
    /* +0x10: 1 when the last level load read a different map name from the
     * one before it (both loaders, levelparse.cpp). */
    unsigned int   mapChanged() const                { return mapChanged_; }
    void           setMapChanged(unsigned int c)     { mapChanged_ = c; }
    /* +0x14: the NEXT level's bonus flag, which OpenLevelFile peeks by
     * loading that level's map first.  SetupLevelObjects reads it to give
     * the completed-level menu node (0x28) two children when it is 0 and
     * one otherwise -- the builder called it a "mode flag". */
    unsigned int   nextLevelBonus() const            { return nextLevelBonus_; }
    void           setNextLevelBonus(unsigned int b) { nextLevelBonus_ = b; }
    /* +0x173b1a: GameTick latches it to 1 the first time the exit
     * condition is met in a level (and plays a crystal sound); the level
     * builder clears it.  Named by offset. */
    int            field_173b1a() const              { return field_173b1a_; }
    void           setField173b1a(int v)             { field_173b1a_ = v; }

    /* +0x173584: the level builder sets it to 1; GameTick loads the level
     * sounds only while it and levelSoundsReady() are both 0.  Named by
     * offset. */
    int            field_173584() const              { return field_173584_; }
    void           setField173584(int v)             { field_173584_ = v; }
    /* +0x18: GameTick adds one per tick and returns it with the low byte
     * masked off; +0x48b14 is bumped beside it.  Neither is read elsewhere
     * in our code. */
    unsigned int   tickCount() const                 { return tickCount_; }
    void           setTickCount(unsigned int n)      { tickCount_ = n; }
    unsigned int   field_48b14() const               { return field_48b14_; }
    void           setField48b14(unsigned int n)     { field_48b14_ = n; }
    /* The game file's name (the .gam, the save-slot files' base and the
     * final directory's %s), 0x80 bytes up to the census. */
    const char    *gameFileName() const              { return gameFileName_; }
    /* The level the main menu shows behind it, which GameTick reloads on
     * returning there; 0x80 bytes up to the extra objects. */
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
    /* The base Tile::at() indexes from -- the map's address.  Objects keep
     * their own copy. */
    unsigned char *tileBase()        { return map_.tileBase(); }
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
     * until it is released.  debounceRef() is for the DEB lvalue aliases. */
    unsigned char  debounce() const                  { return debounce_; }
    void           setDebounce(unsigned char k)      { debounce_ = k; }
    unsigned char &debounceRef()                     { return debounce_; }
    /* Key-rebind capture: the controls menu's rebind nodes name the action
     * and set the flag; HandleKeypress captures the next binding and clears
     * it.  RenderControlsRemap reads the flag. */
    int            rebindActive() const              { return rebindActive_; }
    void           setRebindActive(int a)            { rebindActive_ = a; }
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
     * stateRef() is for the files that alias it as a STATE lvalue. */
    unsigned char  state() const                     { return state_; }
    void           setState(unsigned char s)         { state_ = s; }
    unsigned char &stateRef()                        { return state_; }
    /* GameTick runs InitLevelBasedSounds while this is 0, then sets it;
     * Load, SetupLevelObjects and the 3D-sound toggle clear it. */
    int            levelSoundsReady() const          { return levelSoundsReady_; }
    void           setLevelSoundsReady(int r)        { levelSoundsReady_ = r; }
    /* Save-name text entry in progress (the entry object is at +0x170a6d):
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
     * toggles it, RenderGameOptions shows it, sound setup clears it on
     * failure.  A dword. */
    int            musicOn() const                   { return config_.musicOn(); }
    void           setMusicOn(int on)                { config_.setMusicOn(on); }

    /* Total play time over the whole game, ms: each level's timeElapsed is
     * added as it ends; the save slot stores it, ClearGameState zeroes it. */
    double         totalPlayTime() const             { return totalPlayTime_; }
    void           setTotalPlayTime(double ms)       { totalPlayTime_ = ms; }

    /* ── volumes (the options menu, HandleKeypress 0x3e/0x3f) ────────── */
    /* Percent, steps of 10, shown by RenderGameOptions; each has the
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
    /* 0 = follow the player; nonzero = view from the separate eye at
     * +0x2ab580 (FUN_00404120 / UpdateViewTransform), and 2 also spins the
     * yaw -- the menus and the tally.  Checkpoints restore it. */
    unsigned char  cameraMode() const                { return cameraMode_; }
    void           setCameraMode(unsigned char m)    { cameraMode_ = m; }
    /* The distance the camera eases towards (UpdateViewTransform subtracts
     * the current eye distance and closes a dt-scaled share of the gap):
     * 7.0 by default, 40.0 in CameraOverview, animated by the sway. */
    float          cameraDistance() const            { return cameraDistance_; }
    void           setCameraDistance(float d)        { cameraDistance_ = d; }
    /* The controls menu's camera option: 1 = the camera turns with the
     * player (UpdateViewTransform, FUN_00404120).  GameTick forces it to 1
     * while Player+0xea is set, parking the choice +10 at +0x3215d. */
    unsigned char  cameraTurnsWithPlayer() const     { return config_.cameraTurnsWithPlayer(); }
    void           setCameraTurnsWithPlayer(unsigned char on) { config_.setCameraTurnsWithPlayer(on); }
    /* Where GameTick parks cameraTurnsWithPlayer while Player+0xea forces
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
     * (FUN_00404120 reads it as a float vector); checkpoints restore it
     * from the script player. */
    void           setCameraEye(int i, float v)      { cameraEye_[i] = v; }
    float          cameraEye(int i) const            { return cameraEye_[i]; }
    /* The eye as raw dwords: the level builder zeroes and copies it with
     * dword MOVs, and a bit copy is what keeps that exact. */
    unsigned int   cameraEyeBits(int i) const
    {
        typedef unsigned int __attribute__((aligned(1))) u32_ua;
        return ((const u32_ua *)((const unsigned char *)this +
                                 offsetof(Game, cameraEye_)))[i];
    }
    void           setCameraEyeBits(int i, unsigned int b)
    {
        typedef unsigned int __attribute__((aligned(1))) u32_ua;
        ((u32_ua *)((unsigned char *)this + offsetof(Game, cameraEye_)))[i] = b;
    }
    /* A float vector FUN_00404120 reads beside the eye; checkpoints
     * restore it from the script player's spline point, and the level
     * builder seeds it {0, 1000, 0}.  Not decoded. */
    void           setField13cc94(int i, float v)    { field_13cc94_[i] = v; }
    /* Fields beside it, named by offset: GameTick zeroes +0x13cc90 and
     * +0x13cca8 with the level builder, and copies the float +0x13cca4
     * into the camera distance when the player dies. */
    void           setField13cc90(int v)             { field_13cc90_ = v; }
    /* keypress.cpp: +0x13cc88 is a one-shot latch (cleared, then set on
     * the first press, which also sets +0x13cc8c).  Named by offset. */
    int            field_13cc88() const              { return field_13cc88_; }
    void           setField13cc88(int v)             { field_13cc88_ = v; }
    void           setField13cc8c(int v)             { field_13cc8c_ = v; }
    /* The buffer HandleTypedCheatCode matches typed cheats in. */
    unsigned char *cheatBuffer()                     { return cheatBuffer_; }
    float          field_13cca4() const              { return field_13cca4_; }
    void           setField13cca8(int v)             { field_13cca8_ = v; }
    void           setJoyDeadzone(unsigned short p)  { config_.setJoyDeadzone(p); }

    /* ── the tally's inputs (CalculateLevelScore 0x41a760) ─────────── */
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
     * +0x2ab71f) and the play time against it in milliseconds. */
    int            timeLimit() const                 { return map_.timeLimit(); }
    void           setTimeLimit(int s)               { map_.setTimeLimit(s); }
    unsigned int   timeElapsed() const               { return map_.timeElapsed(); }
    void           setTimeElapsed(unsigned int ms)   { map_.setTimeElapsed(ms); }

    /* ── vitality and the score tally ───────────────────────────────── */
    /* The tally's "vitality" row, which pays it 1:1: Player::fieldD8 per
     * second of field_170a65, times 25, capped at 100 -- a RATE, not
     * health (gamestate.cpp's note: it varies with movement).  Named for
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
    unsigned char *bombCountRef()   { return (unsigned char *)this + offsetof(Game, bombCount_); }
    unsigned char  bombId(unsigned int i) const    { return bombIds_[i]; }
    unsigned char *bombIds()        { return (unsigned char *)this + offsetof(Game, bombIds_); }
    Bomb          *bombSlot(unsigned int id) const { return bombSlots_[id]; }
    Bomb         **bombSlotRef(unsigned int id)
    {
        return (Bomb **)((unsigned char *)this + offsetof(Game, bombSlots_)) + id;
    }

    /* ── foes ───────────────────────────────────────────────────────── */
    /* The same slot/count/ID-list shape as the bombs, and the same *Ref
     * accessors for the shared remove tail. */
    unsigned char  foeCount() const               { return foeCount_; }
    void           setFoeCount(unsigned char n)   { foeCount_ = n; }
    unsigned char *foeCountRef()    { return (unsigned char *)this + offsetof(Game, foeCount_); }
    unsigned char  foeId(unsigned int i) const    { return foeIds_[i]; }
    unsigned char *foeIds()         { return (unsigned char *)this + offsetof(Game, foeIds_); }
    Foe           *foeSlot(unsigned int id) const { return foeSlots_[id]; }
    Foe          **foeSlotRef(unsigned int id)
    {
        return (Foe **)((unsigned char *)this + offsetof(Game, foeSlots_)) + id;
    }
    /* A WORD the foe spawn bumps for type 0x0b; levelsetup.cpp calls it a
     * crystal count.  Not confirmed, so not named. */
    unsigned short field_42252() const             { return field_42252_; }
    void           setField42252(unsigned short n) { field_42252_ = n; }
    /* ── the player ─────────────────────────────────────────────────── */
    /* Embedded at +0x1751c9 (player.h).  A cast rather than a member:
     * player.h includes this header, through movableentity.h. */
    Player       *player()       { return reinterpret_cast<Player *>(gap_1751c9); }
    const Player *player() const { return reinterpret_cast<const Player *>(gap_1751c9); }

    /* The current level's path; diagnostics only. */
    const char    *levelName() const               { return levelName_; }

    /* The named sound assets the spawns and level sounds acquire: a 256-byte
     * file name with its enabled flag immediately after.  Named by offset;
     * meanings not decoded.  They sit on a 0x10c stride (0x42262 + k*0x10c
     * covers every one up to 0x42ffe), so they are likely one table of
     * 0x10c-byte records -- not yet modelled as one. */
    const SoundAssetName *soundAsset42262() const { return &soundAsset42262_; }
    const SoundAssetName *soundAsset4236e() const { return &soundAsset4236e_; }
    const SoundAssetName *soundAsset4247a() const { return &soundAsset4247a_; }
    const SoundAssetName *soundAsset42586() const { return &soundAsset42586_; }
    const SoundAssetName *soundAsset42692() const { return &soundAsset42692_; }
    const SoundAssetName *soundAsset4279e() const { return &soundAsset4279e_; }
    const SoundAssetName *soundAsset428aa() const { return &soundAsset428aa_; }
    const SoundAssetName *soundAsset429b6() const { return &soundAsset429b6_; }
    const SoundAssetName *soundAsset42ac2() const { return &soundAsset42ac2_; }
    const SoundAssetName *soundAsset42bce() const { return &soundAsset42bce_; }
    const SoundAssetName *soundAsset42cda() const { return &soundAsset42cda_; }
    const SoundAssetName *soundAsset42de6() const { return &soundAsset42de6_; }
    const SoundAssetName *soundAsset42ef2() const { return &soundAsset42ef2_; }
    const SoundAssetName *soundAsset42ffe() const { return &soundAsset42ffe_; }
    const SoundAssetName *soundAsset4310a() const { return &soundAsset4310a_; }
    const SoundAssetName *soundAsset43216() const { return &soundAsset43216_; }
    const SoundAssetName *soundAsset441ca() const { return &soundAsset441ca_; }
    const SoundAssetName *soundAsset44c42() const { return &soundAsset44c42_; }
    const SoundAssetName *soundAsset44d4e() const { return &soundAsset44d4e_; }
    const SoundAssetName *soundAsset44e5a() const { return &soundAsset44e5a_; }
    const SoundAssetName *soundAsset456ba() const { return &soundAsset456ba_; }
    const SoundAssetName *soundAsset457c6() const { return &soundAsset457c6_; }
    const SoundAssetName *soundAsset458d2() const { return &soundAsset458d2_; }
    const SoundAssetName *soundAsset46132() const { return &soundAsset46132_; }
    const SoundAssetName *soundAsset46baa() const { return &soundAsset46baa_; }

    /* ── game code still called ─────────────────────────────────────── */
    /* PLACEHOLDER: ClaimSpareObjectIdSlot 0x00417250, __thiscall on Game.
     * Ghidra types it void, but both spawns read AL as the new ID -- see
     * objectspawn.cpp for why it is called through rather than rewritten. */
    unsigned char claimSpareObjectId(unsigned char *ids, unsigned char *count)
    {
        typedef unsigned char (__attribute__((thiscall)) *fn)(Game *, unsigned char *,
                                                              unsigned char *);
        return ((fn)0x00417250)(this, ids, count);
    }

private:
    Game() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(Game);

    unsigned char gap_000000[0x00000c - 0x000000];
    int           field_0c_;                              /* 0x00000c */
    unsigned int  mapChanged_;                            /* 0x000010 */
    unsigned int  nextLevelBonus_;                        /* 0x000014 */
    unsigned int  tickCount_;                             /* 0x000018 */
    unsigned char gap_00001c[0x02023d - 0x00001c];
    TimedSpawner  timedSpawners_[256];                    /* 0x02023d */
    FreeBomb      freeBombs_[256];                        /* 0x02173d */
    unsigned char gap_02223d[0x02223f - 0x02223d];
    CdThemes      cdThemes_;                              /* 0x02223f */
    unsigned char parkedCameraOption_;                    /* 0x03215d */
    unsigned char gap_03215e[0x04215e - 0x03215e];
    unsigned char levelCount_;                            /* 0x04215e */
    char          gameFileName_[0x0421df - 0x04215f];     /* 0x04215f */
    LevelCensus   census_;                                /* 0x0421df */
    unsigned char restartCount_;                          /* 0x04220b */
    unsigned char gap_04220c[0x04224d - 0x04220c];
    /* Foes killed this level; CalculateLevelScore pays 50 each. */
    unsigned char foesKilled_;                            /* 0x04224d */
    unsigned char gap_04224e[0x042250 - 0x04224e];
    unsigned short itemTotal_;                            /* 0x042250 */
    unsigned short field_42252_;                          /* 0x042252 */
    int           levelSoundsReady_;                      /* 0x042254 */
    unsigned char gap_042258[0x042262 - 0x042258];
    SoundAssetName soundAsset42262_;                 /* 0x042262 */
    unsigned char gap_042366[0x04236e - 0x042366];
    SoundAssetName soundAsset4236e_;                 /* 0x04236e */
    unsigned char gap_042472[0x04247a - 0x042472];
    SoundAssetName soundAsset4247a_;                 /* 0x04247a */
    unsigned char gap_04257e[0x042586 - 0x04257e];
    SoundAssetName soundAsset42586_;                 /* 0x042586 */
    unsigned char gap_04268a[0x042692 - 0x04268a];
    SoundAssetName soundAsset42692_;                 /* 0x042692 */
    unsigned char gap_042796[0x04279e - 0x042796];
    SoundAssetName soundAsset4279e_;                 /* 0x04279e */
    unsigned char gap_0428a2[0x0428aa - 0x0428a2];
    SoundAssetName soundAsset428aa_;                 /* 0x0428aa */
    unsigned char gap_0429ae[0x0429b6 - 0x0429ae];
    SoundAssetName soundAsset429b6_;                 /* 0x0429b6 */
    unsigned char gap_042aba[0x042ac2 - 0x042aba];
    SoundAssetName soundAsset42ac2_;                 /* 0x042ac2 */
    unsigned char gap_042bc6[0x042bce - 0x042bc6];
    SoundAssetName soundAsset42bce_;                 /* 0x042bce */
    unsigned char gap_042cd2[0x042cda - 0x042cd2];
    SoundAssetName soundAsset42cda_;                 /* 0x042cda */
    unsigned char gap_042dde[0x042de6 - 0x042dde];
    SoundAssetName soundAsset42de6_;                 /* 0x042de6 */
    unsigned char gap_042eea[0x042ef2 - 0x042eea];
    SoundAssetName soundAsset42ef2_;                 /* 0x042ef2 */
    unsigned char gap_042ff6[0x042ffe - 0x042ff6];
    SoundAssetName soundAsset42ffe_;                 /* 0x042ffe */
    unsigned char gap_043102[0x04310a - 0x043102];
    SoundAssetName soundAsset4310a_;                 /* 0x04310a */
    unsigned char gap_04320e[0x043216 - 0x04320e];
    SoundAssetName soundAsset43216_;                 /* 0x043216 */
    unsigned char gap_04331a[0x0441ca - 0x04331a];
    SoundAssetName soundAsset441ca_;                 /* 0x0441ca */
    unsigned char gap_0442ce[0x044c42 - 0x0442ce];
    SoundAssetName soundAsset44c42_;                 /* 0x044c42 */
    unsigned char gap_044d46[0x044d4e - 0x044d46];
    SoundAssetName soundAsset44d4e_;                 /* 0x044d4e */
    unsigned char gap_044e52[0x044e5a - 0x044e52];
    SoundAssetName soundAsset44e5a_;                 /* 0x044e5a */
    unsigned char gap_044f5e[0x0456ba - 0x044f5e];
    SoundAssetName soundAsset456ba_;                 /* 0x0456ba */
    unsigned char gap_0457be[0x0457c6 - 0x0457be];
    SoundAssetName soundAsset457c6_;                 /* 0x0457c6 */
    unsigned char gap_0458ca[0x0458d2 - 0x0458ca];
    SoundAssetName soundAsset458d2_;                 /* 0x0458d2 */
    unsigned char gap_0459d6[0x046132 - 0x0459d6];
    SoundAssetName soundAsset46132_;                 /* 0x046132 */
    unsigned char gap_046236[0x046baa - 0x046236];
    SoundAssetName soundAsset46baa_;                 /* 0x046baa */
    unsigned char gap_046cae[0x048b12 - 0x046cae];
    unsigned char switchMax_;                             /* 0x048b12 */
    unsigned char stateBeforeMenu_;                       /* 0x048b13 */
    unsigned int  field_48b14_;                           /* 0x048b14 */
    char          menuLevelName_[0x048b98 - 0x048b18];    /* 0x048b18 */
    ExtraObjects  extraObjects_;                          /* 0x048b98 */
    /* The SoundManager is embedded here; its full size is unknown (its
     * lists reach at least +0xa4), so only the bytes up to the next field
     * we use are declared.  soundCreated_ sits inside it at +0x8c.
     * Its real size is 0xb4 (its ctor, dtor, purge, init and setup touch
     * nothing past the second list at +0xa4), so it ends exactly where
     * fixedSounds_ begins. */
    unsigned char soundManagerHead_[0x13cc34 - 0x13cba8];
    int           soundCreated_;                          /* 0x13cc34 */
    unsigned char gap_13cc38[0x13cc5c - 0x13cc38];        /* SoundManager tail */
    FixedSounds   fixedSounds_;                           /* 0x13cc5c */
    unsigned char gap_13cc84[0x13cc88 - 0x13cc84];
    int           field_13cc88_;                          /* 0x13cc88 */
    int           field_13cc8c_;                          /* 0x13cc8c */
    int           field_13cc90_;                          /* 0x13cc90 */
    float         field_13cc94_[3];                       /* 0x13cc94 */
    unsigned char gap_13cca0[0x13cca4 - 0x13cca0];
    float         field_13cca4_;                          /* 0x13cca4 */
    int           field_13cca8_;                          /* 0x13cca8 */
    /* The typed-cheat buffer; declared up to the cheat entry that follows.
     * Its real length is not established. */
    unsigned char cheatBuffer_[0x13cdac - 0x13ccac];      /* 0x13ccac */
    TextEntry     cheatEntry_;                            /* 0x13cdac */
    HighScoreTable highScores_;                           /* 0x13cdbb */
    ScoreTally    tally_;                                 /* 0x1404c1 */
    SwitchCells   switchCells_;                           /* 0x140543 */
    BridgeObject *bridgeSlots_[256];                      /* 0x170643 */
    unsigned char bridgeCount_;                           /* 0x170a43 */
    double        totalPlayTime_;                         /* 0x170a44 */
    double        lastTickTime_;                          /* 0x170a4c */
    double        clock_;                                 /* 0x170a54 */
    TickStep      tickStep_;                              /* 0x170a5c */
    /* Recomputed by GameTick every tick; see vitalityPercent(). */
    unsigned char vitalityPercent_;                       /* 0x170a64 */
    unsigned int  field_170a65_;                          /* 0x170a65 */
    int           textEntryActive_;                       /* 0x170a69 */
    TextEntry     nameEntry_;                             /* 0x170a6d */
    SaveSlots     saveSlots_;                             /* 0x170a7c */
    /* Its length is unknown; declared only as far as the next field. */
    char          levelName_[0x173583 - 0x173483];        /* 0x173483 */
    unsigned char levelIndex_;                            /* 0x173583 */
    int           field_173584_;                          /* 0x173584 */
    SlideObject  *slideSlots_[100];                       /* 0x173588 */
    unsigned char slideCount_;                            /* 0x173718 */
    LiftObject   *liftSlots_[256];                        /* 0x173719 */
    unsigned char liftCount_;                             /* 0x173b19 */
    int           field_173b1a_;                          /* 0x173b1a */
    BreakableTile *breakableSlots_[200];                  /* 0x173b1e */
    unsigned char breakableCount_;                        /* 0x173e3e */
    Bomb         *bombSlots_[500];                        /* 0x173e3f */
    unsigned char bombCount_;                             /* 0x17460f */
    unsigned char bombIds_[500];                          /* 0x174610 */
    Foe          *foeSlots_[500];                         /* 0x174804 */
    unsigned char foeCount_;                              /* 0x174fd4 */
    /* 500 long: the Player object follows at 0x1751c9. */
    unsigned char foeIds_[500];                           /* 0x174fd5 */
    /* The Player (0x241 bytes, player.h) and 7 unknown bytes after it. */
    unsigned char gap_1751c9[0x175412 - 0x1751c9];
    unsigned char rebindCode_;                            /* 0x175412 */
    char          rebindAction_[0x175513 - 0x175413];     /* 0x175413 */
    int           rebindActive_;                          /* 0x175513 */
    unsigned char debounce_;                              /* 0x175517 */
    MenuTree      menu_;                                  /* 0x175518 */
    ScriptPlayer  scriptPlayer_;                          /* 0x195735 */
    float         cameraDistance_;                        /* 0x28ab29 */
    unsigned char cameraMode_;                            /* 0x28ab2d */
    /* The settings (config.h); music, volumes, 3D sound, the camera
     * option and the joystick deadzone live in its persisted blob. */
    Config        config_;                                /* 0x28ab2e */
    float         cameraEye_[3];                          /* 0x2ab580 */
    unsigned char state_;                                 /* 0x2ab58c */
    /* The map: header, grid and snapshot grid, 0x26c37c bytes ending exactly
     * at tallyDone_.  Its header holds the time limit (GameTick times the
     * level out at timeLimit*1000 ms), the ms of play (CalculateLevelScore
     * pays the unused seconds) and the gem quota (CalculateLevelScore
     * 0x41a760 pays 5 a gem up to it and 10 per gem beyond). */
    LevelMap      map_;                                   /* 0x2ab58d */
    int           tallyDone_;                             /* 0x517909 */
};

KAROO_LAYOUT_CHECKS(Game)
{
    KAROO_LAYOUT_AT(soundManagerHead_, 0x13cba8);
    KAROO_LAYOUT_AT(soundCreated_,     0x13cc34);
    KAROO_LAYOUT_AT(fixedSounds_,      0x13cc5c);
    KAROO_LAYOUT_AT(bridgeSlots_,      0x170643);
    KAROO_LAYOUT_AT(bridgeCount_,      0x170a43);
    KAROO_LAYOUT_AT(clock_,            0x170a54);
    KAROO_LAYOUT_AT(tickStep_,         0x170a5c);
    KAROO_LAYOUT_AT(tally_,            0x1404c1);
    KAROO_LAYOUT_AT(vitalityPercent_,  0x170a64);
    KAROO_LAYOUT_AT(field_170a65_,     0x170a65);
    KAROO_LAYOUT_AT(slideSlots_,       0x173588);
    KAROO_LAYOUT_AT(slideCount_,       0x173718);
    KAROO_LAYOUT_AT(liftSlots_,        0x173719);
    KAROO_LAYOUT_AT(liftCount_,        0x173b19);
    KAROO_LAYOUT_AT(breakableSlots_,   0x173b1e);
    KAROO_LAYOUT_AT(breakableCount_,   0x173e3e);
    KAROO_LAYOUT_AT(soundAsset42262_,  0x042262);
    KAROO_LAYOUT_AT(soundAsset4236e_,  0x04236e);
    KAROO_LAYOUT_AT(soundAsset4247a_,  0x04247a);
    KAROO_LAYOUT_AT(soundAsset42586_,  0x042586);
    KAROO_LAYOUT_AT(soundAsset42692_,  0x042692);
    KAROO_LAYOUT_AT(soundAsset4279e_,  0x04279e);
    KAROO_LAYOUT_AT(soundAsset428aa_,  0x0428aa);
    KAROO_LAYOUT_AT(soundAsset429b6_,  0x0429b6);
    KAROO_LAYOUT_AT(soundAsset42ac2_,  0x042ac2);
    KAROO_LAYOUT_AT(soundAsset42bce_,  0x042bce);
    KAROO_LAYOUT_AT(soundAsset42cda_,  0x042cda);
    KAROO_LAYOUT_AT(soundAsset42de6_,  0x042de6);
    KAROO_LAYOUT_AT(soundAsset42ef2_,  0x042ef2);
    KAROO_LAYOUT_AT(soundAsset42ffe_,  0x042ffe);
    KAROO_LAYOUT_AT(soundAsset4310a_,  0x04310a);
    KAROO_LAYOUT_AT(soundAsset43216_,  0x043216);
    KAROO_LAYOUT_AT(soundAsset441ca_,  0x0441ca);
    KAROO_LAYOUT_AT(soundAsset44c42_,  0x044c42);
    KAROO_LAYOUT_AT(soundAsset44d4e_,  0x044d4e);
    KAROO_LAYOUT_AT(soundAsset44e5a_,  0x044e5a);
    KAROO_LAYOUT_AT(soundAsset456ba_,  0x0456ba);
    KAROO_LAYOUT_AT(soundAsset457c6_,  0x0457c6);
    KAROO_LAYOUT_AT(soundAsset458d2_,  0x0458d2);
    KAROO_LAYOUT_AT(soundAsset46132_,  0x046132);
    KAROO_LAYOUT_AT(soundAsset46baa_,  0x046baa);
    KAROO_LAYOUT_AT(bombSlots_,        0x173e3f);
    KAROO_LAYOUT_AT(bombCount_,        0x17460f);
    KAROO_LAYOUT_AT(bombIds_,          0x174610);
    KAROO_LAYOUT_AT(foeSlots_,         0x174804);
    KAROO_LAYOUT_AT(foeCount_,         0x174fd4);
    KAROO_LAYOUT_AT(foeIds_,           0x174fd5);
    KAROO_LAYOUT_AT(field_42252_,      0x042252);
    KAROO_LAYOUT_AT(levelName_,        0x173483);
    KAROO_LAYOUT_AT(map_,              0x2ab58d);
    KAROO_LAYOUT_AT(switchCells_,      0x140543);
    KAROO_LAYOUT_AT(timedSpawners_,    0x02023d);
    KAROO_LAYOUT_AT(freeBombs_,        0x02173d);
    KAROO_LAYOUT_AT(census_,           0x0421df);
    KAROO_LAYOUT_AT(field_0c_,         0x00000c);
    KAROO_LAYOUT_AT(mapChanged_,       0x000010);
    KAROO_LAYOUT_AT(nextLevelBonus_,   0x000014);
    KAROO_LAYOUT_AT(field_13cc90_,     0x13cc90);
    KAROO_LAYOUT_AT(field_13cca4_,     0x13cca4);
    KAROO_LAYOUT_AT(field_13cc88_,     0x13cc88);
    KAROO_LAYOUT_AT(cheatBuffer_,      0x13ccac);
    KAROO_LAYOUT_AT(field_13cc8c_,     0x13cc8c);
    KAROO_LAYOUT_AT(field_13cca8_,     0x13cca8);
    KAROO_LAYOUT_AT(field_173b1a_,     0x173b1a);
    KAROO_LAYOUT_AT(tickCount_,        0x000018);
    KAROO_LAYOUT_AT(gameFileName_,     0x04215f);
    KAROO_LAYOUT_AT(stateBeforeMenu_,  0x048b13);
    KAROO_LAYOUT_AT(field_48b14_,      0x048b14);
    KAROO_LAYOUT_AT(menuLevelName_,    0x048b18);
    KAROO_LAYOUT_AT(field_173584_,     0x173584);
    KAROO_LAYOUT_AT(switchMax_,        0x048b12);
    KAROO_LAYOUT_AT(foesKilled_,       0x04224d);
    KAROO_LAYOUT_AT(restartCount_,     0x04220b);
    KAROO_LAYOUT_AT(itemTotal_,        0x042250);
    KAROO_LAYOUT_AT(tallyDone_,        0x517909);
    KAROO_LAYOUT_AT(levelCount_,       0x04215e);
    KAROO_LAYOUT_AT(levelIndex_,       0x173583);
    KAROO_LAYOUT_AT(cameraMode_,       0x28ab2d);
    KAROO_LAYOUT_AT(cameraDistance_,   0x28ab29);
    KAROO_LAYOUT_AT(totalPlayTime_,    0x170a44);
    KAROO_LAYOUT_AT(lastTickTime_,     0x170a4c);
    KAROO_LAYOUT_AT(state_,            0x2ab58c);
    KAROO_LAYOUT_AT(parkedCameraOption_, 0x03215d);
    KAROO_LAYOUT_AT(levelSoundsReady_, 0x042254);
    KAROO_LAYOUT_AT(textEntryActive_,  0x170a69);
    KAROO_LAYOUT_AT(cheatEntry_,       0x13cdac);
    KAROO_LAYOUT_AT(nameEntry_,        0x170a6d);
    KAROO_LAYOUT_AT(saveSlots_,        0x170a7c);
    KAROO_LAYOUT_AT(rebindCode_,       0x175412);
    KAROO_LAYOUT_AT(rebindAction_,     0x175413);
    KAROO_LAYOUT_AT(rebindActive_,     0x175513);
    KAROO_LAYOUT_AT(debounce_,         0x175517);
    KAROO_LAYOUT_AT(menu_,             0x175518);
    KAROO_LAYOUT_AT(scriptPlayer_,     0x195735);
    KAROO_LAYOUT_AT(cdThemes_,         0x02223f);
    KAROO_LAYOUT_AT(config_,           0x28ab2e);
    KAROO_LAYOUT_AT(extraObjects_,     0x048b98);
    KAROO_LAYOUT_AT(highScores_,       0x13cdbb);
    KAROO_LAYOUT_AT(cameraEye_,        0x2ab580);
    KAROO_LAYOUT_AT(field_13cc94_,     0x13cc94);
}
