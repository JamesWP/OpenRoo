/* FoeModel: where the foes will go, by the foes' own rules, stepped forward.
 *
 * A foe searches for its next step afresh after every step, and the search is
 * not stable: from the next cell of its route it can choose another, even to
 * a target that has not moved.  So the way it will really go is found by
 * stepping it, as the game does.  This is the bot's own copy of those rules,
 * written from the game's (foe.cpp, foepath.cpp, movableentity.cpp) and
 * reading only a BotView, decision for decision: the target each foe type
 * chooses, the capped best-first search run backward from the target with its
 * tie-breaks and its reversed step test, the tank-style turn table, the step's
 * start checks (occupants, walls, climbs) and timing (500 ms a catcher's step,
 * 700 a thrower's, a step that follows within 20 ms of the last starting where
 * it ended), and the terrain's say over a foe at rest, seen to on the tick its
 * step ends (a slide carries it on at 150 ms a cell, ice the way it last
 * moved, a jump pad throws it up to the pad's top and on the way it went
 * (as the player, nav.h), a sticky pad holds it 3 s, a teleporter holds it 500 ms, moves it to
 * its other pad, holds it 500 ms more and sends it on the way it came).  Foes
 * see each other and the player through the cells' occupants, as in the game:
 * a step holds the cell it leaves until it ends, the player's too, and a
 * thrower steps into no occupied cell, a catcher into none but the player's.
 * A returner with no way to its target goes back to the last target it had a
 * way to.
 *
 * The search reads only the terrain (except a gem runner's and a switch
 * runner's, which read the occupants, and any search past a sticky pad), so
 * its answer, the first step from a cell towards a target, is the same every
 * time while the terrain is: the model keeps it, across loads, until the
 * terrain changes.  That is what makes a foe's walked path cheap to work out
 * again for each plan the bot weighs.
 *
 * A catcher caught by a blast dies: a bomb in the view, one the caller adds,
 * or one a thrower drops.  A thrower never does.  A thrower drops a bomb when
 * its target is within a cell of it (diagonals too), at most every 2 s, once
 * it is at rest or has just set off (Foe::step, Foe::dropBomb).  A follower's
 * target is a catcher, so its bombs kill foes.
 *
 * A foe standing on a lift rides it, and steps off where the lift stands
 * level with the next cell.  A foe steps onto a platform where it stands
 * parked with time still for the step, rides it (turning on it or not), and
 * steps off it anywhere but onto its track.
 *
 * The falling tiles fall and come back as the game has them, armed by the
 * player and the foes, and a foe on one when it falls falls with it.
 *
 * The switches work as the game has them (playtick.cpp): whoever comes to rest
 * on one, at its height, works it once (not again before a step forward or
 * back), the player first in a tick, then each foe before its own tick.  A
 * bridge on the move ignores its switch, and the switch waits until it is
 * done.  A bridge stamps or clears a deck cell every 100 ms (bridgeobject.cpp)
 * and its switches stand raised (busy) while it extends or is extended.  A
 * switch runner makes for the nearest raised switch within 7 of it (in the
 * game's list order, tilequery.cpp), its search keeping off the switches not
 * raised unless it stands on one.  The player works a switch a step's time
 * after stepping onto it.
 *
 * What it leaves out: the idle animation's longer moves.  botrun.h's foe log
 * measures how often it is right against the game (tools/bot/foecheck.py). */
#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include "bombmodel.h"
#include "botview.h"

namespace bot {

/* A foe's step, as foe.cpp's spawn sets it. */
const double CATCHER_STEP_MS = 500.0;
const double THROWER_STEP_MS = 700.0;

class FoeModel {
public:
    /* Starts from the view: the foes as they stand (but those dying, which
     * harm nothing now), at the view's clock, and the blasts of the bombs in
     * it (bombmodel.h). */
    void load(const BotView &view);
    /* A blast more, of a bomb the caller means to drop: a foe in it dies. */
    void addBlast(const Blast &b) { blasts_.push_back(b); byCaller_.push_back(1); }
    /* A foe a blast killed: when (the model's clock), where it stood, and
     * whether only a blast the caller added did it (not a bomb in the view,
     * or a foe's, which would have anyway). */
    struct Kill {
        double ms;
        int    u, v;
        bool   byAdded;
    };
    /* The foes killed by blasts since load(). */
    const std::vector<Kill> &kills() const { return kills_; }
    /* How many of them only the caller's blasts killed. */
    int  killed() const;
    /* The blasts of the bombs the foes dropped since load(). */
    const std::vector<Blast> &dropped() const { return dropped_; }
    /* A bridge set going since load(): when (the model's clock), which (its
     * index in the view's bridges), and whether a foe worked the switch (or
     * the player). */
    struct Switched {
        double ms;
        size_t bridge;
        bool   byFoe;
    };
    const std::vector<Switched> &switched() const { return switched_; }
    /* Whether the cell is a bridge's deck the model has cleared since load(). */
    bool cleared(int u, int v) const
    {
        return ownGrid_ && view_->inside(u, v) && cleared_[(size_t)v * view_->extentU + u];
    }

    /* Ticks the foes until the clock reaches untilMs, the player standing on
     * playerAt(t) at each tick's clock t. */
    void run(double untilMs, const std::function<Step(double)> &playerAt);
    /* One tick of run(); nothing if the view has no tick step. */
    void runTick(const std::function<Step(double)> &playerAt);

    double now() const { return clock_; }
    size_t foeCount() const { return foes_.size(); }
    Step   foeCell(size_t i) const { return {foes_[i].u, foes_[i].v}; }
    int    foeKind(size_t i) const { return foes_[i].kind; }
    /* Whether foe i's search finds a way to (u, v) as the foes stand, at the
     * budget its type chases with (a follower's, a catcher). */
    bool reaches(size_t i, int u, int v);
    /* The cells foe i could walk to as the terrain stands, however far: by
     * the search's own step rules, without its budget, and no step a height
     * up but on stairs, which the foe will not take.  One a cell, row by
     * row. */
    std::vector<char> walkable(size_t i) const;
    /* Whether foe i is a thrower that follows the catchers (foetype.h). */
    bool bombsCatchers(size_t i) const;
    /* Whether foe i is a thrower that bombs the player when beside it: not
     * held, and not a follower (whose bombs, dropped(), are at catchers). */
    bool throwsAtPlayer(size_t i) const;
    /* Its index among the view's foes: kept as the foes before it die. */
    size_t foeId(size_t i) const { return foes_[i].id; }

private:
    struct Foe {
        size_t id;                 // index in the view's foes
        int    u, v, h, facing;
        int    kind, type, mode;   // the search's mode: the type, a chaser's for a facing type
        int    homeU, homeV;
        int    lastU = 0, lastV = 0;   // the last target the chase found a way to
        int    moveDir = 0;        // 1..4 a step, 11..14 a turn, 0 at rest
        int    fromU = 0, fromV = 0;
        double animStart = 0, animMs = 0, stepEnd = -1e9, stepMs = 500;
        int    held = 0;
        int    lastMoveDir = 0;    // ice carries it on this way
        double stuckSince = 0;     // on a sticky pad since then, or 0
        double lastDrop = 0;       // a thrower's last bomb
        bool   dropPending = false;
        bool   riding = false;     // on a platform, carried where it goes
        int    tele = 0;           // a teleporter's hold: 1 before the jump, 2 after, since teleAt
        double teleAt = 0;
        bool   onSwitch = false;   // has worked the switch it stands on
        int    switchSlot = -1;    // the switch worked, not yet acted on
        bool   turned = false;     // its chase has found a way (FoeView::turned)
        double fallsUntil = -1;    // falling into the void, gone then (model clock)
        double padAt = -1;         // launched by the jump pad it stands on, then
    };

    /* A bridge as it moves (bridgeobject.cpp): phase 0 extends next, 1
     * retracts next. */
    struct Bridge {
        int    slot;
        std::vector<Step> deck;
        int    height;
        bool   armed;
        int    phase;
        double phaseStart;
    };
    /* The bridges' ticks: each moving one stamps or clears a deck cell. */
    void tickBridges();
    /* Works switch slot: sets its bridge going if it is not on the move
     * already (false then, and the switch waits). */
    bool workSwitch(int slot, bool byFoe);
    /* A mover at rest on (u, v) at height h works the switch there, once. */
    static bool onSwitch(const Cell &c, int h, bool *worked, int *slot);
    /* tilequery.cpp's Sim_FindNearestListedObjectTile: the nearest raised
     * switch within 7 of (u0, v0), not (u0, v0) itself. */
    bool nearestSwitch(int u0, int v0, int *pu, int *pv) const;

    /* The cell's height and kind, beyond the grid a wall. */
    const Cell &cell(int u, int v) const;
    /* Whether the cell holds no tile at the model's clock: a fallen tile's
     * cell does not, a platform's track does. */
    bool empty(int u, int v) const;
    /* The cell's height at the model's clock: a lift's where it has got to. */
    int heightNow(int u, int v) const;
    /* How much longer the platform stands parked on (u, v) at the model's
     * clock, ms; -1 if it is not parked there. */
    double parkedLeft(int u, int v) const;
    int  occ(int u, int v) const;
    void setOcc(int u, int v, int o);

    /* The search (foepath.cpp's FoePath::find): the first cell of the way
     * from the foe to the target, or false. */
    bool firstStep(const Foe &f, int tu, int tv, int cap, int *nu, int *nv);
    bool search(const Foe &f, int tu, int tv, int cap, int *nu, int *nv);
    bool passable(int u, int v, int mode) const;
    bool stepLegal(int uf, int vf, int ut, int vt) const;

    /* One foe's tick (Foe::chooseTarget, then Foe::step). */
    void tickFoe(size_t i);
    void stepFoe(Foe &f, int tu, int tv, int speed);
    /* Foe::dropBomb: a thrower's bomb asked for is dropped, if it may be now. */
    void dropBomb(Foe &f);
    void target(Foe &f, int *tu, int *tv, int *speed);
    /* tilequery.cpp's Sim_FindNearestFlaggedTileInRadius (a gem near a type
     * 3) and Sim_FindFarthestOccupiedTile (a type 7's target). */
    bool nearestGem(int u0, int v0, int radius, int *pu, int *pv) const;
    bool farthestFree(int u0, int v0, int *pu, int *pv) const;
    /* Foe::chase's direction into the turn table, and its post-checks. */
    int  chaseMove(const Foe &f, int tu, int tv, int speed);
    /* What the terrain under a foe at rest does to its move (movableentity.cpp):
     * a sticky pad holds it, a slide carries it on (*sliding), a teleporter
     * holds it and sends it on, a jump pad throws it up and on, ice keeps it
     * going the way it went. */
    int  terrain(Foe &f, int move, bool *sliding);
    /* The movement integrator's start of a move; false if refused.  A slide
     * moves at its own pace. */
    bool startMove(Foe &f, int move, bool sliding);

    const BotView    *view_ = nullptr;
    std::vector<Foe>  foes_;
    std::vector<Blast> blasts_;
    std::vector<char>  byCaller_;   // a blast the caller added, by blasts_ index
    std::vector<Blast> dropped_;
    std::vector<Kill> kills_;
    std::vector<unsigned char> occ_;   // the cells' occupants, as the sim moves them
    std::vector<unsigned char> gem_;   // a gem still on the cell (type 3 foes eat them)
    std::vector<unsigned char> spent_; // a sticky pad used up
    /* The falling tiles (fallingtile.cpp): armed by whatever stands on one,
     * gone FALL_MS later, and back BACK_MS after it was armed if it comes
     * back.  A fallen tile is void to the foes' search. */
    struct Fall {
        int    u, v;
        bool   respawns, armed, gone, goneAtLoad;
        double armedAt;
    };
    std::vector<Bridge> bridges_;
    std::vector<std::vector<Step>> switchCells_;  // by slot, in grid order
    std::vector<Switched> switched_;
    // The cells, as the bridges change them, when the level has any.
    std::vector<Cell> grid_;
    bool   ownGrid_ = false;
    std::vector<char> cleared_;     // a deck cell the model has cleared
    bool   bridgesMoved_ = false;   // a deck cell changed since the load
    bool   playerOnSwitch_ = false;
    int    playerSwitch_ = -1;      // the switch the player worked, not yet acted on
    double playerRestAt_ = 0;       // when the player's step onto its cell ends
    std::vector<Fall> falls_;
    std::vector<int>  fallAt_;         // a cell's index in falls_, or -1
    int               fallsChanged_ = 0;  // tiles gone or back since the load
    void tickFalls();
    double clock_ = 0;
    Step   player_{-1, -1};
    int    playerOcc_ = 4;
    // The cell the player's step left, held until the step ends.
    Step   playerLeft_{-1, -1};
    double leftUntil_ = 0;
    Cell   wall_;

    // The search's scratch, reused.
    struct Node {
        int f, h, g, u, v, key, parent;
        int children[8];
        int next;
    };
    std::vector<Node> nodes_;
    std::vector<int>  openAt_, closedAt_;   // the node a key has, or -1
    std::vector<int>  keys_;                // keys set this search

    // The first steps found, by (from, target, mode, cap), while the terrain
    // hashes to terrain_; -1 for none.  sticky_: the level has sticky pads,
    // whose rule reads occupants, so nothing is kept.
    std::unordered_map<unsigned long long, int> firstSteps_;
    unsigned long long terrain_ = 0;
    bool sticky_ = false;
};

/* How many gems the level can lose and still make its quota: those on the
 * map, hidden in blocks and carried by foes, less those still needed. */
int spareGems(const BotView &view);
/* How many gems the kills wipe: a foe dying leaves what it drops on its
 * cell, nothing if it carries nothing, in place of a gem there
 * (Foe::finishDespawn). */
int gemsWiped(const BotView &view, const std::vector<FoeModel::Kill> &kills);
/* How many catchers a follower in the view can reach, the most any one can:
 * those it will bomb (loads sim with the view). */
int catchersHunted(FoeModel &sim, const BotView &view);

}  // namespace bot
