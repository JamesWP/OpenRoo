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
/* How near the foe must be for a drop to be tried, on the MoveGraph: the
 * bomb rolls, and the foe comes on. */
const double DROP_NEAR_MS = 1200;
/* The moves made after a drop, to get clear of the blast, before it is
 * waited out where the player stands. */
const int ESCAPE_MOVES = 2;

}  // namespace

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
    best_[keyOf(nodes_[0])] = 0;
    push(0);
}

Route::~Route() = default;

bool Route::reached(const Node &n) const
{
    if (!goal_.kill) {
        Cell c = goal_.cell;
        if (goal_.track) {
            const Foe *f = findFoe(*n.world, goal_.foe);
            if (!f)
                return true;   // gone: nothing left to come near
            c = {f->cellU(), f->cellV()};
        }
        if (goal_.nearMs < 0)
            return n.place.cell() == c;
        const double t = guide_->timeTo(guide_->find(n.place), c);
        return t >= 0 && t <= goal_.nearMs;
    }
    return n.dropAt >= 0 && n.now >= n.dropAt + BLAST_OVER_MS && foeDead(*n.world, goal_.foe);
}

bool Route::mayDrop(const Node &n) const
{
    if (!goal_.kill || n.dropAt >= 0 || n.world->player()->bombsCarried() == 0)
        return false;
    const Foe *f = findFoe(*n.world, goal_.foe);
    if (!f)
        return false;
    const double t = guide_->timeTo(guide_->find(n.place), {f->cellU(), f->cellV()});
    return t >= 0 && t <= DROP_NEAR_MS;
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
            if (best_[keyOf(nodes_[at])] != at) {
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
        if (move_ <= DROP && nodes_.size() < maxPlaces_) {
            const int at = expanding_;
            const int kind = move_++;
            const Node &from = nodes_[at];
            Played p;
            std::unique_ptr<World> next;
            if (from.dropAt >= 0) {
                // After the drop: a step or two clear, or the blast waited out.
                if (kind < MOVES && from.escape < ESCAPE_MOVES) {
                    next = std::make_unique<World>(*from.world);
                    p = play(*next, (Move)kind, &work);
                } else if (kind == TIMED_WAIT) {
                    next = std::make_unique<World>(*from.world);
                    p = waitUntil(*next, from.dropAt + BLAST_OVER_MS, &work);
                }
            } else if (kind < MOVES || (kind == WAIT && from.waited < shortWaits_) ||
                       (kind == DROP && mayDrop(from))) {
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
            if (next && kind == DROP && p.kind == Played::Moved)
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
            n.dropAt = kind == DROP ? next->now() : from.dropAt;
            n.escape = from.dropAt >= 0 ? from.escape + 1 : 0;
            n.place  = placeOf(*next);
            // A drop whose blast is over with the foe alive has failed.
            if (n.dropAt >= 0 && n.now >= n.dropAt + BLAST_OVER_MS && !foeDead(*next, goal_.foe)) {
                foeLived_++;
                continue;
            }
            auto it = best_.find(keyOf(n));
            if (it != best_.end() && nodes_[it->second].now <= n.now)
                continue;
            n.world = std::move(next);
            const int id = (int)nodes_.size();
            best_[keyOf(n)] = id;
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
