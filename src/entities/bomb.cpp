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
 * height now.  A destructible block (TILE_DESTRUCTIBLE) at the blast's exact
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
 *   - in the spawn, a failed allocation is written through before the slot is
 *     checked, and the second sound flag is tested twice;
 *   - the spawn's position floats are (u, height, v), not in argument order;
 *   - each sound name is copied with an unbounded strcpy into a 256-byte
 *     buffer, stopped by the enabled flag after each name;
 *   - the remove releases eight sound fields (owner flag 0: a bomb never
 *     destroys a shared buffer), re-reading the slot for each, and unlike the
 *     foe remove leaves the slot dangling.
 *
 * KAROO_SIM_FX=shortfuse halves the three thresholds; =spawnswap exchanges u
 * and v in the spawn (shared with foe.cpp); =keepid and =lowid are in the
 * remove tail (objectremove.cpp).  KAROO_BOMB_DIAG=1 logs the first blast and
 * a tick count every 5000; KAROO_SPAWN_DIAG=1 the first sound acquire and a
 * spawn count every 500; KAROO_REMOVE_DIAG=1 the first release and a removal
 * count every 500.  The first tick, spawn and removal are logged always, so
 * "no bombs" cannot be confused with "flag not set". */

#include <windows.h>
#include <string.h>

#include "bomb.h"
#include "game.h"
#include "tile.h"
#include <stdlib.h>
#include "movableentity.h"
#include "objectremove.h"
#include "soundmanager.h"
#include "audiodev.h"
#include "logger.h"
#include "gameglobals.h"

#include <new>  // std::nothrow

static const double K_FUSE_MS   = 2000.0;
static const double K_CLEAR_MS  = 2400.0;
static const double K_REMOVE_MS = 2600.0;

/* Controls and diagnostics, read by value. */
static int s_fx_shortfuse = 0;
static int s_fx_spawnswap = 0;
static int s_diag_bomb    = 0;
static int s_diag_spawn   = 0;
static int s_diag_remove  = 0;
static int s_init         = 0;

static int env_on(const char *name)
{
    char buf[64];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0;
}

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "shortfuse") == 0) {
            s_fx_shortfuse = 1;
            g_logger.write("bomb: KAROO_SIM_FX=shortfuse -- fuse %.0f ms, not %.0f\n",
                      K_FUSE_MS / 2.0, K_FUSE_MS);
        } else if (strcmp(buf, "spawnswap") == 0) {
            s_fx_spawnswap = 1;
            g_logger.write("bomb: KAROO_SIM_FX=spawnswap -- the bomb spawn tile is "
                      "transposed\n");
        }
    }

    s_diag_bomb   = env_on("KAROO_BOMB_DIAG");
    s_diag_spawn  = env_on("KAROO_SPAWN_DIAG");
    s_diag_remove = env_on("KAROO_REMOVE_DIAG");
}

static inline double fuse_ms(void)   { return s_fx_shortfuse ? K_FUSE_MS   / 2.0 : K_FUSE_MS;   }
static inline double clear_ms(void)  { return s_fx_shortfuse ? K_CLEAR_MS  / 2.0 : K_CLEAR_MS;  }
static inline double remove_ms(void) { return s_fx_shortfuse ? K_REMOVE_MS / 2.0 : K_REMOVE_MS; }

/* Construction: the MovableEntity base, then the bomb's own stores in a fixed
 * order; every other byte is left as allocated.  PRESERVED: the sound slots
 * +0xab..+0xcb are cleared a second time after zeroSoundSlots().  */
Bomb *Bomb::create()
{
    return new (std::nothrow) Bomb;
}

Bomb::Bomb()
{
    // The MovableEntity constructor has run.
    rollSound_        = 0;
    blastSound_       = 0;
    zeroSoundSlots();
    field_7a          = 0;
    dyingStarted_          = 0;
    removeRequested_  = 0;
    dying_          = 0;
    zoneCleared_      = 0;
    field_124         = 1;
    facing_           = 1;
    field_126         = 0.0;
    conveyorDir_          = 0;
    moveDir_         = 0;
    climbing_          = 0;
    falling_         = 0;
    moveState_        = 0;
    held_          = 0;
    field_e8          = 0;
    stepDuration_          = 200.0;
    bombDropRequest_          = 0;
    teleportPhase_          = 0;
    field_156         = 0;
    glides_          = 0;
    slideSlot_         = 0xff;
    field_11a         = 0;
    gliding_          = 0;
    onLift_          = 0;
    kind_             = 9;
    blastSoundPlayed_ = 0;
    sound_ab_         = 0;  // the second clear
    sound_af_         = 0;
    sound_b3_         = 0;
    sound_b7_         = 0;
    sound_bb_         = 0;
    sound_bf_         = 0;
    sound_c3_         = 0;
    sound_c7_         = 0;
    sound_cb_         = 0;
    field_d8          = 0;
    stepGrace_          = 50.0;
}

static unsigned long s_spawns       = 0;
static int           s_logged_spawn = 0;
static int           s_logged_sound = 0;

/* Unbounded, including the terminator: see the file comment. */
static void spawn_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != '\0')
        ;
}

/* One "if enabled: copy the name, acquire, store" block; the slot is re-read
 * for the store. */
void Bomb::acquireInto(Game *game, Bomb **slot, const SoundAssetName *asset,
                       audiodev::Buffer *Bomb::*field)
{
    char name[256];

    if (asset->enabled == 0)
        return;

    spawn_strcpy(name, asset->name);

    if (s_diag_spawn && !s_logged_sound) {
        s_logged_sound = 1;
        g_logger.write("bomb: first sound acquire -- '%s'\n", name);
    }

    (*slot)->*field = game->soundManager()->acquireStatic(name, 1);
}

void Bomb::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                 unsigned int hArg, unsigned int flagArg)
{
    unsigned char u = (unsigned char)(uArg & 0xff);
    unsigned char v = (unsigned char)(vArg & 0xff);
    unsigned char h = (unsigned char)(hArg & 0xff);
    unsigned char f = (unsigned char)(flagArg & 0xff);
    unsigned char id;
    Bomb  *p;
    Bomb **slot;

    fx_init();

    if (s_fx_spawnswap) {
        unsigned char t = u; u = v; v = t;
    }

    id = Object_ClaimSpareId(game->bombIds(), game->bombCountRef());

    p = create();

    slot  = game->bombSlotRef(id);
    *slot = p;

    if (!s_logged_spawn) {
        s_logged_spawn = 1;
        g_logger.write("bomb: first bomb spawn -- game=%p id=%u obj=%p "
                  "u=%u v=%u h=%u flag=%u\n",
                  (void *)game, (unsigned)id, (void *)p, (unsigned)u,
                  (unsigned)v, (unsigned)h, (unsigned)f);
    }
    if (s_diag_spawn) {
        ++s_spawns;
        if ((s_spawns % 500) == 0)
            g_logger.write("bomb: %lu spawns\n", s_spawns);
    }

    // Through the raw pointer, not the slot: PRESERVED.
    p->clock_ = game->clock();

    (*slot)->tickStep_   = game->tickStep();
    (*slot)->tileBase_ = game->tileBase();
    (*slot)->facing_   = f;

    (*slot)->cellU_      = (signed char)u;
    (*slot)->cellV_      = (signed char)v;
    (*slot)->heightCell_ = (signed char)h;

    // Not in argument order: PRESERVED.
    (*slot)->posU_ = (float)(int)(unsigned int)u;
    (*slot)->posY_ = (float)(int)(unsigned int)h;
    (*slot)->posV_ = (float)(int)(unsigned int)v;

    (*slot)->droppedAt_ = *game->clock();

    if (game->soundCreated() != 0) {
        acquireInto(game, slot, game->soundAsset429b6(), &Bomb::sound_b3_);

        if (game->soundAsset42ac2()->enabled != 0) {
            acquireInto(game, slot, game->soundAsset42ac2(), &Bomb::sound_b7_);
            // The same flag, tested again: PRESERVED.
            if (game->soundAsset42ac2()->enabled != 0)
                acquireInto(game, slot, game->soundAsset42ac2(), &Bomb::sound_bb_);
        }

        acquireInto(game, slot, game->soundAsset46baa(), &Bomb::blastSound_);
        acquireInto(game, slot, game->soundAsset457c6(), &Bomb::sound_c3_);
        acquireInto(game, slot, game->soundAsset46132(), &Bomb::rollSound_);
    }
}

static unsigned long s_removals        = 0;
static int           s_logged_remove   = 0;
static int           s_logged_release  = 0;

/* Releases one sound field, re-reading the slot.  Owner flag 0: a bomb never
 * destroys a shared buffer. */
void Bomb::releaseField(SoundManager *sm, Bomb **slot,
                        audiodev::Buffer *Bomb::*field)
{
    audiodev::Buffer *buf = (*slot)->*field;

    if (buf == 0)
        return;

    if (s_diag_remove && !s_logged_release) {
        s_logged_release = 1;
        g_logger.write("bomb: first sound release -- obj=%p buf=%p\n",
                  (void *)*slot, (void *)buf);
    }

    sm->releaseStaticForOwner(buf, 0);
}

void Bomb::remove(Game *game, unsigned int idArg)
{
    unsigned char id  = (unsigned char)(idArg & 0xff);
    Bomb        **slot = game->bombSlotRef(id);
    SoundManager *sm   = game->soundManager();

    fx_init();

    if (*slot == 0)
        return;

    if (!s_logged_remove) {
        s_logged_remove = 1;
        g_logger.write("bomb: first bomb removal -- game=%p id=%u obj=%p\n",
                  (void *)game, (unsigned)id, (void *)*slot);
    }
    if (s_diag_remove) {
        ++s_removals;
        if ((s_removals % 500) == 0)
            g_logger.write("bomb: %lu removals\n", s_removals);
    }

    if (game->soundCreated() != 0) {
        // In a fixed order.
        releaseField(sm, slot, &Bomb::blastSound_);
        releaseField(sm, slot, &Bomb::sound_b3_);
        releaseField(sm, slot, &Bomb::sound_b7_);
        releaseField(sm, slot, &Bomb::sound_bb_);
        releaseField(sm, slot, &Bomb::sound_c3_);
        releaseField(sm, slot, &Bomb::rollSound_);
        releaseField(sm, slot, &Bomb::sound_ab_);
        releaseField(sm, slot, &Bomb::sound_af_);
    }

    Object_DestroyAndCompactId((void **)slot, game->bombCountRef(),
                               game->bombIds(), id,
                               0);  // PRESERVED: not nulled
}

static unsigned long s_ticks        = 0;
static int           s_logged_first = 0;
static int           s_logged_blast = 0;

Tile *Bomb::tile(int u, int v) const
{
    return Tile::at(tileBase_, u, v);
}

void Bomb::tick()
{
    double diff;

    fx_init();

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
    if (rollSound_ != 0) {
        rollSound_->setPosition((float)(int)cellU_,
                              (float)(int)heightCell_,
                              -(float)(int)cellV_,
                              1);
        if (dying_ == 0)  // raised by the blast below
            rollSound_->play(false);
        else
            rollSound_->stop();
    }

    // Roll: still fusing.
    diff = now_ - droppedAt_;
    if (!(diff >= fuse_ms())) {
        pendingMove_ = facing_;
        updateMovement();
        if (moveDir_ != 0)
            held_ = 1;
        return;
    }

    // Blast.
    slideSlot_    = 0xff;
    held_     = 1;
    dying_     = 1;  // halts the roll sound
    moveDir_    = 0;
    pendingMove_ = 0;

    if (blastSoundPlayed_ == 0) {
        if (blastSound_ != 0) {
            // From the float position, not the grid bytes.
            blastSound_->setPosition(posU_, posY_, -posV_, 1);
            blastSound_->play(false);
        }
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

            if ((signed char)t->objectMarker() == TILE_DESTRUCTIBLE &&
                (int)heightCell_ == (int)t->height() &&
                t->busy() == 0) {

                t->setBusy(1);  // spent
                t->setBlastTime(now_);
                t->setField203(1);
                t->setField20f(1);

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
    if (diff >= clear_ms() && zoneCleared_ == 0) {
        for (int u = (int)cellU_ - 1; u <= (int)cellU_ + 1; ++u) {
            for (int v = (int)cellV_ - 1; v <= (int)cellV_ + 1; ++v) {
                Tile *t = tile(u, v);

                t->setBlastHeight(0);

                // No busy test here: PRESERVED.
                if ((signed char)t->objectMarker() == TILE_DESTRUCTIBLE &&
                    (int)heightCell_ == (int)t->height())
                    t->setField203(0);
            }
        }
    }

    // Remove.
    diff = now_ - droppedAt_;
    if (diff >= remove_ms() && zoneCleared_ == 0) {
        zoneCleared_ = 1;
        moveState_   = 4;  // despawn
        updateMovement();
        return;
    }

    zoneCleared_ = 0;
    updateMovement();
}

