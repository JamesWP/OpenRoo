/* The board: the level as the planner (goap.h) plans over it.
 *
 * The planner's facts say where the player is, which bombable blocks are
 * spent, and what has been taken.  The board answers what those facts imply,
 * from the model: the gems on the map in each world (worlds.h), the times to
 * go from a place to a cell, the quota's route from a place, and the places a
 * bomb dropped would spend blocks from.
 *
 * Collecting the gem quota is the route optimiser's question (route.h): from
 * a place, which gems and in what order, on the way to the exit.  The board
 * answers it and keeps the answer for each place. */
#pragma once

#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "botview.h"
#include "worlds.h"

namespace bot {

/* Where the player stands, at rest: a cell and a facing (0: as the player
 * faces now). */
struct Place {
    int u = 0, v = 0, facing = 0;
    bool operator<(const Place &o) const
    {
        return std::make_tuple(u, v, facing) < std::make_tuple(o.u, o.v, o.facing);
    }
};

/* A set of gems taken, by Board::gemBit. */
using Taken = uint64_t;

/* A pickup the plans may use, bombs or a paraglider: on the map, or (block
 * >= 0) hidden in that bombable block until it is spent. */
struct Pickup {
    enum Kind { Bombs, Glider };
    Step cell;
    int  block = -1;
    Kind kind = Bombs;
};

/* A place a bomb dropped spends blocks from, and which. */
struct Site {
    Place cell;
    Changes hits = 0;
    int     far = 0;    // a bait: the cells the nearest thrower walks to stand beside it
};

/* A bombable block that pens a thrower in: spent, the thrower can reach
 * catchers it cannot now.  A follower bombs the catchers it follows
 * (foemodel.h), so they die without a bomb of the player's. */
struct Release {
    Changes gate = 0;   // the block's bit
    int     foes = 0;   // the catchers it then reaches
};

/* A way across a run of the falling tiles that do not come back, which the
 * other walks keep off (fallingmodel.h, oneShotRuns): from a cell beside it,
 * over it, to another; the run falls behind the player.  Its bit is the
 * run's, in Facts::crossed. */
struct Crossing {
    Step     from;
    Place    to;
    double   ms = 0;
    unsigned bit = 0;
};

/* The way to collect the quota from a place and go on to the exit. */
struct GemRoute {
    bool              found = false;
    std::vector<Step> gems;        // in visiting order (and the time bonuses, if timed)
    double            toLast = 0;  // ms, to the last gem
    double            toExit = 0;  // ms, from the last gem to the exit
    unsigned          bonuses = 0; // timed: the time bonuses on the way, by Board::timeBonuses
};

class Board {
public:
    /* The view as it is now, and the worlds over its terrain (which the
     * caller keeps, and has updated to this view). */
    Board(const BotView &live, Worlds &worlds);

    const BotView &live() const { return live_; }
    Place start() const { return {live_.player.u, live_.player.v, live_.player.facing}; }
    int   gemsNeeded() const { return gemsNeeded_; }
    int   bombs() const { return live_.player.bombs; }
    int   foes() const { return (int)live_.foes.size(); }
    /* Whether the foe leaves a gem when killed (foe.cpp, finishDespawn). */
    static bool carrier(const FoeView &foe);
    /* The foes that do. */
    int   carriers() const { return carriers_; }
    Step  exit() const { return {live_.exitU, live_.exitV}; }
    /* Whether the player has, or could pick up, a glide. */
    bool glidesToBe() const;
    /* The pickups, on the map and hidden. */
    const std::vector<Pickup> &pickups() const { return pickups_; }
    /* The time bonuses, on the map and hidden (their kind unused), apart
     * from pickups(): no plan but one against the clock goes for them. */
    const std::vector<Pickup> &timeBonuses() const { return timeBonuses_; }
    /* Whether a time bonus is on the cell, to take now. */
    bool timeBonus(Step c) const;
    /* The ms of play left with the time bonuses taken (a bit each, by their
     * place in timeBonuses()), as the plans' times count it: less a margin
     * for their error.  < 0 if the level has no time limit. */
    double timeLeft(unsigned bonuses) const;
    /* Whether the pickup is there to take once the blocks in s are spent. */
    static bool available(const Pickup &p, Changes s) { return p.block < 0 || (s >> p.block & 1); }

    Worlds &worlds() const { return *worlds_; }
    /* The changes that spend every block. */
    Changes allBlocks() const;
    /* The most open world: every block spent, every bridge extended, and the
     * switches passed over (worlds.h).  Its routes are a lower bound. */
    Changes everything() const;
    /* How many cells can be come to rest on from the cell, in the world s. */
    int reach(Changes s, Step from) const;
    /* Whether some cell can be come to rest on from the cell in the world
     * next that cannot be from the place in the world s. */
    bool opens(Changes next, Step cell, Changes s, Place from) const;
    /* The catchers that can be come to from the cell in the world next and
     * cannot be from the place in the world s: a bridge lets them in. */
    int letsIn(Changes next, Step cell, Changes s, Place from) const;
    /* Whether bridge i is extended in the world s. */
    bool extended(Changes s, size_t i) const
    {
        return live_.bridges[i].extended != ((s & worlds_->bridgeBit(i)) != 0);
    }
    /* The bridge the switch on the cell works, by its index, or -1. */
    int switched(Step cell) const;
    /* The switches: each working switch cell and its bridge's index. */
    const std::vector<std::pair<Step, int>> &switches() const { return switches_; }

    /* The gems to be had in the world s: those on the map, and those the
     * spent blocks hide. */
    const std::vector<Step> &gems(Changes s) const;

    /* The least time from a place to a cell, in the world s, ms; < 0 if
     * there is no way. */
    double time(Changes s, Place from, Step to) const;
    /* A turn to a facing, from a place, ms. */
    double turnMs(Place from, int facing) const;

    /* The way to collect need of the gems on the map not taken (the quota, by
     * default) and go on to the exit. */
    const GemRoute &gemRoute(Changes s, Place from, int need = -1, Taken taken = 0) const;
    /* The same against the clock (route.h, planTimedRoute), left ms of play
     * left at from and the bonuses taken already: the gems (need of them, 0
     * for none) and the time bonuses it takes to have them and the exit in
     * time, with the bonuses on the way.  Not found if it cannot be done. */
    const GemRoute &timedRoute(Changes s, Place from, int need, Taken taken, unsigned bonuses,
                               double left) const;
    /* The way to collect every gem that can be reached and is not taken, up
     * to most, ending at the last (toExit 0): what one part of the level
     * gives before a glide to another. */
    const GemRoute &gather(Changes s, Place from, Taken taken, int most) const;
    /* The gem's bit in a Taken set: by its place among gems(everything()),
     * the first 64. */
    Taken gemBit(Step gem) const;
    /* The gems the quota needs from the map: those the carriers cannot give. */
    int   fromMap() const { return gemsNeeded_ > carriers_ ? gemsNeeded_ - carriers_ : 0; }
    /* Whether, standing on that gem, the rest of the quota (the carriers'
     * gems aside) and the exit can still be had, as things are or with the
     * blocks spent if bombs may spend them (spendable): a gem that drops the
     * player where it cannot is a trap. */
    bool canFinishAfter(Step gem) const;
    /* Whether, standing on that cell, the quota (the carriers' gems aside)
     * and the exit can be had, as canFinishAfter; where glides may be had,
     * any cell, the walk alone being no measure.  Both take a cell from
     * which a one-shot run may be crossed (crossings) to be no trap. */
    bool canFinishFrom(Step cell) const;
    /* The blocks that may yet be spent: every one if the player holds bombs,
     * a bomb pickup is about or a thrower may be baited (baits); else none. */
    Changes spendable() const;

    /* The changes the world s lacks that stand in the way: on the quickest
     * way from a place through the quota's gems to the exit in the most open
     * world (and, with pickups, to the bomb pickups too), the blocks unspent
     * (a gem's or a pickup's own block too) and the bridges not extended.
     * Making any other change cannot shorten the way.  Empty when the way is
     * clear. */
    Changes inTheWay(Changes s, Place from, bool pickups) const;

    /* Every place, in the world s, a bomb would spend some block of that
     * world from: where it lands (rolling a step along the facing) within
     * reach of a block at its height. */
    const std::vector<Site> &sites(Changes s) const;

    /* Every cell, in the world s, a thrower's bomb would spend some block of
     * that world on, with the player standing there as bait: a thrower that
     * bombs the player can walk next to it, and its bomb, dropped there
     * facing the player, rolls onto it, and the player can get clear of it.
     * Facing 0; far, how far the nearest thrower walks to get there. */
    const std::vector<Site> &baits(Changes s) const;

    /* The blocks, each spent alone, that let a follower out to catchers. */
    const std::vector<Release> &releases() const;
    /* The catchers a follower can reach now: it will bomb them. */
    int hunted() const;
    /* Whether a release is still to be had in the world s, with bombs. */
    bool releasePending(Changes s, int bombs) const;

    /* The ways across the one-shot runs: for each run (the first 32), from
     * each cell beside it to each other the player can come to rest on over
     * it alone, the quickest. */
    const std::vector<Crossing> &crossings() const;
    /* Whether a run not in crossed has a way across from a cell reachable
     * from the place, in the world s. */
    bool crossable(Changes s, Place from, unsigned crossed) const;

    /* Why there is no way to the goal at all, for the note: "no route: ...". */
    std::string noRoute() const;

private:
    GemRoute routeFrom(Changes s, Place from, Taken taken, int need, bool toExit) const;
    int facingOf(const Place &p) const { return p.facing ? p.facing : worlds_->pointFacing(); }

    const BotView &live_;
    Worlds        *worlds_;
    int            gemsNeeded_ = 0;
    int            carriers_ = 0;
    std::vector<Pickup> pickups_;
    std::vector<Pickup> timeBonuses_;
    std::vector<std::pair<Step, int>> switches_;
    mutable std::map<Changes, std::vector<Step>> gems_;
    mutable std::map<std::tuple<Changes, Place, int, Taken>, GemRoute> routes_;
    mutable std::map<std::tuple<Changes, Place, int, Taken, unsigned, long>, GemRoute> timed_;
    mutable std::map<std::pair<int, int>, bool> after_;
    mutable std::map<Changes, std::vector<Site>> sites_;
    mutable std::map<Changes, std::vector<Site>> baits_;
    mutable std::map<std::tuple<Changes, int, int>, int> reach_;
    mutable std::map<std::tuple<Changes, Place, bool>, Changes> way_;
    mutable std::vector<Release> releases_;
    mutable bool releasesFound_ = false;
    mutable Changes spendable_ = 0;
    mutable bool    spendableFound_ = false;
    mutable int  hunted_ = 0;
    mutable std::vector<Crossing> crossings_;
    mutable bool crossingsFound_ = false;
};

}  // namespace bot
