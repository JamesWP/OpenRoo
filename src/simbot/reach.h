/* Reach: the quickest ways from a World, the player at rest, to every place
 * the player can come to rest on, by moves (moves.h) played on copies of the
 * World: Dijkstra on the clock, over places.
 *
 * The first way to a place is the only one kept, so what else differs
 * between two ways there (a gem picked up on one, the foes elsewhere) is
 * not weighed.  The World each way leads to is kept only for the places
 * asked for (the targets): the rest are dropped once their moves on are
 * played, so a search holds few Worlds at once. */
#pragma once

#include <memory>
#include <vector>
#include "moves.h"

class World;

namespace simbot {

struct Cell {
    int u = 0, v = 0;
};

class Reach {
public:
    /* From start, keeping the Worlds of the places on targets, with at most
     * maxPlaces places reached. */
    Reach(const World &start, const std::vector<Cell> &targets, size_t maxPlaces = 6000);
    ~Reach();

    /* The quickest place reached on each target cell, the quickest first. */
    const std::vector<int> &arrivals() const { return arrivals_; }
    /* How many places were reached in all. */
    size_t places() const { return nodes_.size(); }

    const World &world(int at) const { return *nodes_[at].world; }
    Place        place(int at) const { return nodes_[at].place; }
    /* Ms of play from the start. */
    double       ms(int at) const;
    /* The presses of each tick from the start, in order. */
    std::vector<unsigned> presses(int at) const;

private:
    struct Node {
        int    parent = -1;
        Move   move = FORWARD;
        int    ticks = 0;
        double now = 0;
        Place  place;
        std::unique_ptr<World> world;
    };
    std::vector<Node> nodes_;
    std::vector<int>  arrivals_;
    double            start_ = 0;
};

}  // namespace simbot
