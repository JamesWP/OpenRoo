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
 * Controls: KAROO_SIM_FX=liftflip reverses the departure direction, =placeaxis
 * exchanges u and v in the spawn, =keepobjects makes the purge do nothing.
 * KAROO_LIFT_DIAG=1 logs the first rise, fall and departure and a tick count;
 * KAROO_PLACE_DIAG and KAROO_RESET_DIAG log spawns and purges. */

#include <windows.h>
#include <stddef.h>
#include <new>
#include <string.h>

#include "liftobject.h"
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "log.h"

static const float  K_MS_TO_HEIGHT = 0.005f;
static const double K_PARK_DWELL   = 1500.0;

static int s_fx_liftflip    = 0;
static int s_fx_placeaxis   = 0;
static int s_fx_keepobjects = 0;
static int s_diag_lift      = 0;
static int s_diag_place     = 0;
static int s_diag_reset     = 0;
static int s_init           = 0;

static int env_set(const char *name, char *buf, DWORD cb)
{
    DWORD n = GetEnvironmentVariableA(name, buf, cb);
    return n > 0 && n < cb;
}

static void fx_init(void)
{
    char buf[64];

    if (s_init)
        return;
    s_init = 1;

    if (env_set("KAROO_SIM_FX", buf, sizeof(buf))) {
        if (strcmp(buf, "liftflip") == 0) {
            s_fx_liftflip = 1;
            log_write("liftobject: KAROO_SIM_FX=liftflip -- departure "
                      "direction inverted\n");
        } else if (strcmp(buf, "placeaxis") == 0) {
            s_fx_placeaxis = 1;
            log_write("liftobject: KAROO_SIM_FX=placeaxis -- lift spawn "
                      "exchanges u and v\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            log_write("liftobject: KAROO_SIM_FX=keepobjects -- lift purge "
                      "does nothing\n");
        }
    }

    if (env_set("KAROO_LIFT_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_lift = 1;
    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

LiftObject *LiftObject::create()
{
    return new (std::nothrow) LiftObject;
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
    sound_  = 0;
}

LiftObject::~LiftObject()
{
}

static int s_logged_spawn = 0;
static int s_logged_oom   = 0;

void LiftObject::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                       unsigned int baseArg, unsigned int topArg)
{
    unsigned int u, v, base, top;
    unsigned char n;
    LiftObject *obj;
    Tile *tile;
    void *raw;

    fx_init();

    //     // The only place the spawn reads u and v: placeaxis swaps them here.
    u = uArg & 0xff;
    v = vArg & 0xff;
    if (s_fx_placeaxis) {
        unsigned int t = u;
        u = v;
        v = t;
    }
    base = baseArg & 0xff;
    top  = topArg & 0xff;

    raw = create();
    if (raw == 0) {
        if (s_diag_place && !s_logged_oom) {
            s_logged_oom = 1;
            log_write("liftobject: ALLOCATION FAILED in spawn -- the original "
                      "would store through the slot, which now holds NULL\n");
        }
    }

    n = game->liftCount();
    game->setLiftSlot(n, (LiftObject *)raw);
    obj  = (LiftObject *)raw;
    tile = Tile::at(game->tileBase(), (int)u, (int)v);

    if (s_diag_place && !s_logged_spawn) {
        s_logged_spawn = 1;
        log_write("liftobject: first lift spawn -- slot=%u u=%u v=%u "
                  "base=%u top=%u obj=%p\n",
                  (unsigned)n, u, v, base, top, (void *)obj);
    }

    obj->clock_    = game->clock();
    obj->tickStep_   = game->tickStep();
    obj->tileBase_ = game->tileBase();

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

    obj->sound_ = 0;
    tile->clearLiftMovingSince();

    obj->phaseStart_ = *game->clock();

    obj->atTop_ = 0;
    obj->state_ = 1;

    game->setLiftCount((unsigned char)(n + 1));
}

static int s_logged_purge = 0;
static unsigned s_live_purges = 0;

void LiftObject::purgeAll(Game *game)
{
    unsigned char i;

    fx_init();

    if (s_fx_keepobjects)
        return;

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        log_write("liftobject: first purge -- count=%u\n",
                  (unsigned)game->liftCount());
    }

    i = 0;
    if (game->liftCount() != 0) {
        if (s_diag_reset)
            log_write("liftobject: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->liftCount());
        do {
            if (game->soundCreated() != 0) {
                audiodev::Buffer *h = game->liftSlot(i)->sound_;
                if (h != 0)
                    game->soundManager()->releaseStaticForOwner(h, 1);
            }
            LiftObject *obj = game->liftSlot(i);
            if (obj != 0)
                delete obj;
            i++;
        } while (i < game->liftCount());
    }
    game->setLiftCount(0);
}

/* DETERMINISM: truncates toward zero, keeping the low byte.  The tick passes
 * the unrounded long double, not the float stored in height_: truncating the
 * float would differ wherever its rounding crosses an integer. */
static inline signed char ftol_c(long double v)
{
    return (signed char)(long long)v;
}

static unsigned long s_ticks = 0;
static int s_logged_first  = 0;
static int s_logged_rise   = 0;
static int s_logged_fall   = 0;
static int s_logged_depart = 0;

void LiftObject::tick()
{
    Tile *t;

    fx_init();

    //     // Logged even with the diag off, so a log shows whether any lift ran.
    if (!s_logged_first) {
        s_logged_first = 1;
        log_write("liftobject: first lift tick -- this=%p\n", (void *)this);
    }
    if (s_diag_lift) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            log_write("liftobject: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_ = *clock_;

    if (state_ == 1) {
        long double h = ((long double)now_ - (long double)phaseStart_)
                        * (long double)K_MS_TO_HEIGHT
                        + (long double)(int)baseHeight_;
        height_     = (float)h;
        heightCell_ = ftol_c(h);

        //         // Compares against the rounded height_; a NaN completes the rise.
        if (!((long double)(int)topHeight_ > (long double)height_)) {
            signed char top = Tile::at(tileBase_, cellU_, cellV_)->liftTop();

            if (s_diag_lift && !s_logged_rise) {
                s_logged_rise = 1;
                log_write("liftobject: first completed rise -- cell=(%d,%d) "
                          "top=%d\n", (int)cellU_, (int)cellV_, (int)top);
            }

            heightCell_ = top;
            phaseStart_ = now_;
            height_ = (float)(int)top;
            atTop_  = 1;
            state_  = 0;

            if (sound_ != 0)
                sound_->stop();
        }

        if (sound_ != 0)
            sound_->setPosition((float)(int)cellU_, height_,
                                  -(float)(int)cellV_, 1);
    }

    if (state_ == 2) {
        long double h = (long double)(int)topHeight_
                        - ((long double)now_ - (long double)phaseStart_)
                          * (long double)K_MS_TO_HEIGHT;
        height_ = (float)h;
        {
            signed char c = ftol_c(h);
            heightCell_ = c;

            //             // An integer compare, where the rise compares floats.
            if ((int)c <= (int)baseHeight_ - 1) {
                signed char bot =
                    Tile::at(tileBase_, cellU_, cellV_)->liftBottom();

                if (s_diag_lift && !s_logged_fall) {
                    s_logged_fall = 1;
                    log_write("liftobject: first completed fall -- "
                              "cell=(%d,%d) bottom=%d\n",
                              (int)cellU_, (int)cellV_, (int)bot);
                }

                heightCell_ = bot;
                phaseStart_ = now_;
                height_ = (float)(int)bot;
                atTop_  = 0;
                state_  = 0;

                if (sound_ != 0)
                    sound_->stop();
            }
        }

        if (sound_ != 0)
            sound_->setPosition((float)(int)cellU_, height_,
                                  -(float)(int)cellV_, 1);
    }

    t = Tile::at(tileBase_, cellU_, cellV_);

    if (state_ == 0) {
        //         // The park time is published before the departure test replaces it.
        t->setLiftParkedSince(phaseStart_);
        t->setLiftDwell(K_PARK_DWELL);

        //         // A NaN does not depart.
        if (now_ - phaseStart_ >= K_PARK_DWELL) {
            int latch = atTop_;

            if (s_fx_liftflip)
                latch = (latch != 0) ? 0 : 1;

            phaseStart_ = now_;
            state_ = (signed char)((latch != 0) + 1);  // atTop_ set: fall; clear: rise.

            if (s_diag_lift && !s_logged_depart) {
                s_logged_depart = 1;
                log_write("liftobject: first depart -- cell=(%d,%d) "
                          "latch=%d state=%d\n",
                          (int)cellU_, (int)cellV_, latch, (int)state_);
            }

            if (sound_ != 0) {
                sound_->setPosition((float)(int)cellU_, height_,
                                      -(float)(int)cellV_, 1);
                sound_->play(true);
            }
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

