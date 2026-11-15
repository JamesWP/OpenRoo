/* The survey: what the bot works out once, at the start of a level, with a
 * budget of its own (the frames' budgets are for the rest).  The MoveGraph,
 * the gems and the exit; more as they are needed. */
#pragma once

#include <map>
#include <memory>
#include <vector>
#include "movegraph.h"

class World;

namespace simbot {

class Action;

class Survey {
public:
    explicit Survey(const World &start);

    const MoveGraph &graph() const { return graph_; }
    /* The cells the level's gems lie on at the start, in grid order: a gem
     * is known by its index here (the first IndexSet::MAX). */
    const std::vector<Cell> &gems() const { return gems_; }
    /* The cells of the bomb pickups (3 bombs each) at the start, known by
     * their index like the gems. */
    const std::vector<Cell> &pickups() const { return pickups_; }
    /* The cells of the gliders (a glider charge each), likewise. */
    const std::vector<Cell> &gliders() const { return gliders_; }
    /* The foes a bomb kills (the catchers; throwers are proof against
     * blasts) at the start, by Listed id, and the cell each was last seen
     * on (see), known by their index here (the first 64). */
    const std::vector<unsigned> &foes() const { return foes_; }
    Cell seen(int i) const { return seen_[i]; }
    /* The cells of the falling tiles: a foe stood on one when it drops
     * falls with it (trap.cpp). */
    const std::vector<Cell> &fallings() const { return fallings_; }
    /* Notes where the foes are now. */
    void see(const World &w);

    Cell exit() const { return exit_; }
    int  quota() const { return quota_; }

    /* What an Action works out once for a level, kept for it here: made
     * empty the first time the Action asks, filled as it likes (lazily, as
     * the trap's stands are).  The Survey holds what is true of the level;
     * an Action's own working on it is its Working. */
    struct Working {
        virtual ~Working() = default;
    };
    template <class T> T &working(const Action &a) const
    {
        std::unique_ptr<Working> &slot = working_[&a];
        if (!slot)
            slot = std::make_unique<T>();
        return static_cast<T &>(*slot);
    }

private:
    MoveGraph         graph_;
    std::vector<Cell> gems_;
    std::vector<Cell> pickups_;
    std::vector<Cell> gliders_;
    std::vector<unsigned> foes_;
    std::vector<Cell> seen_;
    std::vector<Cell> fallings_;
    mutable std::map<const Action *, std::unique_ptr<Working>> working_;
    Cell              exit_;
    int               quota_ = 0;
};

}  // namespace simbot
