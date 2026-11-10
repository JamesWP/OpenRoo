#include "routing.h"
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

Route::Route(const World &start, Cell target, const MoveGraph &guide, size_t maxPlaces,
             int shortWaits, int timedWaits)
    : target_(target), guide_(&guide), toTarget_(&guide.timesTo(target)),
      maxPlaces_(maxPlaces), shortWaits_(shortWaits), timedWaits_(timedWaits), start_(std::make_unique<World>(start))
{
    Node root;
    root.now   = start.now();
    root.place = placeOf(start);
    root.world = std::make_unique<World>(start);
    nodes_.push_back(std::move(root));
    best_[{nodes_[0].place, 0}] = 0;
    push(0);
}

Route::~Route() = default;

namespace {

/* The estimate for a place the MoveGraph has no way on from (it is only an
 * estimate): far, not never. */
const double NO_WAY_MS = 60000;
/* The move after WAIT: a wait till the schedule's next wait time
 * (schedule.h). */
const int TIMED_WAIT = WAIT + 1;

}  // namespace

void Route::push(int id)
{
    const int g = guide_->find(nodes_[id].place);
    double h = g < 0 ? -1 : (*toTarget_)[g];
    if (h < 0)
        h = NO_WAY_MS;
    open_.push({nodes_[id].now + h, id});
}

Route::Status Route::run(Work &work)
{
    while (status_ == Searching && !work.out()) {
        if (expanding_ < 0) {
            if (open_.empty()) {
                status_ = NoWay;
                break;
            }
            const int at = open_.top().second;
            open_.pop();
            if (best_[{nodes_[at].place, nodes_[at].waited}] != at) {
                nodes_[at].world.reset();   // a sooner way there was found since
                continue;
            }
            if (nodes_[at].place.cell() == target_) {
                found(at);
                break;
            }
            expanding_ = at;
            move_ = 0;
        }
        // One move a turn, so the work is checked between them: the four
        // steps, a short wait, then a wait till the next wait time.
        if (move_ <= TIMED_WAIT && nodes_.size() < maxPlaces_) {
            const int at = expanding_;
            const int kind = move_++;
            const int waited = nodes_[at].waited;
            Played p;
            std::unique_ptr<World> next;
            if (kind < MOVES || (kind == WAIT && waited < shortWaits_)) {
                next = std::make_unique<World>(*nodes_[at].world);
                p = play(*next, (Move)kind, &work);
            } else if (kind == TIMED_WAIT && waited < timedWaits_) {
                const World &from = *nodes_[at].world;
                for (double t : waitTimes(from, nodes_[at].place.cell()))
                    if (t > from.now()) {
                        next = std::make_unique<World>(from);
                        p = waitUntil(*next, t, &work);
                        break;
                    }
            }
            if (!next || p.kind != Played::Moved)
                continue;
            const Move m = kind < MOVES ? (Move)kind : WAIT;
            const Place place = placeOf(*next);
            const int waitedNow = m == WAIT ? waited + 1 : 0;
            auto it = best_.find({place, waitedNow});
            if (it != best_.end() && nodes_[it->second].now <= next->now())
                continue;
            Node n;
            n.parent = at;
            n.move   = m;
            n.ticks  = p.ticks;
            n.waited = waitedNow;
            n.now    = next->now();
            n.place  = place;
            n.world  = std::move(next);
            const int id = (int)nodes_.size();
            nodes_.push_back(std::move(n));
            best_[{place, waitedNow}] = id;
            push(id);
            continue;
        }
        if (expanding_ != 0)
            nodes_[expanding_].world.reset();
        expanding_ = -1;
    }
    return status_;
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
