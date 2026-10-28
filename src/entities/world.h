/* World: the level in play, owned by Game.  Its map (the header and both tile
 * grids), the objects in it (one EntityList per kind, the level builder's
 * census and spawn tables, the switch cells and the extra 3D objects), the
 * player, and the clock they all run on.
 *
 * A const World is a read-only view of the level: every accessor has a const
 * overload that hands out only const objects (EntityList's constness is
 * deep).  Code that only looks -- the HUD, the debug panels, a player AI
 * planning its moves -- takes a const World &.
 */
#pragma once
#include "playevents.h"

#include <array>
#include "levelmap.h"
#include "levelcensus.h"
#include "switchcells.h"
#include "extraobjects.h"
#include "tickstep.h"
#include "entitycontext.h"
#include "entitylist.h"
#include "player.h"
#include "bomb.h"
#include "foe.h"
#include "liftobject.h"
#include "platformobject.h"
#include "bridgeobject.h"
#include "fallingtile.h"
#include "effects.h"


class World {
public:
    /* ── the level ─────────────────────────────────────────────────── */

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

    /* The cells each switch controls (switchcells.h), and the highest
     * switch index the level builder saw -- the list walks run to it. */
    SwitchCells       *switchCells()                  { return &switchCells_; }
    const SwitchCells *switchCells() const            { return &switchCells_; }
    unsigned char      switchMax() const              { return switchMax_; }
    void               setSwitchMax(unsigned char n)  { switchMax_ = n; }

    /* The level builder's census and spawn tables (levelcensus.h); the
     * census counts the entries in use. */
    LevelCensus        *census()                       { return &census_; }
    const LevelCensus  *census() const                 { return &census_; }
    TimedSpawner       *timedSpawner(unsigned i)       { return &timedSpawners_[i]; }
    const TimedSpawner *timedSpawner(unsigned i) const { return &timedSpawners_[i]; }
    FreeBomb           *freeBomb(unsigned i)           { return &freeBombs_[i]; }
    const FreeBomb     *freeBomb(unsigned i) const     { return &freeBombs_[i]; }

    /* The level's extra 3D objects, from its .leo (extraobjects.h). */
    ExtraObjects       *extraObjects()       { return &extraObjects_; }
    const ExtraObjects *extraObjects() const { return &extraObjects_; }

    /* The current level's path; diagnostics only. */
    const char    *levelName() const               { return levelName_; }
    /* SetCurrentLevelName copies entry `levelNo & 0xff` into levelName_. */
    char          *levelNameBuffer()                 { return levelName_; }

    /* The crystals in the level: the level builder counts each crystal tile
     * and each foe of type 0x0b spawned (EntityContext::crystalsInLevel).
     * Summed into the census total, and logged as the crystals in the level. */
    unsigned short crystalsInLevel() const             { return crystalsInLevel_; }
    void           setCrystalsInLevel(unsigned short n) { crystalsInLevel_ = n; }

    /* ── the clock ─────────────────────────────────────────────────── */

    /* The game clock in ms.  Objects keep a pointer to it and re-read it
     * every tick. */
    double         now() const       { return clock_; }
    double        *clock()           { return &clock_; }
    /* Advances the clock dt ms, or sets it. */
    void           advanceClock(double dt) { clock_ += dt; }
    void           setNow(double ms)       { clock_ = ms; }
    /* The tick step, dt; objects copy it to their own each tick. */
    TickStep       *tickStep()       { return &tickStep_; }
    const TickStep *tickStep() const { return &tickStep_; }

    /* ── the objects ───────────────────────────────────────────────── */

    Player       *player()       { return &player_; }
    const Player *player() const { return &player_; }

    /* Each kind in the order it was spawned; removing one keeps the order
     * of the rest. */
    EntityList<Bomb>                 &bombs()           { return bombs_; }
    const EntityList<Bomb>           &bombs() const     { return bombs_; }
    EntityList<Foe>                  &foes()            { return foes_; }
    const EntityList<Foe>            &foes() const      { return foes_; }
    EntityList<LiftObject>           &lifts()           { return lifts_; }
    const EntityList<LiftObject>     &lifts() const     { return lifts_; }
    EntityList<PlatformObject>       &platforms()       { return platforms_; }
    const EntityList<PlatformObject> &platforms() const { return platforms_; }
    EntityList<BridgeObject>         &bridges()         { return bridges_; }
    const EntityList<BridgeObject>   &bridges() const   { return bridges_; }
    EntityList<FallingTile>          &fallings()        { return fallings_; }
    const EntityList<FallingTile>    &fallings() const  { return fallings_; }

    /* The bridge the switch operates, or null if it operates none. */
    BridgeObject       *bridge(unsigned char switchSlot);
    const BridgeObject *bridge(unsigned char switchSlot) const;

    /* ── changing the level ────────────────────────────────────────── */

    /* What the entities are given at spawn, and the tables they fill. */
    EntityContext entityContext()
    {
        return { &clock_, &tickStep_, &map_, &switchCells_,
                 &switchMax_, levelName_, &crystalsInLevel_ };
    }

    /* Removes every object from the level and resets the switch counts. */
    void clearObjects(PlayEvents &events);
    /* Ticks every object of one kind, in list order. */
    void tickLifts(PlayEvents &events);
    void tickPlatforms(PlayEvents &events);
    void tickFallings(PlayEvents &events);
    void tickBridges(PlayEvents &events);
    /* Ticks each bomb, removing those that asked to be. */
    void tickBombs(PlayEvents &events);
    /* Notes the effects this tick started (pickups, a tile falling) and the
     * player's place on its path, and drops what can no longer be seen.  Once
     * a tick: a tick that does not move the clock records nothing. */
    void recordEffects();

    /* The one-shot effects still to be seen, oldest first, and the player's
     * recent path (effects.h). */
    const std::vector<EffectStart> &effects() const    { return effects_; }
    const std::deque<PathSample>   &playerPath() const { return playerPath_; }

private:
    std::array<TimedSpawner, 256> timedSpawners_{};
    std::array<FreeBomb, 256>     freeBombs_{};
    LevelCensus   census_;
    unsigned char switchMax_{};
    unsigned short crystalsInLevel_{};
    ExtraObjects  extraObjects_;
    SwitchCells   switchCells_;
    double        clock_{};
    TickStep      tickStep_;
    /* As long as a level-name table entry. */
    char          levelName_[0x100]{};
    EntityList<BridgeObject>   bridges_;
    EntityList<PlatformObject> platforms_;
    EntityList<LiftObject>     lifts_;
    EntityList<FallingTile>    fallings_;
    EntityList<Bomb>           bombs_;
    std::vector<EffectStart>   effects_;
    std::deque<PathSample>     playerPath_;
    unsigned                   effectSeed_{};
    double                     effectsAt_ = -1.0;  // the clock recordEffects last ran at
    EntityList<Foe>            foes_;
    Player        player_;
    /* The map: header, grid and snapshot grid.  Its header holds the time limit (GameTick times the
     * level out at timeLimit*1000 ms), the ms of play (CalculateLevelScore
     * pays the unused seconds) and the gem quota (CalculateLevelScore
     * pays 5 a gem up to it and 10 per gem beyond). */
    LevelMap      map_;
};
