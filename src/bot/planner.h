/* The planner policy: the gem quota, then the exit, by whatever the actions
 * (actions.h) can offer that is safe.
 *
 * From rest it asks every action for its options, checks each against the
 * foes and bombs (danger.h), and takes the cheapest safe option of the lowest
 * tier that has one: progress before an ambush, an ambush before fetching
 * bombs, those before a refuge.  The executor (script.h) runs its script.
 * It decides again when the gems or bombs change, the script ends or fails,
 * or (with nothing to do) the terrain changes; while a script runs, the rest
 * of its schedule is checked again every quarter second and the script
 * stopped if it has turned unsafe.  Its note (policy.h) is the decision it
 * last made, or why it has none.
 *
 * KAROO_BOT_DECISIONS=<file> logs every decision, one JSON object a line:
 * the player's state, each tier's options offered and found safe, and the
 * one taken. */
#pragma once

#include <fstream>
#include <string>
#include "actions.h"
#include "foemodel.h"
#include "policy.h"
#include "script.h"

namespace bot {

class Planner : public Policy {
public:
    Planner();
    unsigned act(const BotView &view) override;
    std::string note() const override { return note_; }

private:
    void decide(const BotView &view);
    bool stillSafe(const BotView &view);

    Executor  exec_;
    FoeModel  foes_;
    Distances distances_;
    Option    chosen_;
    double    chosenAt_ = 0;
    bool      decided_ = false;
    bool      stuck_ = false;        // no route: wait for the terrain to change
    unsigned long long terrain_ = 0;
    int       gems_ = -1, bombs_ = -1;
    unsigned long ticks_ = 0;
    std::string note_;
    std::ofstream log_;
};

}  // namespace bot
