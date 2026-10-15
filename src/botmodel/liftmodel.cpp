#include "liftmodel.h"
#include <algorithm>
#include <cmath>

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

const PlatformView *platformThrough(const BotView &view, int u, int v)
{
    for (const PlatformView &p : view.platforms) {
        const int along = p.alongU ? u : v, across = p.alongU ? v : u;
        const int start = p.alongU ? p.originU : p.originV;
        if (across == (p.alongU ? p.originV : p.originU) && along >= start && along <= p.limit)
            return &p;
    }
    return nullptr;
}

bool platformCellAt(const BotView &view, const PlatformView &p, double atMs, Step *cell)
{
    std::vector<Park> atOrigin, atLimit;
    platformSpells(view, p, atMs + 10000.0, &atOrigin, &atLimit);
    const int start = p.alongU ? p.originU : p.originV;
    // The parks in order, each with the end it is at; between two the
    // platform runs from the one end to the other at a cell a HEIGHT_MS.
    std::vector<std::pair<Park, int>> parks;
    for (const Park &k : atOrigin)
        parks.push_back({k, start});
    for (const Park &k : atLimit)
        parks.push_back({k, p.limit});
    std::sort(parks.begin(), parks.end(),
              [](const auto &a, const auto &b) { return a.first.from < b.first.from; });
    int along = -1;
    for (size_t i = 0; i < parks.size() && along < 0; i++) {
        const Park &k = parks[i].first;
        if (atMs < k.from) {
            // Under way to it, from the other end.
            const int to = parks[i].second, from = to == start ? p.limit : start;
            const double cells = std::max(0.0, (k.from - atMs) / HEIGHT_MS);
            const double pos = to > from ? to - cells : to + cells;
            along = (int)std::floor(std::max<double>(start, std::min<double>(p.limit, pos)));
        } else if (atMs <= k.to + view.tickMs) {
            along = parks[i].second;
        }
    }
    if (along < 0)
        return false;
    *cell = p.alongU ? Step{along, p.originV} : Step{p.originU, along};
    return true;
}

}  // namespace bot
