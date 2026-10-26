#include "drops.h"
#include <algorithm>
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include "danger.h"
#include "rules.h"
#include "walk.h"

namespace bot {

namespace {

/* The cells to walk away to: the nearest RETREATS (walk.h). */
const size_t RETREATS = 16;
/* An ambush: the drop sites within this many ms of the player (the nearest
 * SITES of them, and where it stands), the waits before the drop, the few
 * cells to retreat to. */
const double SITE_MS = 1500;
const size_t SITES = 6;
/* Where a bomb must take several foes, the leads to the drop: walks to the
 * LEADS most open cells within LEAD_MS (OPEN_REACH), and back, the drop after
 * one of LEAD_DELAYS; and what each
 * foe killed past the first is worth, in ms, so that the most are taken. */
const double LEAD_MS = 15000;
const size_t LEADS = 4;
const double LEAD_DELAYS[] = {0, 500};
const int    OPEN_REACH = 2;
const double KILL_MS = 4000;
const double DELAYS[] = {0, 250, 500, 750, 1000, 1250, 1500, 2000};
const size_t AMBUSH_RETREATS = 2;
/* A kill is counted on only if the foe dies with the blast this much earlier
 * or later, too: the foe model's timing is good to about that, and a foe
 * caught only on its way past the blast's edge is not caught at all if it
 * is out. */
const double SURE_MS = 250;
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
/* A bomb pickup's bombs (CONTENTS_GRANT_09, tile.h), and a gem's contents. */
const int BOMB_PICKUP = 0x09;
const int PICKUP_BOMBS = 3;
const int GEM = 0x01;

/* The bomb budget: the bombs held and still to be picked up, and the
 * carriers that must die for the gems the quota needs beyond those lying
 * about (hidden ones too). */
struct Budget {
    int bombs = 0, carriers = 0;
};
Budget budget(const BotView &view)
{
    Budget b;
    b.bombs = view.player.bombs;
    int lying = 0;
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const Cell &c = view.cell(u, v);
            b.bombs += PICKUP_BOMBS * ((c.contents == BOMB_PICKUP) + (c.hidden == BOMB_PICKUP));
            lying += (c.contents == GEM) + (c.hidden == GEM);
        }
    b.carriers = std::max(0, view.gemsRequired - view.player.gems - lying);
    return b;
}

/* The foes an ambush must kill: FARM_KILLS where the gems are farmed, as
 * many as the spawners let be about at once.  Where carriers must die and
 * the bombs are fewer than the foes, enough a bomb to have them all: the
 * chasers bunch up, and one bomb takes several.  Else one. */
int killsWanted(const BotView &view)
{
    if (gemsFromSpawners(view)) {
        int most = 1;
        for (const SpawnerView &t : view.spawners)
            most = std::max(most, t.maxFoes);
        return std::min(FARM_KILLS, most);
    }
    const Budget b = budget(view);
    int foes = 0;
    for (const FoeView &f : view.foes)
        foes += !f.dying;
    if (b.carriers <= 0 || b.bombs <= 0 || b.bombs >= foes)
        return 1;
    return (foes + b.bombs - 1) / b.bombs;
}

/* Whether an ambush killing these leaves bombs enough for the carriers
 * still to kill after it. */
bool affordable(const BotView &view, const std::vector<FoeModel::Kill> &kills)
{
    const Budget b = budget(view);
    int carriers = 0;
    for (const FoeModel::Kill &k : kills)
        carriers += k.id < view.foes.size() && view.foes[k.id].drops == GEM;
    return b.bombs - 1 >= b.carriers - carriers;
}

/* The LEADS most open cells within LEAD_MS (the cells within OPEN_REACH on
 * their level, a step up or down), the nearer first where they tie: where
 * chasers led there come up side by side, not in single file, so one blast
 * takes several. */
Near openCells(const Situation &s)
{
    const BotView &view = s.view;
    std::vector<std::pair<int, std::pair<double, Step>>> scored;
    for (const auto &n : nearCells(view, s.from, LEAD_MS, (size_t)-1)) {
        const Step c = n.second;
        const int h = view.cell(c.u, c.v).height;
        int open = 0;
        for (int v = c.v - OPEN_REACH; v <= c.v + OPEN_REACH; v++)
            for (int u = c.u - OPEN_REACH; u <= c.u + OPEN_REACH; u++)
                open += view.inside(u, v) && s.from.cost(u, v) >= 0 && abs(view.cell(u, v).height - h) <= 1;
        scored.push_back({open, n});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    Near out;
    for (size_t i = 0; i < scored.size() && i < LEADS; i++)
        out.push_back(scored[i].second);
    return out;
}

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
    const RoutesOn away(s.nav, before.schedule, site, facing, awayMs, Bound{RETREAT_MS});
    for (const Step &at : retreatCells(view, away.paths(), most)) {
        Option o = before;
        o.script.push_back({ScriptStep::Drop, 0, {}, 0});
        Option walk;
        if (!walkTo(s, away.paths(), at, 0, walk))
            continue;
        append(o, walk);
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
    // None to spare is none, however short the gems are.
    const int spare = std::max(0, spareGems(view));
    const int need = killsWanted(view);
    // Where to drop it: here, or a cell or two off, so that the foe on its
    // way is in the blast as it goes; and, where a bomb must take several,
    // at the end of a lead into open ground (openCells), or there and back,
    // the chasers bunching up behind the player.
    struct Lead {
        Step   site;
        Option going;
        bool   walked;
        bool   led = false;   // a lead (LEAD_DELAYS)
    };
    std::vector<Lead> leads;
    {
        Lead here{{p.u, p.v}, Option(), false};
        here.going.schedule.push_back({0, here.site});
        here.going.cost = s.busyMs;
        leads.push_back(std::move(here));
    }
    for (const auto &[ms, c] : nearCells(view, s.from, SITE_MS, SITES)) {
        Lead l{c, Option(), true};
        if (walkTo(s, s.from, c, 0, l.going))
            leads.push_back(std::move(l));
    }
    if (need > 1)
        for (const auto &[ms, c] : openCells(s)) {
            Lead off{c, Option(), true, true};
            if (!walkTo(s, s.from, c, 0, off.going))
                continue;
            Lead back{{p.u, p.v}, off.going, true, true};
            leads.push_back(std::move(off));
            if (walkOn(s, s.nav, back.going, c, back.site))
                leads.push_back(std::move(back));
        }
    for (const Lead &lead : leads) {
        const Step site = lead.site;
        const Option &going = lead.going;
        const bool walked = lead.walked;
        const std::vector<double> delays = lead.led ? std::vector<double>(std::begin(LEAD_DELAYS), std::end(LEAD_DELAYS))
                                                    : std::vector<double>(std::begin(DELAYS), std::end(DELAYS));
        const int arrives = walked ? facingAfter(p.facing, going.script) : p.facing;
        for (int facing = 1; facing <= 4; facing++) {   // the four directions
            const int turns = (facing - arrives + 4) % 4;
            const double turnMs = (turns == 3 ? 1 : turns) * p.stepMs;
            for (const double delay : delays) {
                const double there = going.cost;   // when it is there, the move under way included
                // Turns without a walk start on the first move's tick.
                const double turned = (walked || turns == 0 ? there : s.from.startMs()) + turnMs;
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
                                         [&](const Option &o) {
                                             return gemsWiped(view, o.killed) > spare || !affordable(view, o.killed);
                                         }),
                          out.end());
                for (size_t i = first; i < out.size(); i++) {
                    out[i].cost += cramped(s, out[i].schedule.back().cell) -
                                   KILL_MS * std::max(0, out[i].kills - 1) * (need > 1);
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

bool bombsShort(const BotView &view)
{
    return !gemsFromSpawners(view) && killsWanted(view) > 1;
}

}  // namespace bot
