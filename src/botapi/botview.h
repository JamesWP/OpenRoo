/* BotView: what the bot knows, a copyable snapshot of the level in play taken
 * once a tick from a const Game (capture.h).  Everything the bot decides is
 * decided from a BotView, never from the game itself, so the planner can be
 * tested on views made by hand and re-run on views read back from a trace.
 * This group (botapi) knows nothing of the game: the run (src/bot) builds the
 * views and presses the keys.
 *
 * The bot has perfect knowledge (docs/BOT_DESIGN.md): it sees a bombable
 * block's hidden contents too.
 *
 * Cells are addressed (u, v).  Facing is 1..4 (tile.h): 1 is v decreasing,
 * 2 u increasing, 3 v increasing, 4 u decreasing. */
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace bot {

/* A cell's place on the grid. */
struct Step {
    int u, v;
};

struct Cell {
    unsigned char height   = 0;  // 0: no tile
    unsigned char kind     = 0;  // TileKind
    unsigned char contents = 0;  // TileContents: what is picked up here
    unsigned char hidden   = 0;  // a bombable block's contents, revealed by a blast
    unsigned char param    = 0;  // a switch's: its slot + 1, as the level file has it
    unsigned char occupant = 0;  // nonzero while something stands here
    unsigned char slideDir = 0;
    unsigned char teleU = 0, teleV = 0;  // a teleporter's destination
    unsigned char blastHeight = 0;       // nonzero while a blast is live here
    unsigned char jumpTop = 0;           // a jump pad's launch height
    bool          track = false;         // a platform's track runs here
    unsigned char liftBottom = 0, liftTop = 0;  // a lift's two heights
    bool          busy = false;          // a falling tile that has fallen

    bool operator==(const Cell &o) const;
    bool operator!=(const Cell &o) const { return !(*this == o); }
};

struct Mover {
    int u = 0, v = 0, h = 0;
    int facing = 0;
    bool moving = false;   // a step or turn is under way
    bool falling = false;  // in the air: falling, gliding or launched
    bool launched = false; // on a jump pad's arc, since launchedAt (clock ms)
    double launchedAt = 0;
    int  teleport = 0;     // a teleporter's hold: 1 before the move, 2 after, since teleportAt
    double teleportAt = 0;
    bool gliding = false;  // in the air under a paraglider: it can step and turn
    bool turning = false;  // the move under way is a turn
    int  moveDir = 0;      // the move under way: 1..4 a step, 11..14 a turn to (n - 10)
    /* At rest: neither stepping, turning nor in the air. */
    bool atRest() const { return !moving && !falling; }
    double animStart = 0;  // clock ms the current or last move began
    double animMs = 0;     // how long that move takes (at rest, the step time)
    double stepEnd = 0;    // clock ms the last step or turn ended
    double stuckSince = 0; // clock ms a sticky pad caught it, 0 if not held
    int    lastMoveDir = 0; // the last step's direction (not a turn's): ice and a teleporter carry it on
};

struct PlayerView : Mover {
    int  bombs = 0, glides = 0, lives = 0;
    int  gems = 0;
    bool reversed = false, frozenFoes = false, protection = false;
    double stepMs = 200;
    // The speed effects (player.cpp), speeding up and slowing down the
    // steps: each runs out on the first tick past this, clock ms; 0 for none.
    double speedUpEnds = 0, slowDownEnds = 0;
    int  moveState = 0;     // nonzero once dead
    double freezeUntil = 0; // the foes are held until then (clock ms), if frozen
    double bombReadyAt = 0; // the next bomb can be dropped from then (clock ms)
};

struct FoeView : Mover {
    int  kind = 0;          // FoeKind: 2 catcher, 3 thrower
    int  type = 0;          // its behaviour (Foe::chooseTarget)
    bool held = false;      // frozen
    bool dying = false;
    int  homeU = 0, homeV = 0;  // where it was spawned
    int  lastU = 0, lastV = 0;  // the last cell its chase found a way to (a returner goes back there)
    int  drops = 0;         // what it leaves on its cell when killed (TileContents)
    double lastDrop = 0;    // a thrower: the clock ms of its last bomb
    bool dropPending = false;  // a thrower: a bomb asked for, dropped once it is at rest
};

struct FallingView {
    int    u = 0, v = 0, h = 0;
    bool   armed = false;
    double armedAt = 0;     // clock ms
    bool   fallen = false;  // gone, until it respawns
    bool   respawns = true;
};

/* A lift's cycle (liftobject.cpp): it rises from its base to its top, parks
 * there 1.5 s, falls to its base and parks there 1.5 s, at 200 ms a height. */
struct LiftView {
    int    u = 0, v = 0;
    int    base = 0, top = 0;
    int    state = 0;          // 0 parked, 1 rising, 2 falling
    bool   atTop = false;      // it last parked at the top
    double phaseStart = 0;     // clock ms the phase began
};

/* A platform's cycle (platformobject.cpp): it runs along its track from its
 * origin to its limit at 200 ms a cell, parks 1.5 s, runs back and parks 1.5 s.
 * It carries what stands on it, at its height. */
struct PlatformView {
    bool   alongU = true;      // the track runs along u (else v)
    int    originU = 0, originV = 0;
    int    limit = 0;          // the last cell of the track along its axis
    int    height = 0;
    int    state = 0;          // 0 parked, 1 advancing, 2 retreating
    bool   atLimit = false;    // it last parked at the limit
    double phaseStart = 0;     // clock ms the phase began
};

/* A switched bridge (bridgeobject.cpp): a deck of cells along one axis, at
 * one height, which a switch of its slot extends or retracts, a cell every
 * 100 ms.  An extended deck is walkable ground; a retracted one is void.
 * Only the bridge the game finds for a slot (World::bridge) is a BridgeView:
 * the switch works no other. */
struct BridgeView {
    int    slot = 0;
    int    height = 0;
    std::vector<Step> deck;    // from the anchor outwards
    bool   extended = false;   // its state, or the one it is moving to
    bool   moving = false;     // a switch has set it going
    double phaseStart = 0;     // clock ms the move began
    double moveMs() const { return 100.0 * deck.size(); }
};

struct BombView : Mover {
    double droppedAt = 0;   // clock ms; it blows 2 s later
};

class BotView {
public:
    int  extentU = 0, extentV = 0;
    std::vector<Cell> cells;  // extentU * extentV, u fastest

    const Cell &cell(int u, int v) const { return cells[(size_t)v * extentU + u]; }
    bool inside(int u, int v) const { return u >= 0 && v >= 0 && u < extentU && v < extentV; }

    std::string level;
    double now = 0;           // clock ms
    double tickMs = 0;        // the clock's step each tick
    int    timeLimitS = 0;
    unsigned timeElapsedMs = 0;
    int    gemsRequired = 0;
    int    exitU = 0, exitV = 0, exitH = 0;

    PlayerView               player;
    std::vector<FoeView>     foes;

    std::vector<FallingView> fallings;
    std::vector<BombView>    bombs;
    std::vector<LiftView>    lifts;
    std::vector<PlatformView> platforms;
    std::vector<BridgeView>   bridges;

    /* Why the player died (it has: player.moveState != 0): foe, drop,
     * timeout, bomb, void, harakiri.  See botview.cpp. */
    const char *deathCause() const;

    /* JSON, one line.  The whole grid when prev is null; otherwise only the
     * cells that differ from prev's, as "changed". */
    std::string toJson(unsigned frame, const BotView *prev) const;
};

}  // namespace bot
