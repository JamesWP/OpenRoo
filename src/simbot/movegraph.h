/* MoveGraph: where each move takes the player, from every place it can come
 * to rest on (moves.h), as the game has it: every move played once from
 * each place, on a copy of the World with the foes removed
 * (whatif::removeFoes).  Built once, at the start of the level (survey.h).
 *
 * A place is played from at the time it was first come to, but with every
 * falling tile back in place (whatif::restoreFallings): an edge has them
 * all, and notes the ones that never come back that it breaks.  Each move is
 * also played after waiting till each of the schedule's wait times there
 * (schedule.h: a lift or platform beside it parking, a sticky pad letting
 * go): an edge is kept for each place first reached that
 * way, the wait counted in.  Still only an
 * estimate for the planner; an Attempt finds the real way. */
#pragma once

#include <stdint.h>
#include <array>
#include <functional>
#include <map>
#include <vector>
#include "moves.h"

class World;

namespace simbot {

/* The one-time falling tiles armed or fallen, a bit for each by its index
 * in World::fallings (the first 64). */
uint64_t onceDown(const World &w);

class MoveGraph {
public:
    /* blasts: whether to find where a bomb dropped at each place blows. */
    MoveGraph(const World &start, bool blasts);

    /* waits waits (schedule.h), then move (or the waits alone, move WAIT);
     * breaks: the one-time falling tiles it arms, a bit for each by its
     * index in World::fallings (the first 64). */
    struct Edge {
        int      to;
        int      waits;
        Move     move;
        double   ms;
        uint64_t breaks;
    };

    size_t size() const { return places_.size(); }
    const Place &place(int i) const { return places_[i]; }
    const std::vector<Edge> &edges(int i) const { return edges_[i]; }
    /* The place's index, or -1; with any height and facing on its cell if
     * not that one. */
    int find(const Place &p) const;
    /* The places on a cell. */
    const std::vector<int> &on(Cell c) const;

    /* The least ms from place from to every place (< 0: no way), worked
     * out the first time it is asked for. */
    const std::vector<double> &times(int from) const;
    /* The least ms from every place to a place on cell c (< 0: no way),
     * worked out the first time it is asked for. */
    const std::vector<double> &timesTo(Cell c) const;
    /* The least ms from place at to cell c or one beside it (a foe's, which
     * the player may not stand on); < 0: no way, or at is -1. */
    double timeTo(int at, Cell c) const;
    /* The quickest arrival on cell c from place from: the place, -1 if
     * there is none, and its ms. */
    int arrive(int from, Cell c, double *ms) const;

    /* The quickest way from place from to a place goal accepts, through no
     * place blocked accepts (from aside): the places along it, from first;
     * empty if there is none. */
    std::vector<int> path(int from, const std::function<bool(int)> &goal,
                          const std::function<bool(int)> &blocked) const;

    /* Where a bomb the player drops at a place blows (the foes removed):
     * dropped standing (DROP) and stepping back (DROP_BACK), each its
     * cell and height; ok false if it never blew.  None if not asked
     * for. */
    struct Blast {
        Cell cell;
        int  h = 0;
        bool ok = false;
    };
    const Blast *blasts(int i) const { return blasts_.empty() || i < 0 ? nullptr : blasts_[i].data(); }

    /* The moves played to build it, and the edges that break a tile. */
    long played() const { return played_; }
    long breaking() const { return breaking_; }

private:
    Blast blastOf(const World &at, Move drop);

    std::vector<Place>              places_;
    std::vector<std::vector<Edge>>  edges_;
    std::vector<std::array<Blast, 2>> blasts_;
    std::map<Place, int>            index_;
    std::map<Cell, std::vector<int>> byCell_;
    mutable std::map<int, std::vector<double>> times_;
    mutable std::map<Cell, std::vector<double>> timesTo_;
    long played_ = 0;
    long breaking_ = 0;
};

}  // namespace simbot
