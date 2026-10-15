/* Blast(s): with a bomb, from a site s whose blast spends a block in the way
 * (or, short of bombs, one in the way to a pickup): the blocks spent (the
 * world changes: what is reachable, what gems they hide), a bomb less, at s.
 * Done as a walk to the site, a turn, a drop, a walk clear and a wait for the
 * blast to be out (drops.h). */
#include "action.h"
#include <algorithm>
#include <map>
#include "drops.h"

namespace bot {

namespace {

/* What a blast takes after the walk and the turn to the site: the fuse, and
 * the blast's clearing. */
const double BLAST_MS = 2600;
/* A blast that spends all of another's blocks and more is taken in its stead
 * if it costs no more than this much longer. */
const double COVER_MS = 1000;
/* The blasts offered from a place: the cheapest few sets of blocks. */
const size_t BLASTS = 24;

class BlastAction : public Action {
public:
    const char *name() const override { return "blast"; }

    /* The cheapest site for each set of blocks it would spend, the cheapest
     * few sets. */
    void expand(const Facts &f, const Board &board, const PlanOptions &options,
                std::vector<Successor> &out) const override
    {
        if (f.bombs <= 0 || (f.changes & board.allBlocks()) == board.allBlocks())
            return;
        std::map<Changes, std::pair<double, Site>> best;
        // Only a blast that spends a block in the way is worth a bomb (and,
        // short of bombs, one in the way to a pickup).
        // When none of those can be reached yet (the way in the most open
        // world runs deeper into the blocks than the player can come), any
        // block that can be is.
        const Changes inTheWay = board.inTheWay(f.changes, f.at, f.bombs <= 1);
        for (const Changes wanted : {inTheWay, ~(Changes)0}) {
            for (const Site &site : board.sites(f.changes)) {
                if (!(site.hits & wanted))
                    continue;
                const double t = board.time(f.changes, f.at, {site.cell.u, site.cell.v});
                if (t < 0)
                    continue;
                const double cost = t + board.turnMs(f.at, site.cell.facing) + BLAST_MS;
                auto it = best.find(site.hits);
                if (it == best.end() || cost < it->second.first)
                    best[site.hits] = {cost, site};
            }
            if (!best.empty())
                break;
        }
        // Leave out a set that another covers, and more, for little more.
        std::vector<std::pair<double, Site>> sorted;
        for (const auto &[hits, c] : best) {
            bool covered = false;
            for (const auto &[other, o] : best)
                covered = covered || (other != hits && (other & hits) == hits &&
                                      o.first <= c.first + COVER_MS);
            if (!covered)
                sorted.push_back(c);
        }
        std::stable_sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        for (size_t i = 0; i < sorted.size() && i < BLASTS; i++) {
            const auto &[cost, site] = sorted[i];
            Successor n{f, {}};
            n.facts.changes |= site.hits;
            if (!board.worlds().has(n.facts.changes) && board.worlds().size() > options.maxWorlds)
                continue;
            n.facts.bombs--;
            n.facts.unused = false;
            n.facts.at = site.cell;
            n.step.end   = n.facts.at;
            n.step.heads = {site.cell.u, site.cell.v};
            n.step.ms    = cost;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &, const Plan &plan,
               std::vector<Option> &out) const override
    {
        offerBlast(s, plan.steps.front().end, out);
    }
};

}  // namespace

const Action &blastAction()
{
    static const BlastAction action;
    return action;
}

}  // namespace bot
