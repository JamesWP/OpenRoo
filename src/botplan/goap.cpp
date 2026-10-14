#include "goap.h"
#include <algorithm>
#include <map>
#include <queue>
#include <tuple>

namespace bot {

namespace {

struct Node {
    Facts    facts;
    double   g = 0;
    int      parent = -1;
    PlanStep step;
};

class Search {
public:
    Search(const Board &board, const std::vector<const PlanAction *> &actions,
           const PlanOptions &options)
        : board_(board), actions_(actions), options_(options)
    {
    }

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

    void expand(int at)
    {
        const Node &node = nodes_[at];
        if (options_.startBy >= 0 && node.g > options_.startBy)
            return;  // too late for any step
        const Facts f = node.facts;
        const double g = node.g;
        const bool root = node.parent < 0;
        std::vector<PlanAction::Successor> next;
        for (size_t i = 0; i < actions_.size(); i++) {
            if (root && (options_.notFirst & (1u << i)))
                continue;
            next.clear();
            actions_[i]->expand(f, board_, options_, next);
            for (PlanAction::Successor &n : next) {
                n.step.action = (int)i;
                n.step.name   = actions_[i]->name();
                n.step.risk   = f.foes * n.step.ms / 1000.0;
                push(n.facts, g + n.step.ms + options_.mu * n.step.risk, at, n.step);
            }
        }
    }

    const Board       &board_;
    const std::vector<const PlanAction *> &actions_;
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
        out += s.name;
    }
    return out;
}

Plan planGoal(const Board &board, const std::vector<const PlanAction *> &actions,
              const PlanOptions &options)
{
    return Search(board, actions, options).run();
}

}  // namespace bot
