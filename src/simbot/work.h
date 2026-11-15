/* Work: what the bot may do in a frame's spare time, counted in units of
 * play: a tick played on a copy of the World costs 1, and 15 more for each
 * foe in it (their searches are most of a tick); a planner's state costs
 * PLAN_NODE_WORK.  Counted, never timed, so a run goes the same every time.
 * FRAME_WORK is about 8 ms on the machines measured. */
#pragma once

class World;

namespace simbot {

const long FRAME_WORK     = 16000;
const long PLAN_NODE_WORK = 50;

struct Work {
    long left = FRAME_WORK;
    long used = 0;
    bool out() const { return left <= 0; }
    void spend(long n) { left -= n; used += n; }
};

/* What a tick of w costs. */
long tickCost(const World &w);

}  // namespace simbot
