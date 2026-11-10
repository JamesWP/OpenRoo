/* An Action: one kind of step, both its halves.  Planning, it offers the
 * Steps it can take from some facts, each with the facts after and an
 * estimate of its ms (from the survey, never played).  Doing, it makes an
 * Attempt at one of them: a search on copies of the World for the presses
 * that bring its effects about, then the pressing of them, a little each
 * frame (work.h).
 *
 * Adding an Action is one file with both halves, and a line in actions(). */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "facts.h"
#include "work.h"

class World;

namespace simbot {

class Action;
class Survey;

/* A planned step: an Action and what it is done to (a gem's index, say). */
struct Step {
    const Action *action = nullptr;
    int           arg = -1;
    Facts         after;
    double        ms = 0;     // the estimate
    std::string   what;       // "collect g3 (5,4)"
};

/* A Step being done. */
class Attempt {
public:
    enum Status {
        Thinking,    // searching: nothing pressed this frame
        Pressing,    // the presses for this tick are given
        Done,        // its effects are brought about
        Impossible,  // no way found as things stand
    };
    virtual ~Attempt() = default;
    /* This frame's share, the live World as it is before the next tick. */
    virtual Status act(const World &live, Work &work, unsigned *presses) = 0;
    /* Why it ended as it did, for the log; "" if nothing to say. */
    virtual std::string why() const { return ""; }
    /* Impossible, what would make it possible: the foes (Listed ids) that
     * must be dead first.  Empty if nothing is known to. */
    virtual std::vector<unsigned> requiresDead() const { return {}; }
};

class Action {
public:
    virtual ~Action() = default;
    virtual const char *name() const = 0;
    virtual void offer(const Facts &f, const Survey &survey, std::vector<Step> &out) const = 0;
    virtual std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const = 0;
};

/* What walking among the foes still alive is reckoned to cost, when bombs
 * are to be had to kill them: each ms of a step counts this many more (one
 * for each foe), so the planner kills them first where it can. */
double foeRisk(const Facts &f, const Survey &survey);

/* Every Action, in the order the planner offers them. */
const std::vector<const Action *> &actions();

}  // namespace simbot
