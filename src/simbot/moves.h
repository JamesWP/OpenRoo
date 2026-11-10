/* Moves: what the player does, played on a World.  A move starts with the
 * player at rest: its key is held for one tick, then nothing until the
 * player is at rest again, wherever the ground has carried it (ice, a
 * slide, a teleporter, a jump pad).  The game is the judge of where it ends:
 * the move is played, not predicted. */
#pragma once

#include <tuple>
#include "work.h"

class World;

namespace simbot {

/* The four steps, then WAIT: nothing held for WAIT_TICKS (a short wait,
 * for a foe to pass), then until at rest. */
enum Move { FORWARD, BACK, LEFT, RIGHT, MOVES, WAIT = MOVES };
const int WAIT_TICKS = 15;


const char *moveName(Move m);
/* The move's press (presses.h). */
unsigned pressOf(Move m);

struct Cell {
    int u = 0, v = 0;
    bool operator==(const Cell &o) const { return u == o.u && v == o.v; }
    bool operator<(const Cell &o) const { return u != o.u ? u < o.u : v < o.v; }
};

/* Where the player stands at rest: its cell, height and facing. */
struct Place {
    int u = 0, v = 0, h = 0, facing = 0;
    auto key() const { return std::make_tuple(u, v, h, facing); }
    bool operator<(const Place &o) const { return key() < o.key(); }
    bool operator==(const Place &o) const { return key() == o.key(); }
    Cell cell() const { return {u, v}; }
};

Place placeOf(const World &w);

/* The player is alive and at rest: not stepping or turning, not in the
 * air, not held by a teleporter. */
bool atRest(const World &w);
bool dead(const World &w);

/* One tick of w, its own step long, with presses held and play running;
 * its sounds go unheard.  With work, charged to it. */
void tick(World &w, unsigned presses, Work *work = nullptr);

/* What came of a move. */
struct Played {
    enum Kind { Moved, Refused, Died, Unsettled };
    Kind kind = Refused;
    int  ticks = 0;     // ticks it took, its press's included
};

/* Plays m on w, the player at rest.  Refused if the player is at rest on
 * the tick after the press where it was (never for WAIT); Unsettled if it is not at rest
 * within the limit (held on a sticky pad, say). */
Played play(World &w, Move m, Work *work = nullptr);
/* Nothing held till the clock is at until, then till at rest (a lift or
 * platform may carry the player meanwhile); a WAIT. */
Played waitUntil(World &w, double until, Work *work = nullptr);

}  // namespace simbot
