#include "nav.h"
#include <algorithm>
#include <functional>
#include <queue>
#include "tile.h"

namespace bot {

namespace {

const double SLIDE_MS    = 150.0;   // a step along a slide (movableentity.cpp)
const double TELEPORT_MS = 500.0;   // each of the teleporter's two waits
const double STICKY_MS   = 3000.0;  // a sticky pad's hold
const double JUMP_MS     = 500.0;   // a jump pad's arc; not measured yet
const int    MAX_CHAIN   = 400;     // forced steps before a chain is taken to loop
const double LIFT_HORIZON_MS = 120000.0;  // how far ahead the lifts are followed
const double LIFT_MARGIN_MS  = 100.0;     // a lift stays parked this long past a step on or off it

bool stairs(int kind) { return kind >= TILE_STAIRS_1 && kind <= TILE_STAIRS_4; }

/* The kinds whose cells move or change under the player: the model does not
 * follow them yet, and treats them as walls. */
bool dynamic(int kind)
{
    return kind == TILE_LIFT || kind == TILE_PLATFORM_U || kind == TILE_PLATFORM_V ||
           kind == TILE_PLATFORM_TRACK || kind == TILE_BRIDGE_U || kind == TILE_BRIDGE_V;
}

}  // namespace

NavModel::NavModel(const BotView &view) : view_(&view)
{
    for (const LiftView &l : view.lifts)
        spells_[l.v * view.extentU + l.u] = parkSpells(view, l, LIFT_HORIZON_MS);
    for (const PlatformView &p : view.platforms) {
        std::vector<Park> atOrigin, atLimit;
        platformSpells(view, p, LIFT_HORIZON_MS, &atOrigin, &atLimit);
        const int lu = p.alongU ? p.limit : p.originU, lv = p.alongU ? p.originV : p.limit;
        const int a = p.originV * view.extentU + p.originU, b = lv * view.extentU + lu;
        spells_[a] = atOrigin;
        spells_[b] = atLimit;
        partner_[a] = b;
        partner_[b] = a;
    }
}

bool NavModel::moving(const Cell &c)
{
    return c.kind == TILE_LIFT || c.track;
}

bool NavModel::ride(int u, int v, double atMs, int *pu, int *pv, double *arriveMs) const
{
    const int here = v * view_->extentU + u;
    const auto it = partner_.find(here);
    if (it == partner_.end())
        return false;
    const std::vector<Park> &there = spells_.at(it->second);
    for (const Park &p : there)
        if (p.from >= atMs) {
            *pu = it->second % view_->extentU;
            *pv = it->second / view_->extentU;
            *arriveMs = p.from;
            return true;
        }
    return false;
}

int NavModel::liftHeight(int u, int v, double atMs, double forMs) const
{
    const auto it = spells_.find(v * view_->extentU + u);
    if (it == spells_.end())
        return -1;
    for (const Park &p : it->second)
        if (p.from <= atMs + 1e-6 && atMs + forMs <= p.to + 1e-6)
            return p.height;
    return -1;
}

void NavModel::parkStarts(int u, int v, double atMs, std::vector<double> *out) const
{
    const auto it = spells_.find(v * view_->extentU + u);
    if (it == spells_.end())
        return;
    for (const Park &p : it->second)
        if (p.from > atMs)
            out->push_back(p.from);
}

const char *NavModel::refused(const Cell &a, int h, const Cell &b, int d) const
{
    if (b.kind == TILE_IMPASSABLE)
        return "wall";
    if (b.kind == TILE_BOMBABLE && !b.busy)
        return "block";
    if (dynamic(b.kind))
        return "moving";
    const int hb = b.height;
    if (stairs(a.kind)) {
        const int axis = a.kind - 4;
        if (d != axis && d != opposite(axis) && hb == h + 1)
            return "climb";
    } else if (!stairs(b.kind) && hb == h + 1) {
        return "climb";
    }
    return nullptr;
}

unsigned long long navHash(const BotView &view)
{
    unsigned long long h = 1469598103934665603ull;
    auto mix = [&h](unsigned x) { h = (h ^ x) * 1099511628211ull; };
    for (const Cell &c : view.cells) {
        // A lift's height is the clock's (liftmodel.h): what stands is its two.
        // The cells that carry the lifts and platforms: what stands is that
        // they do.
        const bool moves = NavModel::moving(c);
        mix(moves ? 0 : c.height);
        mix(moves ? 0xffff : c.kind);
        mix(c.busy);
        mix(c.slideDir);
        mix(c.liftBottom);
        mix(c.liftTop);
    }
    // Where the platforms run.
    for (const PlatformView &p : view.platforms) {
        mix(p.originU);
        mix(p.originV);
        mix(p.limit);
        mix(p.alongU);
    }
    mix((unsigned)view.player.stepMs);
    return h;
}

Outcome NavModel::step(int u, int v, int f, int d) const
{
    return stepImpl(u, v, f, d, false, 0);
}

Outcome NavModel::stepAt(int u, int v, int f, int d, double atMs) const
{
    return stepImpl(u, v, f, d, true, atMs);
}

Outcome NavModel::stepWhenOpen(int u, int v, int f, int d, double atMs, double *startMs) const
{
    *startMs = atMs;
    Outcome first = stepAt(u, v, f, d, atMs);
    if (first.kind == Outcome::Rest || spells_.empty())
        return first;
    std::vector<double> starts;
    parkStarts(u, v, atMs, &starts);
    if (d >= DIR_MIN && d <= DIR_MAX)
        parkStarts(u + DIR_DU[d], v + DIR_DV[d], atMs, &starts);
    std::sort(starts.begin(), starts.end());
    for (const double t : starts) {
        Outcome o = stepAt(u, v, f, d, t);
        if (o.kind == Outcome::Rest) {
            *startMs = t;
            return o;
        }
    }
    return first;
}

Outcome NavModel::stepImpl(int u, int v, int f, int d, bool timed, double atMs) const
{
    const BotView &w = *view_;
    Outcome out;
    out.facing = f;
    // A lift is open only at the height it is parked at, when timed.
    const double through = w.player.stepMs + LIFT_MARGIN_MS;
    Cell origin = w.cell(u, v);
    if (timed && moving(origin)) {
        const int h = liftHeight(u, v, atMs, through);
        if (h < 0) {
            out.why = "moving";
            return out;
        }
        origin.kind   = TILE_KIND_01;
        origin.height = (unsigned char)h;
    }
    int    cu = u, cv = v, ch = origin.height;
    int    dir = d;
    bool   sliding = false;
    double t = 0;

    for (int n = 0; n < MAX_CHAIN; n++) {
        const int nu = cu + DIR_DU[dir], nv = cv + DIR_DV[dir];
        Cell target;
        const Cell *bp = w.inside(nu, nv) ? &w.cell(nu, nv) : nullptr;
        if (bp && timed && n == 0 && moving(*bp)) {
            const int h = liftHeight(nu, nv, atMs, through);
            // Stepping down onto it starts a fall, and it rides away under
            // the fall: it is boarded level.
            if (h != ch) {
                out.why = "moving";
                return out;
            }
            if (h >= 0) {
                target = *bp;
                target.kind   = TILE_KIND_01;
                target.height = (unsigned char)h;
                bp = &target;
            }
        }
        const char *why = bp ? refused(n == 0 ? origin : w.cell(cu, cv), ch, *bp, dir) : "edge";
        if (why) {
            if (n == 0) {
                out.why = why;
                return out;  // Blocked
            }
            break;  // a forced move stops against it
        }
        const Cell &b = *bp;
        t += sliding ? SLIDE_MS : w.player.stepMs;
        out.cells.push_back({nu, nv});
        if (stairs(b.kind) && dir != b.kind - 4 && dir != opposite(b.kind - 4) && b.height > ch) {
            // A raised stair is a block two cells tall with open space under
            // it: stepping into its cell across it from below walks under the
            // block, into that space, and the player falls.  Level with it or
            // from above, it lands on the stair.
            out.kind = Outcome::Death;
            out.why  = "under stairs";
            out.ms   = t;
            return out;
        }
        if (b.height > ch + 1 && !stairs(b.kind)) {
            // The game takes a step at a face two or more high, and the
            // player falls down it.
            out.kind = Outcome::Death;
            out.why  = "cliff";
            out.ms   = t;
            return out;
        }
        if (b.kind == TILE_EMPTY || b.height == 0) {
            out.kind = Outcome::Death;
            out.why  = "void";
            out.ms   = t;
            return out;
        }
        if (b.height < ch && b.kind != TILE_JUMP_PAD && (ch - b.height >= 3 || b.height <= 1)) {
            out.kind = Outcome::Death;
            out.why  = "drop";
            out.ms   = t;
            return out;
        }
        cu = nu;
        cv = nv;
        ch = b.height;
        sliding = false;

        if (b.kind == TILE_SLIDE) {
            dir = b.slideDir;
            out.facing = dir;
            sliding = true;
        } else if (b.kind == TILE_ICE) {
            // on, the way the step went
        } else if (b.kind == TILE_TELEPORTER && w.inside(b.teleU, b.teleV)) {
            t += 2 * TELEPORT_MS;
            cu = b.teleU;
            cv = b.teleV;
            ch = w.cell(cu, cv).height;
            out.cells.push_back({cu, cv});
        } else if (b.kind == TILE_JUMP_PAD) {
            t += JUMP_MS;
            ch = b.jumpTop;
        } else {
            if (b.kind == TILE_STICKY && !b.busy)
                t += STICKY_MS;
            break;
        }
    }
    out.kind = Outcome::Rest;
    out.u  = cu;
    out.v  = cv;
    out.ms = t;
    return out;
}

Outcome NavModel::turn(int u, int v, int f, int delta) const
{
    Outcome out;
    out.kind   = Outcome::Rest;
    out.u      = u;
    out.v      = v;
    out.facing = turned(f, delta);
    out.ms     = view_->player.stepMs;
    out.cells.push_back({u, v});
    return out;
}

Paths::Paths(const NavModel &nav, int u, int v, int facing, double startMs)
    : nav_(nav), eu_(nav.view().extentU), ev_(nav.view().extentV), startU_(u), startV_(v),
      startMs_(startMs)
{
    const size_t n = (size_t)eu_ * ev_ * 4;
    cost_.assign(n, -1.0);
    parent_.assign(n, -1);
    how_.assign(n, -1);
    departs_.assign(n, 0.0);

    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const int s = index(u, v, facing);
    cost_[s] = startMs;
    departs_[s] = startMs;
    open.push({startMs, s});
    std::vector<char> done(n, 0);
    const bool timed = nav.hasLifts();
    while (!open.empty()) {
        const auto [c, i] = open.top();
        open.pop();
        if (done[i])
            continue;
        done[i] = 1;
        const int f  = i % 4 + 1;
        const int cu = (i / 4) % eu_, cv = (i / 4) / eu_;
        for (int how = 0; how < 4; how++) {
            double start = c;
            Outcome o;
            if (how >= 2)
                o = nav.turn(cu, cv, f, how == 2 ? +1 : -1);
            else if (timed)
                o = nav.stepWhenOpen(cu, cv, f, how == 0 ? f : NavModel::opposite(f), c, &start);
            else
                o = nav.step(cu, cv, f, how == 0 ? f : NavModel::opposite(f));
            if (o.kind != Outcome::Rest)
                continue;
            const int j = index(o.u, o.v, o.facing);
            const double nc = start + o.ms;
            if (cost_[j] < 0 || nc < cost_[j]) {
                cost_[j]   = nc;
                parent_[j] = i;
                how_[j]    = (signed char)how;
                departs_[j] = start;
                open.push({nc, j});
            }
        }
        // Riding a platform to its other end.
        int pu, pv;
        double arrive;
        if (timed && nav.ride(cu, cv, c, &pu, &pv, &arrive)) {
            const int j = index(pu, pv, f);
            if (cost_[j] < 0 || arrive < cost_[j]) {
                cost_[j]   = arrive;
                parent_[j] = i;
                how_[j]    = 4;
                departs_[j] = c;
                open.push({arrive, j});
            }
        }
    }
}

double Paths::cost(int u, int v) const
{
    double best = -1;
    for (int f = 1; f <= 4; f++) {
        const double c = cost_[index(u, v, f)];
        if (c >= 0 && (best < 0 || c < best))
            best = c;
    }
    return best < 0 ? -1 : best - startMs_;
}

int Paths::bestState(int u, int v) const
{
    int best = -1;
    for (int f = 1; f <= 4; f++) {
        const int i = index(u, v, f);
        if (cost_[i] >= 0 && (best < 0 || cost_[i] < cost_[best]))
            best = i;
    }
    return best;
}

int Paths::facingAt(int u, int v) const
{
    const int i = bestState(u, v);
    return i < 0 ? 0 : i % 4 + 1;
}

std::vector<int> Paths::chainTo(int u, int v) const
{
    std::vector<int> chain;
    for (int i = bestState(u, v); i >= 0; i = parent_[i])
        chain.push_back(i);
    return chain;   // the target first, the start last
}

std::vector<Step> Paths::cellsTo(int u, int v) const
{
    const std::vector<int> chain = chainTo(u, v);
    if (chain.empty())
        return {};
    std::vector<Step> cells{{startU_, startV_}};
    for (size_t k = chain.size() - 1; k-- > 0;) {
        const int from = chain[k + 1], to = chain[k];
        const int how = how_[to];
        if (how == 4) {
            cells.push_back({(to / 4) % eu_, (to / 4) / eu_});   // carried to the other end
            continue;
        }
        if (how >= 2)
            continue;  // a turn passes no cell
        const int f  = from % 4 + 1;
        const int fu = (from / 4) % eu_, fv = (from / 4) / eu_;
        const Outcome o = nav_.stepAt(fu, fv, f, how == 0 ? f : NavModel::opposite(f), departs_[to]);
        cells.insert(cells.end(), o.cells.begin(), o.cells.end());
    }
    return cells;
}

std::vector<Leg> Paths::legsTo(int u, int v) const
{
    const std::vector<int> chain = chainTo(u, v);
    if (chain.empty())
        return {};
    std::vector<Leg> legs(1);
    legs[0].cells.push_back({startU_, startV_});
    for (size_t k = chain.size() - 1; k-- > 0;) {
        const int from = chain[k + 1], to = chain[k];
        const int how = how_[to];
        if (how == 4) {
            // Carried to the other end: wait there for the platform.
            Leg leg;
            leg.waitUntil = cost_[to];
            leg.cells.push_back({(to / 4) % eu_, (to / 4) / eu_});
            legs.push_back(leg);
            continue;
        }
        if (how >= 2)
            continue;  // a turn passes no cell
        const int f  = from % 4 + 1;
        const int fu = (from / 4) % eu_, fv = (from / 4) / eu_;
        const int dir = how == 0 ? f : NavModel::opposite(f);
        // A wait before the move: it set off later than it could.
        if (departs_[to] > cost_[from] + 1e-6) {
            Leg leg;
            leg.waitUntil = departs_[to];
            legs.push_back(leg);
        }
        const Outcome o = nav_.stepAt(fu, fv, f, dir, departs_[to]);
        legs.back().cells.insert(legs.back().cells.end(), o.cells.begin(), o.cells.end());
    }
    return legs;
}

std::vector<Timed> Paths::scheduleTo(int u, int v) const
{
    const std::vector<int> chain = chainTo(u, v);
    if (chain.empty())
        return {};
    std::vector<Timed> out{{0, {startU_, startV_}}};
    for (size_t k = chain.size() - 1; k-- > 0;) {
        const int from = chain[k + 1], to = chain[k];
        const int how = how_[to];
        if (how == 4) {
            out.push_back({cost_[to] - startMs_, {(to / 4) % eu_, (to / 4) / eu_}});
            continue;
        }
        if (how >= 2)
            continue;  // a turn enters no cell
        const int f  = from % 4 + 1;
        const int fu = (from / 4) % eu_, fv = (from / 4) / eu_;
        const Outcome o = nav_.stepAt(fu, fv, f, how == 0 ? f : NavModel::opposite(f), departs_[to]);
        const double each = o.cells.empty() ? 0 : o.ms / (double)o.cells.size();
        for (size_t c = 0; c < o.cells.size(); c++)
            out.push_back({departs_[to] - startMs_ + each * (double)c, o.cells[c]});
    }
    return out;
}

}  // namespace bot
