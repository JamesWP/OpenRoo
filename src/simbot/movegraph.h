/* MoveGraph: where each move takes the player, from every place on the
 * ground it can come to rest on (moves.h); a move that begins a glide leads
 * to its landings, worked out by the rules (gliding.h), as the game has it: every move played once from
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
#include <memory>
#include <vector>
#include "moves.h"

class World;

namespace simbot {

/* The one-time falling tiles armed or fallen, a bit for each by its index
 * in World::fallings (the first 64). */
uint64_t onceDown(const World &w);

class MoveGraph {
public:
    /* blasts: whether to find where a bomb dropped at each place blows;
     * glides: whether the player may glide (it is given glider charges at
     * each place). */
    MoveGraph(const World &start, bool blasts, bool glides);

    /* waits waits (schedule.h), then move (or the waits alone, move WAIT);
     * breaks: the one-time falling tiles it arms, a bit for each by its
     * index in World::fallings (the first 64); glides: the glider
     * charges it spends. */
    struct Edge {
        int      to;
        int      waits;
        Move     move;
        double   ms;
        uint64_t breaks;
        int      glides;
    };

    size_t size() const { return places_.size(); }
    const Place &place(int i) const { return places_[i]; }
    const std::vector<Edge> &edges(int i) const { return edges_[i]; }
    /* The place's index, or -1; with any height and facing on its cell if
     * not that one. */
    int find(const Place &p) const;
    /* The place's index, or -1: that place only. */
    int exactly(const Place &p) const;
    /* The places on a cell (on the ground: not in the air above it). */
    const std::vector<int> &on(Cell c) const;

    /* The least ms from place from to every place (< 0: no way), worked
     * out when asked for (the last few kept). */
    using Table = std::shared_ptr<const std::vector<double>>;
    Table times(int from) const;
    /* The least ms from every place to a place on cell c (< 0: no way),
     * likewise. */
    Table timesTo(Cell c) const;
    /* The least ms from place at to cell c (< 0: no way, or at is not on
     * the ground): with glides glider charges or more, the quickest way and,
     * with used, the charges it spends; with none, the quickest that glides
     * nowhere.  Worked out for each cell the first time it is asked for. */
    double timeTo(int at, Cell c, int glides, int *used = nullptr) const;
    /* The least ms from place at to cell c or one beside it (a foe's, which
     * the player may not stand on), never gliding (a foe does not). */
    double timeBeside(int at, Cell c) const;
    /* The arrival on cell c from place from, by timeTo: a place on it (of
     * those come to, one in the region that leads to the most of theirs),
     * -1 if there is no way, and its ms. */
    int arrive(int from, Cell c, double *ms, int glides = 0, int *used = nullptr) const;

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

    /* Regions: the places each can come to the other from, and back (the
     * strongly connected components; a drop or a glide leaves one for good).
     * A place's region, -1 for none; and whether region a reaches region b
     * (or is it), for regions with a place on the ground. */
    int  region(int place) const;
    bool regionReaches(int a, int b) const;
    int  regions() const;

    /* The moves played to build it, and the edges that break a tile. */
    long played() const { return played_; }
    long breaking() const { return breaking_; }
    /* The glide landings worked out (gliding.h). */
    long glided() const { return glided_; }

private:
    Blast blastOf(const World &at, Move drop);

    std::vector<Place>              places_;
    std::vector<std::vector<Edge>>  edges_;
    std::vector<std::array<Blast, 2>> blasts_;
    std::map<Place, int>            index_;
    std::map<Cell, std::vector<int>> byCell_;
    /* To a cell from each ground place (by groundOf_): the least ms
     * gliding nowhere and gliding as it may, and the charges spent on the
     * second (< 0 ms: no way). */
    struct Toward {
        std::vector<float>   walk, any;
        std::vector<uint8_t> glides;
    };
    const Toward &toward(Cell c) const;
    /* The least ms from every place to a place on cell c, with or without
     * the edges that glide, and the charges spent. */
    void backward(Cell c, bool glide, std::vector<double> &t, std::vector<int> *gl) const;

    std::vector<int> groundOf_;   // by place: its index among the ground ones, -1 in the air
    mutable std::map<int, Table> times_;
    mutable std::map<Cell, Table> timesTo_;
    mutable std::map<Cell, Toward> toward_;
    mutable std::vector<std::vector<std::pair<int, const Edge *>>> into_;
    void findRegions() const;
    mutable std::vector<int> region_;            // by place
    mutable std::vector<int> ground_;            // by region: its index among the ground ones, or -1
    mutable std::vector<std::vector<uint64_t>> reaches_;   // by ground index: a bit per ground index
    long played_ = 0;
    long breaking_ = 0;
    long glided_ = 0;
};

}  // namespace simbot
