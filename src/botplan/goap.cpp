#include "goap.h"
#include <algorithm>
#include <map>
#include <queue>
#include <tuple>

namespace bot {

namespace {

/* What an ambush takes: turning, the drop, the walk away and the fuse. */
const double AMBUSH_MS = 2500;
/* What a blast takes after the walk and the turn to the site: the fuse, and
 * the blast's clearing. */
const double BLAST_MS = 2600;
/* A blast that spends all of another's blocks and more is taken in its stead
 * if it costs no more than this much longer. */
const double COVER_MS = 1000;
/* The blasts offered from a place: the cheapest few sets of blocks. */
const size_t BLASTS = 24;
/* The bombs a pickup gives (player.cpp). */
const int PICKUP_BOMBS = 3;

struct Facts {
    Place    at;
    Spent    spent = 0;
    int      gemsNeeded = 0;
    int      bombs = 0;
    int      foes = 0;
    unsigned pickups = 0;    // a bit per board pickup taken (the first 32)
    bool     unused = false; // bombs taken that the plan has not used yet
    bool     atExit = false;

    auto key() const
    {
        return std::make_tuple(at.u, at.v, at.facing, spent, gemsNeeded, bombs, foes, pickups,
                               unused, atExit);
    }
    bool operator<(const Facts &o) const { return key() < o.key(); }
};

struct Node {
    Facts    facts;
    double   g = 0;
    int      parent = -1;
    PlanStep step;
};

class Search {
public:
    Search(const Board &board, const PlanOptions &options) : board_(board), options_(options) {}

    Plan run()
    {
        Facts start;
        start.at         = board_.start();
        start.gemsNeeded = board_.gemsNeeded();
        start.bombs      = board_.bombs();
        start.foes       = board_.foes();
        push(start, 0, -1, PlanStep());
        Plan plan;
        while (!open_.empty() && plan.nodes < options_.maxNodes) {
            const auto [f, g, at] = open_.top();
            open_.pop();
            const Node node = nodes_[at];
            if (g > best_[node.facts] + 1e-9)
                continue;  // reached more cheaply since
            plan.nodes++;
            if (reached(node.facts)) {
                plan.found = true;
                plan.cost  = node.g;
                for (int i = at; nodes_[i].parent >= 0; i = nodes_[i].parent)
                    plan.steps.insert(plan.steps.begin(), nodes_[i].step);
                return plan;
            }
            expand(at);
        }
        return plan;
    }

private:
    bool reached(const Facts &f) const
    {
        return options_.goal == REMOVE_FOE ? f.foes < board_.foes() : f.atExit;
    }

    /* The least the rest can cost: for finishing, the quota's route and the
     * exit, with every block spent; a foe could be had at once. */
    double heuristic(const Facts &f) const
    {
        if (options_.goal == REMOVE_FOE)
            return 0;
        if (f.atExit)
            return 0;
        if (f.gemsNeeded > 0) {
            const GemRoute &r = board_.gemRoute(board_.everything(), f.at);
            return r.found ? r.toLast + r.toExit : -1;
        }
        return board_.time(board_.everything(), f.at, board_.exit());
    }

    void push(const Facts &f, double g, int parent, const PlanStep &step)
    {
        auto it = best_.find(f);
        if (it != best_.end() && it->second <= g)
            return;
        const double h = heuristic(f);
        if (h < 0)
            return;  // the goal cannot be reached from here
        best_[f] = g;
        nodes_.push_back({f, g, parent, step});
        // Ties go to the node pushed first, so the search is the same each run.
        open_.push({g + h, g, (int)nodes_.size() - 1});
    }

    void add(int from, ActionKind kind, Facts next, PlanStep step)
    {
        const Node &node = nodes_[from];
        if (node.parent < 0 && (options_.notFirst & (1u << kind)))
            return;
        if (options_.startBy >= 0 && node.g > options_.startBy)
            return;  // too late
        step.kind = kind;
        step.risk = node.facts.foes * step.ms / 1000.0;
        push(next, node.g + step.ms + options_.mu * step.risk, from, step);
    }

    void expand(int at)
    {
        const Facts f = nodes_[at].facts;
        if (f.gemsNeeded > 0) {
            const GemRoute &r = board_.gemRoute(f.spent, f.at);
            if (r.found) {
                Facts n = f;
                n.gemsNeeded = 0;
                n.at = {r.gems.back().u, r.gems.back().v, 0};
                PlanStep s;
                s.goes  = true;
                s.end   = n.at;
                s.gems  = r.gems;
                s.ms    = r.toLast;
                add(at, COLLECT, n, s);
            }
        } else if (!f.unused) {
            const double t = board_.time(f.spent, f.at, board_.exit());
            if (t >= 0) {
                Facts n = f;
                n.atExit = true;
                n.at = {board_.exit().u, board_.exit().v, 0};
                PlanStep s;
                s.goes = true;
                s.end  = n.at;
                s.ms   = t;
                add(at, EXIT, n, s);
            }
        }
        const bool blocksLeft = f.spent != board_.everything();
        if (f.bombs == 0 && (f.foes > 0 || blocksLeft)) {
            const std::vector<Pickup> &pickups = board_.pickups();
            for (size_t i = 0; i < pickups.size() && i < 32; i++) {
                if ((f.pickups & (1u << i)) || !Board::available(pickups[i], f.spent))
                    continue;
                const double t = board_.time(f.spent, f.at, pickups[i].cell);
                if (t < 0)
                    continue;
                Facts n = f;
                n.pickups |= 1u << i;
                n.bombs += PICKUP_BOMBS;
                n.unused = true;
                n.at = {pickups[i].cell.u, pickups[i].cell.v, 0};
                PlanStep s;
                s.goes = true;
                s.end  = n.at;
                s.ms   = t;
                add(at, BOMBS, n, s);
            }
        }
        if (f.bombs > 0 && blocksLeft)
            expandBlasts(at, f);
        if (f.bombs > 0 && f.foes > 0) {
            Facts n = f;
            n.bombs--;
            n.foes--;
            n.unused = false;
            PlanStep s;
            s.ms = AMBUSH_MS;
            add(at, AMBUSH, n, s);
        }
    }

    /* The cheapest site for each set of blocks it would spend, the cheapest
     * few sets. */
    void expandBlasts(int at, const Facts &f)
    {
        std::map<Spent, std::pair<double, Site>> best;
        // Only a blast that spends a block in the way is worth a bomb (and,
        // short of bombs, one in the way to a pickup).
        const Spent wanted = board_.inTheWay(f.spent, f.at, f.bombs <= 1);
        for (const Site &site : board_.sites(f.spent)) {
            if (!(site.hits & wanted))
                continue;
            const double t = board_.time(f.spent, f.at, {site.cell.u, site.cell.v});
            if (t < 0)
                continue;
            const double cost = t + board_.turnMs(f.at, site.cell.facing) + BLAST_MS;
            auto it = best.find(site.hits);
            if (it == best.end() || cost < it->second.first)
                best[site.hits] = {cost, site};
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
            Facts n = f;
            n.spent |= site.hits;
            if (!board_.worlds().has(n.spent) && board_.worlds().size() > options_.maxWorlds)
                continue;
            n.bombs--;
            n.unused = false;
            n.at = site.cell;
            PlanStep s;
            s.goes = true;
            s.end  = n.at;
            s.hits = site.hits;
            s.ms   = cost;
            add(at, BLAST, n, s);
        }
    }

    const Board       &board_;
    const PlanOptions &options_;
    std::vector<Node>  nodes_;
    std::map<Facts, double> best_;
    // (f, g, node): the least f first, then the node pushed first.
    struct Order {
        bool operator()(const std::tuple<double, double, int> &a,
                        const std::tuple<double, double, int> &b) const
        {
            if (std::get<0>(a) != std::get<0>(b))
                return std::get<0>(a) > std::get<0>(b);
            return std::get<2>(a) > std::get<2>(b);
        }
    };
    std::priority_queue<std::tuple<double, double, int>,
                        std::vector<std::tuple<double, double, int>>, Order> open_;
};

}  // namespace

const char *actionName(ActionKind kind)
{
    switch (kind) {
    case COLLECT: return "collect";
    case BOMBS:   return "bombs";
    case BLAST:   return "blast";
    case AMBUSH:  return "ambush";
    case EXIT:    return "exit";
    default:      return "?";
    }
}

const char *goalName(GoalKind goal)
{
    switch (goal) {
    case FINISH:     return "finish";
    case REMOVE_FOE: return "remove foe";
    default:         return "?";
    }
}

std::string Plan::describe() const
{
    std::string out;
    for (const PlanStep &s : steps) {
        if (!out.empty())
            out += ", ";
        out += actionName(s.kind);
    }
    return out;
}

Plan planGoal(const Board &board, const PlanOptions &options)
{
    return Search(board, options).run();
}

}  // namespace bot
