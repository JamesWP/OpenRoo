#include "nav.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include "fallingmodel.h"
#include "tile.h"

namespace bot {

namespace {

const double SLIDE_MS    = 150.0;   // a step along a slide (movableentity.cpp)
const double TELEPORT_MS = 500.0;   // each of the teleporter's two waits
const double STICKY_MS   = 3000.0;  // a sticky pad's hold (movableentity.cpp)
const double ARC_BIAS    = 7.2f;    // a jump pad's launch (movableentity.cpp)
const double GRAVITY_TWO = 19.62f;
const double GRAVITY_HALF = 4.905f;
const double FALL_SPEED  = 3.0f;    // the free fall (movableentity.cpp): its start, a height a s
const double FALL_ACCEL  = 5.405f;  // and its gain, a height a s^2
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

NavModel::NavModel(const BotView &view, bool oneShots) : view_(&view)
{
    if (!oneShots) {
        const std::vector<int> runs = keptOffRuns(view);
        for (size_t i = 0; i < runs.size(); i++)
            if (runs[i] >= 0)
                oneShot_.insert((int)i);
    }
    for (const LiftView &l : view.lifts)
        spells_[l.v * view.extentU + l.u] = parkSpells(view, l, LIFT_HORIZON_MS);
    for (const FallingView &f : view.fallings)
        if (f.fallen && f.respawns)
            back_[f.v * view.extentU + f.u] = Fallen{backAt(view, {f.u, f.v}), f.h};
    for (const PlatformView &p : view.platforms) {
        std::vector<Park> atOrigin, atLimit;
        platformSpells(view, p, LIFT_HORIZON_MS, &atOrigin, &atLimit);
        const int lu = p.alongU ? p.limit : p.originU, lv = p.alongU ? p.originV : p.limit;
        const int a = p.originV * view.extentU + p.originU, b = lv * view.extentU + lu;
        spells_[a] = atOrigin;
        spells_[b] = atLimit;
        partner_[a] = b;
        partner_[b] = a;
        const int start = p.alongU ? p.originU : p.originV;
        for (int along = start + 1; along < p.limit; along++) {
            const Step at = p.alongU ? Step{along, p.originV} : Step{p.originU, along};
            for (const double t : platformPasses(view, p, along, LIFT_HORIZON_MS)) {
                alight_[a].push_back({at, t});
                alight_[b].push_back({at, t});
            }
        }
    }
}

void NavModel::arm(Step c, double atMs)
{
    const int at = c.v * view_->extentU + c.u;
    double from, back;
    if (back_.count(at) || !goneSpell(*view_, c, atMs, &from, &back))
        return;
    back_[at] = Fallen{back, view_->cell(c.u, c.v).height, from};
}

double NavModel::fallMs(int drop, double tickMs)
{
    if (drop <= 0 || tickMs <= 0)
        return 0;
    // From a height well clear of zero, so that the game's truncation of it
    // is the floor; the elapsed ms are whole, as the game takes them.
    const int from = 64, floor = from - drop;
    for (int k = 1;; k++) {
        const double ts = std::floor(k * tickMs + 1e-6) * (double)0.001f;
        const double h  = (-FALL_SPEED - ts * FALL_ACCEL) * ts + from;
        if ((int)(long long)h + 1 <= floor)
            return (k + 2) * tickMs;
    }
}

double NavModel::stickyMs(double tickMs)
{
    if (tickMs <= 0)
        return STICKY_MS;
    // Held while the clock is within the hold of the tick it caught the player.
    int k = 1;
    while (k * tickMs <= STICKY_MS)
        k++;
    return k * tickMs;
}

double NavModel::jumpMs(int rise, double tickMs)
{
    if (tickMs <= 0)
        return 0;
    const double v0 = std::sqrt((rise + (double)(float)ARC_BIAS) * GRAVITY_TWO);
    for (int k = 1;; k++) {
        const double ts = k * tickMs * (double)0.001f;
        if ((float)((v0 - ts * GRAVITY_HALF) * ts) >= (float)rise)
            return (k + 1) * tickMs;
    }
}

bool NavModel::falls(const Cell &a, const Cell &b)
{
    return b.height < a.height && !stairs(b.kind) &&
           (a.height - b.height >= 2 || !stairs(a.kind));
}

const BridgeView *switchedBy(const BotView &view, int u, int v)
{
    if (!view.inside(u, v))
        return nullptr;
    const Cell &c = view.cell(u, v);
    if (c.kind != TILE_SWITCH || c.param == 0)
        return nullptr;
    for (const BridgeView &b : view.bridges)
        if (b.slot == c.param - 1)
            return &b;
    return nullptr;
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

std::vector<NavModel::Alighting> NavModel::alightings(int u, int v, double atMs) const
{
    std::vector<Alighting> out;
    const auto it = alight_.find(v * view_->extentU + u);
    if (it == alight_.end())
        return out;
    // The first pass of each cell after atMs.
    for (const Alighting &a : it->second) {
        if (a.ms < atMs)
            continue;
        bool seen = false;
        for (Alighting &o : out)
            if (o.at.u == a.at.u && o.at.v == a.at.v) {
                o.ms = std::min(o.ms, a.ms);
                seen = true;
            }
        if (!seen)
            out.push_back(a);
    }
    return out;
}

Outcome NavModel::stepOff(int u, int v, int f, int d, double atMs) const
{
    const PlatformView *p = platformThrough(*view_, u, v);
    if (!p || (p->alongU ? DIR_DU[d] != 0 : DIR_DV[d] != 0)) {
        Outcome out;
        out.facing = f;
        out.why = "along the track";
        return out;
    }
    return stepImpl(u, v, f, d, true, atMs, p->height);
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

bool parkedFoe(const BotView &view, int u, int v)
{
    for (const FoeView &f : view.foes)
        if (f.u == u && f.v == v && f.kind == 3 && f.held && !f.moving && !f.dying)
            return true;
    return false;
}

std::vector<int> keptOffRuns(const BotView &view)
{
    // Kept by terrain: the tests walk the level.
    static std::map<unsigned long long, std::vector<int>> kept;
    if (view.fallings.empty())
        return std::vector<int>(view.cells.size(), -1);
    const unsigned long long key = navHash(view);
    const auto it = kept.find(key);
    if (it != kept.end())
        return it->second;
    if (kept.size() > 64)
        kept.clear();
    std::vector<int> runs = oneShotRuns(view);
    std::map<int, std::vector<int>> cells;
    for (size_t i = 0; i < runs.size(); i++)
        if (runs[i] >= 0)
            cells[runs[i]].push_back((int)i);
    for (const auto &[run, in] : cells) {
        // The ground beside the run.
        std::set<int> beside;
        for (const int i : in) {
            const int u = i % view.extentU, v = i / view.extentU;
            for (int d = DIR_MIN; d <= DIR_MAX; d++) {
                const int bu = u + DIR_DU[d], bv = v + DIR_DV[d];
                if (!view.inside(bu, bv) || runs[(size_t)bv * view.extentU + bu] == run)
                    continue;
                const Cell &c = view.cell(bu, bv);
                if (c.kind != TILE_EMPTY && c.kind != TILE_IMPASSABLE && c.height > 0)
                    beside.insert(bv * view.extentU + bu);
            }
        }
        // Without it, does each of them still reach every other?
        NavModel without(view, true);
        for (const int i : in)
            without.shut({i % view.extentU, i / view.extentU});
        bool bridge = false;
        for (auto a = beside.begin(); a != beside.end() && !bridge; ++a) {
            const Paths from(without, *a % view.extentU, *a / view.extentU, DIR_MIN);
            for (const int b : beside)
                bridge = bridge || (b != *a && from.cost(b % view.extentU, b / view.extentU) < 0);
        }
        if (!bridge)
            for (const int i : in)
                runs[(size_t)i] = -1;
    }
    return kept[key] = runs;
}

unsigned long long navHash(const BotView &view)
{
    unsigned long long h = 1469598103934665603ull;
    auto mix = [&h](unsigned x) { h = (h ^ x) * 1099511628211ull; };
    std::vector<char> falls(view.cells.size(), 0);
    const std::vector<int> runs = oneShotRuns(view);
    for (const FallingView &f : view.fallings) {
        const size_t i = (size_t)f.v * view.extentU + f.u;
        falls[i] = runs[i] >= 0 ? 2 : 1;
    }
    size_t at = 0;
    for (const Cell &c : view.cells) {
        // A falling tile stands or has fallen: for the model it comes back,
        // but a one-shot tile, which is kept off until it is armed.
        const char fall = falls[at++];
        if (fall) {
            mix(0xfa10 + fall);
            continue;
        }
        // The cells that carry the lifts and platforms: what stands is that
        // they do (liftmodel.h gives their heights by the clock).
        const bool moves = NavModel::moving(c);
        mix(moves ? 0 : c.height);
        mix(moves ? 0xffff : c.kind);
        mix(c.busy);
        mix(c.slideDir);
        mix(c.liftBottom);
        mix(c.liftTop);
    }
    // The bridges' states (their decks are in the cells but for a moving one).
    for (const BridgeView &b : view.bridges) {
        mix(b.extended);
        mix(b.moving);
    }
    // Where the platforms run.
    for (const PlatformView &p : view.platforms) {
        mix(p.originU);
        mix(p.originV);
        mix(p.limit);
        mix(p.alongU);
    }
    for (const FoeView &f : view.foes)
        if (parkedFoe(view, f.u, f.v))
            mix((unsigned)(f.v * view.extentU + f.u));
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
    if (first.kind == Outcome::Rest || (spells_.empty() && back_.empty()))
        return first;
    std::vector<double> starts;
    parkStarts(u, v, atMs, &starts);
    if (d >= DIR_MIN && d <= DIR_MAX) {
        parkStarts(u + DIR_DU[d], v + DIR_DV[d], atMs, &starts);
        const auto fallen = back_.find((v + DIR_DV[d]) * view_->extentU + u + DIR_DU[d]);
        if (fallen != back_.end() && fallen->second.from <= atMs && fallen->second.back > atMs)
            starts.push_back(fallen->second.back);
    }
    std::sort(starts.begin(), starts.end());
    // Standing on a platform is riding it: the wait for a later start is
    // only while it stays parked.
    const Cell &here = view_->cell(u, v);
    const bool onPlatform = here.track && here.kind != TILE_LIFT;
    for (const double t : starts) {
        if (onPlatform && liftHeight(u, v, atMs, t - atMs) < 0)
            break;
        Outcome o = stepAt(u, v, f, d, t);
        if (o.kind == Outcome::Rest) {
            *startMs = t;
            return o;
        }
    }
    return first;
}

Outcome NavModel::stepImpl(int u, int v, int f, int d, bool timed, double atMs,
                           int fromHeight) const
{
    const BotView &w = *view_;
    Outcome out;
    out.facing = f;
    // A lift is open only at the height it is parked at, when timed.
    const double through = w.player.stepMs + LIFT_MARGIN_MS;
    Cell origin = w.cell(u, v);
    if (fromHeight >= 0) {
        origin.kind   = TILE_KIND_01;
        origin.height = (unsigned char)fromHeight;
    } else if (timed && moving(origin)) {
        // Off a platform as it stands at a cell, which the step need not
        // outlast: the game lets the rider go if it is there as the step
        // starts (movableentity.cpp).  A lift must stay for the step.
        const int h = liftHeight(u, v, atMs, origin.kind == TILE_LIFT ? through : 0);
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
        if (bp && timed && bp->kind == TILE_EMPTY) {
            // A fallen tile is a floor again when it is back.
            const auto fallen = back_.find(nv * w.extentU + nu);
            if (fallen != back_.end() && atMs + t >= fallen->second.back) {
                target = *bp;
                target.kind   = TILE_FALLING;
                target.height = (unsigned char)fallen->second.height;
                target.busy   = false;
                bp = &target;
            }
        }
        if (bp && timed && bp->kind == TILE_FALLING) {
            // An armed tile is gone while it is fallen.
            const auto armed = back_.find(nv * w.extentU + nu);
            if (armed != back_.end() && armed->second.from <= atMs + t &&
                atMs + t < armed->second.back) {
                target = *bp;
                target.kind   = TILE_EMPTY;
                target.height = 0;
                bp = &target;
            }
        }
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
        if (!why && (parkedFoe(w, nu, nv) || shut_.count(nv * w.extentU + nu)))
            why = "foe";
        if (!why && oneShot_.count(nv * w.extentU + nu))
            why = "one-shot";
        if (why) {
            if (n == 0) {
                out.why = why;
                return out;  // Blocked
            }
            break;  // a forced move stops against it
        }
        const Cell &b = *bp;
        out.at.push_back(t);
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
        const Cell &a = n == 0 ? origin : w.cell(cu, cv);
        if (falls(a, b) && ch > b.height)
            t += fallMs(ch - b.height, w.tickMs);
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
            out.at.push_back(t);
            out.cells.push_back({cu, cv});
        } else if (b.kind == TILE_JUMP_PAD) {
            t += jumpMs(b.jumpTop - b.height, w.tickMs);
            ch = b.jumpTop;
        } else {
            if (b.kind == TILE_STICKY && !b.busy)
                t += stickyMs(w.tickMs);
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

Paths::Paths(const NavModel &nav, int u, int v, int facing, double startMs,
             bool throughSwitches, const GlideSource *glides)
    : nav_(nav), eu_(nav.view().extentU), ev_(nav.view().extentV), startU_(u), startV_(v),
      startMs_(startMs)
{
    const size_t n = (size_t)eu_ * ev_ * 4;
    cost_.assign(n, -1.0);
    parent_.assign(n, -1);
    how_.assign(n, -1);
    departs_.assign(n, 0.0);
    alightAt_.assign(n, -1);

    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const int s = index(u, v, facing);
    cost_[s] = startMs;
    departs_[s] = startMs;
    open.push({startMs, s});
    std::vector<char> done(n, 0);
    const bool timed = nav.timed();
    while (!open.empty()) {
        const auto [c, i] = open.top();
        open.pop();
        if (done[i])
            continue;
        done[i] = 1;
        const int f  = i % 4 + 1;
        const int cu = (i / 4) % eu_, cv = (i / 4) / eu_;
        if (!throughSwitches && (cu != u || cv != v) && switchedBy(nav.view(), cu, cv))
            continue;
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
        // Gliding down, where the bound allows it.
        if (glides)
            for (const Landing &l : glides->glides(cu, cv, f)) {
                const int j = index(l.cell.u, l.cell.v, l.facing);
                const double nc = c + l.ms;
                if (cost_[j] < 0 || nc < cost_[j]) {
                    cost_[j]   = nc;
                    parent_[j] = i;
                    how_[j]    = 5;
                    departs_[j] = c;
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
        // Stepping off it sideways as it passes a cell between its ends.
        if (timed)
            for (const NavModel::Alighting &a : nav.alightings(cu, cv, c))
                for (int how = 0; how < 2; how++) {
                    const Outcome o = nav.stepOff(a.at.u, a.at.v, f, how == 0 ? f : NavModel::opposite(f), a.ms);
                    if (o.kind != Outcome::Rest)
                        continue;
                    const int j = index(o.u, o.v, o.facing);
                    const double nc = a.ms + o.ms;
                    if (cost_[j] < 0 || nc < cost_[j]) {
                        cost_[j]     = nc;
                        parent_[j]   = i;
                        how_[j]      = (signed char)(6 + how);
                        departs_[j]  = a.ms;
                        alightAt_[j] = a.at.v * eu_ + a.at.u;
                        open.push({nc, j});
                    }
                }
    }
}

Outcome Paths::alighted(int from, int to) const
{
    const int f = from % 4 + 1;
    const int au = alightAt_[to] % eu_, av = alightAt_[to] / eu_;
    return nav_.stepOff(au, av, f, how_[to] == 6 ? f : NavModel::opposite(f), departs_[to]);
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
        if (how == 4 || how == 5) {
            cells.push_back({(to / 4) % eu_, (to / 4) / eu_});   // carried, or glided, there
            continue;
        }
        if (how >= 6) {
            const Outcome o = alighted(from, to);
            cells.push_back({alightAt_[to] % eu_, alightAt_[to] / eu_});   // carried there
            cells.insert(cells.end(), o.cells.begin(), o.cells.end());
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
        if (how == 4 || how == 5) {
            // Carried to the other end (wait there for the platform), or glided.
            Leg leg;
            leg.waitUntil = cost_[to];
            leg.cells.push_back({(to / 4) % eu_, (to / 4) / eu_});
            legs.push_back(leg);
            continue;
        }
        if (how >= 6) {
            // Carried on to the cell, and off it as the platform passes.
            Leg leg;
            leg.waitUntil = departs_[to];
            leg.stepOff = true;
            leg.facing = from % 4 + 1;
            leg.cells.push_back({alightAt_[to] % eu_, alightAt_[to] / eu_});
            const Outcome o = alighted(from, to);
            leg.cells.insert(leg.cells.end(), o.cells.begin(), o.cells.end());
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
            leg.facing = f;
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
        if (how == 4 || how == 5) {
            out.push_back({cost_[to] - startMs_, {(to / 4) % eu_, (to / 4) / eu_}});
            continue;
        }
        if (how >= 6) {
            const Outcome o = alighted(from, to);
            const double at = departs_[to] - startMs_;
            out.push_back({at - nav_.view().player.stepMs, {alightAt_[to] % eu_, alightAt_[to] / eu_}});
            const double each = o.cells.empty() ? 0 : o.ms / (double)o.cells.size();
            for (size_t c = 0; c < o.cells.size(); c++)
                out.push_back({at + each * (double)c, o.cells[c]});
            continue;
        }
        if (how >= 2)
            continue;  // a turn enters no cell
        const int f  = from % 4 + 1;
        const int fu = (from / 4) % eu_, fv = (from / 4) / eu_;
        const Outcome o = nav_.stepAt(fu, fv, f, how == 0 ? f : NavModel::opposite(f), departs_[to]);
        for (size_t c = 0; c < o.cells.size(); c++)
            out.push_back({departs_[to] - startMs_ + o.at[c], o.cells[c]});
    }
    return out;
}

NavModel aroundThrowers(const BotView &view, double startMs, double withinMs)
{
    NavModel nav(view);
    std::vector<Step> cells;
    for (const FoeView &f : view.foes) {
        if (f.kind != 3 || f.dying)
            continue;
        cells.push_back({f.u, f.v});
        if (f.moving && f.moveDir >= 1 && f.moveDir <= 4)
            cells.push_back({f.u - DIR_DU[f.moveDir], f.v - DIR_DV[f.moveDir]});
    }
    if (cells.empty())
        return nav;
    const PlayerView &p = view.player;
    const Paths open(nav, p.u, p.v, p.facing, startMs);
    for (const Step &c : cells) {
        const double t = open.cost(c.u, c.v);
        if (t >= 0 && t <= withinMs)
            nav.shut(c);
    }
    return nav;
}

}  // namespace bot
