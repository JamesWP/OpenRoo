/* Bomb: construction, spawn, tick and remove.  Every function that touches a
 * Bomb field is here; bomb.h keeps the fields private.
 *
 * The tick is three thresholds on now - droppedAt, in ms:
 *   roll    < 2000   the pending move is the bomb's own facing, so the bomb
 *                    slides forward from the cell it was dropped on (it looks
 *                    placed in front of the player);
 *   blast  >= 2000   writes its height into each tile's blast height over the
 *                    3x3 block around it, diagonals included;
 *   clear  >= 2400   zeroes the blast height over the same block;
 *   remove >= 2600   moveState 4; the movement code then raises the flag
 *                    GameTick reads to call remove().
 * A tile's blast height is live: nonzero means that cell is exploding at that
 * height now.  A bombable block (TILE_BOMBABLE) at the blast's exact
 * height is marked busy (spent), stamped with the blast time, logs "obstacle
 * is exploding", and has its hidden contents promoted into its contents.
 *
 * PRESERVED:
 *   - the fuse test is !(diff >= 2000), so an unordered compare rolls; the
 *     2400 and 2600 tests are plain >=;
 *   - the loop bounds are re-read each pass (nothing in the body changes
 *     them);
 *   - the clear loop does not check the busy flag; the blast loop does;
 *   - the height compare is signed against unsigned, so a negative bomb
 *     height never matches;
 *   - the blast height is stored as the raw byte;
 *   - the spawn's position floats are (u, height, v), not in argument order;
 *   - each sound name is copied with an unbounded strcpy into a 256-byte
 *     buffer, stopped by the enabled flag after each name;
 *   - the remove releases eight sound fields (owner flag 0: a bomb never
 *     destroys a shared buffer).
 *
 * KAROO_BOMB_DIAG=1 logs the first blast and
 * a tick count every 5000; KAROO_SPAWN_DIAG=1 the first sound acquire and a
 * spawn count every 500; KAROO_REMOVE_DIAG=1 the first release and a removal
 * count every 500.  The first tick, spawn and removal are logged always, so
 * "no bombs" cannot be confused with "flag not set". */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>

#include "bomb.h"
#include "levelmap.h"
#include "tile.h"
#include <stdlib.h>
#include "movableentity.h"
#include "logger.h"

static const double K_FUSE_MS   = 2000.0;
static const double K_CLEAR_MS  = 2400.0;
static const double K_REMOVE_MS = 2600.0;

/* Diagnostics, read by value. */
static int s_diag_bomb    = 0;
static int s_diag_spawn   = 0;
static int s_diag_remove  = 0;
static int s_init         = 0;

static void diag_init(void)
{
    if (s_init)
        return;
    s_init = 1;

    s_diag_bomb   = sysdev::envFlag("KAROO_BOMB_DIAG");
    s_diag_spawn  = sysdev::envFlag("KAROO_SPAWN_DIAG");
    s_diag_remove = sysdev::envFlag("KAROO_REMOVE_DIAG");
}

/* Construction: the MovableEntity base, then the bomb's own stores in a fixed
 * order.  The timing fields are zeroed (see below), which the original left as
 * allocated.  */
Bomb::Bomb()
{
    // The MovableEntity constructor has run.
    soundSource_      = SoundSource::Bomb;
    dyingStarted_          = 0;
    removeRequested_  = 0;
    dying_          = 0;
    zoneCleared_      = 0;
    field_124         = 1;
    facing_           = 1;
    stuckSince_         = 0.0;
    iceDir_          = 0;
    moveDir_         = 0;
    sliding_          = 0;
    falling_         = 0;
    moveState_        = 0;
    held_          = 0;
    bombsCarried_          = 0;
    stepDuration_          = 200.0;
    bombDropRequest_          = 0;
    teleportPhase_          = 0;
    field_156         = 0;
    glides_          = 0;
    platformSlot_         = 0xff;
    launched_         = 0;
    gliding_          = 0;
    onLift_          = 0;
    kind_             = 9;
    blastSoundPlayed_ = 0;
    field_d8          = 0;
    stepGrace_          = 50.0;

    // The first step commit reads now_ - stepEnd_ to decide where the move
    // animation starts.  Zero is "long ago"; a recycled heap block could hold
    // a NaN there, which made animStart_ and then the bomb's position NaN --
    // and only in a run that draws, since drawing changes what the heap held.
    // The same fix as Foe::Foe.
    stepEnd_          = 0.0;
    animStart_        = 0.0;
    animDuration_     = 0.0;
    idleDuration_     = 0.0;
    lastActive_       = 0.0;
    lastContact_      = 0.0;
    dyingSince_       = 0.0;
    teleportSince_    = 0.0;
    fallStart_        = 0.0;
    fallSpeed_        = 0.0f;
}

static unsigned long s_spawns       = 0;
static int           s_logged_spawn = 0;

void Bomb::spawn(const EntityContext &ctx, EntityList<Bomb> &bombs, PlayEvents &events,
                 unsigned int uArg, unsigned int vArg, unsigned int hArg, unsigned int flagArg)
{
    unsigned char u = (unsigned char)(uArg & 0xff);
    unsigned char v = (unsigned char)(vArg & 0xff);
    unsigned char h = (unsigned char)(hArg & 0xff);
    unsigned char f = (unsigned char)(flagArg & 0xff);

    diag_init();

    Bomb &b = bombs.add(std::unique_ptr<Bomb>(new Bomb));

    if (!s_logged_spawn) {
        s_logged_spawn = 1;
        g_logger.write("bomb: first bomb spawn -- n=%u obj=%p "
                  "u=%u v=%u h=%u flag=%u\n",
                  (unsigned)bombs.size(), (void *)&b, (unsigned)u,
                  (unsigned)v, (unsigned)h, (unsigned)f);
    }
    if (s_diag_spawn) {
        ++s_spawns;
        if ((s_spawns % 500) == 0)
            g_logger.write("bomb: %lu spawns\n", s_spawns);
    }

    b.clock_    = ctx.clock;
    b.tickStep_ = ctx.tickStep;
    b.map_      = ctx.map;
    b.facing_   = f;

    b.cellU_      = (signed char)u;
    b.cellV_      = (signed char)v;
    b.heightCell_ = (signed char)h;

    b.posU_ = (float)(int)(unsigned int)u;
    b.posY_ = (float)(int)(unsigned int)h;
    b.posV_ = (float)(int)(unsigned int)v;

    b.droppedAt_ = *ctx.clock;

    events.entityCreated(SoundSource::Bomb, b.id());
}

static unsigned long s_removals        = 0;
static int           s_logged_remove   = 0;

void Bomb::remove(EntityList<Bomb> &bombs, std::size_t i, PlayEvents &events)
{
    diag_init();

    if (!s_logged_remove) {
        s_logged_remove = 1;
        g_logger.write("bomb: first bomb removal -- i=%u obj=%p\n",
                  (unsigned)i, (void *)&bombs[i]);
    }
    if (s_diag_remove) {
        ++s_removals;
        if ((s_removals % 500) == 0)
            g_logger.write("bomb: %lu removals\n", s_removals);
    }

    events.entityDestroyed(SoundSource::Bomb, bombs[i].id());
    bombs.erase(i);
}

static unsigned long s_ticks        = 0;
static int           s_logged_first = 0;
static int           s_logged_blast = 0;

Tile *Bomb::tile(int u, int v) const
{
    return map_->tile( u, v);
}

void Bomb::tick(PlayEvents &events)
{
    double diff;

    diag_init();

    if (!s_logged_first) {
        s_logged_first = 1;
        g_logger.write("bomb: first bomb tick -- this=%p\n", (void *)this);
    }
    if (s_diag_bomb) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            g_logger.write("bomb: %lu ticks\n", s_ticks);
    }

    // Refresh the two cached copies.
    tickStepCopy_ = *tickStep_;
    now_        = *clock_;

    // The roll sound, on the grid cell: (u, h, -v).
    if (dying_ == 0) {  // raised by the blast below
        events.soundPlay(sound(Sfx::BombRoll), (float)(int)cellU_, (float)(int)heightCell_,
                         -(float)(int)cellV_);
    } else {
        events.soundMove(sound(Sfx::BombRoll), (float)(int)cellU_, (float)(int)heightCell_,
                         -(float)(int)cellV_);
        events.soundStop(sound(Sfx::BombRoll));
    }

    // Roll: still fusing.
    diff = now_ - droppedAt_;
    if (!(diff >= K_FUSE_MS)) {
        pendingMove_ = facing_;
        updateMovement(events);
        if (moveDir_ != 0)
            held_ = 1;
        return;
    }

    // Blast.
    platformSlot_    = 0xff;
    held_     = 1;
    dying_     = 1;  // halts the roll sound
    moveDir_    = 0;
    pendingMove_ = 0;

    if (blastSoundPlayed_ == 0) {
        // From the float position, not the grid bytes.
        events.soundPlay(sound(Sfx::BombBlast), posU_, posY_, -posV_);
        blastSoundPlayed_ = 1;
    }

    if (s_diag_bomb && !s_logged_blast) {
        s_logged_blast = 1;
        g_logger.write("bomb: first blast at u=%d v=%d h=%d after %.0f ms\n",
                  (int)cellU_, (int)cellV_, (int)heightCell_, diff);
    }

    for (int u = (int)cellU_ - 1; u <= (int)cellU_ + 1; ++u) {
        for (int v = (int)cellV_ - 1; v <= (int)cellV_ + 1; ++v) {
            Tile *t = tile(u, v);

            // The raw byte of the height: PRESERVED.
            t->setBlastHeight((unsigned char)heightCell_);

            if ((signed char)t->objectMarker() == TILE_BOMBABLE &&
                (int)heightCell_ == (int)t->height() &&
                t->busy() == 0) {

                t->setBusy(1);  // spent
                t->setBlastTime(now_);
                t->setBlasting(true);

                g_logger.logMessage(1,
                                   "GAME: obstacle is exploding at:%d,%d,%d",
                                   u, v, (int)t->height());

                // Promote the hidden contents so they can be picked up.
                signed char hidden = (signed char)t->field202();
                if (hidden != 0) {
                    t->setContents((unsigned char)hidden);
                    t->setField202(0);
                }
            }
        }
    }

    // Clear: the kill zone closes.
    diff = now_ - droppedAt_;
    if (diff >= K_CLEAR_MS && zoneCleared_ == 0) {
        for (int u = (int)cellU_ - 1; u <= (int)cellU_ + 1; ++u) {
            for (int v = (int)cellV_ - 1; v <= (int)cellV_ + 1; ++v) {
                Tile *t = tile(u, v);

                t->setBlastHeight(0);

                // No busy test here: PRESERVED.
                if ((signed char)t->objectMarker() == TILE_BOMBABLE &&
                    (int)heightCell_ == (int)t->height())
                    t->setBlasting(false);
            }
        }
    }

    // Remove.
    diff = now_ - droppedAt_;
    if (diff >= K_REMOVE_MS && zoneCleared_ == 0) {
        zoneCleared_ = 1;
        moveState_   = 4;  // despawn
        updateMovement(events);
        return;
    }

    zoneCleared_ = 0;
    updateMovement(events);
}

