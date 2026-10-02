/* BreakableTile: spawn, tick and purge.
 *
 * The tick is a three-state machine on the tile under the breakable:
 *   ARM      someone on the tile, not armed, tile not busy -> armedAt = now
 *   FALL     armed and now - armedAt >= 1500 ms -> the cell becomes void,
 *            busy, and respawnPending unless noRespawn
 *   RESPAWN  respawnPending and now - armedAt > 5000 ms -> the tile is back
 * Both delays run from armedAt, so a tile that fell at 1500 ms is back 3500 ms
 * later.
 *
 * PRESERVED: Foe::remove never clears a tile's occupant byte, so a foe that
 * dies on a breakable leaves it set; when the tile respawns the stale occupant
 * re-arms it and it drops again 1.5 s later.  tests/destr-bait.rec waits out
 * that second fall.
 *
 * Controls: KAROO_SIM_FX=slowfall triples the fall delay (about 270 frames
 * instead of 90 on Forest\DestrStart), =placeaxis exchanges u and v in the
 * spawn, =keepobjects makes the purge do nothing.  KAROO_PLACE_DIAG=1 logs
 * spawns and a failed allocation; KAROO_RESET_DIAG=1 logs purges and sound
 * releases. */

#include <windows.h>
#include <stdint.h>
#include "sysdev.h"
#include <new>
#include <string.h>

#include "breakabletile.h"
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "logger.h"

static const double FALL_DELAY_MS    = 1500.0;
static const double RESPAWN_DELAY_MS = 5000.0;

static int s_fx_slowfall    = 0;
static int s_fx_placeaxis   = 0;
static int s_fx_keepobjects = 0;
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
        if (lstrcmpiA(buf, "slowfall") == 0) {
            s_fx_slowfall = 1;
            g_logger.write("breakabletile: KAROO_SIM_FX=slowfall -- fall delay "
                      "%.0f ms, not %.0f\n",
                      FALL_DELAY_MS * 3.0, FALL_DELAY_MS);
        } else if (strcmp(buf, "placeaxis") == 0) {
            s_fx_placeaxis = 1;
            g_logger.write("breakabletile: KAROO_SIM_FX=placeaxis -- u and v are "
                      "exchanged at the single point the spawn reads them\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            g_logger.write("breakabletile: KAROO_SIM_FX=keepobjects -- breakable "
                      "purge does nothing\n");
        }
    }

    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

BreakableTile *BreakableTile::create()
{
    return new (std::nothrow) BreakableTile;
}

BreakableTile::BreakableTile()
{
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
    fallSound_      = 0;
    respawnSound_   = 0;
    armed_          = 0;
    respawnPending_ = 0;
    justFell_       = 0;
    justRespawned_  = 0;
}

BreakableTile::~BreakableTile()
{
}

static unsigned s_spawns       = 0;
static int      s_logged_spawn = 0;
static int      s_logged_oom   = 0;

unsigned int BreakableTile::spawn(Game *game, unsigned int uArg,
                                  unsigned int vArg, unsigned int heightArg,
                                  unsigned int paramArg)
{
    unsigned int u, v, height, idx;
    unsigned char n;
    BreakableTile *obj;

    fx_init();
    s_spawns++;
    if (s_diag_place && (s_spawns % 500) == 0)
        g_logger.write("breakabletile: %u spawns\n", s_spawns);

    u = uArg & 0xff;
    v = vArg & 0xff;
    //     // The only place the spawn reads its coordinates: placeaxis swaps them
    //     // here.
    if (s_fx_placeaxis) {
        unsigned int t = u;
        u = v;
        v = t;
    }
    height = heightArg & 0xff;

    obj = create();
    if (obj == 0 && s_diag_place && !s_logged_oom) {
        s_logged_oom = 1;
        g_logger.write("breakabletile: ALLOCATION FAILED in spawn -- the original "
                  "would store through the slot, which now holds NULL\n");
    }

    n = game->breakableCount();
    game->setBreakableSlot(n, obj);

    if (s_diag_place && !s_logged_spawn) {
        s_logged_spawn = 1;
        g_logger.write("breakabletile: first breakable spawn -- slot=%u u=%u v=%u "
                  "height=%u p4=%u obj=%p\n",
                  (unsigned)n, u, v, height, paramArg & 0xff, (void *)obj);
    }

    //     // PRESERVED: a failed allocation is not checked; the first store faults.
    obj->clock_    = game->clock();
    obj->tickStep_   = game->tickStep();
    obj->tileBase_ = game->tileBase();

    obj->cellU_      = (signed char)u;
    obj->cellV_      = (signed char)v;
    obj->heightCell_ = (signed char)height;

    obj->posU_ = (float)(int)u;
    obj->posY_ = (float)(int)height;
    obj->posV_ = -(float)(int)v;  // PRESERVED: v is negated, u and height are not.

    obj->noRespawn_ = (int)(paramArg & 0xff);

    //     // PRESERVED: cellV_ is written a second time, with the same value.
    obj->cellV_ = (signed char)v;

    //     // The TILE_BREAKABLE marker is read back by SetupLevelObjects when it
    //     // places further objects, which is why placeaxis also fails
    //     // levelreport.py.
    idx = v + u * 100;
    Tile::at(game->tileBase(), (int)u, (int)v)->setObjectMarker(TILE_BREAKABLE);

    game->setBreakableCount((unsigned char)(n + 1));

    return idx & 0xffffff00;
}

static int      s_logged_purge   = 0;
static int      s_logged_release = 0;
static unsigned s_live_purges    = 0;

static void release_sound(Game *game, audiodev::Buffer *h)
{
    if (h == 0)
        return;
    if (s_diag_reset && !s_logged_release) {
        s_logged_release = 1;
        g_logger.write("breakabletile: first sound release -- h=%p\n", (void *)h);
    }
    game->soundManager()->releaseStaticForOwner(h, 1);
}

void BreakableTile::purgeAll(Game *game)
{
    unsigned char i;

    fx_init();

    if (s_fx_keepobjects)
        return;

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        g_logger.write("breakabletile: first purge -- count=%u\n",
                  (unsigned)game->breakableCount());
    }

    i = 0;
    if (game->breakableCount() != 0) {
        if (s_diag_reset)
            g_logger.write("breakabletile: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->breakableCount());
        do {
            //             // The two releases are siblings: a null fall sound does not skip
            //             // the respawn sound.
            if (game->soundCreated() != 0) {
                release_sound(game, game->breakableSlot(i)->fallSound_);
                release_sound(game, game->breakableSlot(i)->respawnSound_);
            }
            BreakableTile *obj = game->breakableSlot(i);
            if (obj != 0)
                delete obj;
            i++;
        } while (i < game->breakableCount());
    }
    game->setBreakableCount(0);
}

Tile *BreakableTile::tile() const
{
    return Tile::at(tileBase_, (int)cellU_, (int)cellV_);
}

void BreakableTile::playAtTile(audiodev::Buffer *snd, const Tile *t) const
{
    snd->setPosition((float)(int)cellU_,
                          (float)t->height(),
                          -(float)(int)cellV_,
                          1);
    snd->play(false);
}

void BreakableTile::tick()
{
    fx_init();
    const double fallDelay = s_fx_slowfall ? FALL_DELAY_MS * 3.0 : FALL_DELAY_MS;

    tickStepCopy_ = *tickStep_;
    now_        = *clock_;

    Tile *t = tile();
    const double now = now_;

    if ((signed char)t->occupant() != 0 && armed_ == 0 && t->busy() == 0) {
        armedAt_ = now;
        armed_   = 1;
        t->setBusy(0);  // PRESERVED: a dead store; busy was just tested for 0.
    }

    justFell_      = 0;
    justRespawned_ = 0;

    //     // DETERMINISM: the fall compares >= and the respawn >; with a fixed
    //     // step an exact hit is reachable, so the difference is observable.
    if (armed_ != 0) {
        const double elapsed = now - armedAt_;
        justFell_ = 0;
        if (elapsed >= fallDelay) {
            eventTime_ = now;
            justFell_  = 1;

            //             // PRESERVED: silent on the second fall of a re-armed tile.
            if (respawnPending_ == 0 && fallSound_ != 0)
                playAtTile(fallSound_, t);

            t->setObjectMarker(TILE_EMPTY);
            if (noRespawn_ == 0)
                respawnPending_ = 1;
            t->setBusy(1);
            armed_ = 0;
        }
    }

    if (respawnPending_ != 0) {
        const double elapsed = now - armedAt_;
        if (elapsed >= RESPAWN_DELAY_MS && elapsed != RESPAWN_DELAY_MS) {
            eventTime_     = now;
            justRespawned_ = 1;

            if (respawnSound_ != 0)
                playAtTile(respawnSound_, t);

            t->setObjectMarker(TILE_BREAKABLE);
            armed_          = 0;
            respawnPending_ = 0;
            t->setBusy(0);
        }
    }
}

