/* Foe's spawn, remove, per-tick step and chase-target search, plus the
 * chooseTarget/dropBomb/checkPlayerContact/finishDespawn helpers GameTick's
 * foe loop calls directly. */

#include <windows.h>
#include <string.h>

#include "foe.h"
#include "game.h"
#include "tile.h"
#include "movableentity.h"
#include "entitymath.h"
#include "foepath.h"
#include "voicepool.h"
#include "objectremove.h"
#include "soundmanager.h"
#include "static.h"
#include "bomb.h"
#include "tilequery.h"
#include "log.h"

#include <new>

/* KAROO_SIM_FX=spawnswap transposes u and v where the spawn reads them (shared
 * with bomb.cpp's spawn).  KAROO_SIM_FX=nocontact stops the contact latch and
 * hit flag firing.  KAROO_SIM_FX=foeunfreeze ignores the freeze gate, so a
 * frozen foe keeps chasing.  KAROO_SIM_FX=chaseback reverses the
 * delta-to-facing table, so foes step away from their target; matched
 * case-insensitively.  KAROO_SIM_FX=keepid and =lowid live in the shared
 * remove tail (objectremove.cpp).  KAROO_SPAWN_DIAG, KAROO_REMOVE_DIAG and
 * KAROO_FOESTEP_DIAG log periodic counts and first-occurrence events for the
 * spawn, remove and step below. */
static int s_fx_spawnswap = 0;
static int s_fx_nocontact = 0;
static int s_fx_unfreeze  = 0;
static int s_fx_chaseback = 0;
static int s_diag_spawn   = 0;
static int s_diag_remove  = 0;
static int s_diag_step    = 0;
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
        if (strcmp(buf, "spawnswap") == 0) {
            s_fx_spawnswap = 1;
            log_write("foe: KAROO_SIM_FX=spawnswap -- the spawn tile is "
                      "transposed (u and v exchanged at the single point they "
                      "are read), so every foe comes into existence at the "
                      "mirrored cell\n");
        } else if (strcmp(buf, "nocontact") == 0) {
            s_fx_nocontact = 1;
            log_write("foe: KAROO_SIM_FX=nocontact -- the foe hit flag is "
                      "never raised\n");
        } else if (strcmp(buf, "foeunfreeze") == 0) {
            s_fx_unfreeze = 1;
            log_write("foe: KAROO_SIM_FX=foeunfreeze -- the freeze gate is "
                      "ignored, so frozen foes keep chasing\n");
        } else if (lstrcmpiA(buf, "chaseback") == 0) {
            s_fx_chaseback = 1;
            log_write("foe: KAROO_SIM_FX=chaseback -- foes step away, not "
                      "toward\n");
        }
    }

    s_diag_spawn  = env_on("KAROO_SPAWN_DIAG");
    s_diag_remove = env_on("KAROO_REMOVE_DIAG");
    s_diag_step   = env_on("KAROO_FOESTEP_DIAG");
}

Tile *Foe::tile(int u, int v) const
{
    return Tile::at(tileBase_, u, v);
}

const Foe::Vtbl Foe::VTABLE = { &Foe::scalarDeletingDtor };

Foe *Foe::create()
{
    return new (std::nothrow) Foe;
}

Foe::Foe()
{
    vtable_          = &VTABLE;
    zeroSoundSlots();
    field_7a         = 0;
    dyingStarted_         = 0;
    removeRequested_ = 0;
    dying_         = 0;
    conveyorDir_         = 0;
    field_124        = 1;
    moveDir_        = 0;
    pendingMove_     = 0;
    field_126        = 0.0;
    climbing_         = 0;
    falling_        = 0;
    slideSlot_        = 0xff;
    field_d7         = 0xff;
    stepGrace_         = 20.0;
    idleDuration_         = 1000.0;
    moveState_       = 0;
    held_         = 0;
    field_e8         = 0;
    bombDropRequest_         = 0;
    teleportPhase_         = 0;
    field_156        = 0;
    glides_         = 0;
    field_11a        = 0;
    gliding_         = 0;
    field_d3         = 0;
    onLift_         = 0;
    pool_9f_         = 0;
    sound_ab_        = 0;
    sound_af_        = 0;
    sound_b3_        = 0;
    sound_b7_        = 0;
    sound_bb_        = 0;
    sound_bf_        = 0;
    sound_c3_        = 0;
    sound_c7_        = 0;
    sound_cb_        = 0;
    sound_cf_        = 0;
    anim_         = 0;
    idleStarted_         = 0;
    pickedUp_         = 0;
    dropContents_    = 0;
    type_            = 0;
    pathfinder_      = 0;
}

void Foe::destroy()
{
    FoePath *pf;

    tile(cellU_, cellV_)->setField1a1(0);
    tile(cellU_, cellV_)->setOccupant(0);
    tile(cellU_ - stepU_, cellV_ - stepV_)->setOccupant(0);

    pf = pathfinder_;
    if (pf != 0)
        FoePath::destroy(pf);
}

void *Foe::scalarDeletingDtor(Foe *self, unsigned int flags)
{
    self->destroy();
    if (flags & 1)
        delete self;
    return self;
}

static unsigned char s_type_seen[256];
static unsigned int  s_hi_level[256];

static unsigned int level_hash(const char *s)
{
    unsigned int h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h ? h : 1u;
}

static unsigned long s_spawns          = 0;
static int           s_logged_spawn    = 0;
static int           s_logged_hightype = 0;
static int           s_logged_kind2    = 0;
static int           s_logged_kind3    = 0;

unsigned char Foe::spawn(Game *game, unsigned int uArg, unsigned int vArg,
                         unsigned int hArg, unsigned int kindArg,
                         unsigned int typeArg)
{
    unsigned char u    = (unsigned char)(uArg & 0xff);
    unsigned char v    = (unsigned char)(vArg & 0xff);
    unsigned char h    = (unsigned char)(hArg & 0xff);
    signed char   kind = (signed char)(kindArg & 0xff);
    unsigned char type = (unsigned char)(typeArg & 0xff);
    unsigned char id;
    unsigned int  i;
    Foe         **slot;
    Foe          *p;

    fx_init();

    if (s_fx_spawnswap) {
        unsigned char t = u; u = v; v = t;
    }

    id = Object_ClaimSpareId(game->foeIds(), game->foeCountRef());

    p = create();

    i     = (unsigned int)id;
    slot  = game->foeSlotRef(i);
    *slot = p;

    if (!s_logged_spawn) {
        s_logged_spawn = 1;
        log_write("foe: first foe spawn -- game=%p id=%u obj=%p "
                  "u=%u v=%u h=%u kind=%d type=%u\n",
                  (void *)game, (unsigned)id, (void *)p, (unsigned)u,
                  (unsigned)v, (unsigned)h, (int)kind, (unsigned)type);
    }
    if (s_diag_spawn) {
        const char  *lvl = game->levelName();
        unsigned int lh  = level_hash(lvl);

        ++s_spawns;
        if ((s_spawns % 500) == 0)
            log_write("foe: %lu spawns\n", s_spawns);
        if (!s_type_seen[type]) {
            s_type_seen[type] = 1;
            log_write("foe: type %u (0x%02x)%s first seen in '%s'\n",
                      (unsigned)type, (unsigned)type,
                      type > 0x64 ? " >0x64" : "", lvl);
        }
        if (type > 0x64 && s_hi_level[type] != lh) {
            s_hi_level[type] = lh;
            log_write("foe: HIGHTYPE %u (0x%02x) in '%s'\n",
                      (unsigned)type, (unsigned)type, lvl);
        }
    }

    // PRESERVED: the field stores below go through the pointer the allocator
    // returned, not the slot; a failed allocation leaves this NULL and crashes
    // here rather than failing gracefully.
    p->clock_ = game->clock();

    (*slot)->tickStep_ = game->tickStep();

    // each foe spawned in the same frame starts 1500ms later than the last, so
    // their step and idle timers do not all line up.
    (*slot)->lastActive_ = (double)(int)(i * 0x5dc) + *game->clock();

    (*slot)->kind_ = (unsigned char)kind;
    if (kind == 2) {
        (*slot)->stepDuration_ = 500.0;
        if (s_diag_spawn && !s_logged_kind2) {
            s_logged_kind2 = 1;
            log_write("foe: first kind-2 foe\n");
        }
    } else if (kind == 3) {
        (*slot)->stepDuration_ = 700.0;
        if (s_diag_spawn && !s_logged_kind3) {
            s_logged_kind3 = 1;
            log_write("foe: first kind-3 foe\n");
        }
    }

    (*slot)->tileBase_ = game->tileBase();
    (*slot)->type_     = type;

    if (type > 0x64) {
        (*slot)->facing_ = (unsigned char)(type - 0x64);  // facing_ carries the type - 0x64
        type = 0;  // type is zeroed so the pathfinder mode below never sees it.
        if (s_diag_spawn && !s_logged_hightype) {
            s_logged_hightype = 1;
            log_write("foe: first type > 0x64\n");
        }
    } else {
        (*slot)->facing_ = 1;
        if (type == 0x0b) {
            (*slot)->dropContents_ = 1;
            game->setField42252((unsigned short)(game->field_42252() + 1));
        } else if (type == 0x4d) {
            (*slot)->dropContents_ = 7;
        } else if (type == 0x07) {
            (*slot)->dropContents_ = 1;
        }
    }

    (*slot)->cellU_      = (signed char)u;
    (*slot)->cellV_      = (signed char)v;
    (*slot)->heightCell_ = (signed char)h;
    (*slot)->homeU_      = u;
    (*slot)->homeV_      = v;
    (*slot)->homeH_      = h;

    (*slot)->posU_ = (float)(int)(unsigned int)u;
    (*slot)->posY_ = (float)(int)(unsigned int)h;
    (*slot)->posV_ = (float)(int)(unsigned int)v;

    Tile::at(game->tileBase(), (int)u, (int)v)->setOccupant((*slot)->kind_);

    // PRESERVED: FoePath::create can return NULL on a failed allocation, and
    // setMode is called on it unconditionally below.
    (*slot)->pathfinder_ = FoePath::create((*slot)->tileBase_, 0);

    (*slot)->pathfinder_->setMode(type);

    return id;
}

static unsigned long s_removals       = 0;
static int           s_logged_remove  = 0;
static int           s_logged_release = 0;

static void release(SoundManager *sm, void *obj, void *buf, int bPool)
{
    if (buf == 0)
        return;

    if (s_diag_remove && !s_logged_release) {
        s_logged_release = 1;
        log_write("foe: first sound release -- obj=%p buf=%p pool=%d\n",
                  obj, buf, bPool);
    }

    if (bPool)
        sm->releasePooledForOwner((VoicePool *)buf, 1);
    else
        sm->releaseStaticForOwner((CStaticSoundbuffer *)buf, 1);
}

void Foe::remove(Game *game, unsigned int idArg)
{
    unsigned char id   = (unsigned char)(idArg & 0xff);
    Foe         **slot = game->foeSlotRef(id);
    SoundManager *sm   = game->soundManager();

    fx_init();

    if (*slot == 0)
        return;

    if (!s_logged_remove) {
        s_logged_remove = 1;
        log_write("foe: first foe removal -- game=%p id=%u obj=%p\n",
                  (void *)game, (unsigned)id, (void *)*slot);
    }
    if (s_diag_remove) {
        ++s_removals;
        if ((s_removals % 500) == 0)
            log_write("foe: %lu removals\n", s_removals);
    }

    if (game->soundCreated() != 0) {
        release(sm, *slot, (*slot)->pool_9f_,  1);
        release(sm, *slot, (*slot)->sound_b3_, 0);
        release(sm, *slot, (*slot)->sound_c7_, 0);
        release(sm, *slot, (*slot)->sound_b7_, 0);
        release(sm, *slot, (*slot)->sound_bb_, 0);

        if ((*slot)->sound_c3_ != 0) {
            ((*slot)->sound_c3_)->haltPlayback();
            release(sm, *slot, (*slot)->sound_c3_, 0);
        }

        release(sm, *slot, (*slot)->sound_ab_, 0);
        release(sm, *slot, (*slot)->sound_af_, 0);
        release(sm, *slot, (*slot)->sound_cb_, 0);
        release(sm, *slot, (*slot)->sound_cf_, 1);
        release(sm, *slot, (*slot)->sound_a7_, 0);
    }

    Object_DestroyAndCompactId((void **)slot, game->foeCountRef(),
                               game->foeIds(), id,
                               1);
}

static inline int ftol_i(double v)
{
    return (int)(long long)v;
}

static inline int iabs_orig(int v)
{
    int m = v >> 31;
    return (v ^ m) - m;
}

static unsigned long s_ticks          = 0;
static int           s_logged_first   = 0;
static int           s_logged_contact = 0;
static int           s_logged_consume = 0;
static int           s_logged_chase   = 0;
static int           s_logged_turn    = 0;
static int           s_logged_repost  = 0;
static int           s_logged_frozen  = 0;

void Foe::step(unsigned char playerU, unsigned char playerV)
{
    fx_init();

    if (!s_logged_first) {
        s_logged_first = 1;
        log_write("foe: first foe step -- this=%p\n", (void *)this);
    }
    if (s_diag_step) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            log_write("foe: %lu ticks\n", s_ticks);
    }

    tickStepCopy_ = *tickStep_;
    now_        = *clock_;

    if ((signed char)kind_ == 3 && held_ == 0 && bombDropRequest_ == 0) {
        if (iabs_orig((int)playerU - (int)cellU_) < 2 &&
            iabs_orig((int)playerV - (int)cellV_) < 2) {

            // PRESERVED: the difference is compared unsigned, so a
            // last-contact time after now wraps to a huge value and still
            // clears the 2000ms threshold.
            int last = ftol_i(lastContact_);
            int now  = ftol_i(now_);

            if ((unsigned int)(now - last) >= 0x7d0u) {
                if (s_diag_step && !s_logged_contact) {
                    s_logged_contact = 1;
                    log_write("foe: first contact -- foe=(%d,%d) "
                              "player=(%u,%u)\n",
                              (int)cellU_, (int)cellV_,
                              (unsigned)playerU, (unsigned)playerV);
                }
                if (!s_fx_nocontact) {
                    lastContact_ = now_;
                    bombDropRequest_ = 1;
                }
            }
        }
    }

    pickedUp_ = 0;
    if ((signed char)type_ == 3 && moveDir_ == 0) {
        Tile *t = tile(cellU_, cellV_);

        if ((int)heightCell_ == (int)t->height() &&
            (signed char)t->contents() == CONTENTS_CRYSTAL) {

            t->setContents(0);

            if (s_diag_step && !s_logged_consume) {
                s_logged_consume = 1;
                log_write("foe: first tile consume -- cell=(%d,%d)\n",
                          (int)cellU_, (int)cellV_);
            }

            if (pool_9f_ != 0) {
                pool_9f_->broadcastCoordinates((float)(int)cellU_,
                                                  (float)(int)heightCell_,
                                                  -(float)(int)cellV_,
                                                  1);
                pool_9f_->cycle(0);
            }
            pickedUp_ = 1;
        }
    }

    pendingMove_ = 0;
    if (moveDir_ != 0) {
        if (s_diag_step && !s_logged_frozen) {
            s_logged_frozen = 1;
            log_write("foe: first FROZEN foe -- +0x14e=%d cell=(%d,%d)\n",
                      moveDir_, (int)cellU_, (int)cellV_);
        }
    }
    if (moveDir_ != 0 && !s_fx_unfreeze) {
        updateMovement();
        return;
    }

    chase(playerU, playerV, chaseSpeed_);

    if ((signed char)pendingMove_ != 0) {
        if (s_diag_step && !s_logged_chase) {
            s_logged_chase = 1;
            log_write("foe: first chase direction -- dir=%u foe=(%d,%d)\n",
                      (unsigned)pendingMove_, (int)cellU_, (int)cellV_);
        }
        // the early return: steps 6 and 7 (the turn table, return-to-post) run
        // only when the chase above found no move.
        targetU_ = playerU;
        targetV_ = playerV;
        updateMovement();
        return;
    }

    if ((signed char)type_ == 2) {
        Tile *t = tile(cellU_, cellV_);

        if ((signed char)t->objectMarker() == TILE_SWITCH) {
            unsigned char facing;

            if (s_diag_step && !s_logged_turn) {
                s_logged_turn = 1;
                log_write("foe: first turn table -- cell=(%d,%d)\n",
                          (int)cellU_, (int)cellV_);
            }

            if ((signed char)lastMoveDir_ != 0)
                pendingMove_ = Sim_GetTurnedDirection(lastMoveDir_, 2);

            facing = facing_;

            if (pendingMove_ == facing) {
                pendingMove_ = facing;
                turnKind_    = 1;
            }
            if (pendingMove_ == Sim_GetTurnedDirection(facing, 1)) {
                turnKind_    = 2;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
            }
            if (pendingMove_ == Sim_GetTurnedDirection(facing_, 3)) {
                turnKind_    = 4;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 3) + 10);
            }
            // PRESERVED: tests the turn-2 direction but writes the turn-1 one.
            if (pendingMove_ == Sim_GetTurnedDirection(facing_, 2)) {
                turnKind_    = 2;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
            }
        }
    }

    if ((signed char)type_ == 4) {
        if (s_diag_step && !s_logged_repost) {
            s_logged_repost = 1;
            log_write("foe: first return-to-post -- target=(%u,%u)\n",
                      (unsigned)targetU_, (unsigned)targetV_);
        }
        chase(targetU_, targetV_, chaseSpeed_);
    }

    updateMovement();
}

static inline unsigned ftol32(double v)
{
    return (unsigned)(long long)v;
}

unsigned char Foe::chase(unsigned char targetU, unsigned char targetV,
                         unsigned short speed)
{
    fx_init();

    pathfinder_->setCap(speed);
    pathfinder_->setTarget(targetU, targetV);

    if ((signed char)type_ == 2)
        pathfinder_->setMode((field_d3 != 0) ? 0 : 2);

    if (moveDir_ != 0)
        return pendingMove_;

    if (pathfinder_->find((int)cellU_, (int)cellV_,
                          (unsigned)targetU, (unsigned)targetV) == 0) {
        pendingMove_ = 0;
        return pendingMove_;
    }

    pathfinder_->setResult(pathfinder_->result()->parent());

    const PathNode *node = pathfinder_->result();
    const unsigned char nu = (unsigned char)node->u();
    const unsigned char nv = (unsigned char)node->v();

    const signed char du = (signed char)(unsigned char)(nu - (unsigned char)cellU_);
    const signed char dv = (signed char)(unsigned char)(nv - (unsigned char)cellV_);

    const unsigned char dirNorth = s_fx_chaseback ? 3 : 1;
    const unsigned char dirEast  = s_fx_chaseback ? 4 : 2;
    const unsigned char dirSouth = s_fx_chaseback ? 1 : 3;
    const unsigned char dirWest  = s_fx_chaseback ? 2 : 4;

    if (dv == -1) pendingMove_ = dirNorth;
    if (du ==  1) pendingMove_ = dirEast;
    if (dv ==  1) pendingMove_ = dirSouth;
    if (du == -1) pendingMove_ = dirWest;

    if ((signed char)field_124 == 1) {
        const unsigned char facing = facing_;

        if (pendingMove_ == facing) {
            pendingMove_ = facing;
            turnKind_    = 1;
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing, 2)) {
            pendingMove_ = Sim_GetTurnedDirection(facing_, 2);
            turnKind_    = 3;
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 1)) {
            turnKind_    = 2;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 3)) {
            turnKind_    = 4;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 3) + 10);
        }
        // PRESERVED: tests the turn-2 direction but writes the turn-1 one, as
        // the step-time turn table above does.
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 2)) {
            turnKind_    = 2;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
        }
    }

    if (facing_ != pendingMove_) {
        if (facing_ != Sim_GetTurnedDirection(pendingMove_, 2))
            return pendingMove_;
    }

    Tile *step = Tile::at(tileBase_, (int)nu, (int)nv);
    Tile *here = tile(cellU_, cellV_);

    if (step->objectMarker() == TILE_EMPTY)
        pendingMove_ = 0;
    if (slideSlot_ != 0xff && step->slideTrack() != 0)
        pendingMove_ = 0;
    if (here->objectMarker() == TILE_JUMP_PAD)
        pendingMove_ = 0;

    if (step->objectMarker() == TILE_LIFT) {
        const unsigned a = ftol32(step->liftParkedSince());
        const unsigned b = ftol32(now_ - (double)a);
        const unsigned c = ftol32(step->liftDwell());
        const unsigned d = ftol32(animDuration_);
        if ((int)(c - b) < (int)d)
            pendingMove_ = 0;
        if ((unsigned)step->height() != (unsigned)(int)heightCell_)
            pendingMove_ = 0;
    }

    if (here->objectMarker() == TILE_LIFT) {
        if ((unsigned)step->height() != (unsigned)(int)heightCell_)
            pendingMove_ = 0;
    }

    if (step->objectMarker() != TILE_SLIDE_TRACK)
        return pendingMove_;

    {
        const unsigned e = ftol32(step->slideParkedSince());
        const unsigned f = ftol32(now_ - (double)e);
        const unsigned g = ftol32(step->slideDwell());
        const unsigned h = ftol32(animDuration_);
        if ((int)(g - f) >= (int)h)
            return pendingMove_;
    }

    pendingMove_ = 0;
    return pendingMove_;
}

/* GameTick's foe loop calls chooseTarget once per foe to pick this tick's
 * target, then dropBomb, checkPlayerContact and finishDespawn as needed; the
 * Game and Player values it passes are read once, here. */

void Foe::chooseTarget(Game *game, int hold,
                       unsigned char playerU, unsigned char playerV,
                       unsigned char escortU, unsigned char escortV,
                       unsigned char *pu, unsigned char *pv)
{
    unsigned char tu = playerU, tv = playerV;

    held_ = hold;
    chaseSpeed_ = 0x32;
    if (type_ == 1) {
        tu = escortU;
        tv = escortV;
        chaseSpeed_ = 400;
    }
    if (type_ == 2) {
        tu = (unsigned char)cellU_;
        tv = (unsigned char)cellV_;
        if (Sim_FindNearestListedObjectTile(game, &tu, &tv, 7) != 0) {
            chaseSpeed_ = 100;
            chase(tu, tv, 100);
            if (field_d3 == 0 && pendingMove_ == 0 && turnKind_ == 0) {
                tu = playerU;
                tv = playerV;
                chaseSpeed_ = 0x32;
            }
        } else {
            tu = playerU;
            tv = playerV;
            chaseSpeed_ = 100;
        }
    }
    if (type_ == 3) {
        tu = (unsigned char)cellU_;
        tv = (unsigned char)cellV_;
        if (Sim_FindNearestFlaggedTileInRadius(game, &tu, &tv, 5) == 0) {
            tu = playerU;
            tv = playerV;
            chaseSpeed_ = 100;
        } else {
            chaseSpeed_ = 0x96;
            chase(tu, tv, 0x96);
            if (pendingMove_ == 0) {
                tu = playerU;
                tv = playerV;
                chaseSpeed_ = 100;
            }
        }
    }
    if (type_ == 5) {
        int found = 0;
        held_ = 0;
        for (int j = 0; j < (int)game->foeCount(); ++j) {
            Foe *other = game->foeSlot(game->foeId(j));
            if (other->kind_ == 2) {
                found = 1;
                tu = (unsigned char)other->cellU_;
                tv = (unsigned char)other->cellV_;
                chaseSpeed_ = 0x96;
            }
        }
        if (!found)
            held_ = 1;
    }
    if (type_ == 7) {
        held_ = 0;
        if (Sim_FindFarthestOccupiedTile(this, &tu, &tv) != 0)
            chaseSpeed_ = 0x96;
        else
            held_ = 1;
    }
    *pu = tu;
    *pv = tv;
}

void Foe::dropBomb(Game *game)
{
    if (bombDropRequest_ == 0 || type_ == 2)
        return;
    int spawn = 1, offset = 0;
    if (moveDir_ != 0) {
        long double since = (long double)*game->clock() - (long double)animStart_;
        if (since < 50.0L)
            offset = 1;
        else
            spawn = 0;
    }
    if (!spawn)
        return;
    if (offset)
        Bomb::spawn(game, (unsigned char)((unsigned char)cellU_ - (unsigned char)stepU_),
                          (unsigned char)((unsigned char)cellV_ - (unsigned char)stepV_),
                          (unsigned char)((unsigned char)heightCell_ - (unsigned char)field_141),
                          facing_);
    else
        Bomb::spawn(game, (unsigned char)cellU_, (unsigned char)cellV_,
                          (unsigned char)heightCell_, facing_);
    bombDropRequest_ = 0;
}

void Foe::checkPlayerContact(unsigned char *playerMoveState,
                             float playerU, float playerY, float playerV)
{
    if (*playerMoveState == 0) {
        if (kind_ != 3 && moveState_ == 0) {
            long double dx = (long double)posU_ - (long double)playerU;
            long double dy = (long double)posY_ - (long double)playerY;
            long double dz = (long double)posV_ - (long double)playerV;
            long double s = dz * dz + dy * dy;
            s = s + dx * dx;
            long double dist;
            __asm__("fsqrt" : "=t"(dist) : "0"(s));
            if (dist < 0.5L)
                *playerMoveState = 1;
        }
    } else if (moveDir_ == 0) {
        anim_ = 0x28;
    }
}

bool Foe::finishDespawn(LevelMap *map)
{
    if (moveState_ == 0 || falling_ != 0)
        return false;
    dying_ = 1;
    Tile *home = map->snapshot((signed char)homeU_, (signed char)homeV_);
    if (home->contents() != CONTENTS_TIMED_SPAWN)
        home->setContents(0);
    if (removeRequested_ == 0)
        return false;
    if ((long double)posY_ > 0.0L)
        tile(cellU_, cellV_)->setContents(dropContents_);
    return true;
}
