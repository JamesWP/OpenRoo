/* BridgeObject: spawn, tick, purge and the deck quad.
 *
 * A switch arms a bridge; the tick then runs one phase and disarms it:
 *   EXTENDING   coord = rest + elapsed * 0.01 * step
 *   RETRACTING  coord = (span * step + rest) - elapsed * 0.01 * step
 * A phase ends when elapsed >= span * 100 ms: the sound halts, the bridge
 * disarms, the phase flips and the coordinate snaps.  While moving, each cell
 * short of the guard is stamped as deck (extending) or cleared (retracting).
 *
 * Controls: KAROO_SIM_FX=deckaxis swaps the travel axis 1 and 2 in the tick,
 * =bridgespan exchanges the spawn's two far-end arms, =keepobjects makes the
 * purge do nothing.  Exchanging the spawn's axis codes instead walks a scan
 * out of the tile array on Space\Bridge01, so bridgespan leaves the scan
 * alone.  KAROO_DECK_DIAG=1 logs the first stamp and unstamp and a tick count;
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
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "logger.h"

/* The float nearest 0.01; widened where it is multiplied. */
static const float K_MS_TO_TILE = 0.01f;

static int s_fx_deckaxis  = 0;
static int s_fx_bridgespan  = 0;
static int s_fx_keepobjects = 0;
static int s_diag_deck    = 0;
static int s_diag_place     = 0;
static int s_diag_reset     = 0;
static int s_init           = 0;

static int env_set(const char *name, char *buf, uint32_t cb)
{
    uint32_t n = sysdev::getEnv(name, buf, cb);
    return n > 0 && n < cb;
}

static void fx_init(void)
{
    char buf[64];

    if (s_init)
        return;
    s_init = 1;

    if (env_set("KAROO_SIM_FX", buf, sizeof(buf))) {
        if (strcmp(buf, "deckaxis") == 0) {
            s_fx_deckaxis = 1;
            g_logger.write("bridgeobject: KAROO_SIM_FX=deckaxis -- travel axis "
                      "1<->2 flipped\n");
        } else if (strcmp(buf, "bridgespan") == 0) {
            s_fx_bridgespan = 1;
            g_logger.write("bridgeobject: KAROO_SIM_FX=bridgespan -- the two arms "
                      "that compute the bridge's far end (+0x38) are "
                      "exchanged, so a forward run records the backward extent "
                      "and vice versa.  The SCAN is untouched, so this stays "
                      "in bounds\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            g_logger.write("bridgeobject: KAROO_SIM_FX=keepobjects -- bridge purge "
                      "does nothing\n");
        }
    }

    if (env_set("KAROO_DECK_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_deck = 1;
    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

BridgeObject *BridgeObject::create()
{
    return new (std::nothrow) BridgeObject;
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
static int s_logged_oom   = 0;

void BridgeObject::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                         unsigned int heightArg, unsigned int slotArg,
                         unsigned int axisArg)
{
    unsigned int u, v, height, axis, slot;
    unsigned int var;
    LevelMap *base;
    BridgeObject *obj;
    Tile *tile;

    fx_init();

    u      = uArg & 0xff;
    v      = vArg & 0xff;
    height = heightArg & 0xff;
    slot   = slotArg & 0xff;
    axis   = axisArg & 0xff;

    obj = create();
    if (obj == 0 && s_diag_place && !s_logged_oom) {
        s_logged_oom = 1;
        g_logger.write("bridgeobject: ALLOCATION FAILED in spawn -- the original "
                  "faults at 0xc, and so does this\n");
    }

    //     // The slot is the switch slot argument, not the count; the count is only
    //     // a running total.  PRESERVED: a failed allocation is not checked; the
    //     // first store faults.
    game->setBridgeSlot(slot, obj);

    obj->clock_  = game->clock();
    obj->tickStep_ = game->tickStep();
    obj->axis_   = (unsigned char)axis;

    base = game->map();
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

        if ((obj->step_ == 1) != (s_fx_bridgespan != 0))
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

        if ((obj->step_ == 1) != (s_fx_bridgespan != 0))
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

    obj->sound_ = 0;

    game->setBridgeCount((unsigned char)(game->bridgeCount() + 1));

    if (s_diag_place)
        g_logger.write("bridgeobject:   bridge done -- +0x38=%u +0x45=%u +0x57=%d "
                  "+0x5d=%u +0x5e=%u\n",
                  (unsigned)obj->guard_, (unsigned)(unsigned char)obj->span_,
                  (int)obj->step_, (unsigned)obj->endU_, (unsigned)obj->endV_);
}

/* The purge indexes with a signed int against the zero-extended count. */
static int s_logged_purge = 0;
static unsigned s_live_purges = 0;

void BridgeObject::purgeAll(Game *game)
{
    int i;

    fx_init();

    if (s_fx_keepobjects)
        return;

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        g_logger.write("bridgeobject: first purge -- count=%u\n",
                  (unsigned)game->bridgeCount());
    }

    i = 0;
    if (game->bridgeCount() != 0) {
        if (s_diag_reset)
            g_logger.write("bridgeobject: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->bridgeCount());
        do {
            if (game->soundCreated() != 0) {
                audiodev::Buffer *h = game->bridgeSlot(i)->sound_;
                if (h != 0)
                    game->soundManager()->releaseStaticForOwner(h, 1);
            }
            BridgeObject *obj = game->bridgeSlot(i);
            if (obj != 0)
                delete obj;
            i++;
        } while (i < (int)(unsigned int)game->bridgeCount());
    }
    game->setBridgeCount(0);
}

void BridgeObject::playArmSound()
{
    if (sound_ != 0) {
        sound_->setPosition((float)(int)cellU_, (float)(int)heightCell_,
                              -(float)(int)cellV_, 1);
        sound_->play(true);
    }
}

/* DETERMINISM: truncates toward zero, keeping the low byte. */
static inline signed char ftol_c(long double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first   = 0;
static int s_logged_stamp   = 0;
static int s_logged_unstamp = 0;

void BridgeObject::tick()
{
    unsigned char axis;
    long double elapsed;
    double span100;
    signed char cu, cv;

    fx_init();

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

    //     // deckaxis flips the axis here, the one point every axis decision
    //     // reads.
    axis = axis_;
    if (s_fx_deckaxis) {
        if (axis == 1)      axis = 2;
        else if (axis == 2) axis = 1;
    }

    if (armed_ == 0)
        return;

    elapsed = (long double)now_ - (long double)phaseStart_;

    //     // DETERMINISM: an unsigned conversion, so a negative span gives a huge
    //     // threshold.  A NaN elapsed ends the phase.
    span100 = (double)(unsigned int)((int)span_ * 100);

    if (phase_ == 0) {

        if (!((long double)span100 > elapsed)) {
            //             // Snaps to (int)(step * span) + rest, never the live coordinate:
            //             // one cell index, then both floats.
            if (sound_ != 0)
                sound_->stop();
            armed_ = 0;
            phase_ = 1;

            if (axis == 1)
                cellU_ = ftol_c((long double)(int)((int)step_ * (int)span_)
                                + (long double)restU_);
            else
                cellV_ = ftol_c((long double)(int)((int)step_ * (int)span_)
                                + (long double)restV_);

            posU_ = (float)(int)cellU_;
            posV_ = (float)(int)cellV_;
            return;
        }

        //         // The sound is placed from last frame's cell, before the move.
        if (sound_ != 0)
            sound_->setPosition((float)(int)cellU_,
                                  (float)(int)heightCell_,
                                  -(float)(int)cellV_, 1);

        //         // DETERMINISM: long double, associated as written; a one-ulp change
        //         // moves a whole cell.
        {
            long double t = elapsed * (long double)K_MS_TO_TILE;
            if (axis == 1)
                posU_ = (float)(t * (long double)(int)step_
                                + (long double)restU_);
            else
                posV_ = (float)(t * (long double)(int)step_
                                + (long double)restV_);
        }

        cu = ftol_c((long double)posU_);
        cellU_ = cu;
        cv = ftol_c((long double)posV_);
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

            t->setObjectMarker(0x14);
            t->setBusy(1);
            t->setHeight(tileHeight_);
            t->setBridgeAxis(axis);
            t->setField1f6(1);
            t->setBridgeSlot(slot_);
        }
        return;
    }

    if (!((long double)span100 > elapsed)) {
        //             // Snaps back to rest.
        if (sound_ != 0)
            sound_->stop();
        armed_ = 0;
        phase_ = 0;

        if (axis == 1)
            cellU_ = ftol_c((long double)restU_);
        else
            cellV_ = ftol_c((long double)restV_);

        posU_ = (float)(int)cellU_;
        posV_ = (float)(int)cellV_;
        return;
    }

    if (sound_ != 0)
        sound_->setPosition((float)(int)cellU_,
                              (float)(int)heightCell_,
                              -(float)(int)cellV_, 1);

    //         // DETERMINISM: the destination first, then the travel subtracted;
    //         // not re-associated.
    {
        int step = (int)step_;
        long double travel = elapsed * (long double)K_MS_TO_TILE
                             * (long double)step;
        if (axis == 1)
            posU_ = (float)(((long double)(int)((int)span_ * step)
                             + (long double)restU_) - travel);
        else
            posV_ = (float)(((long double)(int)((int)span_ * step)
                             + (long double)restV_) - travel);
    }

    cu = ftol_c((long double)posU_);
    cellU_ = cu;
    cv = ftol_c((long double)posV_);
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

/* The deck quad from the anchor to the live end, z negated, one unit wide;
 * bridgesurf.cpp sets the render states and draws it. */
bool BridgeObject::buildSurface(BridgeVertex v[4], double t, bool backward,
                                BridgeSurfaceInfo *info) const
{
    if (armed_ == 0 && phase_ == 0)
        return false;

    for (int q = 0; q < 4; q++) {
        v[q].x = v[q].y = v[q].z = 0.0f;
        v[q].diffuse = 0xFFFFFFFF;  // PRESERVED: dead; the caller rewrites it.
        v[q].u0 = v[q].v0 = v[q].u1 = v[q].v1 = 0.0f;
    }

    float ax = restU_;
    float ay = restY_;
    float az = -restV_;
    float bx = posU_;
    float by = posY_;
    float bz = -posV_;
    float n  = (float)span_;

    float dx = bx - ax, dy = by - ay, dz = bz - az;
    float len = (float)sqrt(dy * dy + dz * dz + dx * dx);  // Summed in the original order.
    float vs  = len * 0.25f;

    float f = (float)fmod(t * (double)0.001f / n, 1.0);
    if (backward)
        f = -f;
    float fn = f * n;

    if ((signed char)axis_ == 1) {
        float x0 = ax - 0.5f;
        float zlo = az - 0.5f, zhi = az + 0.5f;
        if (step_ > 0) {
            v[0].x = x0 + len; v[0].y = ay; v[0].z = zlo;
            v[0].u0 = 0.0f; v[0].v0 = vs - fn;
            v[0].u1 = 0.0f; v[0].v1 = 1.0f;
            v[1].x = x0;       v[1].y = ay; v[1].z = zlo;
            v[1].u0 = 0.0f; v[1].v0 = -fn;
            v[1].u1 = 0.0f; v[1].v1 = 0.0f;
            v[2].x = x0 + len; v[2].y = ay; v[2].z = zhi;
            v[2].u0 = 1.0f; v[2].v0 = vs - fn;
            v[2].u1 = 1.0f; v[2].v1 = 1.0f;
            v[3].x = x0;       v[3].y = ay; v[3].z = zhi;
            v[3].u0 = 1.0f; v[3].v0 = -fn;
            v[3].u1 = 1.0f; v[3].v1 = 0.0f;
        } else {
            v[0].x = x0;       v[0].y = ay; v[0].z = zlo;
            v[0].u0 = 0.0f; v[0].v0 = fn + vs;
            v[0].u1 = 0.0f; v[0].v1 = 1.0f;
            v[1].x = x0 - len; v[1].y = ay; v[1].z = zlo;
            v[1].u0 = 0.0f; v[1].v0 = fn;
            v[1].u1 = 0.0f; v[1].v1 = 0.0f;
            v[2].x = x0;       v[2].y = ay; v[2].z = zhi;
            v[2].u0 = 1.0f; v[2].v0 = fn + vs;
            v[2].u1 = 1.0f; v[2].v1 = 1.0f;
            v[3].x = x0 - len; v[3].y = ay; v[3].z = zhi;
            v[3].u0 = 1.0f; v[3].v0 = fn;
            v[3].u1 = 1.0f; v[3].v1 = 0.0f;
        }
    } else {
        float xhi = ax + 0.5f, xlo = ax - 0.5f;
        float z1 = az + 0.5f;
        if (step_ > 0) {
            v[0].x = xhi; v[0].y = ay; v[0].z = z1 - len;
            v[0].u0 = 1.0f; v[0].v0 = vs - fn;
            v[0].u1 = 1.0f; v[0].v1 = 1.0f;
            v[1].x = xlo; v[1].y = ay; v[1].z = z1 - len;
            v[1].u0 = 0.0f; v[1].v0 = vs - fn;
            v[1].u1 = 0.0f; v[1].v1 = 1.0f;
            v[2].x = xhi; v[2].y = ay; v[2].z = z1;
            v[2].u0 = 1.0f; v[2].v0 = -fn;
            v[2].u1 = 1.0f; v[2].v1 = 0.0f;
            v[3].x = xlo; v[3].y = ay; v[3].z = z1;
            v[3].u0 = 0.0f; v[3].v0 = -fn;
            v[3].u1 = 0.0f; v[3].v1 = 0.0f;
        } else {
            v[0].x = xhi; v[0].y = ay; v[0].z = z1;
            v[0].u0 = 1.0f; v[0].v0 = fn + vs;
            v[0].u1 = 1.0f; v[0].v1 = 1.0f;
            v[1].x = xlo; v[1].y = ay; v[1].z = z1;
            v[1].u0 = 0.0f; v[1].v0 = fn + vs;
            v[1].u1 = 0.0f; v[1].v1 = 1.0f;
            v[2].x = xhi; v[2].y = ay; v[2].z = z1 + len;
            v[2].u0 = 1.0f; v[2].v0 = fn;
            v[2].u1 = 1.0f; v[2].v1 = 0.0f;
            v[3].x = xlo; v[3].y = ay; v[3].z = z1 + len;
            v[3].u0 = 0.0f; v[3].v0 = fn;
            v[3].u1 = 0.0f; v[3].v1 = 0.0f;
        }
    }

    info->axis = (signed char)axis_;
    info->dir  = step_;
    info->n    = span_;
    info->len  = len;
    info->f    = f;
    return true;
}


static void deck_swatch(ImDrawList *dl, ImVec2 a, ImVec2 b, void *)
{
    // As the map draws one: extended deck inside the span's outline.
    dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * 0.6f, b.y), IM_COL32(190, 140, 80, 255));
    dl->AddRect(a, b, IM_COL32(170, 110, 50, 255), 0.0f, 0, 2.0f);
}

/* Bridges are objects, not tiles: the builder clears their cells.  The whole
 * span is outlined; the deck fills as far as it has extended. */
void BridgeObject::debugDraw() const
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
    const SwitchCells *sc = Game::instance()->switchCells();
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
