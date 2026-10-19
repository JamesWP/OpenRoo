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
    double   ms = 0;     // time, of g
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

    /* The facts as they stand. */
    Facts now() const
    {
        Facts start;
        start.at         = board_.start();
        start.gemsNeeded = board_.gemsNeeded();
        start.bombs      = board_.bombs();
        start.glides     = board_.live().player.glides;
        start.foes       = board_.foes();
        start.carriers   = board_.carriers();
        return start;
    }

    Plan run() { return run(now()); }

    Plan run(const Facts &start)
    {
        push(start, 0, 0, -1, PlanStep());
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
                plan.ms    = node.ms;
                for (int i = at; nodes_[i].parent >= 0; i = nodes_[i].parent)
                    plan.steps.insert(plan.steps.begin(), nodes_[i].step);
                return plan;
            }
            expand(at);
        }
        return plan;
    }

private:
    bool reached(const Facts &f)
    {
        if (options_.goal != REMOVE_FOE)
            return f.atExit;
        return f.foes < board_.foes() && finishable(f);
    }

    /* Whether the level can still be finished after f, if it can be now: a
     * foe is not removed with a bomb the way to the exit needs. */
    bool finishable(const Facts &f)
    {
        PlanOptions finish = options_;
        finish.goal    = FINISH;
        finish.startBy = -1;
        finish.root.clear();
        if (finishableNow_ < 0)
            finishableNow_ = Search(board_, actions_, finish).run().found;
        return !finishableNow_ || Search(board_, actions_, finish).run(f).found;
    }

    /* The least the rest can cost: for finishing, the route through the gems
     * the carriers cannot give and the exit, in the most open world; a foe
     * could be had at once. */
    double heuristic(const Facts &f) const
    {
        if (options_.goal == REMOVE_FOE)
            return 0;
        if (f.atExit)
            return 0;
        // The carriers' gems could stand in for the map's.
        const int fromMap = f.gemsNeeded - f.carriers;
        if (fromMap > 0) {
            const GemRoute &r = board_.gemRoute(board_.everything(), f.at, fromMap, f.taken);
            return r.found ? r.toLast + r.toExit : -1;
        }
        return board_.time(board_.everything(), f.at, board_.exit());
    }

    void push(const Facts &f, double g, double ms, int parent, const PlanStep &step)
    {
        auto it = best_.find(f);
        if (it != best_.end() && it->second <= g)
            return;
        const double h = heuristic(f);
        if (h < 0)
            return;  // the goal cannot be reached from here
        if (options_.timed && ms + h > board_.timeLeft(~0u))
            return;  // not in time, with every time bonus
        best_[f] = g;
        nodes_.push_back({f, g, ms, parent, step});
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
        const double ms = node.ms;
        const bool root = node.parent < 0;
        const double left = options_.timed ? board_.timeLeft(f.bonuses) - ms : -1;
        std::vector<PlanAction::Successor> next;
        for (size_t i = 0; i < actions_.size(); i++) {
            next.clear();
            actions_[i]->expand(f, left, board_, options_, next);
            for (PlanAction::Successor &n : next) {
                if (root && !known((int)i, n.step))
                    continue;
                n.step.action = (int)i;
                n.step.name   = actions_[i]->name();
                n.step.risk   = f.foes * n.step.ms / 1000.0;
                if (options_.timed && ms + n.step.ms > board_.timeLeft(n.facts.bonuses))
                    continue;  // out of time
                push(n.facts, g + n.step.ms + options_.mu * n.step.risk, ms + n.step.ms, at,
                     n.step);
            }
        }
    }

    /* A first step as found out, if it was: false if it cannot be had now;
     * its ms the real one if known. */
    bool known(int action, PlanStep &step) const
    {
        for (const PlanOptions::Known &k : options_.root)
            if (k.action == action && !(k.end < step.end) && !(step.end < k.end)) {
                if (k.ms < 0)
                    return false;
                step.ms = k.ms;
            }
        return true;
    }

    const Board       &board_;
    const std::vector<const PlanAction *> &actions_;
    const PlanOptions &options_;
    int                finishableNow_ = -1;   // unknown yet
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
    const Plan plan = Search(board, actions, options).run();
    const double left = board.timeLeft(0);
    if (!plan.found || options.goal != FINISH || options.timed || left < 0 || plan.ms <= left)
        return plan;
    PlanOptions timed = options;
    timed.timed = true;
    const Plan inTime = Search(board, actions, timed).run();
    return inTime.found ? inTime : plan;
}

}  // namespace bot
