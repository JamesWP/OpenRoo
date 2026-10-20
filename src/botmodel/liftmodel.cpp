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
    // A move takes its heights, a park its dwell, each ending on the first
    // tick past it (liftobject.cpp), which the clock's step of 1/60 s makes
    // the tick it is due (a 200 ms height is 12 ticks).
    const double move = (l.top - l.base) * HEIGHT_MS;
    double at = l.phaseStart - view.now;
    int    state = l.state;
    bool   top = l.atTop;
    for (int guard = 0; at < horizonMs && guard < 1000; guard++) {
        if (state == 0) {
            // Parked from at; it leaves a dwell later.
            out.push_back({at, at + DWELL_MS, top ? l.top : l.base});
            at += DWELL_MS;
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
    const double move = span * HEIGHT_MS;   // a cell takes as long as a height (platformobject.cpp)
    double at = p.phaseStart - view.now;
    int    state = p.state;
    bool   limit = p.atLimit;
    for (int guard = 0; at < horizonMs && guard < 1000; guard++) {
        if (state == 0) {
            (limit ? atLimit : atOrigin)->push_back({at, at + DWELL_MS, p.height});
            at += DWELL_MS;
            state = limit ? 2 : 1;
        } else {
            at += move;
            limit = state == 1;
            state = 0;
        }
    }
}

std::vector<double> platformPasses(const BotView &view, const PlatformView &p, int along,
                                   double horizonMs)
{
    std::vector<double> out;
    const int start = p.alongU ? p.originU : p.originV;
    if (along <= start || along >= p.limit)
        return out;
    std::vector<Park> atOrigin, atLimit;
    platformSpells(view, p, horizonMs, &atOrigin, &atLimit);
    // Each park ends a run from the other end, at a cell a HEIGHT_MS.
    for (const Park &k : atOrigin)
        if (k.from - (along - start) * HEIGHT_MS >= 0)
            out.push_back(k.from - (along - start) * HEIGHT_MS);
    for (const Park &k : atLimit)
        if (k.from - (p.limit - along) * HEIGHT_MS >= 0)
            out.push_back(k.from - (p.limit - along) * HEIGHT_MS);
    std::sort(out.begin(), out.end());
    return out;
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

const LiftView *liftAt(const BotView &view, int u, int v)
{
    for (const LiftView &l : view.lifts)
        if (l.u == u && l.v == v)
            return &l;
    return nullptr;
}

double liftCycleMs(const LiftView &lift)
{
    return 2 * (HEIGHT_MS * (lift.top - lift.base) + DWELL_MS);
}

int liftHeightAt(const BotView &view, const LiftView &l, double atMs)
{
    const std::vector<Park> parks = parkSpells(view, l, atMs + 10000.0);
    for (size_t i = 0; i < parks.size(); i++) {
        const Park &k = parks[i];
        if (atMs < k.from) {
            // On its way to it, from the other end.
            const int other = k.height == l.top ? l.base : l.top;
            const int heights = (int)std::ceil(std::max(0.0, k.from - atMs - view.tickMs) / HEIGHT_MS);
            const int h = k.height > other ? k.height - heights : k.height + heights;
            return std::max(std::min(l.base, l.top), std::min(std::max(l.base, l.top), h));
        }
        if (atMs <= k.to + view.tickMs)
            return k.height;
    }
    return -1;
}

}  // namespace bot
