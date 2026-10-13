#include "liftmodel.h"

namespace bot {

namespace {

const double HEIGHT_MS = 200.0;   // a height's rise or fall
const double DWELL_MS  = 1500.0;  // a park

}  // namespace

std::vector<Park> parkSpells(const BotView &view, const LiftView &l, double horizonMs)
{
    std::vector<Park> out;
    // A move takes its heights and a tick: the lift notices it has arrived on
    // the tick after.
    const double move = (l.top - l.base) * HEIGHT_MS + view.tickMs;
    double at = l.phaseStart - view.now;
    int    state = l.state;
    bool   top = l.atTop;
    for (int guard = 0; at < horizonMs && guard < 1000; guard++) {
        if (state == 0) {
            // Parked from at; it leaves a dwell later (the tick after).
            out.push_back({at, at + DWELL_MS, top ? l.top : l.base});
            at += DWELL_MS + view.tickMs;
            state = top ? 2 : 1;
        } else {
            at += move;
            top = state == 1;
            state = 0;
        }
    }
    return out;
}

void platformSpells(const BotView &view, const PlatformView &p, double horizonMs,
                    std::vector<Park> *atOrigin, std::vector<Park> *atLimit)
{
    const int span = p.limit - (p.alongU ? p.originU : p.originV);
    const double move = span * HEIGHT_MS + view.tickMs;   // a cell takes as long as a height
    double at = p.phaseStart - view.now;
    int    state = p.state;
    bool   limit = p.atLimit;
    for (int guard = 0; at < horizonMs && guard < 1000; guard++) {
        if (state == 0) {
            (limit ? atLimit : atOrigin)->push_back({at, at + DWELL_MS, p.height});
            at += DWELL_MS + view.tickMs;
            state = limit ? 2 : 1;
        } else {
            at += move;
            limit = state == 1;
            state = 0;
        }
    }
}

}  // namespace bot
