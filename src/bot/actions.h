/* The actions: each proposes ways to do one thing, as options the planner
 * (planner.h) weighs.  An option is a script (script.h), the player's place
 * over time while it runs (a schedule, nav.h), the blasts of any bombs it
 * drops, a cost in ms and a tier.  Actions do not judge safety: the planner
 * checks every option against the foes and bombs (danger.h) before it takes
 * one.  The tiers order the kinds of option; the planner takes the cheapest
 * safe option of the lowest tier that has one.
 *
 *   progress  the route's next gem, the other gems, the exit once the quota
 *             is in; each also after a wait, so a foe can pass (tier 0);
 *   ambush    turn, drop a bomb, walk away: only options whose blast the foe
 *             model says kills a foe, so they are checked here (tier 1);
 *   bombs     walk to a bomb pickup, when out of bombs and foes are about
 *             (tier 2);
 *   advance   part of the way to progress's best target: any cell along the
 *             path, the nearer the target the cheaper, so the farthest safe
 *             one is taken; waiting there for a foe to come into a bomb's
 *             reach is how an ambush becomes possible (tier 3);
 *   refuge    walk to a nearby cell to stand on (tier 4). */
#pragma once

#include <string>
#include <vector>
#include "bombmodel.h"
#include "foemodel.h"
#include "nav.h"
#include "script.h"

namespace bot {

/* The tiers, in the order the planner prefers them. */
enum Tier { PROGRESS, AMBUSH, BOMBS, ADVANCE, REFUGE, TIERS };

struct Option {
    int                tier = PROGRESS;
    std::string        what;          // the decision, for the note and the log
    Script             script;
    std::vector<Timed> schedule;      // ms from now
    std::vector<Blast> blasts;        // of the bombs it drops
    double             cost = 0;      // ms, including the rest of the route after it
    double             lingerMs = 1000;  // how long the end must stay safe
    bool               checked = false;  // the action has checked it already
    int                kills = 0;
};

/* What the actions share for one decision: the view, the nav model and the
 * quickest routes from the player as it stands. */
struct PlanContext {
    PlanContext(const BotView &v, FoeModel &m);

    const BotView &view;
    NavModel       nav;
    Paths          from;
    FoeModel      &foes;
    std::string    stuck;  // set when progress has no route at all
    Step           goal{-1, -1};  // progress's best target, for advance
};

/* The gem routes, kept between decisions while the terrain stands: every
 * gem and the quickest times between them and to the exit. */
class GemSurvey {
public:
    void update(const BotView &view);
    const std::vector<Step> &gems() const { return gems_; }
    double between(size_t i, size_t j) const { return between_[i][j]; }
    double toExit(size_t i) const { return between_[i][gems_.size()]; }

private:
    unsigned long long terrain_ = 0;
    bool               done_ = false;
    std::vector<Step>  gems_;
    std::vector<std::vector<double>> between_;
};

void proposeProgress(PlanContext &ctx, GemSurvey &survey, std::vector<Option> &out);
void proposeAmbush(PlanContext &ctx, std::vector<Option> &out);
void proposeBombs(PlanContext &ctx, std::vector<Option> &out);
void proposeAdvance(PlanContext &ctx, std::vector<Option> &out);
void proposeRefuge(PlanContext &ctx, std::vector<Option> &out);

/* The terrain hash the routes depend on. */
unsigned long long terrainHash(const BotView &view);

}  // namespace bot
