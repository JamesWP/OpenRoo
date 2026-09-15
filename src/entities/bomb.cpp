/* Bomb -- spawn, tick and remove (COHESION_PLAN.md Band 4a).
 *
 *     Game::SpawnBombObject          0x00417700   (was objectspawn.cpp)
 *     Game::UpdateBombFuseAndBlast   0x00402870   (was bombfuse.cpp)
 *     Game::RemoveEnemyObject        0x00417a20   (was objectremove.cpp)
 *
 *     Bomb ctor / dtor               0x00402700 / 0x00402840 + 0x00402860
 *                                    -- ours, on the MovableEntity base
 *                                    (movableentity.cpp)
 *
 * Every function of ours that reads or writes a Bomb field lives here; bomb.h
 * keeps the fields private.  The "enemy" table is the bomb table: the spawn
 * is its only filler.
 *
 * ─── The tick (GAMETICK_PLAN.md Band A) ──────────────────────────────────
 *
 * The game clock is in milliseconds, and the whole state machine is three
 * thresholds against `now - droppedAt`:
 *
 *   ROLL     < 2000 ms   pending move = the bomb's own facing, then
 *                        UpdateEntityMovement -- the bomb SLIDES FORWARD from
 *                        the cell it was dropped on, which is why it looks
 *                        like it was placed in the cell in front of John.
 *   BLAST   >= 2000 ms   writes its own height into tile+0x1a0 across the
 *                        whole 3x3 block (u-1..u+1, v-1..v+1), diagonals
 *                        included.
 *   CLEAR   >= 2400 ms   zeroes tile+0x1a0 over the same 3x3.
 *   REMOVE  >= 2600 ms   moveState = 4; UpdateEntityMovement then raises
 *                        +0x7e, which GameTick reads to call remove().
 *
 * tile+0x1a0 is a LIVE lethality flag holding the blast's height -- nonzero
 * means that cell is exploding at that height right now.
 *
 * DESTRUCTIBLE BLOCKS are tile kind 0x17.  A blast at their exact height
 * marks them spent (tile+0x217), stamps the blast time into tile+0x207,
 * raises tile+0x203 and tile+0x20f, logs "obstacle is exploding", and
 * promotes the block's hidden contents (tile+0x202) into its contents byte
 * (tile+0x19f).  CLEAR drops tile+0x203 again for those same cells.
 *
 * __thiscall, no stack arguments, bare RET.  ONE E8 call site, 0x00415185
 * in GameTick (ours: game->bombSlot(id)->tick()).  The original leaves a
 * value in EAX which that caller overwrites unread, so the tick is void.
 *
 * Bugs and exactness preserved:
 *
 * 1. THE FUSE COMPARE IS NaN-ASYMMETRIC.  The original is `FCOMP 2000.0;
 *    FNSTSW AX; TEST AH,1; JNZ roll` -- an unordered compare rolls.  So the
 *    test is written `!(diff >= fuse)`, unordered-true in C++ exactly as the
 *    original is.  The 2400 and 2600 tests take the opposite branch on C0 and
 *    are plain `>=`, unordered-false -- also exact.
 * 2. THE LOOP BOUNDS ARE RE-READ EVERY ITERATION (0x00402a6f, 0x00402a84).
 *    Nothing in the body writes cellU/cellV, so it cannot differ; transcribed
 *    as written.
 * 3. THE CLEAR LOOP DOES NOT CHECK `spent`; the BLAST loop does.
 * 4. THE HEIGHT COMPARE MIXES SIGNEDNESS: heightCell read signed (MOVSX),
 *    tile height unsigned (MOVZX).  A negative bomb height never matches.
 * 5. tile+0x1a0 IS WRITTEN FROM A PLAIN BYTE LOAD of heightCell (MOV DL), so
 *    the unsigned byte pattern lands there.
 *
 * ─── The spawn ───────────────────────────────────────────────────────────
 *
 * __thiscall on Game, four dword stack arguments (RET 0x10), void.  Two E8
 * sites, 0x00415524 and 0x00415B25, both in GameTick (the player's drop and
 * a foe's).
 *
 * 1. A FAILED ALLOCATION DEREFERENCES NULL.  The first field store goes
 *    through the pointer operator_new returned, not through the slot; every
 *    later store re-reads the slot.  Transcribed as written.
 * 2. THE SECOND SOUND FLAG IS TESTED TWICE -- the inner test can never fail
 *    when the outer one passed.  Kept.
 * 3. THE THREE FLOATS ARE NOT IN PARAMETER ORDER: +0x25 u, +0x29 HEIGHT,
 *    +0x2d v, none negated.
 * 4. THE STRING COPY IS AN UNBOUNDED strcpy (`repne scasb; rep movs`) into a
 *    256-byte buffer; the enabled flag right after each name stops it.
 *
 * Allocation is our own `new` (see "Construction and destruction" below).
 * Kept as callbacks: ClaimSpareObjectIdSlot
 * (Game::claimSpareObjectId; its AL return is a compiler artefact, see
 * objectspawn.cpp) and AcquireSoundBuffer (SoundManager::acquireStatic).
 *
 * The original's MSVC EH frame around the allocation is not reproduced: the
 * allocator returns NULL rather than throwing, so the frame is unobservable.
 *
 * ─── The remove ──────────────────────────────────────────────────────────
 *
 * __thiscall on Game, one dword (the ID), RET 4.  Four E8 sites (0x00414C03
 * 0x004151A5 0x004165C0 0x0041867B).  Releases eight sound fields with owner
 * flag 0 (never destroys a shared buffer), in the listing's order, re-reading
 * the slot for each; then the tail shared with the foe remove
 * (objectremove.h).  THE DEFECT: unlike the foe remove, the slot is NOT
 * nulled -- it is left dangling.  Reproduced deliberately.
 *
 * ─── Floating-point copies ───────────────────────────────────────────────
 *
 * Doubles are copied by plain assignment where the original uses integer
 * MOVs (now <- *clock, droppedAt <- *clock, tile blast time <- now).  They
 * differ only for a signalling NaN, which no clock value holds
 * (COHESION_PLAN.md, template 3).
 *
 * ─── Controls and diags ──────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=shortfuse halves all three thresholds (2000/2400/2600 ->
 * 1000/1200/1300 ms) -- a measurement: it moves WHEN tiles become lethal.
 * KAROO_SIM_FX=spawnswap exchanges u and v in the spawn (shared with the foe
 * spawn in objectspawn.cpp).  KAROO_SIM_FX=keepid lives in the shared remove
 * tail (objectremove.cpp).
 *
 * KAROO_BOMB_DIAG=1 logs the first blast and a tick count every 5000;
 * KAROO_SPAWN_DIAG=1 the first sound acquire and a spawn count every 500;
 * KAROO_REMOVE_DIAG=1 the first sound release and a removal count every 500.
 * The first tick, first spawn and first removal are logged unconditionally:
 * a flag-gated line cannot tell "no bombs" from "flag never arrived".
 */

#include <windows.h>
#include <string.h>

#include "bomb.h"
#include "game.h"
#include "tile.h"
#include "alloc.h"
#include "movableentity.h"
#include "objectremove.h"
#include "soundmanager.h"
#include "static.h"
#include "gamelog.h"
#include "log.h"

#include <new>               /* std::nothrow */

/* The game's own logger instance, Logger at 0x0046c4c0. */
#define GAME_LOGGER   ((GameLogger *)0x0046c4c0)

/* DAT_0045d2b0 / 0045d2a8 / 0045d2a0: 409f4.., 40a2c.., 40a45.. */
static const double K_FUSE_MS   = 2000.0;
static const double K_CLEAR_MS  = 2400.0;
static const double K_REMOVE_MS = 2600.0;

#define TILE_KIND_DESTRUCTIBLE 0x17

/* ─── Controls and diags, read by VALUE, never by presence ──────────────── */
static int s_fx_shortfuse = 0;
static int s_fx_spawnswap = 0;
static int s_diag_bomb    = 0;   /* KAROO_BOMB_DIAG   */
static int s_diag_spawn   = 0;   /* KAROO_SPAWN_DIAG  */
static int s_diag_remove  = 0;   /* KAROO_REMOVE_DIAG */
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
            log_write("bomb: KAROO_SIM_FX=shortfuse -- fuse %.0f ms, not %.0f\n",
                      K_FUSE_MS / 2.0, K_FUSE_MS);
        } else if (strcmp(buf, "spawnswap") == 0) {
            s_fx_spawnswap = 1;
            log_write("bomb: KAROO_SIM_FX=spawnswap -- the bomb spawn tile is "
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

/* ═══ Construction and destruction ═════════════════════════════════════════
 *
 * 0x402700 constructs in two layers: the MovableEntity base 0x438720 (ours:
 * MovableEntity() zeroes the three floats), then the bomb's own stores, in
 * the listing's order below.  Every other byte is left as operator new
 * returned it, as the original leaves it.  The ctor clears +0xab..+0xcb a
 * second time right after ZeroEntitySoundSlotPointers (but not +0xa3, +0xa7
 * or +0xcf); dead stores, kept.  Two pairs of dword stores are one double
 * each: +0x66/+0x6a = 200.0 and +0x48/+0x4c = 50.0.
 *
 * 0x402840 (vtable slot 0; 0x45d29c has ONE slot -- the next dword is 0)
 * calls 0x402860, which re-installs 0x45d29c and jumps to the base dtor
 * 0x438760; then Free2 if flags & 1.  All three vtable stores are dead -- the
 * only caller passes flags 1 -- and are not reproduced.
 *
 * A byte scan of .text for 0x45d29c finds only the ctor (00402727) and the
 * dtor (00402862).  0x402700's one caller is the stubbed spawn; 0x402840 is
 * reached only through the vtable, from the remove (ours).  So we create and
 * destroy every bomb, and use our own new/delete.
 */
const Bomb::Vtbl Bomb::VTABLE = { &Bomb::scalarDeletingDtor };

Bomb *Bomb::create()
{
    return new (std::nothrow) Bomb;
}

Bomb::Bomb()
{
    /* MovableEntity() has run: 0x438720 */
    vtable_           = &VTABLE;
    rollSound_        = 0;                  /* +0x15a */
    blastSound_       = 0;                  /* +0x15e */
    zeroSoundSlots();                       /* 0x43ad60 */
    field_7a          = 0;
    field_82          = 0;
    removeRequested_  = 0;
    field_86          = 0;
    zoneCleared_      = 0;
    field_124         = 1;
    facing_           = 1;
    field_126         = 0.0;                /* two zero dwords */
    field_58          = 0;
    field_14e         = 0;
    field_fb          = 0;
    field_120         = 0;
    moveState_        = 0;
    field_ef          = 0;
    field_e8          = 0;
    field_66          = 200.0;              /* 0 at +0x66, 0x40690000 at +0x6a */
    field_e4          = 0;
    field_ff          = 0;
    field_156         = 0;
    field_e9          = 0;
    field_11e         = 0xff;
    field_11a         = 0;
    field_ea          = 0;
    field_9b          = 0;
    kind_             = 9;
    blastSoundPlayed_ = 0;
    sound_ab_         = 0;                  /* the redundant second clear */
    sound_af_         = 0;
    sound_b3_         = 0;
    sound_b7_         = 0;
    sound_bb_         = 0;
    sound_bf_         = 0;
    sound_c3_         = 0;
    sound_c7_         = 0;
    sound_cb_         = 0;
    field_d8          = 0;
    field_48          = 50.0;               /* 0 at +0x48, 0x40490000 at +0x4c */
}

void *Bomb::scalarDeletingDtor(Bomb *self, unsigned int flags)
{
    if (flags & 1)
        delete self;
    return self;
}

/* ═══ 0x00417700 -- Game::SpawnBombObject ══════════════════════════════════ */
static unsigned long s_spawns       = 0;
static int           s_logged_spawn = 0;
static int           s_logged_sound = 0;

/* The inline `repne scasb` + `rep movsd/movsb` pair: an unbounded strcpy
 * including the terminator.  Not bounded here either -- see the header. */
static void spawn_strcpy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != '\0')
        ;
}

/* One `if (enabled) { strcpy; Acquire; store }` block.  The slot is re-read
 * for the store, as in the original. */
void Bomb::acquireInto(Game *game, Bomb **slot, const SoundAssetName *asset,
                       CStaticSoundbuffer *Bomb::*field)
{
    char name[256];

    if (asset->enabled == 0)
        return;

    spawn_strcpy(name, asset->name);

    if (s_diag_spawn && !s_logged_sound) {
        s_logged_sound = 1;
        log_write("bomb: first sound acquire -- '%s'\n", name);
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

    id = game->claimSpareObjectId(game->bombIds(), game->bombCountRef());

    p = create();

    slot  = game->bombSlotRef(id);
    *slot = p;

    if (!s_logged_spawn) {
        s_logged_spawn = 1;
        log_write("bomb: first bomb spawn -- game=%p id=%u obj=%p "
                  "u=%u v=%u h=%u flag=%u\n",
                  (void *)game, (unsigned)id, (void *)p, (unsigned)u,
                  (unsigned)v, (unsigned)h, (unsigned)f);
    }
    if (s_diag_spawn) {
        ++s_spawns;
        if ((s_spawns % 500) == 0)
            log_write("bomb: %lu spawns\n", s_spawns);
    }

    /* Defect 1: through the raw pointer, NOT the slot. */
    p->clock_ = game->clock();

    (*slot)->tickStep_   = game->tickStep();
    (*slot)->tileBase_ = game->tileBase();
    (*slot)->facing_   = f;

    (*slot)->cellU_      = (signed char)u;
    (*slot)->cellV_      = (signed char)v;
    (*slot)->heightCell_ = (signed char)h;

    /* Not in parameter order -- detail 3. */
    (*slot)->posU_ = (float)(int)(unsigned int)u;
    (*slot)->posY_ = (float)(int)(unsigned int)h;
    (*slot)->posV_ = (float)(int)(unsigned int)v;

    (*slot)->droppedAt_ = *game->clock();

    if (game->soundCreated() != 0) {
        acquireInto(game, slot, game->soundAsset429b6(), &Bomb::sound_b3_);

        if (game->soundAsset42ac2()->enabled != 0) {
            acquireInto(game, slot, game->soundAsset42ac2(), &Bomb::sound_b7_);
            /* Detail 2: the same flag, tested again. */
            if (game->soundAsset42ac2()->enabled != 0)
                acquireInto(game, slot, game->soundAsset42ac2(), &Bomb::sound_bb_);
        }

        acquireInto(game, slot, game->soundAsset46baa(), &Bomb::blastSound_);
        acquireInto(game, slot, game->soundAsset457c6(), &Bomb::sound_c3_);
        acquireInto(game, slot, game->soundAsset46132(), &Bomb::rollSound_);
    }
}

/* ═══ 0x00417a20 -- Game::RemoveEnemyObject ════════════════════════════════ */
static unsigned long s_removals        = 0;
static int           s_logged_remove   = 0;
static int           s_logged_release  = 0;

/* Release one sound field, re-reading the slot as the original does.  Owner
 * flag 0: a bomb never destroys a shared buffer. */
void Bomb::releaseField(SoundManager *sm, Bomb **slot,
                        CStaticSoundbuffer *Bomb::*field)
{
    CStaticSoundbuffer *buf = (*slot)->*field;

    if (buf == 0)
        return;

    if (s_diag_remove && !s_logged_release) {
        s_logged_release = 1;
        log_write("bomb: first sound release -- obj=%p buf=%p\n",
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
        log_write("bomb: first bomb removal -- game=%p id=%u obj=%p\n",
                  (void *)game, (unsigned)id, (void *)*slot);
    }
    if (s_diag_remove) {
        ++s_removals;
        if ((s_removals % 500) == 0)
            log_write("bomb: %lu removals\n", s_removals);
    }

    if (game->soundCreated() != 0) {
        /* Order verbatim from 0x00417a54..0x00417b22. */
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
                               0);                /* NOT nulled -- the defect */
}

/* ═══ 0x00402870 -- Game::UpdateBombFuseAndBlast ═══════════════════════════ */
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
        log_write("bomb: first bomb tick -- this=%p\n", (void *)this);
    }
    if (s_diag_bomb) {
        ++s_ticks;
        if ((s_ticks % 5000) == 0)
            log_write("bomb: %lu ticks\n", s_ticks);
    }

    /* Refresh the two cached copies, in the original's order. */
    tickStepCopy_ = *tickStep_;
    now_        = *clock_;

    /* The rolling/ticking loop sound, on the grid cell: (u, h, -v). */
    if (rollSound_ != 0) {
        CStatic_Set3DPosition(rollSound_,
                              (float)(int)cellU_,
                              (float)(int)heightCell_,
                              -(float)(int)cellV_,
                              1);
        if (field_86 == 0)                  /* raised by the BLAST below */
            CStatic_TriggerPlayback(rollSound_, 0);
        else
            CStatic_HaltPlayback(rollSound_);
    }

    /* ── ROLL: still fusing -- `!(>=)`, bug 1 ─────────────────────────── */
    diff = now_ - droppedAt_;
    if (!(diff >= fuse_ms())) {
        pendingMove_ = facing_;
        updateMovement();
        if (field_14e != 0)
            field_ef = 1;
        return;
    }

    /* ── BLAST ───────────────────────────────────────────────────────── */
    field_11e    = 0xff;
    field_ef     = 1;
    field_86     = 1;                       /* halts the roll sound */
    field_14e    = 0;
    pendingMove_ = 0;

    if (blastSoundPlayed_ == 0) {
        if (blastSound_ != 0) {
            /* From the FLOAT world position, not the grid bytes. */
            CStatic_Set3DPosition(blastSound_, posU_, posY_, -posV_, 1);
            CStatic_TriggerPlayback(blastSound_, 0);
        }
        blastSoundPlayed_ = 1;
    }

    if (s_diag_bomb && !s_logged_blast) {
        s_logged_blast = 1;
        log_write("bomb: first blast at u=%d v=%d h=%d after %.0f ms\n",
                  (int)cellU_, (int)cellV_, (int)heightCell_, diff);
    }

    for (int u = (int)cellU_ - 1; u <= (int)cellU_ + 1; ++u) {
        for (int v = (int)cellV_ - 1; v <= (int)cellV_ + 1; ++v) {
            Tile *t = tile(u, v);

            /* Plain byte store of the height -- bug 5. */
            t->setBlastHeight((unsigned char)heightCell_);

            if ((signed char)t->objectMarker() == TILE_KIND_DESTRUCTIBLE &&
                (int)heightCell_ == (int)t->height() &&
                t->field217() == 0) {

                t->setField217(1);                    /* spent */
                t->setBlastTime(now_);
                t->setField203(1);
                t->setField20f(1);

                GameLog_LogMessage(GAME_LOGGER, 1,
                                   "GAME: obstacle is exploding at:%d,%d,%d",
                                   u, v, (int)t->height());

                /* Promote the hidden contents so they can be picked up. */
                signed char hidden = (signed char)t->field202();
                if (hidden != 0) {
                    t->setContents((unsigned char)hidden);
                    t->setField202(0);
                }
            }
        }
    }

    /* ── CLEAR: the kill zone closes ─────────────────────────────────── */
    diff = now_ - droppedAt_;
    if (diff >= clear_ms() && zoneCleared_ == 0) {
        for (int u = (int)cellU_ - 1; u <= (int)cellU_ + 1; ++u) {
            for (int v = (int)cellV_ - 1; v <= (int)cellV_ + 1; ++v) {
                Tile *t = tile(u, v);

                t->setBlastHeight(0);

                /* No `spent` test here -- bug 3. */
                if ((signed char)t->objectMarker() == TILE_KIND_DESTRUCTIBLE &&
                    (int)heightCell_ == (int)t->height())
                    t->setField203(0);
            }
        }
    }

    /* ── REMOVE ──────────────────────────────────────────────────────── */
    diff = now_ - droppedAt_;
    if (diff >= remove_ms() && zoneCleared_ == 0) {
        zoneCleared_ = 1;
        moveState_   = 4;                /* despawn */
        updateMovement();
        return;
    }

    zoneCleared_ = 0;
    updateMovement();
}

/* ═══ Exports -- thin ABI shims; patch.py routes the three originals here ═ */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_UpdateBombFuseAndBlast(Bomb *self)
{
    self->tick();
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnBombObject(Game *self, unsigned int uArg, unsigned int vArg,
                    unsigned int hArg, unsigned int flagArg)
{
    Bomb::spawn(self, uArg, vArg, hArg, flagArg);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveEnemyObject(Game *self, unsigned int idArg)
{
    Bomb::remove(self, idArg);
}
