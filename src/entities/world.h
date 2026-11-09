/* World: the level in play, owned by Game.  Its map (the header and both tile
 * grids), the objects in it (one EntityList per kind, the level builder's
 * census and spawn tables), the
 * player, and the clock they all run on.
 *
 * A const World is a read-only view of the level: every accessor has a const
 * overload that hands out only const objects (EntityList's constness is
 * deep).  Code that only looks -- the HUD, the debug panels, a player AI
 * planning its moves -- takes a const World &.
 *
 * A World is a value: nothing in it points anywhere, so a copy is a whole,
 * separate level that goes on from where the original was (the bot plays
 * ahead on copies).  KAROO_WORLD_COPY=1 checks that the game keeps nothing
 * pointing into it: each tick then runs on a fresh copy at a new address.
 */
#pragma once
#include "crtrand.h"
#include "playevents.h"

#include <array>
#include "levelmap.h"
#include "levelcensus.h"
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
#include "presses.h"


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

    /* The level builder's census and spawn tables (levelcensus.h); the
     * census counts the entries in use. */
    LevelCensus        *census()                       { return &census_; }
    const LevelCensus  *census() const                 { return &census_; }
    TimedSpawner       *timedSpawner(unsigned i)       { return &timedSpawners_[i]; }
    const TimedSpawner *timedSpawner(unsigned i) const { return &timedSpawners_[i]; }
    FreeBomb           *freeBomb(unsigned i)           { return &freeBombs_[i]; }
    const FreeBomb     *freeBomb(unsigned i) const     { return &freeBombs_[i]; }


    /* The current level's path; diagnostics only. */
    const char    *levelName() const               { return levelName_; }
    /* SetCurrentLevelName copies entry `levelNo & 0xff` into levelName_. */
    char          *levelNameBuffer()                 { return levelName_; }


    /* ── the clock ─────────────────────────────────────────────────── */

    /* The game clock in ms. */
    double         now() const       { return clock_; }
    /* The rules' random numbers: the level's setup and the random pickups draw from them. */
    CrtRand       &random()          { return random_; }
    /* Foes killed this level, kept over its restarts: every 15th spawned foe
     * drops a life, and the level's score pays 50 for each. */
    unsigned char  foesKilled() const               { return foesKilled_; }
    void           setFoesKilled(unsigned char n)   { foesKilled_ = n; }
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

    /* What a tick's rules report to the game around them. */
    struct TickResult {
        unsigned playedMs = 0;  // the play time counted this tick
    };

    /* One tick of the level, dt ms on, with the presses held (presses.h);
     * running is whether play is on (the game's state is Playing), which
     * lets the player act, the foes move and the level end.  beginTick
     * then update: code with no Game -- the bot -- calls this. */
    TickResult tick(double dt, unsigned presses, bool running, PlayEvents &events)
    {
        beginTick(dt);
        return update(presses, running, events);
    }
    /* The game makes the two halves itself, its menus between them: they run
     * on the new clock, and may start a level (core/gametick.cpp).  The
     * clock on dt ms (0 while paused); then every rule, in order
     * (worldrules.cpp). */
    void       beginTick(double dt) { tickStep_.value = dt; clock_ += dt; }
    TickResult update(unsigned presses, bool running, PlayEvents &events);

    /* What the entities are given as they spawn and each tick. */
    EntityContext entityContext()
    {
        return { clock_, tickStep_, &map_ };
    }

    /* Removes every object from the level. */
    void clearObjects(PlayEvents &events);

    /* Whether the gems are in, and whether the player has reached the open
     * exit; both cleared as a level (re)starts. */
    bool gemsGoalReached() const { return gemsGoalReached_; }
    bool completed() const       { return completed_; }
    void resetLevelEnd()         { gemsGoalReached_ = false; completed_ = false; }

    /* The one-shot effects still to be seen, oldest first, and the player's
     * recent path (effects.h). */
    const std::vector<EffectStart> &effects() const    { return effects_; }
    const std::deque<PathSample>   &playerPath() const { return playerPath_; }

private:
    /* update's steps, in the order it takes them. */
    /* Ticks every object of one kind, in list order. */
    void tickLifts(PlayEvents &events);
    void tickPlatforms(PlayEvents &events);
    void tickFallings(PlayEvents &events);
    void tickBridges(PlayEvents &events);
    /* Ticks each bomb, removing those that asked to be. */
    void tickBombs(PlayEvents &events);
    /* The tile underfoot, the player's step and its pickups
     * (Player::updateTileEffects). */
    void updatePlayer(PlayEvents &events);
    /* While running: the timed spawners, a downed player's loops silenced,
     * the countdown, and the presses acted on if the player is up.  While
     * not: the player kept from idling, and the countdown silenced. */
    void tickControls(bool running, unsigned presses, PlayEvents &events);
    /* The player's actions for the presses held, in the order the game
     * always made them: turns, then moves, then the bomb and harakiri. */
    void applyPresses(unsigned presses, PlayEvents &events);
    /* The countdown's last ten seconds: a tick sound as each whole second
     * left is passed, while the player has not timed out.  stopCountdown
     * silences it, as play stops. */
    void updateCountdown(PlayEvents &events);
    void stopCountdown(PlayEvents &events);
    /* Spawns a foe from each timed spawner whose interval has passed. */
    void spawnTimedFoes();
    /* Drops the bomb the player asked for, unless a step is too far along. */
    void dropPlayerBomb();
    /* Arms the bridge of the switch the player stepped on. */
    void playerSwitch(PlayEvents &events);
    /* The level's play time, while running and the player is up; the ms
     * counted. */
    unsigned countPlayTime(bool running);
    /* Arms the bridge of any switch a foe stepped on, then moves each foe;
     * they hold still unless play is running, the player is up and they are
     * not frozen.  Removes the foes that finished dying, counting them. */
    void tickFoes(bool running, PlayEvents &events);
    /* Notes the effects this tick started (pickups, a tile falling) and the
     * player's place on its path, and drops what can no longer be seen.  Once
     * a tick: a tick that does not move the clock records nothing. */
    void recordEffects();
    /* While play runs: times the level out, opens the exit once the gems
     * are in, and holds the player on it, completing the level when its
     * step is over. */
    void updateLevelEnd(PlayEvents &events);

    std::array<TimedSpawner, 256> timedSpawners_{};
    std::array<FreeBomb, 256>     freeBombs_{};
    LevelCensus   census_;
    double        clock_{};
    TickStep      tickStep_;
    CrtRand       random_;  // the rules' random numbers
    unsigned char foesKilled_{};
    bool          gemsGoalReached_{};
    bool          completed_{};
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
