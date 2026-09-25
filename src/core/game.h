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
    DWORD unknown104;   /* ThemeSound_Add's arg4; the .thm path passes 1 */
    DWORD unknown108;   /* ThemeSound_Add's arg3; the .thm path passes 1 */
};
static_assert(sizeof(SoundAssetName) == 0x10c, "SoundAssetName stride");

/* The theme sound table ("TSM" in its log line), Game+0x42258.  A .thm
 * `Sound <event> <wave>` line fills entries[id] through ThemeSound_Add
 * (theme.cpp); the id is RegisterThemeSound's event number, so e.g. entry 0
 * is movecatcher and entry 70 explosionbomb.  The event table's largest id is
 * 0x47, but the table holds 100 entries: ReleaseAll 0x440400 clears exactly
 * 100, ending at Game+0x48b12 where switchMax_ begins.  Lifecycle in
 * theme.cpp; the vtable is ours, one slot. */
#define THEME_SOUND_COUNT 100
struct __attribute__((packed)) ThemeSoundTable {
    void          *vtable;       /* +0 */
    DWORD          unknown4;     /* +4  never written */
    WORD           unknown8;     /* +8  zeroed by the ctor, never read */
    SoundAssetName entries[THEME_SOUND_COUNT];
};
static_assert(sizeof(ThemeSoundTable) == 10 + 100 * 0x10c, "ThemeSoundTable size");

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

    /* The level-report tallies (reportwriter.cpp owns all of them).  Get/set
     * rather than a reference: Game is packed, so a `short &` into it will
     * not bind. */
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
    /* 0 = follow the player; nonzero = view from the separate eye at
     * +0x2ab580 (FramePose_Player 0x404120 / UpdateViewTransform), and 2 also spins the
     * yaw -- the menus and the tally.  Checkpoints restore it. */
    unsigned char  cameraMode() const                { return cameraMode_; }
    void           setCameraMode(unsigned char m)    { cameraMode_ = m; }
    /* The distance the camera eases towards (UpdateViewTransform subtracts
     * the current eye distance and closes a dt-scaled share of the gap):
     * 7.0 by default, 40.0 in CameraOverview, animated by the sway. */
    float          cameraDistance() const            { return cameraDistance_; }
    void           setCameraDistance(float d)        { cameraDistance_ = d; }
    /* The controls menu's camera option: 1 = the camera turns with the
     * player (UpdateViewTransform, FramePose_Player 0x404120).  GameTick forces it to 1
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
     * (FramePose_Player 0x404120 makes it the camera's focus, z negated);
     * checkpoints restore it from the script player. */
    void           setCameraEye(int i, float v)      { cameraEye_[i] = v; }
    float          cameraEye(int i) const            { return cameraEye_[i]; }
    /* A float vector RenderGameFrame's scripted-camera branch (0x427059,
     * taken instead of UpdateViewTransform when Game+0x196086 is set) reads
     * beside the eye -- not 0x404120, as this comment used to say;
     * checkpoints restore it from the script player's spline point, and the
     * level builder seeds it {0, 1000, 0}.  Not decoded. */
    void           setField13cc94(int i, float v)    { field_13cc94_[i] = v; }
    /* +0x13cca4 is the zoom distance the Zoom In/Out actions step (clamped
     * 2..20); GameTick eases cameraDistance towards it and copies it back
     * when the overview ends.  +0x13cca8 is the overview flag the OverView
     * action raises (with cameraDistance 40); GameTick and the level
     * builder clear it.  +0x13cc90 is set by both zoom actions and cleared
     * by the same two; its reader is not decoded. */
    void           setField13cc90(int v)             { field_13cc90_ = v; }
    float          zoomDistance() const              { return zoomDistance_; }
    void           setZoomDistance(float d)          { zoomDistance_ = d; }
    int            overviewActive() const            { return overviewActive_; }
    void           setOverviewActive(int v)          { overviewActive_ = v; }
    /* keypress.cpp: +0x13cc88 is a one-shot latch (cleared, then set on
     * the first press, which also sets +0x13cc8c).  Named by offset. */
    int            field_13cc88() const              { return field_13cc88_; }
    void           setField13cc88(int v)             { field_13cc88_ = v; }
    int            field_13cc8c() const              { return field_13cc8c_; }
    void           setField13cc8c(int v)             { field_13cc8c_ = v; }
    /* The buffer HandleTypedCheatCode matches typed cheats in. */
    unsigned char *cheatBuffer()                     { return cheatBuffer_; }
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
     * Game::Load 0x4145c0 is the constructor: build every member, read the
     * .gam, save slots, Karoo.cfg and high scores, then enter the first
     * level.  Destruct 0x414b70 saves the scores and config and tears it
     * all down.  The vtable is ours (one slot; 0x45d3b8 is a tripwire). */
    Game *construct(const char *gameName);
    void  destruct();

private:
    int   loadGameFile(const char *name);   /* LoadGameFile 0x41cbf0 */
    void  releaseAllSounds();               /* ReleaseAllSoundBuffers 0x41b3b0 */

private:
    Game() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(Game);

    const void   *vtable_;                                /* 0x000000  ours, one slot */
    double        field_04_;                              /* 0x000004  Load sets 1.0; reader not decoded */
    int           field_0c_;                              /* 0x00000c */
    unsigned int  mapChanged_;                            /* 0x000010 */
    unsigned int  nextLevelBonus_;                        /* 0x000014 */
    unsigned int  tickCount_;                             /* 0x000018 */
    MenuTree      rootMenu_;                              /* 0x00001c  constructed, never navigated */
    int           initialised_;                           /* 0x020239  0 until Load completes */
    TimedSpawner  timedSpawners_[256];                    /* 0x02023d */
    FreeBomb      freeBombs_[256];                        /* 0x02173d */
    unsigned char gap_02223d[0x02223f - 0x02223d];
    CdThemes      cdThemes_;                              /* 0x02223f */
    unsigned char parkedCameraOption_;                    /* 0x03215d */
    /* 256 level names, 0x100 each, tiling 0x03215e..0x04215e exactly.
     * SetCurrentLevelName copies entry `levelNo & 0xff` into levelName_. */
    char          levelNameTable_[256][0x100];            /* 0x03215e */
    unsigned char levelCount_;                            /* 0x04215e */
    char          gameFileName_[0x0421df - 0x04215f];     /* 0x04215f */
    LevelCensus   census_;                                /* 0x0421df */
    unsigned char restartCount_;                          /* 0x04220b */
    /* The level-report tallies, zeroed and accumulated by
     * Report_WriteLevelReport (reportwriter.cpp) and by nothing else. */
    unsigned short reportLevelsWithScript_;               /* 0x04220c */
    unsigned char gap_04220e[0x042212 - 0x04220e];
    int            reportScoreTotal_;                     /* 0x042212 */
    unsigned char gap_042216[0x042243 - 0x042216];
    unsigned short reportTallyA_;                         /* 0x042243 */
    unsigned short reportLevelsWithBonus_;                /* 0x042245 */
    unsigned short reportLevelsWithLeo_;                  /* 0x042247 */
    unsigned char gap_042249[0x04224d - 0x042249];
    /* Foes killed this level; CalculateLevelScore pays 50 each. */
    unsigned char foesKilled_;                            /* 0x04224d */
    unsigned char gap_04224e[0x042250 - 0x04224e];
    unsigned short itemTotal_;                            /* 0x042250 */
    unsigned short field_42252_;                          /* 0x042252 */
    int           levelSoundsReady_;                      /* 0x042254 */
    ThemeSoundTable themeSounds_;                         /* 0x042258 */
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
    int           field_13cc84_;                          /* 0x13cc84  Load zeroes; reader not decoded */
    int           field_13cc88_;                          /* 0x13cc88 */
    int           field_13cc8c_;                          /* 0x13cc8c */
    int           field_13cc90_;                          /* 0x13cc90 */
    float         field_13cc94_[3];                       /* 0x13cc94 */
    unsigned char gap_13cca0[0x13cca4 - 0x13cca0];
    float         zoomDistance_;                          /* 0x13cca4 */
    int           overviewActive_;                        /* 0x13cca8 */
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
    KAROO_LAYOUT_AT(field_04_,         0x000004);
    KAROO_LAYOUT_AT(rootMenu_,         0x00001c);
    KAROO_LAYOUT_AT(initialised_,      0x020239);
    KAROO_LAYOUT_AT(field_13cc84_,     0x13cc84);
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
    KAROO_LAYOUT_AT(bombSlots_,        0x173e3f);
    KAROO_LAYOUT_AT(bombCount_,        0x17460f);
    KAROO_LAYOUT_AT(bombIds_,          0x174610);
    KAROO_LAYOUT_AT(foeSlots_,         0x174804);
    KAROO_LAYOUT_AT(foeCount_,         0x174fd4);
    KAROO_LAYOUT_AT(foeIds_,           0x174fd5);
    KAROO_LAYOUT_AT(field_42252_,      0x042252);
    KAROO_LAYOUT_AT(levelName_,        0x173483);
    KAROO_LAYOUT_AT(levelNameTable_,   0x03215e);
    KAROO_LAYOUT_AT(reportLevelsWithScript_, 0x04220c);
    KAROO_LAYOUT_AT(reportScoreTotal_,       0x042212);
    KAROO_LAYOUT_AT(reportTallyA_,           0x042243);
    KAROO_LAYOUT_AT(reportLevelsWithBonus_,  0x042245);
    KAROO_LAYOUT_AT(reportLevelsWithLeo_,    0x042247);
    KAROO_LAYOUT_AT(themeSounds_,            0x042258);
    KAROO_LAYOUT_AT(themeSounds_.entries[0],  0x042262);
    KAROO_LAYOUT_AT(themeSounds_.entries[30], 0x0441ca);
    KAROO_LAYOUT_AT(themeSounds_.entries[70], 0x046baa);
    KAROO_LAYOUT_AT(map_,              0x2ab58d);
    KAROO_LAYOUT_AT(switchCells_,      0x140543);
    KAROO_LAYOUT_AT(timedSpawners_,    0x02023d);
    KAROO_LAYOUT_AT(freeBombs_,        0x02173d);
    KAROO_LAYOUT_AT(census_,           0x0421df);
    KAROO_LAYOUT_AT(field_0c_,         0x00000c);
    KAROO_LAYOUT_AT(mapChanged_,       0x000010);
    KAROO_LAYOUT_AT(nextLevelBonus_,   0x000014);
    KAROO_LAYOUT_AT(field_13cc90_,     0x13cc90);
    KAROO_LAYOUT_AT(zoomDistance_,     0x13cca4);
    KAROO_LAYOUT_AT(field_13cc88_,     0x13cc88);
    KAROO_LAYOUT_AT(cheatBuffer_,      0x13ccac);
    KAROO_LAYOUT_AT(field_13cc8c_,     0x13cc8c);
    KAROO_LAYOUT_AT(overviewActive_,   0x13cca8);
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

/* The lifecycle exports (game.cpp): 0x4145c0 (thiscall, RET 4, returns
 * this), 0x414b70, and 0x414b50 -- slot 0 of our Game table, which WinMain's
 * `delete game` calls through. */
extern "C" __declspec(dllexport) Game *__attribute__((thiscall))
Game_Construct(Game *self, const char *gameName);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Game_Destruct(Game *self);
extern "C" __declspec(dllexport) Game *__attribute__((thiscall))
Game_ScalarDestructor(Game *self, unsigned char flags);
