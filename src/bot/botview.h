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
    bool          busy = false;          // a falling tile that has fallen

    bool operator==(const Cell &o) const;
    bool operator!=(const Cell &o) const { return !(*this == o); }
};

struct Mover {
    int u = 0, v = 0, h = 0;
    int facing = 0;
    bool moving = false;   // a step or turn is under way
};

struct PlayerView : Mover {
    int  bombs = 0, glides = 0, lives = 0;
    int  gems = 0;
    bool reversed = false, frozenFoes = false, protection = false;
    double stepMs = 200;
    int  moveState = 0;     // nonzero once dead
};

struct FoeView : Mover {
    int  kind = 0;          // FoeKind: 2 catcher, 3 thrower
    int  type = 0;          // its behaviour (Foe::chooseTarget)
    bool held = false;      // frozen
    bool dying = false;
};

struct FallingView {
    int    u = 0, v = 0, h = 0;
    bool   armed = false;
    double armedAt = 0;     // clock ms
    bool   fallen = false;  // gone, until it respawns
    bool   respawns = true;
};

struct BombView {
    int    u = 0, v = 0, h = 0;
    double droppedAt = 0;   // clock ms
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
    int    timeLimitS = 0;
    unsigned timeElapsedMs = 0;
    int    gemsRequired = 0;
    int    exitU = 0, exitV = 0, exitH = 0;

    PlayerView               player;
    std::vector<FoeView>     foes;
    std::vector<FallingView> fallings;
    std::vector<BombView>    bombs;

    /* JSON, one line.  The whole grid when prev is null; otherwise only the
     * cells that differ from prev's, as "changed". */
    std::string toJson(unsigned frame, const BotView *prev) const;
};

}  // namespace bot
