#include "nav.h"
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

bool stairs(int kind) { return kind >= TILE_STAIRS_1 && kind <= TILE_STAIRS_4; }

/* The kinds whose cells move or change under the player: the model does not
 * follow them yet, and treats them as walls. */
bool dynamic(int kind)
{
    return kind == TILE_LIFT || kind == TILE_PLATFORM_U || kind == TILE_PLATFORM_V ||
           kind == TILE_PLATFORM_TRACK || kind == TILE_BRIDGE_U || kind == TILE_BRIDGE_V;
}

}  // namespace

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

Outcome NavModel::step(int u, int v, int f, int d) const
{
    const BotView &w = *view_;
    Outcome out;
    out.facing = f;
    int    cu = u, cv = v, ch = w.cell(u, v).height;
    int    dir = d;
    bool   sliding = false;
    double t = 0;

    for (int n = 0; n < MAX_CHAIN; n++) {
        const int nu = cu + DIR_DU[dir], nv = cv + DIR_DV[dir];
        const char *why = w.inside(nu, nv) ? refused(w.cell(cu, cv), ch, w.cell(nu, nv), dir)
                                           : "edge";
        if (why) {
            if (n == 0) {
                out.why = why;
                return out;  // Blocked
            }
            break;  // a forced move stops against it
        }
        const Cell &b = w.cell(nu, nv);
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

Paths::Paths(const NavModel &nav, int u, int v, int facing)
    : nav_(nav), eu_(nav.view().extentU), ev_(nav.view().extentV), startU_(u), startV_(v)
{
    const size_t n = (size_t)eu_ * ev_ * 4;
    cost_.assign(n, -1.0);
    parent_.assign(n, -1);
    how_.assign(n, -1);

    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const int s = index(u, v, facing);
    cost_[s] = 0;
    open.push({0, s});
    std::vector<char> done(n, 0);
    while (!open.empty()) {
        const auto [c, i] = open.top();
        open.pop();
        if (done[i])
            continue;
        done[i] = 1;
        const int f  = i % 4 + 1;
        const int cu = (i / 4) % eu_, cv = (i / 4) / eu_;
        for (int how = 0; how < 4; how++) {
            const Outcome o =
                how == 0 ? nav.step(cu, cv, f, f) :
                how == 1 ? nav.step(cu, cv, f, NavModel::opposite(f)) :
                how == 2 ? nav.turn(cu, cv, f, +1) : nav.turn(cu, cv, f, -1);
            if (o.kind != Outcome::Rest)
                continue;
            const int j = index(o.u, o.v, o.facing);
            const double nc = c + o.ms;
            if (cost_[j] < 0 || nc < cost_[j]) {
                cost_[j]   = nc;
                parent_[j] = i;
                how_[j]    = (signed char)how;
                open.push({nc, j});
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
    return best;
}

std::vector<Step> Paths::cellsTo(int u, int v) const
{
    int best = -1;
    for (int f = 1; f <= 4; f++) {
        const int i = index(u, v, f);
        if (cost_[i] >= 0 && (best < 0 || cost_[i] < cost_[best]))
            best = i;
    }
    if (best < 0)
        return {};
    std::vector<int> chain;
    for (int i = best; i >= 0; i = parent_[i])
        chain.push_back(i);
    std::vector<Step> cells{{startU_, startV_}};
    for (size_t k = chain.size() - 1; k-- > 0;) {
        const int from = chain[k + 1], to = chain[k];
        const int f  = from % 4 + 1;
        const int fu = (from / 4) % eu_, fv = (from / 4) / eu_;
        const int how = how_[to];
        if (how >= 2)
            continue;  // a turn passes no cell
        const Outcome o = nav_.step(fu, fv, f, how == 0 ? f : NavModel::opposite(f));
        cells.insert(cells.end(), o.cells.begin(), o.cells.end());
    }
    return cells;
}

}  // namespace bot
