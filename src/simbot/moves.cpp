#include "moves.h"
#include "presses.h"
#include "world.h"

namespace simbot {

namespace {

/* How long a move may take before it is given up on: long enough for a
 * slide or ice across a level, and a teleporter's holds. */
const int MOVE_TICKS = 600;
/* A wait's limit: a lift's whole cycle, and its ride after. */
const int WAIT_LIMIT_TICKS = 1800;

}  // namespace

const char *moveName(Move m)
{
    static const char *const names[] = {"forward", "back", "left", "right", "wait",
                                         "timed wait", "drop", "drop back"};
    return m <= DROP_BACK ? names[m] : "?";
}

unsigned pressOf(Move m)
{
    static const unsigned presses[] = {PRESS_FORWARD, PRESS_BACK, PRESS_LEFT, PRESS_RIGHT};
    if (m < MOVES)
        return presses[m];
    if (m == DROP)
        return PRESS_BOMB;
    if (m == DROP_BACK)
        return PRESS_BOMB | PRESS_BACK;
    return 0u;
}


std::vector<unsigned> pressesOf(Move m, const Played &p)
{
    std::vector<unsigned> out(p.ticks, 0u);
    if (!out.empty())
        out[0] = pressOf(m);
    if (p.stepAt >= 0 && p.stepAt < p.ticks)
        out[p.stepAt] = PRESS_FORWARD;
    return out;
}

Place placeOf(const World &w)
{
    const Player *p = w.player();
    return {p->cellU(), p->cellV(), p->heightCell(), p->facing(), p->gliding() != 0};
}

const Foe *findFoe(const World &w, unsigned id)
{
    for (const Foe &f : w.foes())
        if (f.id() == id)
            return &f;
    return nullptr;
}

bool foeDead(const World &w, unsigned id)
{
    const Foe *f = findFoe(w, id);
    return !f || f->moveState() == 4;   // blasted; one that fell is gone soon after
}

bool gliding(const World &w)
{
    return w.player()->gliding() != 0;
}

bool dead(const World &w)
{
    return w.player()->moveState() != 0;
}

bool atRest(const World &w)
{
    const Player *p = w.player();
    if (dead(w) || p->moveDir() != 0 || p->launched() || p->teleportPhase() != 0)
        return false;
    return p->gliding() != 0 || p->falling() == 0;
}

long tickCost(const World &w)
{
    return 1 + 15 * (long)w.foes().size();
}

void tick(World &w, unsigned presses, Work *work)
{
    if (work)
        work->spend(tickCost(w));
    PlayEvents unheard;
    w.tick(w.tickStep()->value, presses, true, unheard);
}

Played play(World &w, Move m, Work *work)
{
    Played out;
    const Place from = placeOf(w);
    const unsigned bombs = w.player()->bombsCarried();
    tick(w, pressOf(m), work);
    out.ticks = 1;
    if (m == WAIT) {
        for (; out.ticks < WAIT_TICKS && !dead(w); out.ticks++)
            tick(w, 0, work);
    } else if (m == DROP || m == DROP_BACK) {
        if (w.player()->bombsCarried() == bombs) {
            out.kind = Played::Refused;
            return out;
        }
    } else if (atRest(w) && placeOf(w) == from) {
        out.kind = Played::Refused;
        return out;
    }
    // In the air, a turn is followed by a step: the four moves are the four
    // ways.
    const bool turnThenStep = from.air && (m == LEFT || m == RIGHT);
    for (;;) {
        while (!atRest(w)) {
            if (dead(w)) {
                out.kind = Played::Died;
                return out;
            }
            if (out.ticks >= MOVE_TICKS) {
                out.kind = Played::Unsettled;
                return out;
            }
            tick(w, 0, work);
            out.ticks++;
        }
        if (!turnThenStep || out.stepAt >= 0 || !gliding(w))
            break;
        out.stepAt = out.ticks;
        tick(w, PRESS_FORWARD, work);
        out.ticks++;
    }
    out.kind = Played::Moved;
    return out;
}

Played waitUntil(World &w, double until, Work *work)
{
    Played out;
    while (out.ticks == 0 || w.now() < until || !atRest(w)) {
        if (dead(w)) {
            out.kind = Played::Died;
            return out;
        }
        if (out.ticks >= WAIT_LIMIT_TICKS) {
            out.kind = Played::Unsettled;
            return out;
        }
        tick(w, 0, work);
        out.ticks++;
    }
    out.kind = Played::Moved;
    return out;
}

}  // namespace simbot
