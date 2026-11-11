/* Moves: what the player does, played on a World.  A move starts with the
 * player at rest: its key is held for one tick, then nothing until the
 * player is at rest again, wherever the ground has carried it (ice, a
 * slide, a teleporter, a jump pad), or the step taken while gliding.  The game is the judge of where it ends:
 * the move is played, not predicted. */
#pragma once

#include <tuple>
#include <vector>
#include "work.h"

class Foe;
class World;

namespace simbot {

/* The four steps; then WAIT: nothing held for WAIT_TICKS (a short wait,
 * for a foe to pass), then until at rest; TIMED_WAIT, nothing held till a
 * time (waitUntil); DROP, a bomb dropped where the player stands, which
 * rolls on the way it faces (and off a ledge, to blow where it lands);
 * DROP_BACK, the drop pressed with BACK: the game leaves the bomb in the
 * cell stepped back from (worldrules.cpp), rolling on as the player backs
 * away. */
enum Move { FORWARD, BACK, LEFT, RIGHT, MOVES, WAIT = MOVES, TIMED_WAIT, DROP, DROP_BACK };
const int WAIT_TICKS = 15;


const char *moveName(Move m);
/* The move's press (presses.h). */
unsigned pressOf(Move m);

struct Cell {
    int u = 0, v = 0;
    bool operator==(const Cell &o) const { return u == o.u && v == o.v; }
    bool operator<(const Cell &o) const { return u != o.u ? u < o.u : v < o.v; }
};

/* Where the player stands at rest: its cell, height and facing, and
 * whether it is in the air (gliding, no step under way: there LEFT and
 * RIGHT turn and then step, so each move goes a cell). */
struct Place {
    int  u = 0, v = 0, h = 0, facing = 0;
    bool air = false;
    auto key() const { return std::make_tuple(u, v, h, facing, air); }
    bool operator<(const Place &o) const { return key() < o.key(); }
    bool operator==(const Place &o) const { return key() == o.key(); }
    Cell cell() const { return {u, v}; }
};

Place placeOf(const World &w);

/* The foe with Listed id id, or null if it is gone. */
const Foe *findFoe(const World &w, unsigned id);
/* The foe is gone, or blasted and dying. */
bool foeDead(const World &w, unsigned id);

/* The player is alive and at rest: not stepping or turning, not falling,
 * not held by a teleporter; or gliding with no step under way (a glide's
 * places are in the air: a step is taken there, or a WAIT drifts down). */
bool atRest(const World &w);
bool gliding(const World &w);
bool dead(const World &w);
/* The way the lift or platform the player stands on is going: 0 none (or
 * parked), 1 up or out along its track, 2 back. */
int carried(const World &w);

/* One tick of w, its own step long, with presses held and play running;
 * its sounds go unheard.  With work, charged to it. */
void tick(World &w, unsigned presses, Work *work = nullptr);

/* What came of a move. */
struct Played {
    enum Kind { Moved, Refused, Died, Unsettled };
    Kind kind = Refused;
    int  ticks = 0;     // ticks it took, its press's included
    int  stepAt = -1;   // the tick FORWARD was pressed, after a turn in the air
};
/* The presses of a move played: its key on the first tick, FORWARD on
 * tick stepAt (if any), nothing on the others. */
std::vector<unsigned> pressesOf(Move m, const Played &p);

/* Plays m on w, the player at rest.  Refused if the player is at rest on
 * the tick after the press where it was (never for WAIT; for DROP and
 * DROP_BACK, if no bomb was dropped); Unsettled if it is not at rest
 * within the limit (held on a sticky pad, say). */
Played play(World &w, Move m, Work *work = nullptr);
/* Nothing held till the clock is at until, then till at rest (a lift or
 * platform may carry the player meanwhile); a WAIT. */
Played waitUntil(World &w, double until, Work *work = nullptr);

}  // namespace simbot
