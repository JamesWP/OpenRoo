#include "routing.h"
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
             int maxWaits)
    : target_(target), guide_(&guide), toTarget_(&guide.timesTo(target)),
      maxPlaces_(maxPlaces), maxWaits_(maxWaits), start_(std::make_unique<World>(start))
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
        // One move a turn, so the work is checked between them.
        const int lastMove = nodes_[expanding_].waited < maxWaits_ ? WAIT : MOVES - 1;
        if (move_ <= lastMove && nodes_.size() < maxPlaces_) {
            const int at = expanding_;
            auto next = std::make_unique<World>(*nodes_[at].world);
            const Played p = play(*next, (Move)move_, &work);
            const Move m = (Move)move_++;
            if (p.kind != Played::Moved)
                continue;
            const Place place = placeOf(*next);
            const int waited = m == WAIT ? nodes_[at].waited + 1 : 0;
            auto it = best_.find({place, waited});
            if (it != best_.end() && nodes_[it->second].now <= next->now())
                continue;
            Node n;
            n.parent = at;
            n.move   = m;
            n.ticks  = p.ticks;
            n.waited = waited;
            n.now    = next->now();
            n.place  = place;
            n.world  = std::move(next);
            const int id = (int)nodes_.size();
            nodes_.push_back(std::move(n));
            best_[{place, waited}] = id;
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
