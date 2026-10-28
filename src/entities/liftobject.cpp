/* LiftObject: spawn, tick and purge.
 *
 * The tick is a three-state machine:
 *   1 RISING   height = base + elapsed * 0.005, until height >= top
 *   2 FALLING  height = top - elapsed * 0.005, until the truncated height
 *              <= base - 1
 *   0 PARKED   publishes its park time on the tile and, after 1500 ms,
 *              departs in the direction atTop_ selects
 * At 0.005 per ms a lift covers one height unit in 200 ms.  The state tests
 * are separate ifs, so a lift that arrives parks in the same tick.
 *
 * KAROO_LIFT_DIAG=1 logs the first rise, fall and departure and a tick count;
 * KAROO_PLACE_DIAG and KAROO_RESET_DIAG log spawns and purges. */

#include <stdint.h>
#include "sysdev.h"
#include <stddef.h>
#include <new>
#include <string.h>

#include "liftobject.h"
#include "levelmap.h"
#include "tile.h"
#include "logger.h"

static const float  K_MS_TO_HEIGHT = 0.005f;
static const double K_PARK_DWELL   = 1500.0;

static int s_diag_lift      = 0;
static int s_diag_place     = 0;
static int s_init           = 0;

static void diag_init(void)
{

    if (s_init)
        return;
    s_init = 1;

    s_diag_lift = sysdev::envFlag("KAROO_LIFT_DIAG");
    s_diag_place = sysdev::envFlag("KAROO_PLACE_DIAG");
}

/* Only the fields the original constructors write; the rest is left as `new`
 * returned it. */
LiftObject::LiftObject()
{
    posU_   = 0.0f;
    height_ = 0.0f;
    posV_   = 0.0f;
    state_  = 1;
    atTop_  = 1;
}

LiftObject::~LiftObject()
{
}

static int s_logged_spawn = 0;

void LiftObject::spawn(const EntityContext &ctx, EntityList<LiftObject> &lifts, unsigned int uArg,
                       unsigned int vArg, unsigned int baseArg, unsigned int topArg)
{
    unsigned int u, v, base, top;
    unsigned char n;
    LiftObject *obj;
    Tile *tile;

    diag_init();

    u = uArg & 0xff;
    v = vArg & 0xff;
    base = baseArg & 0xff;
    top  = topArg & 0xff;

    n    = (unsigned char)lifts.size();
    obj  = &lifts.add(std::unique_ptr<LiftObject>(new LiftObject));
    tile = ctx.map->tile( (int)u, (int)v);

    if (s_diag_place && !s_logged_spawn) {
        s_logged_spawn = 1;
        g_logger.write("liftobject: first lift spawn -- slot=%u u=%u v=%u "
                  "base=%u top=%u obj=%p\n",
                  (unsigned)n, u, v, base, top, (void *)obj);
    }

    obj->clock_    = ctx.clock;
    obj->tickStep_   = ctx.tickStep;
    obj->map_ = ctx.map;

    obj->cellU_      = (signed char)u;
    obj->cellV_      = (signed char)v;
    obj->slot_       = n;
    obj->heightCell_ = (signed char)base;
    obj->height_     = (float)(int)base;

    tile->setLiftBottom((unsigned char)base);
    obj->baseHeight_ = (signed char)base;
    tile->setLiftTop((unsigned char)top);
    obj->topHeight_  = (signed char)top;
    tile->setLiftSlot(n);

    tile->clearLiftMovingSince();

    obj->phaseStart_ = *ctx.clock;

    obj->atTop_ = 0;
    obj->state_ = 1;
}

/* DETERMINISM: truncates toward zero, keeping the low byte.  The tick passes
 * the unrounded double, not the float stored in height_: truncating the
 * float would differ wherever its rounding crosses an integer. */
static inline signed char ftol_c(double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first  = 0;
static int s_logged_rise   = 0;
static int s_logged_fall   = 0;
static int s_logged_depart = 0;

void LiftObject::tick(PlayEvents &events)
{
    const SoundKey moving{SoundSource::Lift, id(), Sfx::LiftMoving};
    Tile *t;

    diag_init();

    //     // Logged even with the diag off, so a log shows whether any lift ran.
    if (!s_logged_first) {
        s_logged_first = 1;
        g_logger.write("liftobject: first lift tick -- this=%p\n", (void *)this);
    }
    if (s_diag_lift) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            g_logger.write("liftobject: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_ = *clock_;

    if (state_ == 1) {
        double h = ((double)now_ - (double)phaseStart_)
                        * (double)K_MS_TO_HEIGHT
                        + (double)(int)baseHeight_;
        height_     = (float)h;
        heightCell_ = ftol_c(h);

        //         // Compares against the rounded height_; a NaN completes the rise.
        if (!((double)(int)topHeight_ > (double)height_)) {
            signed char top = map_->tile( cellU_, cellV_)->liftTop();

            if (s_diag_lift && !s_logged_rise) {
                s_logged_rise = 1;
                g_logger.write("liftobject: first completed rise -- cell=(%d,%d) "
                          "top=%d\n", (int)cellU_, (int)cellV_, (int)top);
            }

            heightCell_ = top;
            phaseStart_ = now_;
            height_ = (float)(int)top;
            atTop_  = 1;
            state_  = 0;

            events.soundStop(moving);
        }

        events.soundMove(moving, (float)(int)cellU_, height_, -(float)(int)cellV_);
    }

    if (state_ == 2) {
        double h = (double)(int)topHeight_
                        - ((double)now_ - (double)phaseStart_)
                          * (double)K_MS_TO_HEIGHT;
        height_ = (float)h;
        {
            signed char c = ftol_c(h);
            heightCell_ = c;

            //             // An integer compare, where the rise compares floats.
            if ((int)c <= (int)baseHeight_ - 1) {
                signed char bot =
                    map_->tile( cellU_, cellV_)->liftBottom();

                if (s_diag_lift && !s_logged_fall) {
                    s_logged_fall = 1;
                    g_logger.write("liftobject: first completed fall -- "
                              "cell=(%d,%d) bottom=%d\n",
                              (int)cellU_, (int)cellV_, (int)bot);
                }

                heightCell_ = bot;
                phaseStart_ = now_;
                height_ = (float)(int)bot;
                atTop_  = 0;
                state_  = 0;

                events.soundStop(moving);
            }
        }

        events.soundMove(moving, (float)(int)cellU_, height_, -(float)(int)cellV_);
    }

    t = map_->tile( cellU_, cellV_);

    if (state_ == 0) {
        //         // The park time is published before the departure test replaces it.
        t->setLiftParkedSince(phaseStart_);
        t->setLiftDwell(K_PARK_DWELL);

        //         // A NaN does not depart.
        if (now_ - phaseStart_ >= K_PARK_DWELL) {
            int latch = atTop_;

            phaseStart_ = now_;
            state_ = (signed char)((latch != 0) + 1);  // atTop_ set: fall; clear: rise.

            if (s_diag_lift && !s_logged_depart) {
                s_logged_depart = 1;
                g_logger.write("liftobject: first depart -- cell=(%d,%d) "
                          "latch=%d state=%d\n",
                          (int)cellU_, (int)cellV_, latch, (int)state_);
            }

            events.soundStart(moving, (float)(int)cellU_, height_, -(float)(int)cellV_);
        }

        //         // Cleared after the departure test, so a departing lift's moving-since
        //         // is set only on the next tick.
        t->clearLiftMovingSince();
    } else {
        t->setLiftMovingSince(phaseStart_);
    }

    t->setHeight((unsigned char)heightCell_);
    t->setLiftLiveHeight(height_);

    posU_ = (float)(int)cellU_;
    posV_ = (float)(int)cellV_;
}


