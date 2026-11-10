/* MoveGraph: where each move takes the player, from every place it can come
 * to rest on (moves.h), as the game has it: every move played once from
 * each place, on a copy of the World with the foes removed
 * (whatif::removeFoes).  Built once, at the start of the level (survey.h).
 *
 * A move is played once, at whatever time its place was first come to, so
 * what hangs on time (a lift, a platform, a falling tile) is as it was then:
 * the graph is the planner's estimate, and an Attempt finds the real way. */
#pragma once

#include <map>
#include <vector>
#include "moves.h"

class World;

namespace simbot {

class MoveGraph {
public:
    explicit MoveGraph(const World &start);

    struct Edge {
        int    to;
        Move   move;
        double ms;
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
    /* The quickest arrival on cell c from place from: the place, -1 if
     * there is none, and its ms. */
    int arrive(int from, Cell c, double *ms) const;

    /* The moves played to build it. */
    long played() const { return played_; }

private:
    std::vector<Place>              places_;
    std::vector<std::vector<Edge>>  edges_;
    std::map<Place, int>            index_;
    std::map<Cell, std::vector<int>> byCell_;
    mutable std::map<int, std::vector<double>> times_;
    long played_ = 0;
};

}  // namespace simbot
