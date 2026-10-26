#include "throwers.h"
#include <algorithm>
#include <set>
#include <utility>
#include "danger.h"
#include "walk.h"

namespace bot {

namespace {

/* A thrower is led a leg of this many of the way's turning points
 * (Paths::cellsTo) at a time, the player waiting after each for it to come
 * within LEAD_NEAR, LEAD_WAIT_MS at most. */
const int    LEAD_LEG = 3;
const int    LEAD_NEAR = 4;
const double LEAD_WAIT_MS = 6000;
/* How long the player waits beside a teleporter for a thrower to go through. */
const double SEND_WAIT_MS = 15000;
/* A teleporter's tile kind (TILE_TELEPORTER, tile.h). */
const int TELEPORTER = 0x0f;
/* The cells around a cell, the four sides first. */
const int AROUND_DU[8] = {0, 1, 0, -1, 1, 1, -1, -1}, AROUND_DV[8] = {-1, 0, 1, 0, -1, 1, 1, -1};

}  // namespace

bool baitedBomb(Situation &s, const Option &going, double waitMs, Blast *out)
{
    const BotView &view = s.view;
    follow(s.foes, view, going.schedule, view.now + going.cost + waitMs);
    for (const Blast &b : s.foes.dropped())
        if (spendsBlock(view, b)) {
            if (b.from - FUSE_MS - view.now < going.cost - view.tickMs)
                return false;
            *out = b;
            return true;
        }
    return false;
}

bool drawThrower(Situation &s, Option &o, Step by, int near, double maxMs)
{
    const BotView &view = s.view;
    const double there = view.now + o.cost;
    if (!follow(s.foes, view, o.schedule, there + maxMs,
                [&] { return s.foes.now() >= there && throwerNear(s.foes, by, near); }))
        return false;
    waitTill(view, o, s.foes.now() - view.now);
    return true;
}

bool leadThrower(Situation &s, Option &o, Step from, Step to)
{
    const BotView &view = s.view;
    Step at = from;
    std::set<std::pair<int, int>> sentThrough;
    for (int legs = 0; at.u != to.u || at.v != to.v; legs++) {
        if (legs > 64)
            return false;
        const RoutesOn routes(s.nav, o.schedule, at, facingAfter(view.player.facing, o.script), o.cost);
        const Paths &on = routes.paths();
        const std::vector<Step> cells = on.cellsTo(to.u, to.v);
        if (cells.size() < 2)
            return false;
        // A pad on the way: the player waits beside it, where the thrower's
        // chase goes over it (foemodel.h), till it is out the far end.
        bool through = false;
        for (size_t i = 1; i + 1 < cells.size(); i++) {
            const Cell &t = view.cell(cells[i].u, cells[i].v);
            // Either end of a pad gone through already is no new one.
            if (t.kind != TELEPORTER || !sentThrough.insert({cells[i].u, cells[i].v}).second)
                continue;
            sentThrough.insert({t.teleU, t.teleV});
            for (int d = 0; d < 8 && !through; d++) {
                const Step b{cells[i].u + AROUND_DU[d], cells[i].v + AROUND_DV[d]};
                if (!view.inside(b.u, b.v) || view.cell(b.u, b.v).kind == TELEPORTER ||
                    view.cell(b.u, b.v).height != t.height || on.cost(b.u, b.v) < 0)
                    continue;
                Option w = o;
                if (walkOn(s, s.nav, w, at, b) && drawThrower(s, w, {t.teleU, t.teleV}, 1, SEND_WAIT_MS)) {
                    o = std::move(w);
                    at = b;
                    through = true;
                }
            }
            break;
        }
        if (through)
            continue;
        const Step end = cells[std::min(cells.size() - 1, (size_t)LEAD_LEG)];
        Option w = o;
        if (!walkOn(s, s.nav, w, at, end))
            return false;
        const bool last = end.u == to.u && end.v == to.v;
        if (!last && !drawThrower(s, w, end, LEAD_NEAR, LEAD_WAIT_MS))
            return false;
        o = std::move(w);
        at = end;
    }
    return true;
}

}  // namespace bot
