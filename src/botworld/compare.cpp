#include "compare.h"
#include <stdio.h>

namespace bot {

namespace {

const char *const NAMES[PART_COUNT] = {
    "player_cell", "player_facing", "player_moving", "player_alive", "player_items",
    "foe_count",   "foe_cells",     "bombs",         "bridges",      "fallings",
    "lifts",       "platforms",     "cells",
};

std::string note(const char *fmt, int a, int b, int c = 0, int d = 0, int e = 0, int f = 0)
{
    char buf[96];
    snprintf(buf, sizeof(buf), fmt, a, b, c, d, e, f);
    return buf;
}

void set(PartResult &r, bool right, const std::string &detail = {})
{
    r.counted = true;
    r.right   = right;
    if (!right)
        r.detail = detail;
}

int living(const BotView &view)
{
    int n = 0;
    for (const FoeView &f : view.foes)
        n += !f.dying;
    return n;
}

}  // namespace

const char *partName(SimPart part)
{
    return NAMES[part];
}

std::vector<PartResult> compareViews(const BotView &pred, const BotView &seen)
{
    std::vector<PartResult> out(PART_COUNT);
    const PlayerView &p = pred.player, &s = seen.player;

    set(out[PART_PLAYER_CELL], p.u == s.u && p.v == s.v && p.h == s.h,
        note("(%d,%d,%d) for (%d,%d,%d)", p.u, p.v, p.h, s.u, s.v, s.h));
    set(out[PART_PLAYER_FACING], p.facing == s.facing, note("%d for %d", p.facing, s.facing));
    set(out[PART_PLAYER_MOVING], p.moving == s.moving && p.falling == s.falling,
        note("moving %d falling %d for %d %d", p.moving, p.falling, s.moving, s.falling));
    set(out[PART_PLAYER_ALIVE], (p.moveState == 0) == (s.moveState == 0),
        note("dead %d for %d", p.moveState != 0, s.moveState != 0));
    set(out[PART_PLAYER_ITEMS], p.gems == s.gems && p.bombs == s.bombs && p.glides == s.glides,
        note("gems %d bombs %d glides %d for %d %d %d", p.gems, p.bombs, p.glides, s.gems, s.bombs,
             s.glides));

    const int pl = living(pred), sl = living(seen);
    if (pl || sl)
        set(out[PART_FOE_COUNT], pl == sl, note("%d for %d", pl, sl));
    if (!seen.foes.empty() && pred.foes.size() == seen.foes.size()) {
        bool right = true;
        std::string detail;
        for (size_t i = 0; i < seen.foes.size() && right; i++) {
            const FoeView &a = pred.foes[i], &b = seen.foes[i];
            right = a.u == b.u && a.v == b.v;
            if (!right)
                detail = note("foe %d (%d,%d) for (%d,%d)", (int)i, a.u, a.v, b.u, b.v);
        }
        set(out[PART_FOE_CELLS], right, detail);
    }

    if (!pred.bombs.empty() || !seen.bombs.empty()) {
        bool right = pred.bombs.size() == seen.bombs.size();
        for (size_t i = 0; i < seen.bombs.size() && right; i++)
            right = pred.bombs[i].u == seen.bombs[i].u && pred.bombs[i].v == seen.bombs[i].v;
        set(out[PART_BOMBS], right,
            note("%d bombs for %d", (int)pred.bombs.size(), (int)seen.bombs.size()));
    }

    if (!seen.bridges.empty()) {
        bool right = pred.bridges.size() == seen.bridges.size();
        std::string detail;
        for (size_t i = 0; i < seen.bridges.size() && right; i++) {
            const BridgeView &a = pred.bridges[i], &b = seen.bridges[i];
            right = a.extended == b.extended && a.moving == b.moving;
            if (!right)
                detail = note("bridge %d extended %d moving %d for %d %d", (int)i, a.extended,
                              a.moving, b.extended, b.moving);
        }
        set(out[PART_BRIDGES], right, detail);
    }

    if (!seen.fallings.empty()) {
        bool right = pred.fallings.size() == seen.fallings.size();
        std::string detail;
        for (size_t i = 0; i < seen.fallings.size() && right; i++) {
            const FallingView &a = pred.fallings[i], &b = seen.fallings[i];
            right = a.armed == b.armed && a.fallen == b.fallen;
            if (!right)
                detail = note("tile (%d,%d) armed %d fallen %d for %d %d", b.u, b.v, a.armed,
                              a.fallen, b.armed, b.fallen);
        }
        set(out[PART_FALLINGS], right, detail);
    }

    if (!seen.lifts.empty()) {
        bool right = pred.lifts.size() == seen.lifts.size();
        std::string detail;
        for (size_t i = 0; i < seen.lifts.size() && right; i++) {
            const LiftView &a = pred.lifts[i], &b = seen.lifts[i];
            right = a.state == b.state && a.atTop == b.atTop;
            if (!right)
                detail = note("lift (%d,%d) state %d top %d for %d %d", b.u, b.v, a.state, a.atTop,
                              b.state, b.atTop);
        }
        set(out[PART_LIFTS], right, detail);
    }

    if (!seen.platforms.empty()) {
        bool right = pred.platforms.size() == seen.platforms.size();
        std::string detail;
        for (size_t i = 0; i < seen.platforms.size() && right; i++) {
            const PlatformView &a = pred.platforms[i], &b = seen.platforms[i];
            right = a.state == b.state && a.atLimit == b.atLimit;
            if (!right)
                detail = note("platform %d state %d limit %d for %d %d", (int)i, a.state,
                              a.atLimit, b.state, b.atLimit);
        }
        set(out[PART_PLATFORMS], right, detail);
    }

    {
        bool right = pred.cells.size() == seen.cells.size();
        std::string detail;
        for (size_t i = 0; i < seen.cells.size() && right; i++) {
            Cell a = pred.cells[i], b = seen.cells[i];
            a.occupant = b.occupant = 0;
            a.blastHeight = b.blastHeight = 0;
            right = a == b;
            if (!right)
                detail = note("cell (%d,%d)", (int)(i % seen.extentU), (int)(i / seen.extentU));
        }
        set(out[PART_CELLS], right, detail);
    }
    return out;
}

}  // namespace bot
