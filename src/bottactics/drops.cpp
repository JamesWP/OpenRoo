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
/* The waits a blast's drop is tried after: a fallen tile is back 5 s after
 * it was stepped on (fallingmodel.h). */
const double BLAST_WAITS[] = {0, 2500, 5500};

/* Where the gems are had from foes the spawners make, the foes an ambush
 * kills, at the least: the bombs are too few to spend on one foe each
 * (EnemyFactory's 24 for its 20 gems; the enemyfactory-human recording bombs
 * them in twos to fours, as they bunch up chasing the player). */
const int FARM_KILLS = 2;

/* The foes an ambush must kill: FARM_KILLS where the gems are farmed, as
 * many as the spawners let be about at once; else one. */
int killsWanted(const BotView &view)
{
    if (!gemsFromSpawners(view))
        return 1;
    int most = 1;
    for (const SpawnerView &t : view.spawners)
        most = std::max(most, t.maxFoes);
    return std::min(FARM_KILLS, most);
}

/* The game's grace after a move (player.cpp's stepGrace_). */
const double STEP_GRACE_MS = 20.0;

/* When a bomb drops, ms from now, and when the step away from it starts.
 * The script (script.h) presses it once the last bomb's 2 s are up, not
 * before notBefore (a Wait), and after the move ending endMs from now (none
 * if not above 0): on the tick before that move ends, if onEnd (a turn, or
 * a step onto plain ground), so that it drops on the move's last tick, else
 * at rest after it; a bomb pressed drops on the next tick.  The step away is
 * pressed as it drops and taken on the tick after, starting where the last
 * move ended if that is within the game's grace (playtick.cpp, player.cpp). */
struct DropAt {
    double drop, away;
};
DropAt dropAt(const BotView &view, double endMs, bool onEnd, double notBefore)
{
    const double t = view.tickMs;
    const double ready = view.player.bombReadyAt - view.now;
    if (t <= 0) {
        const double drop = std::max({endMs, ready, notBefore, 0.0});
        return {drop, drop};
    }
    auto tickAt = [t](double x) { return std::ceil(x / t - 1e-6) * t; };  // the first at or after x
    double press = std::max({0.0, ready, notBefore - t});
    if (endMs > 0)
        press = std::max(press, tickAt(endMs) - (onEnd ? t : 0));
    const double drop = tickAt(press) + t;
    const double ended = endMs > 0 ? endMs : view.player.stepEnd - view.now;
    const double since = drop + t - ended;
    return {drop, since > 1e-6 && since < STEP_GRACE_MS ? ended : drop + t};
}

/* Ground a step onto ends on: the script presses a bomb for the step's
 * last tick (script.h). */
bool plain(const Cell &c)
{
    return c.kind == 1;   // TILE_KIND_01 (the start is one too, tile.h)
}

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
 * now, setting off awayMs from now: the option so far (its script and
 * schedule up to the drop) with a retreat added and checked.  wait: stay
 * until the blast has cleared. */
void retreats(Situation &s, const Option &before, Step site, int facing, double dropIn,
              double awayMs, bool wait, int needKills, size_t most, std::vector<Option> &out)
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
    const Paths away(after, site.u, site.v, facing, awayMs);
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
        if (!walkTo(s, away, at, 0, walk))
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
    // None to spare is none, however short the gems are.
    const int spare = std::max(0, spareGems(view));
    const int need = killsWanted(view);
    for (size_t i = 0; i < near.size() && i < SITES; i++)
        sites.push_back(near[i]);
    for (const auto &[walkMs, site] : sites) {
        Option going;
        int arrives = p.facing;
        if (walkMs > 0) {
            if (!walkTo(s, s.from, site, 0, going))
                continue;
            arrives = facingAfter(p.facing, going.script);
        } else {
            going.schedule.push_back({0, site});
            going.cost = s.busyMs;
        }
        for (int facing = 1; facing <= 4; facing++) {   // the four directions
            const int turns = (facing - arrives + 4) % 4;
            const double turnMs = (turns == 3 ? 1 : turns) * p.stepMs;
            for (const double delay : DELAYS) {
                const double there = going.cost;   // when it is there, the move under way included
                // Turns without a walk start on the first move's tick.
                const double turned = (walkMs > 0 || turns == 0 ? there : s.from.startMs()) + turnMs;
                const bool onEnd = turns > 0 || (there > 0 && plain(view.cell(site.u, site.v)));
                const DropAt at = dropAt(view, turned, onEnd, delay > 0 ? turned + delay : 0);
                const double ready = std::max(there + turnMs + delay, p.bombReadyAt - view.now);
                const double dropIn = at.drop;
                Option before = going;
                char buf[96];
                snprintf(buf, sizeof(buf), "ambush: bomb at (%d,%d) facing %d after %.0f ms", site.u,
                         site.v, facing, delay);
                before.what = buf;
                // Always: the arrival facing is a prediction, and a turn to the
                // facing it already has is no turn.
                before.script.push_back({ScriptStep::Turn, facing, {}, 0});
                if (ready > there + turnMs + 1)
                    before.script.push_back({ScriptStep::Wait, 0, {}, view.now + turned + delay});
                before.schedule.push_back({dropIn, site});
                const size_t first = out.size();
                retreats(s, before, site, facing, dropIn, at.away, true, need, AMBUSH_RETREATS, out);
                // Not a kill that wipes a gem the quota needs.
                out.erase(std::remove_if(out.begin() + first, out.end(),
                                         [&](const Option &o) { return gemsWiped(view, o.killed) > spare; }),
                          out.end());
                for (size_t i = first; i < out.size(); i++) {
                    out[i].cost += cramped(s, out[i].schedule.back().cell);
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
    if (!walkTo(s, s.from, {site.u, site.v}, 0, before))
        return;
    const int arrives = facingAfter(p.facing, before.script);
    const int turns = (site.facing - arrives + 4) % 4;
    const double turnMs = (turns == 3 ? 1 : turns) * p.stepMs;
    before.script.push_back({ScriptStep::Turn, site.facing, {}, 0});
    const bool walks = site.u != p.u || site.v != p.v;
    const double endMs = (walks || turns == 0 ? before.cost : s.from.startMs()) + turnMs;
    const bool onEnd = turns > 0 || (before.cost > 0 && plain(view.cell(site.u, site.v)));
    DropAt at = dropAt(view, endMs, onEnd, 0);
    double dropIn = at.drop;
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
            at = dropAt(view, endMs, onEnd, spell->from - view.now);
            before.script.push_back({ScriptStep::Wait, 0, {}, spell->from});
            dropIn = at.drop;
        }
    }
    // Dropped as soon as it is there, or, if there is no way clear then (the
    // falling tiles behind gone), after a wait for them to come back.
    for (const double wait : BLAST_WAITS) {
        if (wait > 0 && !std::isinf(spells.back().to))
            break;  // a carried bomb's drop is timed to its carrier
        Option o = before;
        const DropAt w = wait > 0 ? dropAt(view, endMs, onEnd, dropIn + wait) : at;
        if (wait > 0)
            o.script.push_back({ScriptStep::Wait, 0, {}, view.now + dropIn + wait});
        o.schedule.push_back({w.drop, {site.u, site.v}});
        const size_t had = out.size();
        retreats(s, o, {site.u, site.v}, site.facing, w.drop, w.away, true, 0, RETREATS, out);
        if (out.size() > had)
            break;
    }
}

}  // namespace bot
