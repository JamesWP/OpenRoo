#include "movegraph.h"
#include <algorithm>
#include <deque>
#include <memory>
#include <queue>
#include <set>
#include "gliding.h"
#include "schedule.h"
#include "tile.h"
#include "whatif.h"
#include "world.h"

namespace simbot {

namespace {

/* Ticks a dropped bomb is followed for, at most, till it blows. */
const int BLAST_TICKS = 400;
/* The glider charges the player is given at each place, when the level has
 * any: enough for any one move. */
const int GRAPH_GLIDES = 9;
/* The drifts a landing by the rules may want, played, to come down. */
const int LAND_DRIFTS = 2;
/* The whole-graph tables kept at once (times, timesTo). */
const size_t KEPT_TABLES = 16;

}  // namespace

uint64_t onceDown(const World &w)
{
    uint64_t bits = 0;
    int i = 0;
    for (const FallingTile &f : w.fallings()) {
        if (i < 64 && f.noRespawn() &&
            (f.armed() || w.map()->tile(f.cellU(), f.cellV())->objectMarker() != TILE_FALLING))
            bits |= (uint64_t)1 << i;
        i++;
    }
    return bits;
}

MoveGraph::Blast MoveGraph::blastOf(const World &at, Move drop)
{
    World w(at);
    whatif::giveBomb(w);
    const size_t before = w.bombs().size();
    tick(w, pressOf(drop));
    played_++;
    if (w.bombs().size() <= before)
        return {};
    const Bomb &b = w.bombs()[before];
    const double fuse = b.droppedAt() + Bomb::FUSE_MS;
    for (int i = 0; i < BLAST_TICKS && w.now() < fuse; i++) {
        tick(w, 0);
        if (w.bombs().size() <= before)
            return {};
    }
    const Bomb &blown = w.bombs()[before];
    return {{blown.cellU(), blown.cellV()}, blown.heightCell(), true};
}

MoveGraph::MoveGraph(const World &start, bool blasts, bool glides)
{
    // Each ground place's World, until its moves are played.
    std::deque<std::pair<int, std::unique_ptr<World>>> open;
    auto add = [&](const Place &p) {
        auto it = index_.find(p);
        if (it != index_.end())
            return -1;
        const int i = (int)places_.size();
        places_.push_back(p);
        edges_.emplace_back();
        index_[p] = i;
        if (!p.air)
            byCell_[p.cell()].push_back(i);
        return i;
    };

    // The air: a place for each cell, facing and height cell gliding, kept
    // at the highest the player comes to it (it may drift lower from
    // there), its moves worked out by the rules (gliding.h), highest first.
    // Where each glide began, played: what its moves take, and the World
    // its landings are played from.
    struct Start {
        std::unique_ptr<World> world;
        GlideCosts costs;
    };
    std::vector<Start> starts;
    struct InAir {
        float  y;
        int    start;
        int    parent;   // the air place it is come to from, -1: its start
        Move   move;
    };
    std::map<int, InAir> air;
    std::priority_queue<std::pair<float, int>> rising;   // highest first
    // Where each landing by the rules truly ends: -1 nowhere.
    std::map<Place, int> landed;

    // To the air place the player glides on, from place from.
    auto takeOff = [&](int from, std::unique_ptr<World> next, Edge e) {
        const Air a = airOf(*next);
        const Place p{a.u, a.v, a.heightCell(), a.facing, true};
        int i = add(p);
        if (i < 0)
            i = index_[p];
        e.to = i;
        edges_[from].push_back(e);
        auto it = air.find(i);
        if (it != air.end() && it->second.y >= a.y)
            return;
        const GlideCosts costs = glideCosts(*next);
        played_ += WAIT + 1;
        starts.push_back({std::move(next), costs});
        air[i] = {a.y, (int)starts.size() - 1, -1, WAIT};
        edges_[i].clear();
        rising.push({a.y, i});
    };
    // The ground place a landing by the rules from air place at, by move m,
    // truly ends on: played from where the glide began.
    auto land = [&](int at, Move m, const Place &by) {
        auto it = landed.find(by);
        if (it != landed.end())
            return it->second;
        std::vector<Move> moves{m};
        for (int n = at; air.at(n).parent >= 0; n = air.at(n).parent)
            moves.push_back(air.at(n).move);
        std::reverse(moves.begin(), moves.end());
        auto real = std::make_unique<World>(*starts[air.at(at).start].world);
        bool ok = true;
        for (size_t k = 0; ok && k < moves.size(); k++) {
            played_++;
            ok = play(*real, moves[k]).kind == Played::Moved;
        }
        // Come down by the rules a little short: drift onto it.
        for (int d = 0; ok && d < LAND_DRIFTS && gliding(*real); d++) {
            played_++;
            ok = play(*real, WAIT).kind == Played::Moved;
        }
        int i = -1;
        if (ok && !gliding(*real)) {
            const Place to = placeOf(*real);
            i = add(to);
            if (i >= 0)
                open.push_back({i, std::move(real)});
            else
                i = index_[to];
        }
        landed[by] = i;
        return i;
    };
    // Air place at's moves.
    auto glideFrom = [&](int at) {
        const InAir from = air.at(at);
        const GlideCosts &costs = starts[from.start].costs;
        Air a0;
        a0.u = places_[at].u;
        a0.v = places_[at].v;
        a0.facing = places_[at].facing;
        a0.y = from.y;
        std::set<int> reached{at};
        for (int m = 0; m <= WAIT; m++) {
            Air a = a0;
            const Glided g = glide(*start.map(), a, (Move)m, costs);
            glided_++;
            int i = -1;
            if (g == Glided::Landed) {
                i = land(at, (Move)m, {a.u, a.v, (int)a.y, a.facing, false});
            } else if (g == Glided::Flying) {
                const Place p{a.u, a.v, a.heightCell(), a.facing, true};
                i = add(p);
                if (i < 0)
                    i = index_[p];
                auto it = air.find(i);
                if (it == air.end() || it->second.y < a.y) {
                    air[i] = {a.y, from.start, at, (Move)m};
                    edges_[i].clear();
                    rising.push({a.y, i});
                }
            }
            if (i >= 0 && reached.insert(i).second)
                edges_[at].push_back({i, 0, (Move)m, costs.ms[m], 0, 0});
        }
    };

    auto first = std::make_unique<World>(start);
    whatif::removeFoes(*first);
    open.push_back({add(placeOf(*first)), std::move(first)});
    while (!open.empty() || !rising.empty()) {
        if (open.empty()) {
            const auto [y, at] = rising.top();
            rising.pop();
            if (air.at(at).y == y && edges_[at].empty())
                glideFrom(at);
            continue;
        }
        auto [at, world] = std::move(open.front());
        open.pop_front();
        whatif::restoreFallings(*world);
        if (glides)
            whatif::giveGlides(*world, GRAPH_GLIDES);
        const uint64_t down0 = onceDown(*world);
        const double t0 = world->now();
        if (blasts) {
            blasts_.resize(places_.size());
            for (int k = 0; k < 2; k++)
                blasts_[at][k] = blastOf(*world, k ? DROP_BACK : DROP);
        }
        std::set<int> reached{at};
        // An edge to where next is, if that place is not reached from at yet;
        // whether the place is new (to be played from in its turn).
        auto reach = [&](std::unique_ptr<World> next, int w, Move m) {
            const uint64_t breaks = onceDown(*next) & ~down0;
            // A glider picked up on the way counts against it: none spent.
            const int used = std::max(0, GRAPH_GLIDES - (int)next->player()->glides());
            const Edge e{-1, w, m, next->now() - t0, breaks, glides ? used : 0};
            if (gliding(*next)) {
                takeOff(at, std::move(next), e);
                return false;
            }
            const Place to = placeOf(*next);
            int i = add(to);
            const bool fresh = i >= 0;
            if (!fresh)
                i = index_[to];
            if (!reached.insert(i).second)
                return false;
            breaking_ += breaks != 0;
            edges_[at].push_back(e);
            edges_[at].back().to = i;
            if (fresh)
                open.push_back({i, std::move(next)});
            return fresh;
        };
        // waited: at's World after w waits, each till the next wait time.
        auto waited = std::make_unique<World>(*world);
        const std::vector<double> times = waitTimes(*world, places_[at].cell());
        for (int w = 0; w <= (int)times.size(); w++) {
            if (w > 0) {
                if (times[w - 1] <= waited->now())
                    continue;   // passed while the last one settled
                played_++;
                if (waitUntil(*waited, times[w - 1]).kind != Played::Moved)
                    break;
                const bool fresh = reach(std::make_unique<World>(*waited), w, WAIT);
                // Carried off by a lift or a platform: a new place is played
                // from in its own turn, and the waits go no further; one known
                // (come to at another time, the lift going the other way,
                // say) is ridden on through, its moves its own.
                if (!(placeOf(*waited) == places_[at])) {
                    if (fresh)
                        break;
                    continue;
                }
            }
            for (int m = 0; m < MOVES; m++) {
                auto next = std::make_unique<World>(*waited);
                const Played p = play(*next, (Move)m);
                played_++;
                if (p.kind == Played::Moved)
                    reach(std::move(next), w, (Move)m);
            }
        }
    }
    if (blasts)
        blasts_.resize(places_.size());
    int grounds = 0;
    for (const Place &p : places_)
        groundOf_.push_back(p.air ? -1 : grounds++);
}

int MoveGraph::find(const Place &p) const
{
    auto it = index_.find(p);
    if (it != index_.end())
        return it->second;
    const std::vector<int> &here = on(p.cell());
    return here.empty() ? -1 : here.front();
}

const std::vector<int> &MoveGraph::on(Cell c) const
{
    static const std::vector<int> none;
    auto it = byCell_.find(c);
    return it == byCell_.end() ? none : it->second;
}

MoveGraph::Table MoveGraph::times(int from) const
{
    auto it = times_.find(from);
    if (it != times_.end())
        return it->second;
    if (times_.size() >= KEPT_TABLES)
        times_.clear();
    auto table = std::make_shared<std::vector<double>>();
    std::vector<double> &t = *table;
    t.assign(places_.size(), -1);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    t[from] = 0;
    open.push({0, from});
    while (!open.empty()) {
        const auto [ms, at] = open.top();
        open.pop();
        if (ms > t[at])
            continue;
        for (const Edge &e : edges_[at]) {
            const double next = ms + e.ms;
            if (t[e.to] < 0 || next < t[e.to]) {
                t[e.to] = next;
                open.push({next, e.to});
            }
        }
    }
    times_[from] = table;
    return table;
}

void MoveGraph::backward(Cell c, bool glide, std::vector<double> &t, std::vector<int> *gl) const
{
    if (into_.empty()) {
        // The edges into each place, once.
        into_.resize(places_.size());
        for (size_t i = 0; i < places_.size(); i++)
            for (const Edge &e : edges_[i])
                into_[e.to].push_back({(int)i, &e});
    }
    t.assign(places_.size(), -1);
    if (gl)
        gl->assign(places_.size(), 0);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    for (int i : on(c)) {
        t[i] = 0;
        open.push({0, i});
    }
    while (!open.empty()) {
        const auto [ms, at] = open.top();
        open.pop();
        if (ms > t[at])
            continue;
        for (const auto &[from, e] : into_[at]) {
            if (!glide && e->glides > 0)
                continue;
            const double next = ms + e->ms;
            if (t[from] < 0 || next < t[from]) {
                t[from] = next;
                if (gl)
                    (*gl)[from] = (*gl)[at] + e->glides;
                open.push({next, from});
            }
        }
    }
}

MoveGraph::Table MoveGraph::timesTo(Cell c) const
{
    auto it = timesTo_.find(c);
    if (it != timesTo_.end())
        return it->second;
    if (timesTo_.size() >= KEPT_TABLES)
        timesTo_.clear();
    auto t = std::make_shared<std::vector<double>>();
    backward(c, true, *t, nullptr);
    timesTo_[c] = t;
    return t;
}

const MoveGraph::Toward &MoveGraph::toward(Cell c) const
{
    auto it = toward_.find(c);
    if (it != toward_.end())
        return it->second;
    std::vector<double> walk, any;
    std::vector<int> gl;
    backward(c, false, walk, nullptr);
    backward(c, true, any, &gl);
    Toward &out = toward_[c];
    for (size_t p = 0; p < places_.size(); p++) {
        if (groundOf_[p] < 0)
            continue;
        out.walk.push_back((float)walk[p]);
        out.any.push_back((float)any[p]);
        out.glides.push_back((uint8_t)std::min(gl[p], 255));
    }
    return out;
}

double MoveGraph::timeTo(int at, Cell c, int glides, int *used) const
{
    if (used)
        *used = 0;
    if (at < 0 || groundOf_[at] < 0)
        return -1;
    const Toward &t = toward(c);
    const int g = groundOf_[at];
    if (glides > 0 && t.any[g] >= 0 && t.glides[g] <= glides) {
        if (used)
            *used = t.glides[g];
        return t.any[g];
    }
    return t.walk[g];
}

double MoveGraph::timeBeside(int at, Cell c) const
{
    double best = -1;
    for (const Cell &d : {Cell{0, 0}, Cell{1, 0}, Cell{-1, 0}, Cell{0, 1}, Cell{0, -1}}) {
        const double t = timeTo(at, {c.u + d.u, c.v + d.v}, 0);
        if (t >= 0 && (best < 0 || t < best))
            best = t;
    }
    return best;
}

int MoveGraph::arrive(int from, Cell c, double *ms, int glides, int *used) const
{
    *ms = timeTo(from, c, glides, used);
    return *ms < 0 || on(c).empty() ? -1 : on(c).front();
}

std::vector<int> MoveGraph::path(int from, const std::function<bool(int)> &goal,
                                 const std::function<bool(int)> &blocked) const
{
    std::vector<double> t(places_.size(), -1);
    std::vector<int> parent(places_.size(), -1);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    t[from] = 0;
    open.push({0, from});
    while (!open.empty()) {
        const auto [ms, at] = open.top();
        open.pop();
        if (ms > t[at])
            continue;
        if (goal(at)) {
            std::vector<int> out;
            for (int p = at; p >= 0; p = parent[p])
                out.push_back(p);
            return {out.rbegin(), out.rend()};
        }
        for (const Edge &e : edges_[at]) {
            const double next = ms + e.ms;
            if (blocked(e.to) || (t[e.to] >= 0 && next >= t[e.to]))
                continue;
            t[e.to] = next;
            parent[e.to] = at;
            open.push({next, e.to});
        }
    }
    return {};
}

void MoveGraph::findRegions() const
{
    if (!region_.empty())
        return;
    // Tarjan's, without recursion: regions are numbered sinks first.
    const int n = (int)places_.size();
    region_.assign(n, -1);
    std::vector<int> index(n, -1), low(n, 0), stack;
    std::vector<char> onStack(n, 0);
    std::vector<std::pair<int, size_t>> call;   // place, next edge
    int next = 0, regions = 0;
    for (int root = 0; root < n; root++) {
        if (index[root] >= 0)
            continue;
        call.push_back({root, 0});
        index[root] = low[root] = next++;
        stack.push_back(root);
        onStack[root] = 1;
        while (!call.empty()) {
            auto &[v, e] = call.back();
            if (e < edges_[v].size()) {
                const int w = edges_[v][e++].to;
                if (index[w] < 0) {
                    index[w] = low[w] = next++;
                    stack.push_back(w);
                    onStack[w] = 1;
                    call.push_back({w, 0});
                } else if (onStack[w]) {
                    low[v] = std::min(low[v], index[w]);
                }
                continue;
            }
            const int done = v;
            call.pop_back();
            if (!call.empty())
                low[call.back().first] = std::min(low[call.back().first], low[done]);
            if (low[done] == index[done]) {
                int w;
                do {
                    w = stack.back();
                    stack.pop_back();
                    onStack[w] = 0;
                    region_[w] = regions;
                } while (w != done);
                regions++;
            }
        }
    }
    // The regions with ground places, and what each reaches, sinks first.
    ground_.assign(regions, -1);
    int grounds = 0;
    for (int p = 0; p < n; p++)
        if (!places_[p].air && ground_[region_[p]] < 0)
            ground_[region_[p]] = grounds++;
    const size_t words = (grounds + 63) / 64;
    std::vector<std::vector<int>> members(regions);
    for (int p = 0; p < n; p++)
        members[region_[p]].push_back(p);
    std::vector<std::vector<uint64_t>> all(regions, std::vector<uint64_t>(words, 0));
    for (int r = 0; r < regions; r++) {
        if (ground_[r] >= 0)
            all[r][ground_[r] / 64] |= (uint64_t)1 << (ground_[r] % 64);
        for (int p : members[r])
            for (const Edge &e : edges_[p]) {
                const int d = region_[e.to];
                if (d != r)
                    for (size_t k = 0; k < words; k++)
                        all[r][k] |= all[d][k];
            }
    }
    reaches_.assign(grounds, {});
    for (int r = 0; r < regions; r++)
        if (ground_[r] >= 0)
            reaches_[ground_[r]] = std::move(all[r]);
}

int MoveGraph::region(int place) const
{
    findRegions();
    return place < 0 ? -1 : region_[place];
}

int MoveGraph::regions() const
{
    findRegions();
    return (int)ground_.size();
}

bool MoveGraph::regionReaches(int a, int b) const
{
    findRegions();
    if (a < 0 || b < 0 || ground_[a] < 0 || ground_[b] < 0)
        return false;
    const int gb = ground_[b];
    return reaches_[ground_[a]][gb / 64] >> (gb % 64) & 1;
}

}  // namespace simbot
