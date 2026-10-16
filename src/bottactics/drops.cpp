#include "drops.h"
#include <algorithm>
#include <cmath>
#include <stdio.h>
#include "danger.h"
#include "walk.h"

namespace bot {

namespace {

/* The cells to walk away to: within this many ms of the drop, the nearest
 * RETREATS of them. */
const double RETREAT_MS = 2500;
const size_t RETREATS = 16;
/* An ambush: the drop sites within this many ms of the player (the nearest
 * SITES of them, and where it stands), the waits before the drop, the few
 * cells to retreat to. */
const double SITE_MS = 1500;
const size_t SITES = 6;
const double DELAYS[] = {0, 250, 500, 750, 1000, 1250, 1500, 2000};
const size_t AMBUSH_RETREATS = 2;
/* A kill is counted on only if the foe dies with the blast this much earlier
 * or later, too: the foe model's timing is good to about that, and a foe
 * caught only on its way past the blast's edge is not caught at all if it
 * is out. */
const double SURE_MS = 250;
/* A wait after a drop ends this long after the blast's zone clears: the
 * foe model's chase assumed the player stays put until then. */
const double CLEAR_MARGIN_MS = 200;

/* Whether o's blasts still kill as many foes, and the player stays safe,
 * with them going off SURE_MS earlier or later. */
bool sure(Situation &s, const Option &o, int killed)
{
    for (const double shift : {-SURE_MS, SURE_MS}) {
        std::vector<Blast> shifted = o.blasts;
        for (Blast &b : shifted) {
            b.from += shift;
            b.until += shift;
        }
        const Danger d = checkPlan(s.foes, s.view, o.schedule, o.lingerMs + SURE_MS, shifted);
        if (d.hit || d.killed < killed)
            return false;
    }
    return true;
}

/* The ways to walk clear after a drop at site facing facing, dropIn ms from
 * now: the option so far (its script and schedule up to the drop) with a
 * retreat added and checked.  wait: stay until the blast has cleared. */
void retreats(Situation &s, const Option &before, Step site, int facing, double dropIn,
              bool wait, int needKills, size_t most, std::vector<Option> &out)
{
    const BotView &view = s.view;
    const Blast blast = predictBomb(view, site.u, site.v, view.cell(site.u, site.v).height,
                                    facing, view.now + dropIn);
    if (blast.lost)
        return;
    // The way away is found after the way there, which arms the falling
    // tiles it goes over.
    NavModel after = s.nav;
    for (const Timed &t : before.schedule)
        after.arm(t.cell, t.t);
    const Paths away(after, site.u, site.v, facing, dropIn);
    std::vector<std::pair<double, Step>> cells;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = away.cost(u, v);
            // Not onto a platform or a lift: it may carry the player along
            // with the bomb.
            if (c > 0 && c <= RETREAT_MS && !NavModel::moving(view.cell(u, v)))
                cells.push_back({c, {u, v}});
        }
    std::sort(cells.begin(), cells.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    for (size_t i = 0; i < cells.size() && i < most; i++) {
        const Step at = cells[i].second;
        Option o = before;
        o.script.push_back({ScriptStep::Drop, 0, {}, 0});
        Option walk;
        if (!walkTo(s, away, at, 0, dropIn, walk))
            continue;
        o.script.insert(o.script.end(), walk.script.begin(), walk.script.end());
        o.schedule.insert(o.schedule.end(), walk.schedule.begin() + 1, walk.schedule.end());
        o.cost = walk.cost;
        o.blasts.push_back(blast);
        if (wait) {
            o.script.push_back({ScriptStep::Wait, 0, {}, blast.until + CLEAR_MARGIN_MS});
            // The foes were predicted chasing the player where this puts it:
            // broken off, the bomb would go off on a different chase.
            o.committedUntil = blast.until + CLEAR_MARGIN_MS;
            o.stepMs         = o.committedUntil - view.now;
        }
        // The end must stay safe until the blast is over.
        o.lingerMs = std::max(1000.0, blast.until - view.now - o.schedule.back().t + 200);
        const Danger d = checkPlan(s.foes, view, o.schedule, o.lingerMs, o.blasts);
        if (d.hit || d.killed < needKills || (needKills > 0 && !sure(s, o, d.killed)))
            continue;
        o.checked = true;
        o.kills = d.killed;
        o.killed = d.kills;
        o.what = before.what;
        char buf[96];
        snprintf(buf, sizeof(buf), ", away to (%d,%d)", at.u, at.v);
        o.what += buf;
        out.push_back(std::move(o));
    }
}

}  // namespace

void offerAmbushes(Situation &s, std::vector<Option> &out)
{
    const BotView &view = s.view;
    const PlayerView &p = view.player;
    // In the air the player's landing, and so the drop's time, is not
    // known.
    if (p.bombs <= 0 || view.foes.empty() || p.falling)
        return;
    // Where to drop it: here, or a cell or two off, so that the foe on its
    // way is in the blast as it goes.
    std::vector<std::pair<double, Step>> sites{{0, {p.u, p.v}}};
    std::vector<std::pair<double, Step>> near;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = s.from.cost(u, v);
            if (c > 0 && c <= SITE_MS)
                near.push_back({c, {u, v}});
        }
    std::sort(near.begin(), near.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    const int spare = spareGems(view);
    for (size_t i = 0; i < near.size() && i < SITES; i++)
        sites.push_back(near[i]);
    for (const auto &[walkMs, site] : sites) {
        Option going;
        int arrives = p.facing;
        if (walkMs > 0) {
            // The drops on the way are landed after the steps.
            const double falls = s.nav.fallsAlong(s.from.cellsTo(site.u, site.v));
            if (!walkTo(s, s.from, site, 0, s.busyMs + falls, going))
                continue;
            arrives = Motor::facingAfter(p.facing, s.from.cellsTo(site.u, site.v));
        } else {
            going.schedule.push_back({0, site});
            going.cost = s.busyMs;
        }
        for (int facing = 1; facing <= 4; facing++) {   // the four directions
            const int turns = (facing - arrives + 4) % 4;
            const double turnMs = (turns == 3 ? 1 : turns) * p.stepMs;
            for (const double delay : DELAYS) {
                const double there = going.cost;   // when it is there, the move under way included
                const double ready = std::max(there + turnMs + delay, p.bombReadyAt - view.now);
                const double dropIn = ready + view.tickMs;
                Option before = going;
                char buf[96];
                snprintf(buf, sizeof(buf), "ambush: bomb at (%d,%d) facing %d after %.0f ms", site.u,
                         site.v, facing, delay);
                before.what = buf;
                // Always: the arrival facing is a prediction, and a turn to the
                // facing it already has is no turn.
                before.script.push_back({ScriptStep::Turn, facing, {}, 0});
                if (ready > there + turnMs + 1)
                    before.script.push_back({ScriptStep::Wait, 0, {}, view.now + ready});
                before.schedule.push_back({dropIn, site});
                const size_t first = out.size();
                retreats(s, before, site, facing, dropIn, true, 1, AMBUSH_RETREATS, out);
                // Not a kill that wipes a gem the quota needs.
                out.erase(std::remove_if(out.begin() + first, out.end(),
                                         [&](const Option &o) { return gemsWiped(view, o.killed) > spare; }),
                          out.end());
                for (size_t i = first; i < out.size(); i++) {
                    char more[48];
                    snprintf(more, sizeof(more), ", %d foe%s", out[i].kills,
                             out[i].kills == 1 ? "" : "s");
                    out[i].what += more;
                }
            }
        }
    }
}

void offerBlast(Situation &s, Place site, std::vector<Option> &out)
{
    const BotView &view = s.view;
    const PlayerView &p = view.player;
    Option before;
    char buf[64];
    snprintf(buf, sizeof(buf), "blast from (%d,%d) facing %d", site.u, site.v, site.facing);
    before.what = buf;
    // There first, then the turn.
    if (!walkTo(s, s.from, {site.u, site.v}, 0, 0, before))
        return;
    const int arrives = Motor::facingAfter(p.facing, s.from.cellsTo(site.u, site.v));
    const int turns = (site.facing - arrives + 4) % 4;
    const double turnMs = (turns == 3 ? 1 : turns) * p.stepMs;
    before.script.push_back({ScriptStep::Turn, site.facing, {}, 0});
    double dropIn = std::max(before.cost + turnMs, p.bombReadyAt - view.now) + view.tickMs;
    const int h = view.cell(site.u, site.v).height;
    // Carried off (bombmodel.h), where it blows hangs on when: the first
    // spell of drops that spends a block, at its start.
    const std::vector<DropSpell> spells =
        dropSpells(view, site.u, site.v, h, site.facing, view.now + dropIn);
    if (spells.empty())
        return;
    if (!std::isinf(spells.back().to)) {
        const auto spell = std::find_if(spells.begin(), spells.end(), [&](const DropSpell &d) {
            return spendsBlock(view, d.blast);
        });
        if (spell == spells.end())
            return;
        if (spell->from > view.now + dropIn) {
            dropIn = spell->from - view.now;
            before.script.push_back({ScriptStep::Wait, 0, {}, view.now + dropIn - view.tickMs});
        }
    }
    before.schedule.push_back({dropIn, {site.u, site.v}});
    retreats(s, before, {site.u, site.v}, site.facing, dropIn, true, 0, RETREATS, out);
}

}  // namespace bot
