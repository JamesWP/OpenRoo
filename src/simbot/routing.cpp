#include "routing.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "schedule.h"
#include "tile.h"
#include "world.h"

namespace simbot {

bool Expect::operator==(const Expect &o) const
{
    return now == o.now && u == o.u && v == o.v && h == o.h && facing == o.facing &&
           moveDir == o.moveDir;
}

Expect expectOf(const World &w)
{
    const Player *p = w.player();
    return {w.now(), p->cellU(), p->cellV(), p->heightCell(), p->facing(), p->moveDir()};
}

namespace {

/* The estimate for a place the MoveGraph has no way on from (it is only an
 * estimate): far, not never. */
const double NO_WAY_MS = 60000;
/* How long the player must outlive, standing, at a goal (a foe may be on
 * its way): the next step is thought out meanwhile. */
const double SAFE_MS = 1000;
/* The moves made after a drop, to get clear of the blast, before it is
 * waited out where the player stands. */
const int ESCAPE_MOVES = 2;
/* still: how near (cells) something on the clock makes a step's landing
 * uncertain. */
const int STILL_CELLS = 1;
/* foeNear: what the foe's walk may take above the short waits. */
const double NEAR_MS = 1000;
/* What a step back counts above its time: enough to choose the step
 * forward over it, no more. */
const double BACK_MS = 1;

}  // namespace

const double BLAST_OVER_MS = Bomb::CLEAR_MS + 20;

bool Goal::reached(const Route::Node &n) const
{
    return n.place.cell() == cell && !n.place.air && safe(*n.world);
}

bool Goal::done(const World &live) const
{
    const Place at = placeOf(live);
    return at.cell() == cell && !at.air;
}

Route::Route(const World &start, std::shared_ptr<Goal> goal, const MoveGraph &guide,
             size_t maxPlaces, int shortWaits, int timedWaits)
    : goal_(std::move(goal)), guide_(&guide), toCell_(guide.timesTo(goal_->cell)), maxPlaces_(maxPlaces),
      shortWaits_(shortWaits), timedWaits_(timedWaits), start_(std::make_unique<World>(start))
{
    const LevelMap *map = start.map();
    for (int u = 0; u < map->extentU(); u++)
        for (int v = 0; v < map->extentV(); v++)
            if (map->tile(u, v)->contents() == CONTENTS_CRYSTAL)
                gemCells_.push_back({u, v});
    Node root;
    root.now   = start.now();
    root.cost  = root.now;
    root.place = placeOf(start);
    root.world = std::make_unique<World>(start);
    nodes_.push_back(std::move(root));
    nodes_[0].key = keyOf(nodes_[0]);
    best_[nodes_[0].key] = 0;
    push(0);
}

Route::~Route() = default;

Place Route::keyPlace(const Node &n) const
{
    // Riding a lift, the player's height is the lift's cut down to a whole
    // one, which a move ending just before the lift comes to a height reads
    // as the one below: a turn and a wait ending a tick apart would read a
    // height apart.  The lift's own height, to the nearest whole, is where
    // the player is.
    Place at = n.place;
    if (!at.air && carried(*n.world) != 0)
        for (const LiftObject &l : n.world->lifts())
            if (l.cellU() == at.u && l.cellV() == at.v)
                at.h = (int)lroundf(l.height());
    return at;
}

Route::Key Route::keyOf(const Node &n) const
{
    if (n.dropAt < 0)
        return goal_->key(n, {keyPlace(n), n.waited, carried(*n.world), false, {}, {}});
    Cell bomb{-1, -1};
    if (!n.world->bombs().empty()) {
        const Bomb &b = n.world->bombs()[n.world->bombs().size() - 1];
        bomb = {b.cellU(), b.cellV()};
    }
    return goal_->key(n, {n.place, n.waited, carried(*n.world), true, {-1, -1}, bomb});
}

bool safe(const World &at)
{
    if (at.foes().empty())
        return true;
    World w(at);
    const double until = w.now() + SAFE_MS;
    while (w.now() < until) {
        tick(w, 0);
        if (dead(w))
            return false;
    }
    return true;
}

bool Route::gemsEnough(const World &w) const
{
    int left = w.player()->gemsCollected();
    for (const Cell &c : gemCells_)
        left += w.map()->tile(c.u, c.v)->contents() == CONTENTS_CRYSTAL;
    return left >= w.gemsRequired();
}

bool Route::reached(const Node &n) const
{
    // Never at the cost of the level: a blast may take gems with it.
    return gemsEnough(*n.world) && goal_->reached(n);
}

bool Route::mayDrop(const Node &n) const
{
    return n.dropAt < 0 && n.world->player()->bombsCarried() > 0 && goal_->mayDrop(n);
}

bool Route::foeNear(const Node &n) const
{
    const double within = shortWaits_ * WAIT_TICKS * 1000.0 / 60 + NEAR_MS;
    const int at = guide_->find(n.place);
    for (const Foe &f : n.world->foes()) {
        if (f.moveState() == 4 || f.heightCell() < 0)
            continue;
        const int from = guide_->find({f.cellU(), f.cellV(), f.heightCell(), 0});
        // Off the player's MoveGraph (or the player is): near enough.
        if (from < 0 || at < 0)
            return true;
        const double t = guide_->timeBeside(from, n.place.cell());
        if (t >= 0 && t <= within)
            return true;
    }
    return false;
}

bool Route::still(const World &w, const Place &from, const Place &to) const
{
    if (from.air || to.air || !w.bombs().empty() || carried(w) != 0)
        return false;
    for (const BridgeObject &b : w.bridges())
        if (b.armed())
            return false;
    const int u0 = std::min(from.u, to.u) - STILL_CELLS, u1 = std::max(from.u, to.u) + STILL_CELLS;
    const int v0 = std::min(from.v, to.v) - STILL_CELLS, v1 = std::max(from.v, to.v) + STILL_CELLS;
    for (const LiftObject &l : w.lifts())
        if (l.cellU() >= u0 && l.cellU() <= u1 && l.cellV() >= v0 && l.cellV() <= v1)
            return false;
    for (int u = u0; u <= u1; u++)
        for (int v = v0; v <= v1; v++) {
            const int m = (signed char)w.map()->tile(u, v)->objectMarker();
            if (m == TILE_PLATFORM_U || m == TILE_PLATFORM_V || m == TILE_PLATFORM_TRACK)
                return false;
        }
    return true;
}

bool Route::stillKey(const Node &n, int move, StepKey *key) const
{
    if (n.dropAt >= 0 || n.place.air || move >= MOVES)
        return false;
    const auto &fl = n.world->fallings();
    if (fl.size() > 32)
        return false;
    uint64_t bits = 0;
    for (size_t i = 0; i < fl.size(); i++)
        bits |= (uint64_t)(fl[i].armed() ? 1 : 0) << (2 * i) |
                (uint64_t)(fl[i].respawnPending() ? 1 : 0) << (2 * i + 1);
    // And which way each bridge goes next (one may have been switched).
    const auto &br = n.world->bridges();
    if (br.size() > 64)
        return false;
    uint64_t ways = 0;
    for (size_t i = 0; i < br.size(); i++)
        ways |= (uint64_t)(br[i].phase() ? 1 : 0) << i;
    *key = {n.place, move, bits, ways};
    return true;
}

bool Route::graphStep(const Node &n, int move, Seen *out) const
{
    if (graphWrong_)
        return false;
    for (const FallingTile &t : n.world->fallings())
        if (t.armed() || t.respawnPending())
            return false;
    const int at = guide_->exactly(n.place);
    if (at < 0)
        return false;
    const MoveGraph::Edge *one = nullptr;
    for (const MoveGraph::Edge &e : guide_->edges(at))
        if (e.move == move) {
            if (one || e.waits != 0 || e.breaks != 0 || e.glides != 0)
                return false;   // more than one way, or not a plain step
            one = &e;
        }
    if (!one)
        return false;
    *out = {guide_->place(one->to), one->ms};
    return true;
}

void Route::push(int id)
{
    const Node &n = nodes_[id];
    const int g = guide_->find(n.place);
    double toCell = g < 0 ? -1 : (*toCell_)[g];
    if (toCell < 0)
        toCell = NO_WAY_MS;
    const double h = goal_->estimate(n, toCell);
    open_.push({n.cost + h, n.presses, id});
}

Route::Status Route::run(Work &work)
{
    const long before = work.used;
    while (status_ == Searching && !work.out()) {
        if (expanding_ < 0) {
            if (open_.empty()) {
                status_ = NoWay;
                break;
            }
            const int at = std::get<2>(open_.top());
            open_.pop();
            if (best_[nodes_[at].key] != at) {
                nodes_[at].world.reset();   // a sooner way there was found since
                continue;
            }
            if (reached(nodes_[at])) {
                found(at);
                break;
            }
            expanding_ = at;
            move_ = 0;
        }
        // One move a turn, so the work is checked between them: the four
        // steps, a short wait, a wait till the next wait time, a drop.
        if (nodes_.size() >= maxPlaces_)
            capped_ = true;
        if (move_ <= DROP_BACK && nodes_.size() < maxPlaces_) {
            const int at = expanding_;
            const int kind = move_++;
            const Node &from = nodes_[at];
            // A still step seen before, or as the MoveGraph has it: played
            // only if it may come somewhere sooner than known.
            StepKey sk;
            const bool keyed = stillKey(from, kind, &sk);
            Seen graphSays;
            const bool graphHas = keyed && graphStep(from, kind, &graphSays);
            if (keyed) {
                auto known = stepsSeen_.find(sk);
                const Seen *seen = known != stepsSeen_.end() ? &known->second
                                   : graphHas              ? &graphSays
                                                           : nullptr;
                if (seen && still(*from.world, from.place, seen->to)) {
                    // A tick less: the same step may end a tick sooner,
                    // as the clock falls.
                    const double cost = from.cost + seen->ms - from.world->tickStep()->value +
                                        (kind == BACK ? BACK_MS : 0);
                    auto it = best_.find(Key{seen->to, 0, 0, false, {}, {}});
                    if (it != best_.end() &&
                        (nodes_[it->second].cost < cost ||
                         (nodes_[it->second].cost == cost &&
                          nodes_[it->second].presses <= from.presses + 1))) {
                        skipped_++;
                        continue;
                    }
                }
            }
            Played p;
            std::unique_ptr<World> next;
            if (from.dropAt >= 0 && from.now < from.dropAt + BLAST_OVER_MS) {
                // After the drop: a step or two clear, or the blast waited out.
                if (kind < MOVES && from.escape < ESCAPE_MOVES) {
                    next = std::make_unique<World>(*from.world);
                    p = play(*next, (Move)kind, &work);
                } else if (kind == TIMED_WAIT) {
                    next = std::make_unique<World>(*from.world);
                    p = waitUntil(*next, from.dropAt + BLAST_OVER_MS, &work);
                }
            } else if (kind < MOVES ||
                       (kind == WAIT &&
                        (gliding(*from.world) || (from.waited < shortWaits_ && foeNear(from)))) ||
                       ((kind == DROP || kind == DROP_BACK) && mayDrop(from))) {
                next = std::make_unique<World>(*from.world);
                p = play(*next, (Move)kind, &work);
            } else if (kind == TIMED_WAIT && from.waited < timedWaits_ && !gliding(*from.world)) {
                for (double t : waitTimes(*from.world, from.place.cell()))
                    if (t > from.world->now()) {
                        next = std::make_unique<World>(*from.world);
                        p = waitUntil(*next, t, &work);
                        break;
                    }
            }
            if (next && (kind == DROP || kind == DROP_BACK) && p.kind == Played::Moved)
                drops_++;
            if (next && p.kind == Played::Died && from.dropAt >= 0)
                diedAfterDrop_++;
            if (!next || p.kind != Played::Moved)
                continue;
            if (keyed && still(*from.world, from.place, placeOf(*next)) && carried(*next) == 0) {
                const Seen s{placeOf(*next), next->now() - from.now};
                stepsSeen_[sk] = s;
                if (graphHas && (!(graphSays.to == s.to) ||
                                 graphSays.ms - from.world->tickStep()->value > s.ms + 1e-6))
                    graphWrong_ = true;   // its guesses are not taken from now on
            }
            Node n;
            n.parent = at;
            n.move   = (Move)kind;
            n.ticks  = p.ticks;
            n.stepAt = p.stepAt;
            n.place  = placeOf(*next);
            // Waits in a row where the player stands: carried off by a lift
            // or a platform, it stands somewhere new.
            n.waited = (kind == WAIT || kind == TIMED_WAIT) && n.place == from.place ? from.waited + 1 : 0;
            n.now    = next->now();
            // Facing the way it goes rather than backing, when it is no
            // slower: a step back counts a little more.
            n.cost   = from.cost + (n.now - from.now) + (kind == BACK ? BACK_MS : 0);
            n.presses = from.presses + (kind == WAIT || kind == TIMED_WAIT ? 0 : 1);
            n.dropAt = kind == DROP || kind == DROP_BACK ? next->now() - (p.ticks - 1) * next->tickStep()->value
                                                         : from.dropAt;
            n.escape = from.dropAt >= 0 ? from.escape + 1 : 0;
            n.world = std::move(next);
            if (!goal_->keeps(n))
                continue;
            const Key key = keyOf(n);
            auto it = best_.find(key);
            if (it != best_.end() &&
                (nodes_[it->second].cost < n.cost ||
                 (nodes_[it->second].cost == n.cost && nodes_[it->second].presses <= n.presses)))
                continue;
            const int id = (int)nodes_.size();
            best_[key] = id;
            n.key = key;
            nodes_.push_back(std::move(n));
            places_++;
            push(id);
            continue;
        }
        if (expanding_ != 0)
            nodes_[expanding_].world.reset();
        expanding_ = -1;
    }
    spent_ += work.used - before;
    return status_;
}

std::string Route::summary() const
{
    char buf[128];
    snprintf(buf, sizeof(buf), "places %zu, work %ld%s, skipped %d", places_, spent_,
             capped_ ? " (capped)" : "", skipped_);
    std::string out = buf;
    if (graphWrong_)
        out += ", the MoveGraph wrong";
    if (drops_ > 0 || diedAfterDrop_ > 0) {
        snprintf(buf, sizeof(buf), ", drops %d, player died %d after one", drops_, diedAfterDrop_);
        out += buf;
    }
    out += goal_->summary();
    return out;
}

void Route::found(int at)
{
    status_ = Found;
    std::vector<int> chain;
    for (int n = at; n > 0; n = nodes_[n].parent)
        chain.push_back(n);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const Node &n = nodes_[*it];
        Played p;
        p.ticks  = n.ticks;
        p.stepAt = n.stepAt;
        const std::vector<unsigned> ps = pressesOf(n.move, p);
        presses_.insert(presses_.end(), ps.begin(), ps.end());
    }
    // The World before each press, played once more from the start.
    World w(*start_);
    for (unsigned p : presses_) {
        expects_.push_back(expectOf(w));
        tick(w, p);
    }
    // The search's Worlds are done with.
    nodes_.clear();
    best_.clear();
    open_ = {};
}

}  // namespace simbot
