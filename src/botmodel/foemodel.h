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
 * it ended), and the terrain's say over a foe at rest (a slide carries it on at
 * 150 ms a cell, ice the way it last moved, a sticky pad holds it 3 s).  Foes
 * see each other and the player through the cells' occupants,
 * as in the game.
 *
 * The search reads only the terrain (except a gem runner's and a switch
 * runner's, which read the occupants, and any search past a sticky pad), so
 * its answer, the first step from a cell towards a target, is the same every
 * time while the terrain is: the model keeps it, across loads, until the
 * terrain changes.  That is what makes a foe's walked path cheap to work out
 * again for each plan the bot weighs.
 *
 * A foe caught by a blast dies (a bomb in the view, or one the caller adds).
 *
 * What it leaves out: the lifts, platforms, bridges and falling tiles do not
 * move, foe bombs are not dropped, the idle animation's longer moves, and the
 * target of a switch runner (the view does not carry the switch cells), which
 * is taken to be the player (foetype.h).  botrun.h's foe log measures how often it is right against the game
 * (tools/bot/foecheck.py). */
#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include "bombmodel.h"
#include "botview.h"

namespace bot {

class FoeModel {
public:
    /* Starts from the view: the foes as they stand, at the view's clock, and
     * the blasts of the bombs in it (bombmodel.h). */
    void load(const BotView &view);
    /* A blast more, of a bomb the caller means to drop: a foe in it dies. */
    void addBlast(const Blast &b) { blasts_.push_back(b); }
    /* A foe a blast killed: when (the model's clock), where it stood, and
     * whether only a blast the caller added did it (not a bomb in the view,
     * which would have anyway). */
    struct Kill {
        double ms;
        int    u, v;
        bool   byAdded;
    };
    /* The foes killed by blasts since load(). */
    const std::vector<Kill> &kills() const { return kills_; }
    /* How many of them only the caller's blasts killed. */
    int  killed() const;

    /* Ticks the foes until the clock reaches untilMs, the player standing on
     * playerAt(t) at each tick's clock t. */
    void run(double untilMs, const std::function<Step(double)> &playerAt);
    /* One tick of run(); nothing if the view has no tick step. */
    void runTick(const std::function<Step(double)> &playerAt);

    double now() const { return clock_; }
    size_t foeCount() const { return foes_.size(); }
    Step   foeCell(size_t i) const { return {foes_[i].u, foes_[i].v}; }
    int    foeKind(size_t i) const { return foes_[i].kind; }

private:
    struct Foe {
        int    u, v, h, facing;
        int    kind, type, mode;   // the search's mode: the type, a chaser's for a facing type
        int    homeU, homeV;
        int    moveDir = 0;        // 1..4 a step, 11..14 a turn, 0 at rest
        int    fromU = 0, fromV = 0;
        double animStart = 0, animMs = 0, stepEnd = -1e9, stepMs = 500;
        int    held = 0;
        int    lastMoveDir = 0;    // ice carries it on this way
        double stuckSince = 0;     // on a sticky pad since then, or 0
    };

    /* The cell's height and kind, beyond the grid a wall. */
    const Cell &cell(int u, int v) const;
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
    void target(Foe &f, int *tu, int *tv, int *speed);
    /* tilequery.cpp's Sim_FindNearestFlaggedTileInRadius (a gem near a type
     * 3) and Sim_FindFarthestOccupiedTile (a type 7's target). */
    bool nearestGem(int u0, int v0, int radius, int *pu, int *pv) const;
    bool farthestFree(int u0, int v0, int *pu, int *pv) const;
    /* Foe::chase's direction into the turn table, and its post-checks. */
    int  chaseMove(const Foe &f, int tu, int tv, int speed);
    /* The movement integrator's start of a move; false if refused.  A slide
     * moves at its own pace. */
    bool startMove(Foe &f, int move, bool sliding);

    const BotView    *view_ = nullptr;
    std::vector<Foe>  foes_;
    std::vector<Blast> blasts_;
    size_t viewBlasts_ = 0;   // the blasts that came from the view
    std::vector<Kill> kills_;
    std::vector<unsigned char> occ_;   // the cells' occupants, as the sim moves them
    std::vector<unsigned char> gem_;   // a gem still on the cell (type 3 foes eat them)
    std::vector<unsigned char> spent_; // a sticky pad used up
    double clock_ = 0;
    Step   player_{-1, -1};
    int    playerOcc_ = 4;
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

}  // namespace bot
