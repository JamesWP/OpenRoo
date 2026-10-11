/* World: the level in play, owned by Game.  Its map (the header and both tile
 * grids), the objects in it (the entity tables, the level builder's census
 * and spawn tables, the switch cells and the extra 3D objects), the player,
 * and the clock they all run on.
 */
#pragma once

#include "levelmap.h"
#include "levelcensus.h"
#include "switchcells.h"
#include "extraobjects.h"
#include "tickstep.h"
#include "entitycontext.h"
#include "player.h"

class SoundLibrary;
class Bomb;
class Foe;
class LiftObject;
class PlatformObject;
class BridgeObject;
class FallingTile;

class World {
public:
    /* The cells each switch controls (switchcells.h), and the highest
     * switch index the level builder saw -- the list walks run to it. */
    SwitchCells    *switchCells()               { return &switchCells_; }
    unsigned char   switchMax() const           { return switchMax_; }
    void            setSwitchMax(unsigned char n) { switchMax_ = n; }

    /* The level builder's census and spawn tables (levelcensus.h). */
    LevelCensus    *census()                    { return &census_; }
    TimedSpawner   *timedSpawner(unsigned i)    { return &timedSpawners_[i]; }
    FreeBomb       *freeBomb(unsigned i)        { return &freeBombs_[i]; }

    /* The level's map (levelmap.h): its header and both tile grids. */
    LevelMap       *map()            { return &map_; }
    const LevelMap *map() const      { return &map_; }
    /* Gems the level requires (Player::gemsCollected is the other side). */
    int            gemsRequired() const { return map_.gemsRequired(); }
    /* The level's time limit in seconds (SetupLevelObjects copies it from
     * the map file's) and the play time against it in milliseconds. */
    int            timeLimit() const                 { return map_.timeLimit(); }
    void           setTimeLimit(int s)               { map_.setTimeLimit(s); }
    unsigned int   timeElapsed() const               { return map_.timeElapsed(); }
    void           setTimeElapsed(unsigned int ms)   { map_.setTimeElapsed(ms); }

    /* The 8-byte clock accumulator.  Objects keep a pointer to it and
     * re-read it every tick. */
    double        *clock()           { return &clock_; }
    /* The tick step, dt; objects copy it to their own each tick. */
    TickStep      *tickStep()        { return &tickStep_; }

    /* The level's extra 3D objects, from its .leo (extraobjects.h). */
    ExtraObjects         *extraObjects()             { return &extraObjects_; }

    /* The player (player.h). */
    Player       *player()       { return &player_; }
    const Player *player() const { return &player_; }

    /* The current level's path; diagnostics only. */
    const char    *levelName() const               { return levelName_; }
    /* SetCurrentLevelName copies entry `levelNo & 0xff` into levelName_. */
    char          *levelNameBuffer()                 { return levelName_; }

    /* The crystals in the level: the level builder counts each crystal tile
     * and each foe of type 0x0b spawned (EntityContext::crystalsInLevel).
     * Summed into the census total, and logged as the crystals in the level. */
    unsigned short crystalsInLevel() const             { return crystalsInLevel_; }
    void           setCrystalsInLevel(unsigned short n) { crystalsInLevel_ = n; }

    /* What the entities are given at spawn, and the tables they fill. */
    EntityContext entityContext()
    {
        return { &clock_, &tickStep_, &map_, sound_, &switchCells_,
                 &switchMax_, levelName_, &crystalsInLevel_ };
    }

    /* The sound library the entities acquire their sounds from, set by Game
     * when it is built (it stays Game's). */
    void setSoundLibrary(SoundLibrary *s) { sound_ = s; }

    EntityIdTable<Bomb, 500> &bombs() { return bombs_; }
    EntityIdTable<Foe, 500>  &foes()  { return foes_; }
    EntitySlots<LiftObject, 256>     &lifts()     { return lifts_; }
    EntitySlots<PlatformObject, 100> &platforms() { return platforms_; }
    EntitySlots<BridgeObject, 256>   &bridges()   { return bridges_; }
    EntitySlots<FallingTile, 200>    &fallings()  { return fallings_; }

    /* ── lifts ──────────────────────────────────────────────────────── */
    unsigned char liftCount() const              { return lifts_.count; }
    LiftObject   *liftSlot(unsigned int i) const { return lifts_.slot[i]; }

    /* ── platforms ─────────────────────────────────────────────────────── */
    unsigned char platformCount() const               { return platforms_.count; }
    PlatformObject  *platformSlot(unsigned int i) const  { return platforms_.slot[i]; }

    /* ── bridges ────────────────────────────────────────────────────── */
    /* Indexed by the bridge's switch slot, not the count; the count is a
     * running total (see BridgeObject::spawn). */
    unsigned char bridgeCount() const              { return bridges_.count; }
    BridgeObject *bridgeSlot(unsigned int i) const { return bridges_.slot[i]; }
    BridgeObject *const *bridgeSlots() const       { return bridges_.slot; }

    /* ── falling tiles ────────────────────────────────────────────── */
    unsigned char  fallingCount() const              { return fallings_.count; }
    FallingTile *fallingSlot(unsigned int i) const { return fallings_.slot[i]; }

    /* ── bombs (the game's "enemy" table) ───────────────────────────── */
    /* Slots are indexed by ID; the ID list holds the live IDs, count long. */
    unsigned char  bombCount() const               { return bombs_.count; }
    unsigned char  bombId(unsigned int i) const    { return bombs_.ids[i]; }
    Bomb          *bombSlot(unsigned int id) const { return bombs_.slot[id]; }

    /* ── foes ───────────────────────────────────────────────────────── */
    /* The same slot/count/ID-list shape as the bombs. */
    unsigned char  foeCount() const               { return foes_.count; }
    unsigned char  foeId(unsigned int i) const    { return foes_.ids[i]; }
    Foe           *foeSlot(unsigned int id) const { return foes_.slot[id]; }
    Foe          **foeSlotRef(unsigned int id)   { return &foes_.slot[id]; }

    /* ── whole-table operations (world.cpp) ───────────────────────── */
    /* Removes every object from the level and zeroes the tables' counts. */
    void clearObjects();
    /* Ticks every live object of one kind, in slot order.  The count is
     * re-read each pass. */
    void tickLifts();
    void tickPlatforms();
    void tickFallings();
    void tickBridges();
    /* Ticks and debug-draws each bomb, removing those that asked to be;
     * a removal can make the next bomb be skipped (kept as the original). */
    void tickBombs();

private:
    TimedSpawner  timedSpawners_[256];
    FreeBomb      freeBombs_[256];
    LevelCensus   census_;
    unsigned char switchMax_{};
    unsigned short crystalsInLevel_{};
    ExtraObjects  extraObjects_;
    SwitchCells   switchCells_;
    EntitySlots<BridgeObject, 256> bridges_;
    double        clock_{};
    TickStep      tickStep_;
    /* As long as a level-name table entry. */
    char          levelName_[0x100]{};
    EntitySlots<PlatformObject, 100> platforms_;
    EntitySlots<LiftObject, 256> lifts_;
    EntitySlots<FallingTile, 200> fallings_;
    EntityIdTable<Bomb, 500> bombs_;
    EntityIdTable<Foe, 500>  foes_;
    Player        player_;
    /* The map: header, grid and snapshot grid.  Its header holds the time limit (GameTick times the
     * level out at timeLimit*1000 ms), the ms of play (CalculateLevelScore
     * pays the unused seconds) and the gem quota (CalculateLevelScore
     * pays 5 a gem up to it and 10 per gem beyond). */
    LevelMap      map_;
    /* The sound library the entities acquire from; Game sets it. */
    SoundLibrary *sound_{};
};
