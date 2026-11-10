#include "moves.h"
#include "presses.h"
#include "world.h"

namespace simbot {

namespace {

/* How long a move may take before it is given up on: long enough for a
 * slide or ice across a level, and a teleporter's holds. */
const int MOVE_TICKS = 600;

}  // namespace

const char *moveName(Move m)
{
    static const char *const names[] = {"forward", "back", "left", "right"};
    return m < MOVES ? names[m] : "?";
}

unsigned pressOf(Move m)
{
    static const unsigned presses[] = {PRESS_FORWARD, PRESS_BACK, PRESS_LEFT, PRESS_RIGHT};
    return presses[m];
}

Place placeOf(const World &w)
{
    const Player *p = w.player();
    return {p->cellU(), p->cellV(), p->heightCell(), p->facing()};
}

bool dead(const World &w)
{
    return w.player()->moveState() != 0;
}

bool atRest(const World &w)
{
    const Player *p = w.player();
    return !dead(w) && p->moveDir() == 0 && p->falling() == 0 && p->gliding() == 0 &&
           !p->launched() && p->teleportPhase() == 0;
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
    tick(w, pressOf(m), work);
    out.ticks = 1;
    if (atRest(w) && placeOf(w) == from) {
        out.kind = Played::Refused;
        return out;
    }
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
    out.kind = Played::Moved;
    return out;
}

}  // namespace simbot
