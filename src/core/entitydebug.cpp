/* The level's objects on the debug map (entitydebug.h).  An object's tooltip
 * names it by its id (entitylist.h). */

#include <stdint.h>
#include <algorithm>
#include "entitydebug.h"
#include "dbg.h"
#include "world.h"
#include "player.h"
#include "foe.h"
#include "foepath.h"
#include "bomb.h"
#include "bridgeobject.h"
#include "switchcells.h"
#include "tile.h"
#include "tilenames.h"

static void deck_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    // As the map draws one: extended deck inside the span's outline.
    dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * 0.6f, b.y), IM_COL32(190, 140, 80, 255));
    dl->AddRect(a, b, IM_COL32(170, 110, 50, 255), 0.0f, 0, 2.0f);
}

/* Bridges are objects, not tiles: the builder clears their cells.  The whole
 * span is outlined; the deck fills as far as it has extended. */
static void debug_bridge(const dbg::MapView &m, const BridgeObject &br, const SwitchCells &sc)
{
    ImDrawList *dl = m.dl;
    const BridgeExtent b = br.extent();
    const bool alongU = b.axis == 1;
    const float end  = (alongU ? b.restU : b.restV) + b.step * b.span;
    const float rest = alongU ? b.restU : b.restV;
    const float reach = m.asLoaded ? rest : (alongU ? b.reachU : b.reachV);
    // The cross-axis cell is the anchor's.
    const float cross = alongU ? b.restV : b.restU;
    auto rect = [&](float a0, float a1, ImVec2 *p0, ImVec2 *p1) {
        const float lo = std::min(a0, a1), hi = std::max(a0, a1);
        *p0 = alongU ? m.corner(lo, cross) : m.corner(cross, lo);
        *p1 = alongU ? m.corner(hi, cross + 1) : m.corner(cross + 1, hi);
    };
    ImVec2 p0, p1;
    if (reach != rest) {
        rect(rest, reach, &p0, &p1);
        dl->AddRectFilled(p0, p1, IM_COL32(190, 140, 80, 255));
    }
    rect(rest, end, &p0, &p1);
    dl->AddRect(p0, p1, b.armed ? IM_COL32(255, 220, 60, 255) : IM_COL32(170, 110, 50, 255),
                0.0f, 0, std::max(1.5f, m.cell * 0.1f));

    // A line from each of its switches to the middle of the span: all of
    // them, or those of a hovered switch or bridge.
    const ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    const int hAlong = alongU ? m.hoverU : m.hoverV, hAcross = alongU ? m.hoverV : m.hoverU;
    const bool onSpan = hAcross == (int)cross && hAlong >= std::min(rest, end) && hAlong < std::max(rest, end);
    bool show = m.links || onSpan;
    for (unsigned k = 0; k < sc.count(b.slot); k++)
        show = show || m.hovered(sc.cellU(b.slot, k), sc.cellV(b.slot, k));
    for (unsigned k = 0; show && k < sc.count(b.slot); k++)
        dl->AddLine(m.centre((float)sc.cellU(b.slot, k), (float)sc.cellV(b.slot, k)), mid,
                    IM_COL32(230, 40, 40, 200), std::max(1.0f, m.cell * 0.08f));

    if (onSpan) {
        m.separator();
        m.tip("bridge on switch %d, along %s, %d cells", b.slot + 1, alongU ? "U" : "V", b.span);
        m.tip("%s, %s next", b.armed ? "moving" : "still", b.phase ? "retracts" : "extends");
    }
    m.legend(0, "bridge", deck_swatch);
}

static void bomb_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), (b.x - a.x) * 0.3f,
                        IM_COL32(20, 20, 20, 255));
}

static void debug_bomb(const dbg::MapView &m, const Bomb &b)
{
    if (!m.asLoaded) {
        m.dl->AddCircleFilled(m.centre(b.posU(), b.posV()), m.cell * 0.3f, IM_COL32(20, 20, 20, 255));
        m.legend(2, "bomb", bomb_swatch);
    }
    if (m.hovered((uint8_t)b.cellU(), (uint8_t)b.cellV())) {
        m.separator();
        m.tip("bomb #%u at U%u V%u H%u", b.id(), (uint8_t)b.cellU(), (uint8_t)b.cellV(), (uint8_t)b.heightCell());
        if (b.held())
            m.tip("frozen");
        if (b.dyingStarted())
            m.tip("dying");
    }
}

static void path_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), IM_COL32(255, 140, 0, 220), 2.0f);
}

/* A foe's route left to its target, from its position.  The nodes run on
 * towards the target. */
static void debug_path(const dbg::MapView &m, const FoePath &path, float fromU, float fromV)
{
    if (!path.hasPath() || !path.result() || !m.foePaths || m.asLoaded)
        return;
    ImVec2 prev = m.centre(fromU, fromV);
    int guard = 0;
    for (const PathNode *n = path.result(); n && guard++ < 4096; n = n->parent()) {
        const ImVec2 p = m.centre((float)n->u(), (float)n->v());
        m.dl->AddLine(prev, p, IM_COL32(255, 140, 0, 220), std::max(1.0f, m.cell * 0.1f));
        prev = p;
    }
    m.dl->AddCircle(prev, m.cell * 0.25f, IM_COL32(255, 140, 0, 220), 0, 2.0f);
    m.legend(3, "foe path", path_swatch);
}

static void foe_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), (b.x - a.x) * 0.35f,
                        IM_COL32(230, 50, 50, 255));
}

static void debug_foe(const dbg::MapView &m, const Foe &e)
{
    if (e.pathfinder())  // its route goes under it
        debug_path(m, *e.pathfinder(), e.posU(), e.posV());
    if (!m.asLoaded) {
        const ImVec2 p = m.centre(e.posU(), e.posV());
        m.dl->AddCircleFilled(p, m.cell * 0.35f,
                              e.dyingStarted() ? IM_COL32(120, 60, 60, 255) : IM_COL32(230, 50, 50, 255));
        // A tick for its facing, as the player's.
        const int f = e.facing();
        if (f >= DIR_MIN && f <= DIR_MAX)
            m.dl->AddLine(p, ImVec2(p.x + DIR_DU[f] * m.cell * 0.5f, p.y + DIR_DV[f] * m.cell * 0.5f),
                          IM_COL32(0, 0, 0, 255), 2);
        m.legend(2, "foe", foe_swatch);
    }
    if (m.hovered((uint8_t)e.cellU(), (uint8_t)e.cellV())) {
        m.separator();
        m.tip("foe #%u at U%u V%u H%u", e.id(), (uint8_t)e.cellU(), (uint8_t)e.cellV(), (uint8_t)e.heightCell());
        m.tip("kind %u, behaviour %u, facing %u%s", e.kind(), e.type(), e.facing(), e.moveDir() ? ", moving" : "");
        const char *drops = Tile_ContentsName(e.dropContents());
        m.tip("home U%u V%u H%u, drops %s", e.homeU(), e.homeV(), e.homeH(), drops ? drops : "nothing");
        if (e.held())
            m.tip("frozen");
        if (e.dyingStarted())
            m.tip("dying");
    }
}

static void player_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), (b.x - a.x) * 0.4f,
                        IM_COL32(255, 255, 255, 255));
}

static void debug_player(const dbg::MapView &m, const Player &p)
{
    if (!m.asLoaded) {
        // With a tick for its facing.
        const ImVec2 c = m.centre(p.posU(), p.posV());
        m.dl->AddCircleFilled(c, m.cell * 0.4f, IM_COL32(255, 255, 255, 255));
        const int f = p.facing();
        if (f >= DIR_MIN && f <= DIR_MAX)
            m.dl->AddLine(c, ImVec2(c.x + DIR_DU[f] * m.cell * 0.6f, c.y + DIR_DV[f] * m.cell * 0.6f),
                          IM_COL32(0, 0, 0, 255), 2);
        m.legend(2, "player", player_swatch);
    }
    if (m.hovered((uint8_t)p.cellU(), (uint8_t)p.cellV())) {
        m.separator();
        m.tip("player at H%u, facing %u%s", (uint8_t)p.heightCell(), p.facing(), p.moveDir() ? ", moving" : "");
    }
}

void Entities_DebugDrawMap(const World *world)
{
    if (!dbg::active())
        return;
    dbg::MapLayer layer;
    if (!layer)
        return;
    const dbg::MapView &m = *layer;
    for (const BridgeObject &b : world->bridges())
        debug_bridge(m, b, *world->switchCells());
    for (const Bomb &b : world->bombs())
        debug_bomb(m, b);
    for (const Foe &e : world->foes())
        debug_foe(m, e);
    debug_player(m, *world->player());  // last, so it sits over the foes
}
