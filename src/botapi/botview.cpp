#include "botview.h"
#include <stdarg.h>
#include <stdio.h>

namespace bot {

bool Cell::operator==(const Cell &o) const
{
    return height == o.height && kind == o.kind && contents == o.contents &&
           hidden == o.hidden && param == o.param && occupant == o.occupant &&
           slideDir == o.slideDir && teleU == o.teleU && teleV == o.teleV &&
           blastHeight == o.blastHeight && jumpTop == o.jumpTop && busy == o.busy &&
           track == o.track && liftBottom == o.liftBottom && liftTop == o.liftTop;
}

namespace {

void appendf(std::string &out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void appendf(std::string &out, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out += buf;
}

void cell_json(std::string &out, int u, int v, const Cell &c)
{
    appendf(out, "[%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d]", u, v, c.height, c.kind,
            c.contents, c.hidden, c.param, c.occupant, c.slideDir, c.teleU, c.teleV,
            c.blastHeight, c.jumpTop, c.busy ? 1 : 0);
}

}  // namespace

/* From the player's move state (movableentity.cpp, foe.cpp, playtick.cpp,
 * player.cpp):
 *   foe       1, a foe touched it;
 *   drop      2, it landed from too high, or went below the floor;
 *   timeout   3, the time limit ran out;
 *   bomb      4 on a cell with a live blast, a bomb's blast caught it;
 *   void      4 on a cell with no live blast: it fell off the map.  The blast
 *             check matches any height within one of the cell's blast height,
 *             which is 0 while no blast is live, so a player falling through
 *             empty space is caught at height 1 with the bomb's state;
 *   harakiri  6, the player's own harakiri key. */
const char *BotView::deathCause() const
{
    switch (player.moveState) {
    case 1:  return "foe";
    case 2:  return "drop";
    case 3:  return "timeout";
    case 4:  return inside(player.u, player.v) && cell(player.u, player.v).blastHeight != 0
                    ? "bomb" : "void";
    case 6:  return "harakiri";
    default: return "unknown";
    }
}

std::string BotView::toJson(unsigned frame, const BotView *prev) const
{
    std::string out;
    appendf(out, "{\"frame\":%u,\"now\":%.1f,\"elapsed_ms\":%u", frame, now, timeElapsedMs);
    if (!prev || prev->extentU != extentU || prev->extentV != extentV) {
        std::string name;
        for (char c : level) {
            if (c == '"' || c == '\\')
                name += '\\';
            name += c;
        }
        appendf(out, ",\"level\":\"%s\",\"extent\":[%d,%d],\"time_limit_s\":%d,"
                "\"gems_required\":%d,\"exit\":[%d,%d,%d]",
                name.c_str(), extentU, extentV, timeLimitS, gemsRequired, exitU, exitV, exitH);
        out += ",\"cell_fields\":[\"u\",\"v\",\"height\",\"kind\",\"contents\",\"hidden\","
               "\"param\",\"occupant\",\"slide_dir\",\"tele_u\",\"tele_v\",\"blast_height\",\"jump_top\",\"busy\"]";
        out += ",\"cells\":[";
        bool first = true;
        for (int v = 0; v < extentV; v++)
            for (int u = 0; u < extentU; u++) {
                if (!first)
                    out += ',';
                first = false;
                cell_json(out, u, v, cell(u, v));
            }
        out += ']';
    } else {
        out += ",\"changed\":[";
        bool first = true;
        for (int v = 0; v < extentV; v++)
            for (int u = 0; u < extentU; u++)
                if (cell(u, v) != prev->cell(u, v)) {
                    if (!first)
                        out += ',';
                    first = false;
                    cell_json(out, u, v, cell(u, v));
                }
        out += ']';
    }
    const PlayerView &p = player;
    appendf(out, ",\"player\":{\"cell\":[%d,%d,%d],\"facing\":%d,\"moving\":%d,\"falling\":%d,\"bombs\":%d,"
            "\"glides\":%d,\"lives\":%d,\"gems\":%d,\"reversed\":%d,\"freeze\":%d,"
            "\"protection\":%d,\"step_ms\":%.0f,\"move_state\":%d}",
            p.u, p.v, p.h, p.facing, p.moving, p.falling, p.bombs, p.glides, p.lives, p.gems,
            p.reversed, p.frozenFoes, p.protection, p.stepMs, p.moveState);
    out += ",\"foes\":[";
    for (size_t i = 0; i < foes.size(); i++) {
        const FoeView &f = foes[i];
        appendf(out, "%s{\"cell\":[%d,%d,%d],\"facing\":%d,\"moving\":%d,\"kind\":%d,"
                "\"type\":%d,\"held\":%d,\"dying\":%d}", i ? "," : "",
                f.u, f.v, f.h, f.facing, f.moving, f.kind, f.type, f.held, f.dying);
    }
    out += "],\"fallings\":[";
    for (size_t i = 0; i < fallings.size(); i++) {
        const FallingView &f = fallings[i];
        appendf(out, "%s{\"cell\":[%d,%d,%d],\"armed\":%d,\"armed_at\":%.1f,\"fallen\":%d,"
                "\"respawns\":%d}", i ? "," : "",
                f.u, f.v, f.h, f.armed, f.armedAt, f.fallen, f.respawns);
    }
    out += "],\"bombs\":[";
    for (size_t i = 0; i < bombs.size(); i++) {
        const BombView &b = bombs[i];
        appendf(out, "%s{\"cell\":[%d,%d,%d],\"facing\":%d,\"moving\":%d,\"dropped_at\":%.1f}",
                i ? "," : "", b.u, b.v, b.h, b.facing, b.moving, b.droppedAt);
    }
    out += "]}";
    return out;
}

}  // namespace bot
