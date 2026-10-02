/* SlideObject: spawn, tick and purge.
 *
 * The tick is a three-state machine like the lift's, along one grid axis (kind
 * 0x0a: u, otherwise v), at the lift's rate and dwell:
 *   1 ADVANCING   coord = origin + elapsed * 0.005, clearing each tile it
 *                 leaves; ends at the limit and latches atLimit_
 *   2 RETREATING  coord = limit - elapsed * 0.005, the same way back
 *   0 PARKED      publishes its park time on the tile and, after 1500 ms,
 *                 clears its marker and departs in the latched direction
 * The state tests are separate ifs, so a slide that arrives parks in the same
 * tick.  Every tick it then re-stamps its marker and height on its current
 * tile and publishes its live position on its ORIGIN tile.
 *
 * Despite their names, blockstay and KAROO_BLOCK_DIAG act on slides.
 *
 * Controls: KAROO_SIM_FX=blockstay stops the vacated-tile clear, leaving a
 * trail of solid cells; =slideaxis exchanges the spawn's two track kinds;
 * =keepobjects makes the purge do nothing.  KAROO_BLOCK_DIAG=1 logs the first
 * vacate, arrival, return and departure and a tick count; KAROO_PLACE_DIAG=1
 * logs every spawn with its scan kind; KAROO_RESET_DIAG=1 logs purges. */

#include <windows.h>
#include "sysdev.h"
#include <stddef.h>
#include <new>
#include <string.h>

#include "slideobject.h"
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "logger.h"

static const float  K_MS_TO_TILE = 0.005f;
static const double K_PARK_DWELL = 1500.0;

static int s_fx_blockstay   = 0;
static int s_fx_slideaxis   = 0;
static int s_fx_keepobjects = 0;
static int s_diag_block     = 0;
static int s_diag_place     = 0;
static int s_diag_reset     = 0;
static int s_init           = 0;

static int env_set(const char *name, char *buf, DWORD cb)
{
    DWORD n = sysdev::getEnv(name, buf, cb);
    return n > 0 && n < cb;
}

static void fx_init(void)
{
    char buf[64];

    if (s_init)
        return;
    s_init = 1;

    if (env_set("KAROO_SIM_FX", buf, sizeof(buf))) {
        if (strcmp(buf, "blockstay") == 0) {
            s_fx_blockstay = 1;
            g_logger.write("slideobject: KAROO_SIM_FX=blockstay -- vacated tiles "
                      "are never released\n");
        } else if (strcmp(buf, "slideaxis") == 0) {
            s_fx_slideaxis = 1;
            g_logger.write("slideobject: KAROO_SIM_FX=slideaxis -- the two "
                      "slide-track kind codes are exchanged, so a track meant "
                      "to run along U is scanned along V and vice versa; scan, "
                      "stamping and recorded span all move together\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            g_logger.write("slideobject: KAROO_SIM_FX=keepobjects -- slide purge "
                      "does nothing\n");
        }
    }

    if (env_set("KAROO_BLOCK_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_block = 1;
    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

SlideObject *SlideObject::create()
{
    return new (std::nothrow) SlideObject;
}

SlideObject::SlideObject()
{
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
    sound_  = 0;
}

SlideObject::~SlideObject()
{
}

static int s_logged_spawn = 0;
static int s_logged_oom   = 0;

void SlideObject::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                        unsigned int heightArg, unsigned int kindArg)
{
    unsigned int u, v, height, kind;
    unsigned char n, var, last;
    Tile *base;
    SlideObject *obj;
    Tile *tile;

    fx_init();

    u      = uArg & 0xff;
    v      = vArg & 0xff;
    height = heightArg & 0xff;
    kind   = kindArg & 0xff;

    if (s_fx_slideaxis) {
        if (kind == TILE_SLIDE_U)
            kind = 0x0b;
        else if (kind == TILE_SLIDE_V)
            kind = 0x0a;
    }

    base = game->tileBase();
    tile = Tile::at(base, (int)u, (int)v);

    //     // Four fields of the spawn cell are cleared before the allocation.
    tile->setHeight(0);
    tile->setSlideTrack(0);
    tile->setClimbDir(0);
    tile->setField202(0);

    //     // PRESERVED: a failed allocation is not checked; the first store faults.
    obj = create();
    if (obj == 0 && s_diag_place && !s_logged_oom) {
        s_logged_oom = 1;
        g_logger.write("slideobject: ALLOCATION FAILED in spawn -- the original "
                  "would store through the slot, which now holds NULL\n");
    }

    n = game->slideCount();
    game->setSlideSlot(n, obj);

    if (s_diag_place) {
        s_logged_spawn++;
        g_logger.write("slideobject: slide spawn #%d -- slot=%u u=%u v=%u "
                  "height=%u kind=0x%x %s\n",
                  s_logged_spawn, (unsigned)n, u, v, height, kind,
                  (kind == TILE_SLIDE_U) ? "SCAN-U" :
                  (kind == TILE_SLIDE_V) ? "SCAN-V" : "no-scan");
    }

    obj->clock_    = game->clock();
    obj->tickStep_   = game->tickStep();
    obj->kind_     = (signed char)kind;
    obj->state_    = 1;
    obj->tileBase_ = base;

    //     // This clear fires for every kind but covers only the spawn cell.
    //     // PRESERVED: the V scan clears the marker of every cell it walks; the U
    //     // scan leaves them.  Both scans are unbounded and always stamp at least
    //     // the spawn cell.
    tile->setObjectMarker(0);

    if (kind == TILE_SLIDE_U) {
        var = (unsigned char)u;
        for (;;) {
            tile->setSlideTrack(1);
            tile->setSlideHeight((unsigned char)height);
            tile->setSlideSlot(n);
            tile->setSlideOrigin((unsigned char)u, (unsigned char)v);

            last = var;
            var  = (unsigned char)(var + 1);
            tile = Tile::at(base, (int)var, (int)v);
            if (tile->objectMarker() != TILE_EMPTY)
                break;
        }
        obj->trackStart_ = (unsigned char)u;
        obj->limit_      = last;
    } else if (kind == TILE_SLIDE_V) {
        var = (unsigned char)v;
        for (;;) {
            tile->setObjectMarker(0);
            tile->setSlideTrack(1);
            tile->setSlideHeight((unsigned char)height);
            tile->setSlideSlot(n);
            tile->setSlideOrigin((unsigned char)u, (unsigned char)v);

            last = var;
            var  = (unsigned char)(var + 1);
            tile = Tile::at(base, (int)u, (int)var);
            if (tile->objectMarker() != TILE_EMPTY)
                break;
        }
        obj->trackStart_ = (unsigned char)v;
        obj->limit_      = last;
    }

    obj->cellU_      = (signed char)u;
    obj->cellV_      = (signed char)v;
    obj->heightCell_ = (signed char)height;

    obj->posU_ = (float)(int)u;
    obj->posY_ = (float)(int)height;
    obj->posV_ = (float)(int)v;  // PRESERVED: not negated, unlike the breakable's.

    obj->originU_      = (signed char)u;
    obj->originV_      = (signed char)v;
    obj->originHeight_ = (signed char)height;
    obj->tileHeight_   = (unsigned char)height;

    obj->phaseStart_ = 0.0;

    //     // PRESERVED: for any other kind trackStart_ and limit_ are never written
    //     // and this subtracts two uninitialised bytes.  The one caller passes only
    //     // 0x0a or 0x0b.
    obj->span_  = (unsigned char)(obj->limit_ - obj->trackStart_);
    obj->sound_ = 0;

    game->setSlideCount((unsigned char)(n + 1));
}

static int s_logged_purge = 0;
static unsigned s_live_purges = 0;

void SlideObject::purgeAll(Game *game)
{
    unsigned char i;

    fx_init();

    if (s_fx_keepobjects)
        return;

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        g_logger.write("slideobject: first purge -- count=%u\n",
                  (unsigned)game->slideCount());
    }

    i = 0;
    if (game->slideCount() != 0) {
        if (s_diag_reset)
            g_logger.write("slideobject: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->slideCount());
        do {
            if (game->soundCreated() != 0) {
                audiodev::Buffer *h = game->slideSlot(i)->sound_;
                if (h != 0)
                    game->soundManager()->releaseStaticForOwner(h, 1);
            }
            SlideObject *obj = game->slideSlot(i);
            if (obj != 0)
                delete obj;
            i++;
        } while (i < game->slideCount());
    }
    game->setSlideCount(0);
}

/* DETERMINISM: truncates toward zero, keeping the low byte.  Each branch below
 * truncates twice with different arguments: the unrounded long double decides
 * whether a tile was vacated, the rounded float becomes the new cell.  They
 * differ wherever the float's rounding crosses an integer. */
static inline signed char ftol_c(long double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first   = 0;
static int s_logged_vacate  = 0;
static int s_logged_advance = 0;
static int s_logged_retreat = 0;
static int s_logged_depart  = 0;

void SlideObject::vacate()
{
    if (s_fx_blockstay)
        return;
    if (s_diag_block && !s_logged_vacate) {
        s_logged_vacate = 1;
        g_logger.write("slideobject: first vacate -- cell=(%d,%d)\n",
                  (int)cellU_, (int)cellV_);
    }
    Tile::at(tileBase_, cellU_, cellV_)->setObjectMarker(0);
    Tile::at(tileBase_, cellU_, cellV_)->setOccupant(0);
}

void SlideObject::tick()
{
    Tile *t;

    fx_init();

    if (!s_logged_first) {
        s_logged_first = 1;
        g_logger.write("slideobject: first slide tick -- this=%p\n", (void *)this);
    }
    if (s_diag_block) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            g_logger.write("slideobject: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_ = *clock_;

    if (state_ == 1) {
        long double travel = ((long double)now_ - (long double)phaseStart_)
                             * (long double)K_MS_TO_TILE;
        int done = 0;

        if (kind_ == 0x0a) {
            long double pos = travel + (long double)(int)originU_;
            posU_ = (float)pos;

            //             // A signed compare against the cell before it is updated.
            if (ftol_c(pos) > cellU_)
                vacate();

            cellU_ = ftol_c((long double)posU_);

            //             // limit_ is read unsigned to compare and signed to snap.  A NaN
            //             // completes the advance; it does not complete the retreat.
            if (!((long double)(int)limit_ > (long double)posU_)) {
                cellU_ = (signed char)limit_;
                phaseStart_ = now_;
                posU_ = (float)(int)(signed char)limit_;
                state_ = 0;
                atLimit_ = 1;
                done = 1;
            }
        } else {
            long double pos = travel + (long double)(int)originV_;
            posV_ = (float)pos;

            if (ftol_c(pos) > cellV_)
                vacate();

            cellV_ = ftol_c((long double)posV_);

            if (!((long double)(int)limit_ > (long double)posV_)) {
                cellV_ = (signed char)limit_;
                phaseStart_ = now_;
                posV_ = (float)(int)(signed char)limit_;
                state_ = 0;
                atLimit_ = 1;
                done = 1;
            }
        }

        if (done) {
            if (sound_ != 0)
                sound_->stop();

            if (s_diag_block && !s_logged_advance) {
                s_logged_advance = 1;
                g_logger.write("slideobject: first completed advance -- "
                          "cell=(%d,%d) limit=%u\n",
                          (int)cellU_, (int)cellV_, (unsigned)limit_);
            }
        }

        if (sound_ != 0)
            sound_->setPosition((float)(int)cellU_, posY_,
                                  -(float)(int)cellV_, 1);
    }

    if (state_ == 2) {
        int done = 0;

        if (kind_ == 0x0a) {
            long double pos = (long double)(int)limit_
                              - ((long double)now_ - (long double)phaseStart_)
                                * (long double)K_MS_TO_TILE;
            posU_ = (float)pos;

            if (ftol_c(pos) < cellU_)
                vacate();

            cellU_ = ftol_c((long double)posU_);

            if ((long double)posU_ <= (long double)(int)originU_) {
                cellU_ = originU_;
                phaseStart_ = now_;
                posU_ = (float)(int)originU_;
                state_ = 0;
                done = 1;
            }
        } else {
            long double pos = (long double)(int)limit_
                              - ((long double)now_ - (long double)phaseStart_)
                                * (long double)K_MS_TO_TILE;
            posV_ = (float)pos;

            if (ftol_c(pos) < cellV_)
                vacate();

            cellV_ = ftol_c((long double)posV_);

            if ((long double)posV_ <= (long double)(int)originV_) {
                cellV_ = originV_;
                state_ = 0;
                posV_ = (float)(int)originV_;
                phaseStart_ = now_;
                done = 1;
            }
        }

        if (done) {
            atLimit_ = 0;
            if (sound_ != 0)
                sound_->stop();

            if (s_diag_block && !s_logged_retreat) {
                s_logged_retreat = 1;
                g_logger.write("slideobject: first completed retreat -- "
                          "cell=(%d,%d)\n", (int)cellU_, (int)cellV_);
            }
        }

        if (sound_ != 0)
            sound_->setPosition((float)(int)cellU_, posY_,
                                  -(float)(int)cellV_, 1);
    }

    if (state_ == 0) {
        t = Tile::at(tileBase_, cellU_, cellV_);
        t->setSlideParkedSince(phaseStart_);
        t->setSlideDwell(K_PARK_DWELL);

        if (now_ - phaseStart_ >= K_PARK_DWELL) {
            //             // PRESERVED: unlike the lift, a departing slide restarts its
            //             // sound without setting its 3D position.
            Tile::at(tileBase_, cellU_, cellV_)->setObjectMarker(0);

            phaseStart_ = now_;
            state_ = (signed char)((atLimit_ != 0) + 1);

            if (s_diag_block && !s_logged_depart) {
                s_logged_depart = 1;
                g_logger.write("slideobject: first depart -- cell=(%d,%d) "
                          "latch=%d state=%d\n",
                          (int)cellU_, (int)cellV_, atLimit_, (int)state_);
            }

            if (sound_ != 0)
                sound_->play(true);
        }
    }

    t = Tile::at(tileBase_, cellU_, cellV_);
    t->setObjectMarker(0x0c);
    t->setHeight(tileHeight_);

    posY_ = (float)(int)heightCell_;

    //     // Indexed by the origin cell: the home tile carries the live position.
    t = Tile::at(tileBase_, originU_, originV_);
    t->setSlideCell((unsigned char)cellU_, (unsigned char)cellV_);
    t->setSlidePos(posU_, posY_, posV_);
}

