#include "gliding.h"
#include "entitymath.h"
#include "tile.h"
#include "world.h"

namespace simbot {

namespace {

/* Below this height cell the glide ends (movableentity.cpp). */
const int GLIDE_FLOOR = 2;

/* Sinks a by drop over its tile: it keeps to the tile's top once it comes
 * down to it. */
Glided sink(const LevelMap &map, Air &a, float drop)
{
    const Tile *t = map.tile(a.u, a.v);
    const float top = (float)t->height();
    const float y = a.y - drop;
    if (t->objectMarker() != TILE_EMPTY && a.y >= top - 0.5f && y <= top) {
        a.y = top;
        return Glided::Landed;
    }
    a.y = y;
    return a.heightCell() < GLIDE_FLOOR ? Glided::Lost : Glided::Flying;
}

/* A step on way dir from a, sinking drop. */
Glided step(const LevelMap &map, Air &a, int dir, float drop)
{
    static const int du[] = {0, 0, 1, 0, -1}, dv[] = {0, -1, 0, 1, 0};
    const int u = a.u + du[dir], v = a.v + dv[dir];
    if (u < 0 || v < 0 || u >= (int)map.extentU() || v >= (int)map.extentV())
        return Glided::Refused;
    const Tile *t = map.tile(u, v);
    const int k = t->objectMarker();
    if (k == TILE_IMPASSABLE || (k == TILE_BOMBABLE && t->busy() == 0))
        return Glided::Refused;
    if (k != TILE_EMPTY && t->height() == a.heightCell() + 1)
        return Glided::Refused;   // one above
    a.u = u;
    a.v = v;
    return sink(map, a, drop);
}

}  // namespace

Air airOf(const World &w)
{
    const Player *p = w.player();
    Air a;
    a.u = p->cellU();
    a.v = p->cellV();
    a.facing = p->facing();
    a.y = p->posY();
    return a;
}

GlideCosts glideCosts(const World &w)
{
    const double step = w.player()->animDuration() > 0 ? w.player()->animDuration() : 200.0;
    const double drift = WAIT_TICKS * w.tickStep()->value;
    const double ms[] = {step, step, 2 * step, 2 * step, drift};
    GlideCosts c;
    for (int m = 0; m <= WAIT; m++) {
        c.ms[m] = ms[m];
        c.drop[m] = (float)(ms[m] * MovableEntity::GLIDE_RATE);
        World r(w);
        if (play(r, (Move)m).kind == Played::Moved && gliding(r)) {
            c.ms[m] = r.now() - w.now();
            c.drop[m] = w.player()->posY() - r.player()->posY();
        }
    }
    return c;
}

Glided glide(const LevelMap &map, Air &a, Move m, const GlideCosts &costs)
{
    switch (m) {
    case WAIT:
        return sink(map, a, costs.drop[m]);
    case FORWARD:
        return step(map, a, a.facing, costs.drop[m]);
    case BACK:
        return step(map, a, Sim_GetTurnedDirection((unsigned char)a.facing, 2), costs.drop[m]);
    default:   // a turn and a step
        a.facing = Sim_GetTurnedDirection((unsigned char)a.facing, m == LEFT ? 3 : 1);
        return step(map, a, a.facing, costs.drop[m]);
    }
}

}  // namespace simbot
