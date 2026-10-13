#include "goap.h"
#include <map>
#include <queue>
#include <tuple>

namespace bot {

namespace {

/* What an ambush takes: turning, the drop, the walk away and the fuse. */
const double AMBUSH_MS = 2500;
/* The bombs a pickup gives (player.cpp). */
const int PICKUP_BOMBS = 3;

struct Facts {
    int      at = Board::PLAYER;
    int      gemsNeeded = 0;
    int      bombs = 0;
    int      foes = 0;
    unsigned pickups = 0;   // a bit per board pickup taken (the first 32)
    bool     unused = false; // bombs taken that the plan has not used yet
    bool     atExit = false;

    auto key() const
    {
        return std::make_tuple(at, gemsNeeded, bombs, foes, pickups, unused, atExit);
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
            if (node.facts.atExit) {
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
    /* The least the rest can cost: the quota's route and the exit. */
    double heuristic(const Facts &f) const
    {
        if (f.atExit)
            return 0;
        if (f.gemsNeeded > 0) {
            const GemRoute &r = board_.gemRoute(f.at);
            return r.found ? r.toLast + r.toExit : -1;
        }
        return board_.time(f.at, board_.exit());
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
        step.kind = kind;
        step.risk = node.facts.foes * step.ms / 1000.0;
        push(next, node.g + step.ms + options_.mu * step.risk, from, step);
    }

    void expand(int at)
    {
        const Facts f = nodes_[at].facts;
        if (f.gemsNeeded > 0) {
            const GemRoute &r = board_.gemRoute(f.at);
            if (r.found) {
                Facts n = f;
                n.gemsNeeded = 0;
                n.at = r.gems.back();
                PlanStep s;
                s.point = n.at;
                s.gems  = r.gems;
                s.ms    = r.toLast;
                add(at, COLLECT, n, s);
            }
        } else if (!f.unused) {
            const double t = board_.time(f.at, board_.exit());
            if (t >= 0) {
                Facts n = f;
                n.atExit = true;
                n.at = board_.exit();
                PlanStep s;
                s.point = n.at;
                s.ms    = t;
                add(at, EXIT, n, s);
            }
        }
        if (f.bombs == 0 && f.foes > 0) {
            const std::vector<int> &pickups = board_.pickups();
            for (size_t i = 0; i < pickups.size() && i < 32; i++) {
                const double t = board_.time(f.at, pickups[i]);
                if ((f.pickups & (1u << i)) || t < 0)
                    continue;
                Facts n = f;
                n.pickups |= 1u << i;
                n.bombs += PICKUP_BOMBS;
                n.unused = true;
                n.at = pickups[i];
                PlanStep s;
                s.point = n.at;
                s.ms    = t;
                add(at, BOMBS, n, s);
            }
        }
        if (f.bombs > 0 && f.foes > 0) {
            Facts n = f;
            n.bombs--;
            n.foes--;
            n.unused = false;
            PlanStep s;
            s.point = f.at;
            s.ms    = AMBUSH_MS;
            add(at, AMBUSH, n, s);
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
    case AMBUSH:  return "ambush";
    case EXIT:    return "exit";
    default:      return "?";
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
