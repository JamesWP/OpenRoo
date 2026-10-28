/* BridgeObject: spawn, tick, purge and the deck quad.
 *
 * A switch arms a bridge; the tick then runs one phase and disarms it:
 *   EXTENDING   coord = rest + elapsed * 0.01 * step
 *   RETRACTING  coord = (span * step + rest) - elapsed * 0.01 * step
 * A phase ends when elapsed >= span * 100 ms: the sound halts, the bridge
 * disarms, the phase flips and the coordinate snaps.  While moving, each cell
 * short of the guard is stamped as deck (extending) or cleared (retracting).
 *
 * KAROO_DECK_DIAG=1 logs the first stamp and unstamp and a tick count;
 * KAROO_PLACE_DIAG=1 logs every spawn with its scan axis; KAROO_RESET_DIAG=1
 * logs purges. */

#include <stdint.h>
#include "sysdev.h"
#include <stddef.h>
#include <math.h>
#include <new>
#include <string.h>

#include "bridgeobject.h"
#include "dbg.h"
#include "switchcells.h"
#include <algorithm>
#include "levelmap.h"
#include "tile.h"
#include "logger.h"

/* The float nearest 0.01; widened where it is multiplied. */
static const float K_MS_TO_TILE = 0.01f;

static int s_diag_deck    = 0;
static int s_diag_place     = 0;
static int s_init           = 0;

static void diag_init(void)
{

    if (s_init)
        return;
    s_init = 1;

    s_diag_deck = sysdev::envFlag("KAROO_DECK_DIAG");
    s_diag_place = sysdev::envFlag("KAROO_PLACE_DIAG");
}

BridgeObject::BridgeObject()
{
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
    phase_  = 0;
    armed_  = 0;
}

BridgeObject::~BridgeObject()
{
}

/* The spawn's scans: axis 1 walks u, axis 2 walks v, anything else does not
 * scan.  A zero marker behind the spawn cell reverses the step to -1.  The
 * walk is a while loop, stepping while the marker stays zero; neither scan
 * clears markers, and both are unbounded. */
static int s_logged_spawn = 0;

void BridgeObject::spawn(const EntityContext &ctx, EntityList<BridgeObject> &bridges, unsigned int uArg, unsigned int vArg,
                         unsigned int heightArg, unsigned int slotArg,
                         unsigned int axisArg)
{
    unsigned int u, v, height, axis, slot;
    unsigned int var;
    LevelMap *base;
    BridgeObject *obj;
    Tile *tile;

    diag_init();

    u      = uArg & 0xff;
    v      = vArg & 0xff;
    height = heightArg & 0xff;
    slot   = slotArg & 0xff;
    axis   = axisArg & 0xff;

    obj = &bridges.add(std::unique_ptr<BridgeObject>(new BridgeObject));

    obj->clock_  = ctx.clock;
    obj->tickStep_ = ctx.tickStep;
    obj->axis_   = (unsigned char)axis;

    base = ctx.map;
    obj->map_ = base;

    tile = base->tile( (int)u, (int)v);
    tile->setObjectMarker(0);

    obj->endU_ = (unsigned char)u;
    obj->endV_ = (unsigned char)v;

    if (s_diag_place) {
        s_logged_spawn++;
        g_logger.write("bridgeobject: bridge spawn #%d -- slot=%u u=%u v=%u "
                  "height=%u axis=%u %s\n",
                  s_logged_spawn, slot, u, v, height, axis,
                  (axis == 1) ? "SCAN-U" :
                  (axis == 2) ? "SCAN-V" : "no-scan");
    }

    if (axis == 1) {
        obj->step_ = 1;
        obj->endU_ = (unsigned char)u;
        if (base->tile( (int)u - 1, (int)v)->objectMarker() == TILE_EMPTY) {
            obj->step_ = (signed char)0xff;
            obj->endU_ = (unsigned char)(u + 1);
        }
        obj->span_ = 0;

        var = u;
        while (base->tile( (int)var, (int)v)->objectMarker() == TILE_EMPTY) {
            var = (unsigned int)(var + (int)obj->step_);
            obj->span_ = (signed char)(obj->span_ + 1);
        }

        if (obj->step_ == 1)
            obj->guard_ = (unsigned char)(obj->endU_ + (unsigned char)obj->span_);
        else
            obj->guard_ = obj->endU_;
    } else if (axis == 2) {
        obj->step_ = 1;
        obj->endV_ = (unsigned char)v;
        if (base->tile( (int)u, (int)v - 1)->objectMarker() == TILE_EMPTY) {
            obj->step_ = (signed char)0xff;
            obj->endV_ = (unsigned char)(v + 1);
        }
        obj->span_ = 0;

        var = v;
        while (base->tile( (int)u, (int)var)->objectMarker() == TILE_EMPTY) {
            var = (unsigned int)(var + (int)obj->step_);
            obj->span_ = (signed char)(obj->span_ + 1);
        }

        if (obj->step_ == 1)
            obj->guard_ = (unsigned char)(obj->endV_ + (unsigned char)obj->span_);
        else
            obj->guard_ = obj->endV_;
    }

    obj->height_     = (unsigned char)height;
    obj->cellU_      = (signed char)obj->endU_;
    obj->cellV_      = (signed char)obj->endV_;
    obj->heightCell_ = (signed char)height;

    //     // endU_ and endV_ are read signed: a reversed bridge can store 0xff.
    obj->posU_  = (float)(int)(signed char)obj->endU_;
    obj->posY_  = (float)(int)height;
    obj->posV_  = (float)(int)(signed char)obj->endV_;
    obj->restU_ = (float)(int)(signed char)obj->endU_;
    obj->restY_ = (float)(int)height;  // The same value as posY_.
    obj->restV_ = (float)(int)(signed char)obj->endV_;

    obj->slot_       = (unsigned char)slot;
    obj->tileHeight_ = (unsigned char)height;
    obj->phase_      = 0;
    obj->armed_      = 0;

    tile->setBridgeSlot((unsigned char)slot);
    tile->setBridgeAxis((unsigned char)axis);
    tile->setField1f6(0);


    if (s_diag_place)
        g_logger.write("bridgeobject:   bridge done -- +0x38=%u +0x45=%u +0x57=%d "
                  "+0x5d=%u +0x5e=%u\n",
                  (unsigned)obj->guard_, (unsigned)(unsigned char)obj->span_,
                  (int)obj->step_, (unsigned)obj->endU_, (unsigned)obj->endV_);
}

void BridgeObject::playArmSound(PlayEvents &events)
{
    events.soundStart({SoundSource::Bridge, id(), Sfx::BridgeMoving}, (float)(int)cellU_,
                      (float)(int)heightCell_, -(float)(int)cellV_);
}

/* DETERMINISM: truncates toward zero, keeping the low byte. */
static inline signed char ftol_c(double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first   = 0;
static int s_logged_stamp   = 0;
static int s_logged_unstamp = 0;

void BridgeObject::tick(PlayEvents &events)
{
    const SoundKey moving{SoundSource::Bridge, id(), Sfx::BridgeMoving};
    unsigned char axis;
    double elapsed;
    double span100;
    signed char cu, cv;

    diag_init();

    if (!s_logged_first) {
        s_logged_first = 1;
        g_logger.write("bridgeobject: first bridge tick -- this=%p\n", (void *)this);
    }
    if (s_diag_deck) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            g_logger.write("bridgeobject: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_ = *clock_;

    axis = axis_;

    if (armed_ == 0)
        return;

    elapsed = (double)now_ - (double)phaseStart_;

    //     // DETERMINISM: an unsigned conversion, so a negative span gives a huge
    //     // threshold.  A NaN elapsed ends the phase.
    span100 = (double)(unsigned int)((int)span_ * 100);

    if (phase_ == 0) {

        if (!((double)span100 > elapsed)) {
            //             // Snaps to (int)(step * span) + rest, never the live coordinate:
            //             // one cell index, then both floats.
            events.soundStop(moving);
            armed_ = 0;
            phase_ = 1;

            if (axis == 1)
                cellU_ = ftol_c((double)(int)((int)step_ * (int)span_)
                                + (double)restU_);
            else
                cellV_ = ftol_c((double)(int)((int)step_ * (int)span_)
                                + (double)restV_);

            posU_ = (float)(int)cellU_;
            posV_ = (float)(int)cellV_;
            return;
        }

        //         // The sound is placed from last frame's cell, before the move.
        events.soundMove(moving, (float)(int)cellU_, (float)(int)heightCell_, -(float)(int)cellV_);

        //         // DETERMINISM: double, associated as written; a one-ulp change
        //         // moves a whole cell.
        {
            double t = elapsed * (double)K_MS_TO_TILE;
            if (axis == 1)
                posU_ = (float)(t * (double)(int)step_
                                + (double)restU_);
            else
                posV_ = (float)(t * (double)(int)step_
                                + (double)restV_);
        }

        cu = ftol_c((double)posU_);
        cellU_ = cu;
        cv = ftol_c((double)posV_);
        cellV_ = cv;

        //         // PRESERVED: axis 1 with u at or past the guard falls into the axis 2
        //         // test and returns without stamping.
        if ((axis == 1 && cu < (signed char)guard_)
            || (axis == 2 && cv < (signed char)guard_)) {
            Tile *t = map_->tile( cu, cv);

            if (s_diag_deck && !s_logged_stamp) {
                s_logged_stamp = 1;
                g_logger.write("bridgeobject: first extend stamp -- axis=%u "
                          "cell=(%d,%d) height=%u\n",
                          (unsigned)axis, (int)cu, (int)cv,
                          (unsigned)tileHeight_);
            }

            t->setObjectMarker(TILE_BRIDGE_DECK);
            t->setBusy(1);
            t->setHeight(tileHeight_);
            t->setBridgeAxis(axis);
            t->setField1f6(1);
            t->setBridgeSlot(slot_);
        }
        return;
    }

    if (!((double)span100 > elapsed)) {
        //             // Snaps back to rest.
        events.soundStop(moving);
        armed_ = 0;
        phase_ = 0;

        if (axis == 1)
            cellU_ = ftol_c((double)restU_);
        else
            cellV_ = ftol_c((double)restV_);

        posU_ = (float)(int)cellU_;
        posV_ = (float)(int)cellV_;
        return;
    }

    events.soundMove(moving, (float)(int)cellU_, (float)(int)heightCell_, -(float)(int)cellV_);

    //         // DETERMINISM: the destination first, then the travel subtracted;
    //         // not re-associated.
    {
        int step = (int)step_;
        double travel = elapsed * (double)K_MS_TO_TILE
                             * (double)step;
        if (axis == 1)
            posU_ = (float)(((double)(int)((int)span_ * step)
                             + (double)restU_) - travel);
        else
            posV_ = (float)(((double)(int)((int)span_ * step)
                             + (double)restV_) - travel);
    }

    cu = ftol_c((double)posU_);
    cellU_ = cu;
    cv = ftol_c((double)posV_);
    cellV_ = cv;

    if ((axis == 1 && cu < (signed char)guard_)
        || (axis == 2 && cv < (signed char)guard_)) {
        Tile *t = map_->tile( cu, cv);

        if (s_diag_deck && !s_logged_unstamp) {
            s_logged_unstamp = 1;
            g_logger.write("bridgeobject: first retract unstamp -- axis=%u "
                      "cell=(%d,%d)\n", (unsigned)axis, (int)cu, (int)cv);
        }

        //             // PRESERVED: clears four of the six fields the extend sets; the
        //             // axis and slot are left behind.
        t->setObjectMarker(0);
        t->setBusy(0);
        t->setHeight(0);
        t->setField1f6(0);
    }
}




static void deck_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    // As the map draws one: extended deck inside the span's outline.
    dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * 0.6f, b.y), IM_COL32(190, 140, 80, 255));
    dl->AddRect(a, b, IM_COL32(170, 110, 50, 255), 0.0f, 0, 2.0f);
}

/* Bridges are objects, not tiles: the builder clears their cells.  The whole
 * span is outlined; the deck fills as far as it has extended. */
void BridgeObject::debugDraw(const SwitchCells &sc_) const
{
    if (!dbg::active())
        return;
    dbg::MapLayer layer;
    if (!layer)
        return;
    const dbg::MapView &m = *layer;
    ImDrawList *dl = m.dl;
    const BridgeExtent b = extent();
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
    const SwitchCells *sc = &sc_;
    const int hAlong = alongU ? m.hoverU : m.hoverV, hAcross = alongU ? m.hoverV : m.hoverU;
    const bool onSpan = hAcross == (int)cross && hAlong >= std::min(rest, end) && hAlong < std::max(rest, end);
    bool show = m.links || onSpan;
    for (unsigned k = 0; k < sc->count(b.slot); k++)
        show = show || m.hovered(sc->cellU(b.slot, k), sc->cellV(b.slot, k));
    for (unsigned k = 0; show && k < sc->count(b.slot); k++)
        dl->AddLine(m.centre((float)sc->cellU(b.slot, k), (float)sc->cellV(b.slot, k)), mid,
                    IM_COL32(230, 40, 40, 200), std::max(1.0f, m.cell * 0.08f));

    if (onSpan) {
        m.separator();
        m.tip("bridge on switch %d, along %s, %d cells", b.slot + 1, alongU ? "U" : "V", b.span);
        m.tip("%s, %s next", b.armed ? "moving" : "still", b.phase ? "retracts" : "extends");
    }
    m.legend(0, "bridge", deck_swatch);
}
