/* BotView: what the bot knows, a copyable snapshot of the level in play taken
 * once a tick from a const Game.  Everything the bot decides is decided from
 * a BotView, never from the game itself, so the planner can be tested on
 * views made by hand and re-run on views read back from a trace.
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

class Game;

namespace bot {

struct Cell {
    unsigned char height   = 0;  // 0: no tile
    unsigned char kind     = 0;  // TileKind
    unsigned char contents = 0;  // TileContents: what is picked up here
    unsigned char hidden   = 0;  // a bombable block's contents, revealed by a blast
    unsigned char param    = 0;
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
    bool turning = false;  // the move under way is a turn
    int  moveDir = 0;      // the move under way: 1..4 a step, 11..14 a turn to (n - 10)
    /* At rest: neither stepping, turning nor in the air. */
    bool atRest() const { return !moving && !falling; }
    double animStart = 0;  // clock ms the current or last move began
    double animMs = 0;     // how long that move takes
};

struct PlayerView : Mover {
    int  bombs = 0, glides = 0, lives = 0;
    int  gems = 0;
    bool reversed = false, frozenFoes = false, protection = false;
    double stepMs = 200;
    int  moveState = 0;     // nonzero once dead
    double freezeUntil = 0; // the foes are held until then (clock ms), if frozen
};

struct FoeView : Mover {
    int  kind = 0;          // FoeKind: 2 catcher, 3 thrower
    int  type = 0;          // its behaviour (Foe::chooseTarget)
    bool held = false;      // frozen
    bool dying = false;
    int  homeU = 0, homeV = 0;  // where it was spawned
};

struct FallingView {
    int    u = 0, v = 0, h = 0;
    bool   armed = false;
    double armedAt = 0;     // clock ms
    bool   fallen = false;  // gone, until it respawns
    bool   respawns = true;
};

struct BombView : Mover {
    double droppedAt = 0;   // clock ms; it blows 2 s later
};

class BotView {
public:
    /* The view of the game as it is now. */
    static BotView capture(const Game &game);

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

    /* Why the player died (it has: player.moveState != 0): foe, drop,
     * timeout, bomb, void, harakiri.  See botview.cpp. */
    const char *deathCause() const;

    /* JSON, one line.  The whole grid when prev is null; otherwise only the
     * cells that differ from prev's, as "changed". */
    std::string toJson(unsigned frame, const BotView *prev) const;
};

}  // namespace bot
