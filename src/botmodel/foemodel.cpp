#include "foemodel.h"
#include <algorithm>
#include <math.h>
#include <stdlib.h>
#include "foetype.h"
#include "liftmodel.h"
#include "tile.h"

namespace bot {

namespace {

const double STEP_GRACE_MS   = 20.0;   // a step this soon after the last starts where it ended
const int    CATCHER = 2, THROWER = 3;
const double SLIDE_MS        = 150.0;  // movableentity.cpp
const double STICKY_MS       = 3000.0;
const double TELEPORT_MS     = 500.0;  // a teleporter's hold, before the jump and after
const unsigned DROP_EVERY_MS = 2000;   // foe.cpp's Foe::step
const double DROP_LATE_MS    = 50.0;   // Foe::dropBomb: this soon into a move it still drops
const double FALL_MS         = 1500.0; // fallingtile.cpp: armed to fallen
const double BACK_MS         = 5000.0; // armed to back

int opposite(int d) { return d > 2 ? d - 2 : d + 2; }
int right(int d)    { return d % 4 + 1; }
int left(int d)     { return (d + 2) % 4 + 1; }
bool stairs(int k)  { return k > 4 && k < 9; }

/* The stairs-orientation code of a step (foepath.cpp's
 * Sim_GetCellStepDirectionCode): its own enumeration, not a facing. */
int step_code(int uf, int vf, int ut, int vt)
{
    if (vf < vt) return 1;
    if (uf < ut) return 4;
    if (vf > vt) return 3;
    return ut < uf ? 2 : 0;
}

/* d is the facing or its reverse: a real step rather than a turn. */
bool forward_or_back(int d, int facing) { return d == facing || d == opposite(facing); }

}  // namespace

const Cell &FoeModel::cell(int u, int v) const
{
    return view_->inside(u, v) ? view_->cell(u, v) : wall_;
}

int FoeModel::heightNow(int u, int v) const
{
    const Cell &c = cell(u, v);
    const LiftView *lift = c.kind == TILE_LIFT ? liftAt(*view_, u, v) : nullptr;
    const int h = lift ? liftHeightAt(*view_, *lift, clock_ - view_->now) : -1;
    return h >= 0 ? h : c.height;
}

bool FoeModel::empty(int u, int v) const
{
    if (!view_->inside(u, v))
        return false;
    const int i = fallAt_[(size_t)v * view_->extentU + u];
    if (i >= 0)
        return falls_[(size_t)i].gone;
    const Cell &c = cell(u, v);
    return c.kind == TILE_EMPTY && !c.track;
}

void FoeModel::tickFalls()
{
    for (Fall &t : falls_) {
        const bool was = t.gone;
        if (occ(t.u, t.v) != 0 && !t.armed && !t.gone) {
            t.armed   = true;
            t.armedAt = clock_;
        }
        if (t.armed && clock_ - t.armedAt >= FALL_MS) {
            t.armed = false;
            t.gone  = true;
        }
        if (t.gone && t.respawns && clock_ - t.armedAt > BACK_MS)
            t.gone = false;
        if (t.gone != was)
            fallsChanged_ += t.gone == t.goneAtLoad ? -1 : 1;
    }
}

double FoeModel::parkedLeft(int u, int v) const
{
    const PlatformView *p = platformThrough(*view_, u, v);
    if (p == nullptr)
        return -1;
    const double at = clock_ - view_->now;
    std::vector<Park> atOrigin, atLimit;
    platformSpells(*view_, *p, at + 1, &atOrigin, &atLimit);
    const bool origin = (p->alongU ? u : v) == (p->alongU ? p->originU : p->originV);
    if (!origin && (p->alongU ? u : v) != p->limit)
        return -1;
    for (const Park &k : origin ? atOrigin : atLimit)
        if (k.from <= at && at <= k.to)
            return k.to - at;
    return -1;
}

int FoeModel::occ(int u, int v) const
{
    return view_->inside(u, v) ? occ_[(size_t)v * view_->extentU + u] : 0;
}

void FoeModel::setOcc(int u, int v, int o)
{
    if (view_->inside(u, v))
        occ_[(size_t)v * view_->extentU + u] = (unsigned char)o;
}

void FoeModel::load(const BotView &view)
{
    view_ = &view;
    wall_ = Cell();
    wall_.kind   = TILE_IMPASSABLE;
    wall_.height = 0xff;
    occ_.resize(view.cells.size());
    gem_.resize(view.cells.size());
    spent_.assign(view.cells.size(), 0);
    for (size_t i = 0; i < view.cells.size(); i++) {
        occ_[i] = view.cells[i].occupant;
        gem_[i] = view.cells[i].contents == CONTENTS_CRYSTAL;
        spent_[i] = view.cells[i].busy;
    }
    clock_ = view.now;
    blasts_.clear();
    byCaller_.clear();
    dropped_.clear();
    kills_.clear();
    for (const BombView &b : view.bombs)
        blasts_.push_back(predictBomb(view, b));
    byCaller_.assign(blasts_.size(), 0);
    player_ = {view.player.u, view.player.v};
    playerOcc_ = view.player.protection ? 3 : 4;
    playerLeft_ = {-1, -1};
    const PlayerView &p = view.player;
    if (p.moving && p.moveDir >= 1 && p.moveDir <= 4) {
        playerLeft_ = {p.u - DIR_DU[p.moveDir], p.v - DIR_DV[p.moveDir]};
        leftUntil_  = p.animStart + p.animMs;
    }
    foes_.clear();
    for (size_t i = 0; i < view.foes.size(); i++) {
        const FoeView &fv = view.foes[i];
        if (fv.dying)
            continue;
        Foe f;
        f.id = i;
        f.u = fv.u;
        f.v = fv.v;
        f.h = fv.h;
        f.facing = fv.facing;
        f.kind = fv.kind;
        f.type = fv.type;
        f.mode = fv.type > FOE_FACING_BASE ? FOE_CHASER : fv.type;
        f.homeU = fv.homeU;
        f.homeV = fv.homeV;
        f.lastU = fv.lastU;
        f.lastV = fv.lastV;
        f.stepMs = fv.kind == THROWER ? THROWER_STEP_MS : CATCHER_STEP_MS;
        f.lastDrop    = fv.lastDrop;
        f.dropPending = fv.dropPending;
        f.stuckSince  = fv.stuckSince;
        f.lastMoveDir = fv.lastMoveDir;
        f.tele        = fv.teleport;
        f.teleAt      = fv.teleportAt;
        if (fv.moving) {
            f.moveDir   = fv.moveDir;
            f.animStart = fv.animStart;
            f.animMs    = fv.animMs;
            if (fv.moveDir >= 1 && fv.moveDir <= 4) {
                f.fromU = fv.u - DIR_DU[fv.moveDir];
                f.fromV = fv.v - DIR_DV[fv.moveDir];
            }
        } else {
            f.stepEnd = fv.stepEnd;
        }
        f.riding = fv.riding;
        foes_.push_back(f);
    }
    falls_.clear();
    fallAt_.assign(view.cells.size(), -1);
    fallsChanged_ = 0;
    for (const FallingView &t : view.fallings) {
        fallAt_[(size_t)t.v * view.extentU + t.u] = (int)falls_.size();
        falls_.push_back({t.u, t.v, t.respawns, t.armed, t.fallen, t.fallen, t.armedAt});
    }
    const size_t keys = (size_t)view.extentU * view.extentV;
    if (openAt_.size() != keys) {
        openAt_.assign(keys, -1);
        closedAt_.assign(keys, -1);
        keys_.clear();
    }

    // What the search reads of the terrain, hashed: the kept first steps
    // stand while it holds.
    unsigned long long h = 1469598103934665603ull;
    auto mix = [&h](unsigned x) { h = (h ^ x) * 1099511628211ull; };
    mix((unsigned)view.extentU);
    mix((unsigned)view.extentV);
    sticky_ = false;
    for (const Cell &c : view.cells) {
        mix(c.height);
        mix(c.kind);
        mix(c.busy);
        mix(c.track);
        mix(c.liftBottom);
        mix(c.liftTop);
        mix(c.slideDir);
        mix(c.jumpTop);
        sticky_ = sticky_ || c.kind == TILE_STICKY;
    }
    if (h != terrain_) {
        firstSteps_.clear();
        terrain_ = h;
    }
}

bool FoeModel::passable(int u, int v, int mode) const
{
    const Cell &c = cell(u, v);
    if (empty(u, v))
        return false;
    if (c.kind == TILE_IMPASSABLE)
        return false;
    if (c.kind == TILE_BOMBABLE && !c.busy)
        return false;
    if (mode == FOE_GEM_RUNNER && (occ(u, v) == 4 || occ(u, v) == 3))
        return false;
    if (mode == FOE_SWITCH_RUNNER && c.kind == TILE_SWITCH && !c.busy && occ(u, v) != 4)
        return false;
    return true;
}

/* foepath.cpp's Sim_CheckCellStepIsLegal: a sequence of assignments to one
 * verdict, in its order. */
bool FoeModel::stepLegal(int uf, int vf, int ut, int vt) const
{
    const Cell &to = cell(ut, vt), &from = cell(uf, vf);
    bool flag = false;
    const int code = step_code(uf, vf, ut, vt);
    const bool alongTo = stairs(to.kind) && (to.kind - 4 == code || opposite(to.kind - 4) == code);
    if (alongTo)
        flag = true;
    if (from.kind != TILE_SLIDE && from.height == to.height)
        flag = true;
    else if (to.kind == TILE_LIFT && (to.liftBottom == from.height || to.liftTop == from.height))
        flag = true;
    else if (from.kind == TILE_LIFT && (from.liftBottom == to.height || from.liftTop == to.height))
        flag = true;
    if (stairs(to.kind)) {
        if (alongTo) {
            const bool ok = !(stairs(from.kind) && code != from.kind);
            if (ok && (int)to.height == (int)from.height - 1)
                flag = true;
        }
    } else {
        const int d = (int)to.height - (int)from.height;
        if (from.kind != TILE_SLIDE && d < 3 && d > 0)
            flag = !from.track;
    }
    if (from.kind == TILE_SLIDE && from.height == to.height) {
        const int dir = from.slideDir;
        flag = (vf < vt && dir == 1) || (uf < ut && dir == 4) ||
               (vf > vt && dir == 3) || (uf > ut && dir == 2);
    }
    if (to.kind == TILE_SLIDE)
        flag = true;
    if (from.kind == TILE_JUMP_PAD) {
        const int lvl = from.jumpTop;
        flag = (uf > ut && lvl == cell(uf + 1, vf).height) ||
               (uf < ut && lvl == cell(uf - 1, vf).height) ||
               (vf > vt && lvl == cell(uf, vf + 1).height) ||
               (vf < vt && lvl == cell(uf, vf - 1).height);
    }
    if (to.kind == TILE_JUMP_PAD)
        flag = true;
    if (from.kind == TILE_STICKY && occ(uf, vf) == 0)
        return occ(ut + (uf - ut) * 2, vt + (vf - vt) * 2) == 4;
    return flag;
}

bool FoeModel::firstStep(const Foe &f, int tu, int tv, int cap, int *nu, int *nv)
{
    const int stride = view_->extentU;
    const bool keep = !sticky_ && fallsChanged_ == 0 && f.mode != FOE_GEM_RUNNER && f.mode != FOE_SWITCH_RUNNER;
    const unsigned long long memo =
        ((((unsigned long long)(f.v * stride + f.u) * 65536 + (unsigned)(tv * stride + tu)) * 256 +
          (unsigned)(f.mode & 0xff)) * 1024) + (unsigned)(cap & 0x3ff);
    if (keep) {
        const auto it = firstSteps_.find(memo);
        if (it != firstSteps_.end()) {
            if (it->second < 0)
                return false;
            *nu = it->second % stride;
            *nv = it->second / stride;
            return true;
        }
    }
    const bool found = search(f, tu, tv, cap, nu, nv);
    if (keep)
        firstSteps_[memo] = found ? *nv * stride + *nu : -1;
    return found;
}

bool FoeModel::search(const Foe &f, int tu, int tv, int cap, int *nu, int *nv)
{
    const int stride = view_->extentU;
    auto key = [stride](int u, int v) { return stride * v + u; };
    if (!passable(tu, tv, f.mode) || !passable(f.u, f.v, f.mode) || key(f.u, f.v) == key(tu, tv))
        return false;

    for (int k : keys_) {
        openAt_[(size_t)k]   = -1;
        closedAt_[(size_t)k] = -1;
    }
    keys_.clear();
    nodes_.clear();
    auto mark = [&](std::vector<int> &at, int k, int n) {
        if (k < 0 || (size_t)k >= at.size())
            return;
        at[(size_t)k] = n;
        keys_.push_back(k);
    };
    auto find = [&](std::vector<int> &at, int k) {
        return (k < 0 || (size_t)k >= at.size()) ? -1 : at[(size_t)k];
    };
    auto make = [&](int u, int v, int g, int h, int parent) {
        Node n;
        n.u = u;
        n.v = v;
        n.g = g;
        n.h = h;
        n.f = g + h;
        n.key = key(u, v);
        n.parent = parent;
        n.next = -1;
        for (int &c : n.children)
            c = -1;
        nodes_.push_back(n);
        return (int)nodes_.size() - 1;
    };
    auto recordChild = [&](int p, int c) {
        // The game's ninth child overwrites the node's list link; a node is
        // relaxed into from at most its four neighbours, so it never has one.
        for (int &slot : nodes_[(size_t)p].children)
            if (slot < 0) {
                slot = c;
                return;
            }
    };
    int open = -1;  // the open list's first node, ascending f
    auto insertOpen = [&](int n) {
        mark(openAt_, nodes_[(size_t)n].key, n);
        const int fn = nodes_[(size_t)n].f;
        int prev = -1, cur = open;
        while (cur >= 0 && nodes_[(size_t)cur].f < fn) {  // a tie goes before
            prev = cur;
            cur = nodes_[(size_t)cur].next;
        }
        nodes_[(size_t)n].next = cur;
        if (prev < 0)
            open = n;
        else
            nodes_[(size_t)prev].next = n;
    };
    std::vector<int> pending;
    auto propagate = [&](int p) {
        auto relaxChildren = [&](int q) {
            for (int c : nodes_[(size_t)q].children) {
                if (c < 0)
                    break;
                const int gnew = nodes_[(size_t)q].g + 1;
                Node &n = nodes_[(size_t)c];
                if (gnew < n.g) {
                    n.g = gnew;
                    n.f = n.h + gnew;
                    n.parent = q;
                    pending.push_back(c);
                }
            }
        };
        relaxChildren(p);
        while (!pending.empty()) {
            const int q = pending.back();
            pending.pop_back();
            relaxChildren(q);
        }
    };
    const int goalU = f.u, goalV = f.v, goalKey = key(f.u, f.v);
    auto relax = [&](int p, int u, int v) {
        const int gnew = nodes_[(size_t)p].g + 1;
        const int k = key(u, v);
        int n = find(openAt_, k);
        if (n >= 0) {
            recordChild(p, n);
            Node &m = nodes_[(size_t)n];
            if (gnew < m.g) {
                m.g = gnew;
                m.f = m.h + gnew;
                m.parent = p;
            }
            return;
        }
        n = find(closedAt_, k);
        if (n >= 0) {
            recordChild(p, n);
            Node &m = nodes_[(size_t)n];
            if (gnew < m.g) {
                m.parent = p;
                m.f = m.h + gnew;
                m.g = gnew;
                propagate(n);
            }
            return;
        }
        const int du = u - goalU, dv = v - goalV;
        n = make(u, v, gnew, du * du + dv * dv, p);
        insertOpen(n);
        recordChild(p, n);
    };

    const int du = tu - f.u, dv = tv - f.v;
    insertOpen(make(tu, tv, 0, du * du + dv * dv, -1));
    static const int SU[4] = {0, 1, 0, -1}, SV[4] = {-1, 0, 1, 0};
    int iter = 0, node = -1;
    if (cap > 0) {
        for (;;) {
            node = open;
            if (node < 0)
                return false;
            open = nodes_[(size_t)node].next;
            mark(openAt_, nodes_[(size_t)node].key, -1);
            mark(closedAt_, nodes_[(size_t)node].key, node);
            if (nodes_[(size_t)node].key == goalKey)
                break;
            const int u = nodes_[(size_t)node].u, v = nodes_[(size_t)node].v;
            for (int i = 0; i < 4; i++) {
                const int u2 = u + SU[i], v2 = v + SV[i];
                if (!passable(u2, v2, f.mode) || !stepLegal(u, v, u2, v2))
                    continue;
                relax(node, u2, v2);
            }
            if (++iter >= cap)
                break;
        }
    }
    if (iter >= cap || node < 0)
        return false;
    const int next = nodes_[(size_t)node].parent;
    if (next < 0)
        return false;
    *nu = nodes_[(size_t)next].u;
    *nv = nodes_[(size_t)next].v;
    return true;
}

bool FoeModel::nearestGem(int u0, int v0, int radius, int *pu, int *pv) const
{
    // Half-open: radius cells one way, radius - 1 the other; row and column
    // 0 never chosen; the distance truncated to a whole cell.
    int best = radius, bu = 0, bv = 0;
    for (int v = v0 - radius; v < v0 + radius; v++)
        for (int u = u0 - radius; u < u0 + radius; u++) {
            if (!(v > 0 && u > 0 && v < view_->extentV && u < view_->extentU))
                continue;
            if (!gem_[(size_t)v * view_->extentU + u])
                continue;
            const int d = (int)(unsigned char)(long long)sqrt((double)((u0 - u) * (u0 - u) +
                                                                         (v0 - v) * (v0 - v)));
            if (d != 0 && d < best) {
                best = d;
                bu = u;
                bv = v;
            }
        }
    if (best >= radius)
        return false;
    *pu = bu;
    *pv = bv;
    return true;
}

bool FoeModel::farthestFree(int u0, int v0, int *pu, int *pv) const
{
    double best = 0;
    int bu = u0, bv = v0;
    for (int v = 0; v < view_->extentV; v++)
        for (int u = 0; u < view_->extentU; u++) {
            if (empty(u, v) || occ(u, v) != 0)
                continue;
            const double d = sqrt((double)((v - v0) * (v - v0) + (u - u0) * (u - u0)));
            if (best < d) {
                best = d;
                bu = u;
                bv = v;
            }
        }
    if (!(best > 0))
        return false;
    *pu = bu;
    *pv = bv;
    return true;
}

void FoeModel::target(Foe &f, int *tu, int *tv, int *speed)
{
    *tu = player_.u;
    *tv = player_.v;
    *speed = 0x32;
    switch (f.type) {
    case FOE_EXIT_GUARD:
        *tu = view_->exitU;
        *tv = view_->exitV;
        *speed = 400;
        break;
    case FOE_SWITCH_RUNNER:  // its switch is not modelled: the player, as when none is near
        *speed = 100;
        break;
    case FOE_GEM_THIEF: {  // a gem within reach if a way to it is found, else the player
        int gu, gv, nu, nv;
        *speed = 100;
        if (f.moveDir == 0 && nearestGem(f.u, f.v, 5, &gu, &gv) &&
            firstStep(f, gu, gv, 0x96, &nu, &nv)) {
            *tu = gu;
            *tv = gv;
            *speed = 0x96;
        }
        break;
    }
    case FOE_FOLLOWER:  // held if there is no catcher
        f.held = 1;
        for (const Foe &o : foes_)
            if (o.kind == CATCHER) {
                *tu = o.u;
                *tv = o.v;
                *speed = 0x96;
                f.held = 0;
            }
        break;
    case FOE_GEM_RUNNER: {  // held if there is no free cell
        int fu, fv;
        if (farthestFree(player_.u, player_.v, &fu, &fv)) {
            *tu = fu;
            *tv = fv;
            *speed = 0x96;
        } else {
            f.held = 1;
        }
        break;
    }
    default:
        break;
    }
}

int FoeModel::chaseMove(const Foe &f, int tu, int tv, int speed)
{
    int nu, nv;
    if (!firstStep(f, tu, tv, speed, &nu, &nv))
        return 0;
    const int du = nu - f.u, dv = nv - f.v;
    int pm = 0;
    if (dv == -1) pm = 1;
    if (du == 1)  pm = 2;
    if (dv == 1)  pm = 3;
    if (du == -1) pm = 4;
    // The turn table (foes turn as the player does).  As in the game, a
    // reverse ends as a turn to the right.
    const int facing = f.facing;
    if (pm == opposite(facing))
        pm = opposite(facing);
    if (pm == right(facing))
        pm = right(facing) + 10;
    if (pm == left(facing))
        pm = left(facing) + 10;
    if (pm == opposite(facing))
        pm = right(facing) + 10;
    if (pm != facing && pm != opposite(facing))
        return pm;  // a turn
    const Cell &step = cell(nu, nv), &here = cell(f.u, f.v);
    // A platform's track is open only where the platform stands parked, long
    // enough still for the step, and never to a foe on a platform (foe.cpp).
    if (step.track && (f.riding || parkedLeft(nu, nv) < f.stepMs))
        return 0;
    if (empty(nu, nv) || here.kind == TILE_JUMP_PAD)
        return 0;
    if (step.kind == TILE_LIFT && heightNow(nu, nv) != f.h)
        return 0;
    if (here.kind == TILE_LIFT && step.height != f.h)
        return 0;
    return pm;
}

bool FoeModel::startMove(Foe &f, int move, bool sliding)
{
    const bool real = move >= 1 && move <= 4;
    const int nu = real ? f.u + DIR_DU[move] : f.u;
    const int nv = real ? f.v + DIR_DV[move] : f.v;
    const Cell &here = cell(f.u, f.v), &dest = cell(nu, nv);
    if (real && forward_or_back(move, f.facing)) {
        const int o = occ(nu, nv);
        if (o != 0 && (f.kind == THROWER || (f.kind == CATCHER && o != 4)))
            return false;
    }
    if (here.kind == TILE_LIFT && real && f.h == dest.height - 1)
        return false;
    if (dest.kind == TILE_IMPASSABLE || (dest.kind == TILE_BOMBABLE && !dest.busy))
        return false;
    if (stairs(here.kind)) {
        const int axis = here.kind - 4;
        if (!(move == axis || move == opposite(axis)) && dest.height == here.height + 1)
            return false;
    }
    if (!stairs(dest.kind) && !stairs(here.kind) && dest.height == here.height + 1)
        return false;
    if (real && occ(nu, nv) != 0 && occ(nu, nv) != 4)
        return false;
    if (real && !view_->inside(nu, nv))
        return false;

    const double since = clock_ - f.stepEnd;
    f.animStart = (since <= 0 || since >= STEP_GRACE_MS) ? clock_ : f.stepEnd;
    f.animMs    = sliding ? SLIDE_MS : f.stepMs;
    f.moveDir   = move;
    if (real) {
        f.riding      = false;
        f.lastMoveDir = move;
        f.fromU = f.u;
        f.fromV = f.v;
        f.u = nu;
        f.v = nv;
        f.h = dest.height;
        setOcc(nu, nv, f.kind);
    }
    return true;
}

void FoeModel::tickFoe(size_t i)
{
    Foe &f = foes_[i];
    f.held = clock_ < view_->player.freezeUntil ? 1 : 0;
    int tu, tv, speed;
    // A foe standing on a lift rides it (movableentity.cpp: on a lift, at
    // its current height), and one on a platform goes where it does, turning
    // or not (a step off it lets go first).
    if (f.moveDir == 0 && cell(f.u, f.v).kind == TILE_LIFT)
        f.h = heightNow(f.u, f.v);
    Step on;
    const PlatformView *p = f.riding ? platformThrough(*view_, f.u, f.v) : nullptr;
    if ((f.moveDir == 0 || f.moveDir > 10) && p && platformCellAt(*view_, *p, clock_ - view_->now, &on) &&
        (on.u != f.u || on.v != f.v)) {
        if (occ(f.u, f.v) == f.kind)
            setOcc(f.u, f.v, 0);
        f.u = on.u;
        f.v = on.v;
        setOcc(f.u, f.v, f.kind);
    }
    target(f, &tu, &tv, &speed);
    // A thrower with its target beside it asks for a bomb.  As in the game,
    // the time since the last is compared unsigned.
    if (f.kind == THROWER && !f.held && !f.dropPending && abs(tu - f.u) < 2 &&
        abs(tv - f.v) < 2 &&
        (unsigned)((int)clock_ - (int)f.lastDrop) >= DROP_EVERY_MS) {
        f.lastDrop    = clock_;
        f.dropPending = true;
    }
    stepFoe(f, tu, tv, speed);
    dropBomb(f);
}

void FoeModel::dropBomb(Foe &f)
{
    if (!f.dropPending || f.type == FOE_SWITCH_RUNNER)
        return;
    Step at{f.u, f.v};
    if (f.moveDir != 0) {
        if (clock_ - f.animStart >= DROP_LATE_MS)
            return;  // asked for still: dropped once it is at rest
        if (f.moveDir >= 1 && f.moveDir <= 4)
            at = {f.fromU, f.fromV};  // just set off: behind it
    }
    const int h = view_->inside(at.u, at.v) ? cell(at.u, at.v).height : f.h;
    const Blast b = predictBomb(*view_, at.u, at.v, h, f.facing, clock_);
    blasts_.push_back(b);
    byCaller_.push_back(0);
    dropped_.push_back(b);
    f.dropPending = false;
}

int FoeModel::terrain(Foe &f, int move, bool *sliding)
{
    if (!view_->inside(f.u, f.v) || f.h != cell(f.u, f.v).height)
        return move;
    const Cell &here = cell(f.u, f.v);
    const size_t at = (size_t)f.v * view_->extentU + f.u;
    if (here.kind == TILE_STICKY && !spent_[at]) {
        if (f.stuckSince == 0)
            f.stuckSince = clock_;
        if (clock_ - f.stuckSince <= STICKY_MS) {
            move = 0;
        } else {
            f.stuckSince = 0;
            spent_[at] = 1;
        }
    } else {
        f.stuckSince = 0;
    }
    if (here.kind == TILE_SLIDE) {
        f.facing = here.slideDir;
        *sliding = true;
        return here.slideDir;
    }
    if (here.kind == TILE_TELEPORTER) {
        // Held, sent to the pad's other end, held again, then on the way it
        // last went.
        move = 0;
        if (f.tele == 1 && clock_ - f.teleAt >= TELEPORT_MS) {
            f.tele   = 2;
            f.teleAt = clock_;
            if (occ(f.u, f.v) == f.kind)
                setOcc(f.u, f.v, 0);
            const int du = here.teleU, dv = here.teleV;
            f.u = du;
            f.v = dv;
            f.h = cell(du, dv).height;
            setOcc(du, dv, f.kind);
        }
        if (f.tele == 0) {
            f.tele   = 1;
            f.teleAt = clock_;
        }
        if (f.tele == 2 && clock_ - f.teleAt >= TELEPORT_MS) {
            f.tele = 0;
            move = f.lastMoveDir;
        }
        return move;
    }
    if (here.kind == TILE_ICE && f.lastMoveDir != 0)
        return f.lastMoveDir;
    return move;
}

void FoeModel::stepFoe(Foe &f, int tu, int tv, int speed)
{
    if (f.moveDir != 0) {
        // The move under way ends this tick if its time is up.
        if (f.animMs <= clock_ - f.animStart) {
            f.stepEnd = f.animStart + f.animMs;
            if (f.moveDir >= 1 && f.moveDir <= 4) {
                if (occ(f.fromU, f.fromV) == f.kind)
                    setOcc(f.fromU, f.fromV, 0);
                f.riding = cell(f.u, f.v).track;
            } else {
                f.facing = f.moveDir - 10;
            }
            f.moveDir = 0;
            // The same pass sees to the terrain it has come to rest on.
            bool sliding = false;
            const int forced = terrain(f, 0, &sliding);
            if (forced != 0)
                startMove(f, forced, sliding);
        }
        return;
    }
    // A gem-taker at rest on a gem takes it.
    if (f.type == FOE_GEM_THIEF && view_->inside(f.u, f.v) && f.h == cell(f.u, f.v).height)
        gem_[(size_t)f.v * view_->extentU + f.u] = 0;
    int move = chaseMove(f, tu, tv, speed);
    if (move != 0) {
        f.lastU = tu;
        f.lastV = tv;
    } else if (f.type == FOE_RETURNER) {
        // No way to its target: back to the last it had a way to (Foe::step).
        move = chaseMove(f, f.lastU, f.lastV, speed);
    }
    if (f.held)
        move = 0;
    bool sliding = false;
    move = terrain(f, move, &sliding);
    if (move != 0)
        startMove(f, move, sliding);
}

void FoeModel::runTick(const std::function<Step(double)> &playerAt)
{
    if (view_ == nullptr || view_->tickMs <= 0)
        return;
    clock_ += view_->tickMs;
    // The level's objects first, as the game ticks them (gametick.cpp).
    tickFalls();
    // The player's step holds the cell it leaves until it ends, and its
    // end comes before the foes (movableentity.cpp).
    auto letGo = [&]() {
        if (playerLeft_.u >= 0 && occ(playerLeft_.u, playerLeft_.v) == playerOcc_ &&
            (playerLeft_.u != player_.u || playerLeft_.v != player_.v))
            setOcc(playerLeft_.u, playerLeft_.v, 0);
        playerLeft_ = {-1, -1};
    };
    if (playerLeft_.u >= 0 && clock_ >= leftUntil_ - 1e-6)
        letGo();
    const Step at = playerAt(clock_);
    if (at.u != player_.u || at.v != player_.v) {
        letGo();
        playerLeft_ = player_;
        leftUntil_  = clock_ + view_->player.stepMs;
        setOcc(at.u, at.v, playerOcc_);
        player_ = at;
    }
    for (size_t i = 0; i < foes_.size(); i++)
        tickFoe(i);
    // A foe on a tile that has fallen falls with it.
    for (size_t i = 0; i < foes_.size();) {
        const Foe &f = foes_[i];
        if (fallAt_.empty() || !empty(f.u, f.v) || fallAt_[(size_t)f.v * view_->extentU + f.u] < 0) {
            ++i;
            continue;
        }
        if (occ(f.u, f.v) == f.kind)
            setOcc(f.u, f.v, 0);
        foes_.erase(foes_.begin() + (long)i);
    }
    // The blasts: a foe in one dies, and its cell is freed.
    for (size_t i = 0; i < foes_.size();) {
        const Foe &f = foes_[i];
        bool dead = false, byOther = false;
        // A thrower takes no harm from a blast (movableentity.cpp).
        for (size_t k = 0; k < blasts_.size() && f.kind != THROWER; k++)
            if (blasts_[k].kills(f.u, f.v, f.h, clock_)) {
                dead = true;
                byOther = byOther || !byCaller_[k];
            }
        if (!dead) {
            ++i;
            continue;
        }
        if (occ(f.u, f.v) == f.kind)
            setOcc(f.u, f.v, 0);
        kills_.push_back({clock_, f.u, f.v, !byOther});
        foes_.erase(foes_.begin() + (long)i);
    }
}

bool FoeModel::reaches(size_t i, int u, int v)
{
    int nu, nv;
    return firstStep(foes_[i], u, v, 0x96, &nu, &nv);
}

std::vector<char> FoeModel::walkable(size_t i) const
{
    static const int SU[4] = {0, 1, 0, -1}, SV[4] = {-1, 0, 1, 0};
    const int W = view_->extentU;
    std::vector<char> seen((size_t)W * view_->extentV, 0);
    const Foe &f = foes_[i];
    if (!view_->inside(f.u, f.v))
        return seen;
    std::vector<Step> open{{f.u, f.v}};
    seen[(size_t)f.v * W + f.u] = 1;
    while (!open.empty()) {
        const Step a = open.back();
        open.pop_back();
        for (int d = 0; d < 4; d++) {
            const int u = a.u + SU[d], v = a.v + SV[d];
            // The search runs from the target back: a step from a to b is
            // tested as one from b to a.  The step itself is the foe's, which
            // will not go a height up but on stairs (startMove): a search
            // that has it do so leaves it standing.
            if (!view_->inside(u, v) || seen[(size_t)v * W + u] || !passable(u, v, f.mode) ||
                !stepLegal(u, v, a.u, a.v))
                continue;
            const Cell &here = cell(a.u, a.v), &dest = cell(u, v);
            const int move = d + 1;
            if (stairs(here.kind) && !(move == here.kind - 4 || move == opposite(here.kind - 4)) &&
                dest.height == here.height + 1)
                continue;
            if (!stairs(dest.kind) && !stairs(here.kind) && dest.height == here.height + 1)
                continue;
            seen[(size_t)v * W + u] = 1;
            open.push_back({u, v});
        }
    }
    return seen;
}

bool FoeModel::bombsCatchers(size_t i) const
{
    return foes_[i].kind == THROWER && foes_[i].type == FOE_FOLLOWER;
}

bool FoeModel::throwsAtPlayer(size_t i) const
{
    return foes_[i].kind == THROWER && !foes_[i].held && foes_[i].type != FOE_FOLLOWER;
}

int FoeModel::killed() const
{
    int n = 0;
    for (const Kill &k : kills_)
        n += k.byAdded;
    return n;
}

void FoeModel::run(double untilMs, const std::function<Step(double)> &playerAt)
{
    if (view_ == nullptr || view_->tickMs <= 0)
        return;
    while (clock_ + view_->tickMs <= untilMs + 1e-6)
        runTick(playerAt);
}

int spareGems(const BotView &view)
{
    int have = 0;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const Cell &c = view.cell(u, v);
            have += (c.contents == CONTENTS_CRYSTAL) + (c.hidden == CONTENTS_CRYSTAL);
        }
    for (const FoeView &f : view.foes)
        have += f.drops == CONTENTS_CRYSTAL && !f.dying;
    return have - std::max(0, view.gemsRequired - view.player.gems);
}

int gemsWiped(const BotView &view, const std::vector<FoeModel::Kill> &kills)
{
    int n = 0;
    for (const FoeModel::Kill &k : kills)
        n += view.cell(k.u, k.v).contents == CONTENTS_CRYSTAL;
    return n;
}

int catchersHunted(FoeModel &sim, const BotView &view)
{
    sim.load(view);
    int most = 0;
    for (size_t i = 0; i < sim.foeCount(); i++) {
        if (!sim.bombsCatchers(i))
            continue;
        int n = 0;
        for (size_t k = 0; k < sim.foeCount(); k++)
            n += sim.foeKind(k) == CATCHER && sim.reaches(i, sim.foeCell(k).u, sim.foeCell(k).v);
        most = std::max(most, n);
    }
    return most;
}

}  // namespace bot
