#include "routing.h"
#include <stdio.h>
#include <stdlib.h>
#include "schedule.h"
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
/* From the drop till the blast is gone, and a tick. */
const double BLAST_OVER_MS = Bomb::CLEAR_MS + 20;
/* nearFoe: the ms a foe may walk, on the MoveGraph, to come beside where
 * the bomb blows: its fuse. */
const double REACH_MS = Bomb::FUSE_MS;
/* foeMoving: a step ended this recently. */
const double MOVING_MS = 500;
/* How long the player must outlive, standing, at a goal (a foe may be on
 * its way): the next step is thought out meanwhile. */
const double SAFE_MS = 1000;
/* The moves made after a drop, to get clear of the blast, before it is
 * waited out where the player stands. */
const int ESCAPE_MOVES = 2;

}  // namespace

bool foeMoving(const World &w, const Foe &f)
{
    return f.moveDir() != 0 || w.now() - f.stepEnd() < MOVING_MS;
}

bool nearFoe(const MoveGraph &g, const Place &place, Cell foe, int foeH, bool moving)
{
    const MoveGraph::Blast *b = g.blasts(g.find(place));
    if (!b)
        return false;
    const int foeAt = g.find({foe.u, foe.v, foeH, 0});
    for (int k = 0; k < 2; k++) {
        if (!b[k].ok)
            continue;
        if (abs(b[k].cell.u - foe.u) <= 1 && abs(b[k].cell.v - foe.v) <= 1 &&
            abs(b[k].h - foeH) <= 1)
            return true;
        if (!moving)
            continue;
        const double t = g.timeTo(foeAt, b[k].cell);
        if (t >= 0 && t <= REACH_MS)
            return true;
    }
    return false;
}

Route::Route(const World &start, Goal goal, const MoveGraph &guide, size_t maxPlaces,
             int shortWaits, int timedWaits)
    : goal_(goal), guide_(&guide), toCell_(&guide.timesTo(goal.cell)), maxPlaces_(maxPlaces),
      shortWaits_(shortWaits), timedWaits_(timedWaits), start_(std::make_unique<World>(start))
{
    Node root;
    root.now   = start.now();
    root.place = placeOf(start);
    root.world = std::make_unique<World>(start);
    nodes_.push_back(std::move(root));
    nodes_[0].key = keyOf(nodes_[0]);
    best_[nodes_[0].key] = 0;
    push(0);
}

Route::~Route() = default;

Route::Key Route::keyOf(const Node &n) const
{
    if (n.dropAt < 0)
        return {n.place, n.waited, false, {}, {}};
    Cell foe{-1, -1}, bomb{-1, -1};
    if (const Foe *f = findFoe(*n.world, goal_.foe))
        foe = {f->cellU(), f->cellV()};
    if (!n.world->bombs().empty()) {
        const Bomb &b = n.world->bombs()[n.world->bombs().size() - 1];
        bomb = {b.cellU(), b.cellV()};
    }
    return {n.place, n.waited, true, foe, bomb};
}

bool Route::safe(const Node &n) const
{
    if (n.world->foes().empty())
        return true;
    World w(*n.world);
    const double until = w.now() + SAFE_MS;
    while (w.now() < until) {
        tick(w, 0);
        if (dead(w))
            return false;
    }
    return true;
}

bool Route::reached(const Node &n) const
{
    if (!goal_.kill) {
        if (goal_.track) {
            const Foe *f = findFoe(*n.world, goal_.foe);
            if (!f)
                return true;   // gone: nothing left to come near
            return nearFoe(*guide_, n.place, {f->cellU(), f->cellV()}, f->heightCell(),
                           foeMoving(*n.world, *f)) &&
                   safe(n);
        }
        return n.place.cell() == goal_.cell && safe(n);
    }
    return n.dropAt >= 0 && n.now >= n.dropAt + BLAST_OVER_MS && foeDead(*n.world, goal_.foe) &&
           safe(n);
}

bool Route::mayDrop(const Node &n) const
{
    if (!goal_.kill || n.dropAt >= 0 || n.world->player()->bombsCarried() == 0)
        return false;
    const Foe *f = findFoe(*n.world, goal_.foe);
    return f && nearFoe(*guide_, n.place, {f->cellU(), f->cellV()}, f->heightCell(),
                           foeMoving(*n.world, *f));
}

void Route::push(int id)
{
    const Node &n = nodes_[id];
    double h;
    if (n.dropAt >= 0) {
        h = n.dropAt + BLAST_OVER_MS - n.now;
        if (h < 0)
            h = 0;
    } else {
        const int g = guide_->find(n.place);
        h = g < 0 ? -1 : (*toCell_)[g];
        if (h < 0)
            h = NO_WAY_MS;
        if (goal_.kill)
            h += BLAST_OVER_MS;
    }
    open_.push({n.now + h, id});
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
            const int at = open_.top().second;
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
            } else if (kind < MOVES || (kind == WAIT && from.waited < shortWaits_) ||
                       ((kind == DROP || kind == DROP_BACK) && mayDrop(from))) {
                next = std::make_unique<World>(*from.world);
                p = play(*next, (Move)kind, &work);
            } else if (kind == TIMED_WAIT && from.waited < timedWaits_) {
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
            Node n;
            n.parent = at;
            n.move   = (Move)kind;
            n.ticks  = p.ticks;
            n.waited = kind == WAIT || kind == TIMED_WAIT ? from.waited + 1 : 0;
            n.now    = next->now();
            n.dropAt = kind == DROP || kind == DROP_BACK ? next->now() - (p.ticks - 1) * next->tickStep()->value
                                                         : from.dropAt;
            n.escape = from.dropAt >= 0 ? from.escape + 1 : 0;
            n.place  = placeOf(*next);
            // A drop whose blast is over with the foe alive has failed.
            if (n.dropAt >= 0 && n.now >= n.dropAt + BLAST_OVER_MS && !foeDead(*next, goal_.foe)) {
                foeLived_++;
                continue;
            }
            n.world = std::move(next);
            const Key key = keyOf(n);
            auto it = best_.find(key);
            if (it != best_.end() && nodes_[it->second].now <= n.now)
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
    snprintf(buf, sizeof(buf), "places %zu, work %ld%s", places_, spent_, capped_ ? " (capped)" : "");
    std::string out = buf;
    if (goal_.kill) {
        snprintf(buf, sizeof(buf), ", drops %d: foe lived %d, player died %d", drops_, foeLived_,
                 diedAfterDrop_);
        out += buf;
    }
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
        presses_.push_back(pressOf(n.move));
        presses_.insert(presses_.end(), n.ticks - 1, 0u);
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
