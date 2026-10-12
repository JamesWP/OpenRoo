/* FallingTile: spawn, tick and purge.
 *
 * The tick is a three-state machine on the tile under the falling tile:
 *   ARM      someone on the tile, not armed, tile not busy -> armedAt = now
 *   FALL     armed and now - armedAt >= 1500 ms -> the cell becomes void,
 *            busy, and respawnPending unless noRespawn
 *   RESPAWN  respawnPending and now - armedAt > 5000 ms -> the tile is back
 * Both delays run from armedAt, so a tile that fell at 1500 ms is back 3500 ms
 * later.
 *
 * PRESERVED: Foe::remove never clears a tile's occupant byte, so a foe that
 * dies on a falling tile leaves it set; when the tile respawns the stale occupant
 * re-arms it and it drops again 1.5 s later.  tests/destr-bait.rec waits out
 * that second fall.
 *
 * KAROO_PLACE_DIAG=1 logs
 * spawns and a failed allocation; KAROO_RESET_DIAG=1 logs purges and sound
 * releases. */

#include <strings.h>
#include <stdint.h>
#include "sysdev.h"
#include <new>
#include <string.h>

#include "fallingtile.h"
#include "soundvoice.h"
#include "levelmap.h"
#include "tile.h"
#include "logger.h"
#include "themedraw.h"

static const double FALL_DELAY_MS    = 1500.0;
static const double RESPAWN_DELAY_MS = 5000.0;

static int s_diag_place     = 0;
static int s_init           = 0;

static void diag_init(void)
{

    if (s_init)
        return;
    s_init = 1;

    s_diag_place = sysdev::envFlag("KAROO_PLACE_DIAG");
}

FallingTile::FallingTile()
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

FallingTile::~FallingTile()
{
}

static unsigned s_spawns       = 0;
static int      s_logged_spawn = 0;

void FallingTile::spawn(const EntityContext &ctx, EntityList<FallingTile> &fallings, unsigned int uArg,
                        unsigned int vArg, unsigned int heightArg, unsigned int paramArg)
{
    unsigned int u, v, height;
    unsigned char n;
    FallingTile *obj;

    diag_init();
    s_spawns++;
    if (s_diag_place && (s_spawns % 500) == 0)
        g_logger.write("fallingtile: %u spawns\n", s_spawns);

    u = uArg & 0xff;
    v = vArg & 0xff;
    height = heightArg & 0xff;

    n   = (unsigned char)fallings.size();
    obj = &fallings.add(std::unique_ptr<FallingTile>(new FallingTile));

    if (s_diag_place && !s_logged_spawn) {
        s_logged_spawn = 1;
        g_logger.write("fallingtile: first falling tile spawn -- slot=%u u=%u v=%u "
                  "height=%u p4=%u obj=%p\n",
                  (unsigned)n, u, v, height, paramArg & 0xff, (void *)obj);
    }

    //     // PRESERVED: a failed allocation is not checked; the first store faults.
    obj->clock_    = ctx.clock;
    obj->tickStep_   = ctx.tickStep;
    obj->map_ = ctx.map;

    obj->cellU_      = (signed char)u;
    obj->cellV_      = (signed char)v;
    obj->heightCell_ = (signed char)height;

    obj->posU_ = (float)(int)u;
    obj->posY_ = (float)(int)height;
    obj->posV_ = -(float)(int)v;  // PRESERVED: v is negated, u and height are not.

    obj->noRespawn_ = (int)(paramArg & 0xff);

    //     // The TILE_FALLING marker is read back by SetupLevelObjects when it
    //     // places further objects, which is why placeaxis also fails
    //     // the level load.
    ctx.map->tile( (int)u, (int)v)->setObjectMarker(TILE_FALLING);
}

void FallingTile::releaseSounds(SoundLibrary *sm)
{
    if (sm->active() && fallSound_ != 0)
        sm->releaseVoice(fallSound_, true);
    fallSound_ = 0;
    if (sm->active() && respawnSound_ != 0)
        sm->releaseVoice(respawnSound_, true);
    respawnSound_ = 0;
}

Tile *FallingTile::tile() const
{
    return map_->tile( (int)cellU_, (int)cellV_);
}

void FallingTile::playAtTile(SoundVoice *snd, const Tile *t) const
{
    snd->setPosition((float)(int)cellU_,
                          (float)t->height(),
                          -(float)(int)cellV_,
                          1);
    snd->play(false);
}

void FallingTile::tick()
{
    diag_init();
    const double fallDelay = FALL_DELAY_MS;

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

            t->setObjectMarker(TILE_FALLING);
            armed_          = 0;
            respawnPending_ = 0;
            t->setBusy(0);
        }
    }
}


void FallingTile::draw(draw::Frame &f, draw::Pass pass) const
{
    if (pass != draw::Pass::Particles || justFell_ == 0 || f.opts.particles == 0)
        return;
    draw::spawnBurst(f, THEME_OBJ_FALLING_DUST,
                     { (float)cellU_, (float)heightCell_, -(float)cellV_ });
}
