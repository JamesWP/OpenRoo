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

/* A bomb pickup: on the map, or (block >= 0) hidden in that bombable block
 * until it is spent. */
struct Pickup {
    Step cell;
    int  block = -1;
};

/* A place a bomb dropped spends blocks from, and which. */
struct Site {
    Place cell;
    Spent hits = 0;
};

/* The way to collect the quota from a place and go on to the exit. */
struct GemRoute {
    bool              found = false;
    std::vector<Step> gems;        // in visiting order
    double            toLast = 0;  // ms, to the last gem
    double            toExit = 0;  // ms, from the last gem to the exit
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
    Step  exit() const { return {live_.exitU, live_.exitV}; }
    /* The bomb pickups, on the map and hidden. */
    const std::vector<Pickup> &pickups() const { return pickups_; }
    /* Whether the pickup is there to take once the blocks in s are spent. */
    static bool available(const Pickup &p, Spent s) { return p.block < 0 || (s >> p.block & 1); }

    Worlds &worlds() const { return *worlds_; }
    /* The set with every block spent. */
    Spent everything() const;

    /* The gems to be had in the world s: those on the map, and those the
     * spent blocks hide. */
    const std::vector<Step> &gems(Spent s) const;

    /* The least time from a place to a cell, in the world s, ms; < 0 if
     * there is no way. */
    double time(Spent s, Place from, Step to) const;
    /* A turn to a facing, from a place, ms. */
    double turnMs(Place from, int facing) const;

    const GemRoute &gemRoute(Spent s, Place from) const;
    /* Whether, standing on that gem as things are, the rest of the quota and
     * the exit can still be had: a gem that drops the player where it cannot
     * is a trap. */
    bool canFinishAfter(Step gem) const;
    /* Whether, standing on that cell as things are, the quota and the exit can
     * be had. */
    bool canFinishFrom(Step cell) const;

    /* The blocks, unspent in the world s, that stand in the way: on the
     * quickest way from a place through the quota's gems to the exit once
     * every block is spent (and, with pickups, to the bomb pickups too), a
     * gem's or a pickup's own block too.  Spending any other block cannot
     * shorten the way.  Empty when the way is clear. */
    Spent inTheWay(Spent s, Place from, bool pickups) const;

    /* Every place, in the world s, a bomb would spend some block of that
     * world from: where it lands (rolling a step along the facing) within
     * reach of a block at its height. */
    const std::vector<Site> &sites(Spent s) const;

    /* Why there is no way to the goal at all, for the note: "no route: ...". */
    std::string noRoute() const;

private:
    GemRoute routeFrom(Spent s, Place from, const Step *taken, int need) const;
    int facingOf(const Place &p) const { return p.facing ? p.facing : worlds_->pointFacing(); }

    const BotView &live_;
    Worlds        *worlds_;
    int            gemsNeeded_ = 0;
    std::vector<Pickup> pickups_;
    mutable std::map<Spent, std::vector<Step>> gems_;
    mutable std::map<std::pair<Spent, Place>, GemRoute> routes_;
    mutable std::map<std::pair<int, int>, bool> after_;
    mutable std::map<Spent, std::vector<Site>> sites_;
    mutable std::map<std::tuple<Spent, Place, bool>, Spent> way_;
};

}  // namespace bot
