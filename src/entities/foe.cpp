/* Foe -- spawn, remove, step and chase (COHESION_PLAN.md Band 4a).
 *
 *     Game::SpawnFoeObject        0x004172d0   (was objectspawn.cpp)
 *     Game::RemoveFoeObject       0x00417530   (was objectremove.cpp)
 *     Game::UpdateFoeObjectStep   0x00412240   (was foestep.cpp)
 *     Game::SetFoeChaseTarget     0x0043a9d0   (was foechase.cpp)
 *
 * Every function of ours that reads or writes a Foe field through the class
 * lives here; foe.h keeps the fields private (protected in the base).  The
 * raw-offset readers that remain are listed in foe.h.
 *
 * ═══ The spawn (GAMETICK_PLAN.md Band A) ══════════════════════════════════
 *
 * __thiscall on Game, FIVE dword stack arguments (RET 0x14), returns the new
 * ID in AL.  Three E8 sites: 0x0041529A 0x00416E8B 0x00416EBC.
 *
 * Allocation and construction are ours (see "Construction and destruction"
 * below), and so is the pathfinder's: AttachFoePathfinderToEntity 0x43a970
 * is FoePath::create() plus the store into +0x13b.  Kept as a callback:
 * ClaimSpareObjectIdSlot 0x417250 through Game::claimSpareObjectId.  It is
 * typed void in Ghidra but both spawns read AL as the new ID; "which local
 * is in AL at a void RET" is a compiler artefact, so it is called through
 * rather than rewritten from a decompile that does not model the return.
 *
 * The MSVC EH frame around the allocation is not reproduced: the allocator
 * returns NULL rather than throwing, so the frame is unobservable.
 *
 * 1. A FAILED ALLOCATION DEREFERENCES NULL.  The first field store goes
 *    through the pointer operator_new returned (00417352 MOV [EAX+0xc],ECX),
 *    not through the slot; every later store re-reads the slot.  Kept.
 * 2. THE SPAWN TIME IS i * 1500, built out of LEAs at 0x0041735f, FILD'd as a
 *    SIGNED 32-bit integer, plus the clock: each object spawned in the same
 *    frame starts 1500 later than the last.
 * 3. THE THREE FLOATS ARE NOT IN PARAMETER ORDER: +0x25 u, +0x29 HEIGHT, +0x2d
 *    v (0x004174b5 / 0x004174cf / 0x004174e0); the byte copies above them
 *    are in parameter order.
 * 4. THE TILE STAMP is the kind byte into tile+0x1a5 of the spawn cell.
 * 5. THE TYPE BYTE IS CONSUMED TWICE, AND ZEROED IN BETWEEN.  A type above
 *    0x64 is stored as `type - 0x64` into facing and then the working copy is
 *    zeroed (`XOR BL,BL`), so the pathfinder's +0x2a at 0x0041751d gets 0 for
 *    those and the original type for the rest.
 *
 * ═══ The remove ═══════════════════════════════════════════════════════════
 *
 * __thiscall on Game, one dword (the ID), RET 4.  Four E8 sites (0x00414BE1
 * 0x00415C52 0x004165A2 0x0041865D).  Releases eleven sound fields with owner
 * flag 1 (so a foe CAN destroy a shared buffer; a bomb, with 0, cannot), two
 * of them voice pools (+0x9f, +0xcf), in the listing's order
 * (0x00417564..0x0041768d), re-reading the slot for each.  +0xc3 is halted
 * first, and the pointer is re-read between the halt and the release
 * (0x004175f5).  Then the tail shared with the bomb remove (objectremove.h),
 * which -- unlike the bomb's -- nulls the slot.
 *
 * ═══ The step ═════════════════════════════════════════════════════════════
 *
 * __thiscall on the foe, TWO dword stack arguments (RET 8): the target cell
 * GameTick's foe loop chose, usually the player's.  One E8 site, 0x00415AA8.
 * `void` is exact: both exits leave UpdateEntityMovement's EAX with AL
 * zeroed, and the caller never reads it.
 *
 *   1. Latch the record and the clock.
 *   2. THE CONTACT TEST.  kind 3, both +0xef and +0xe4 clear, within
 *      Chebyshev distance 1 on BOTH axes, and 2000 ms since the last contact
 *      (+0xdc): latch the contact time and raise the hit flag +0xe4.
 *   3. THE PICKUP.  Clear +0x63.  Type 3 and not frozen, standing at the
 *      tile's height on contents 1: consume it, cycle the voice pool +0x9f,
 *      set +0x63.
 *   4. Clear pendingMove.  FROZEN (+0x14e) goes straight to movement.
 *   5. chase(target).  If it queued a move, remember the target and move --
 *      an EARLY RETURN, so steps 6 and 7 run only when the chase found none.
 *   6. Type 2 on tile kind 0x11: THE TURN TABLE.
 *   7. Type 4: re-chase the remembered target -- "return to post".
 *   8. UpdateEntityMovement.
 *
 * Exactness, all from the listing:
 *
 * a. The two __ftol arguments are +0xdc and `now` (the full 8-byte spill at
 *    [ESP+0xc] after the PUSH EDI); both truncated to 32 bits, subtracted,
 *    and compared UNSIGNED (JC) against 2000 -- a negative difference wraps
 *    and passes.
 * b. The player coordinate is ZERO-extended, the foe's own SIGN-extended,
 *    then |d| < 2 by the CDQ/XOR/SUB idiom.
 * c. The turn table is four SEQUENTIAL ifs, each able to rewrite
 *    pendingMove, and the last TESTS turn 2 but WRITES turn 1
 *    (0x004124CE..0x004124F6).
 * d. The pickup's height compare mixes signedness: +0x33 MOVSX, tile MOVZX.
 *
 * ═══ The chase ════════════════════════════════════════════════════════════
 *
 * __thiscall on the foe, three dword stack arguments, returns pendingMove.
 * Four E8 sites (0x004123D3, 0x00412516, 0x004158EC, 0x004159B3).  The
 * Ghidra decompile DROPS the __ftol arguments; everything below the tile
 * checks is from the listing at 0x0043AC4E and 0x0043ACDF.
 *
 *   1. Stash (speed, targetU, targetV) in the FoePath at +0x2f/+0x31/+0x32;
 *      for type 2 set its mover mode +0x2a from +0xd3.
 *   2. A move in flight (+0x14e) wins; nothing else runs.
 *   3. Search.  On success ADVANCE pf+0xe TO ITS PARENT: the search runs
 *      BACKWARD from the target, so the parent is the foe's next step.
 *   4. (du, dv) -> facing: dv -1 -> 1, du +1 -> 2, dv +1 -> 3, du -1 -> 4.
 *   5. A tracked mover (+0x124 == 1) rewrites that through a cascade, also
 *      recording a sub-mode in +0x125.
 *   6. Cancel the move if the destination is void, blocked, kind 0x0e, or a
 *      kind 9 / 0x0c link whose height or timing does not line up.
 *
 * Exactness, all from the listing:
 *
 * a. The four direction tests are independent ifs, the last match wins.
 * b. So is the cascade; its +10 branches (11..14) disable every later
 *    compare, and its last clause tests delta 2 but stores delta 1.
 * c. Each GetTurnedDirection is called twice, to test and to store.
 * d. The step cell's bytes are ZERO-extended, the foe's cell SIGN-extended.
 * e. The blocker test is unsigned (`CMP byte [ESI+0x11e],0xff / JNC`).
 * f. The __ftol high dword is forced to zero before FILD, so a negative
 *    timestamp re-reads as a huge positive one -- widened through unsigned.
 * g. The kind-9 height test runs twice -- on the step cell's kind, then on
 *    the FOE'S cell kind -- but both times compares the STEP cell's height.
 *
 * ═══ Floating-point copies ════════════════════════════════════════════════
 *
 * now <- *clock and lastContact <- now are plain assignments where the
 * original uses integer MOVs; they differ only for a signalling NaN, which no
 * clock value holds (COHESION_PLAN.md, template 3).
 *
 * ═══ Controls and diags ═══════════════════════════════════════════════════
 *
 * KAROO_SIM_FX=spawnswap  exchanges u and v at the one point the spawn reads
 *                         them (shared with bomb.cpp's spawn).
 * KAROO_SIM_FX=nocontact  the contact latch and hit flag are never raised.
 * KAROO_SIM_FX=foeunfreeze  the freeze gate is ignored.
 * KAROO_SIM_FX=chaseback  reverses the delta-to-facing table: foes step away.
 *                         Matched case-insensitively, as foechase.cpp did.
 * KAROO_SIM_FX=keepid     lives in the shared remove tail (objectremove.cpp).
 *
 * KAROO_SPAWN_DIAG=1   spawn count every 500, each foe type once with the
 *                      level it first appears in, every (level, type > 0x64)
 *                      pair, the first high type and kind-2/kind-3 foe.
 * KAROO_REMOVE_DIAG=1  removal count every 500, first sound release.
 * KAROO_FOESTEP_DIAG=1 step count every 5000, first contact, consume, chase
 *                      direction, turn table, return-to-post, frozen foe.
 * The first spawn, removal and step are logged unconditionally.
 */

#include <windows.h>
#include <string.h>

#include "foe.h"
#include "game.h"
#include "tile.h"
#include "entitymove.h"
#include "entitymath.h"
#include "foepath.h"
#include "voicepool.h"
#include "objectremove.h"
#include "soundmanager.h"
#include "static.h"
#include "bomb.h"
#include "tilequery.h"
#include "log.h"

#include <new>               /* std::nothrow */


/* ─── Controls and diags, read by VALUE, never by presence ──────────────── */
static int s_fx_spawnswap = 0;
static int s_fx_nocontact = 0;
static int s_fx_unfreeze  = 0;
static int s_fx_chaseback = 0;
static int s_diag_spawn   = 0;   /* KAROO_SPAWN_DIAG   */
static int s_diag_remove  = 0;   /* KAROO_REMOVE_DIAG  */
static int s_diag_step    = 0;   /* KAROO_FOESTEP_DIAG */
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

/* ═══ Construction and destruction ═════════════════════════════════════════
 *
 * 0x411ff0 constructs in two layers: the MovableEntity base 0x438720 (ours:
 * MovableEntity() zeroes the three floats), then the foe's own stores, in the
 * listing's order below.  Every other byte is left as operator new returned
 * it, as the original leaves it -- facing, kind, cell and the rest are the
 * spawn's to write.  The ctor zeroes +0xab..+0xcf a second time right after
 * ZeroEntitySoundSlotPointers, plus +0x9f; dead stores for the handles, kept.
 * Two pairs of dword stores are one double each: +0x48/+0x4c = 20.0 and
 * +0x38/+0x3c = 1000.0.
 *
 * 0x412140 (vtable slot 0; 0x45d388 has ONE slot -- the next dword is 0)
 * calls 0x412160 and then Free2 if flags & 1.  0x412160, from its listing:
 * re-install 0x45d388 (dead), clear tile+0x1a1 (dword) and tile+0x1a5 on the
 * foe's cell and tile+0x1a5 on the cell (u - [+0x13f], v - [+0x140]), all
 * MOVSX; destroy and Free2 the FoePath if there is one; then the base dtor
 * 0x438760 (vtable stores only, dead -- the free follows).
 *
 * A byte scan of .text for 0x45d388 finds only the ctor (0041201d) and the
 * dtor (00412180).  0x411ff0's one caller is the spawn (ours); 0x412160's
 * only caller is 0x412140, reached only through the vtable, from the remove
 * (ours).  So we create and destroy every foe, and use our own new/delete.
 * The FoePath is ours too (FoePath::create / destroy, our own allocators;
 * see foepath.cpp's allocator note for why the Player cannot hold one).
 */
const Foe::Vtbl Foe::VTABLE = { &Foe::scalarDeletingDtor };

Foe *Foe::create()
{
    return new (std::nothrow) Foe;
}

Foe::Foe()
{
    /* MovableEntity() has run: 0x438720 */
    vtable_          = &VTABLE;
    zeroSoundSlots();                       /* 0x43ad60 */
    field_7a         = 0;
    field_82         = 0;
    removeRequested_ = 0;
    field_86         = 0;
    field_58         = 0;
    field_124        = 1;
    field_14e        = 0;
    pendingMove_     = 0;
    field_126        = 0.0;             /* two zero dwords */
    field_fb         = 0;
    field_120        = 0;
    field_11e        = 0xff;
    field_d7         = 0xff;
    field_48         = 20.0;                /* 0 at +0x48 ... */
    field_38         = 1000.0;              /* 0 at +0x38 ... */
    moveState_       = 0;
    field_ef         = 0;
    field_e8         = 0;
    field_e4         = 0;
    field_ff         = 0;
    field_156        = 0;
    field_e9         = 0;
    field_11a        = 0;
    field_ea         = 0;
    field_d3         = 0;
    field_9b         = 0;
    pool_9f_         = 0;
    sound_ab_        = 0;                   /* the redundant second clear */
    sound_af_        = 0;
    sound_b3_        = 0;
    sound_b7_        = 0;
    sound_bb_        = 0;
    sound_bf_        = 0;
    sound_c3_        = 0;
    sound_c7_        = 0;
    sound_cb_        = 0;
    sound_cf_        = 0;
    field_9a         = 0;
    field_6e         = 0;
    field_63         = 0;
    dropContents_    = 0;
    type_            = 0;
    /* ... 0x40340000 at +0x4c (00412117) */
    pathfinder_      = 0;
    /* ... 0x408f4000 at +0x3c (00412124) */
}

void Foe::destroy()
{
    FoePath *pf;

    tile(cellU_, cellV_)->setField1a1(0);
    tile(cellU_, cellV_)->setField1a5(0);
    tile(cellU_ - field_13f, cellV_ - field_140)->setField1a5(0);

    pf = pathfinder_;
    if (pf != 0)
        FoePath::destroy(pf);               /* 0x401c00, then Free2 */
}

void *Foe::scalarDeletingDtor(Foe *self, unsigned int flags)
{
    self->destroy();
    if (flags & 1)
        delete self;
    return self;
}

/* ═══ 0x004172d0 -- Game::SpawnFoeObject ═══════════════════════════════════ */

/* Each distinct foe type, logged once with the level it first appeared in;
 * types above 0x64 once per (level, type), keyed by a hash of the level
 * path.  Run under tools/levelreport.py to census all 80 levels. */
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

    id = game->claimSpareObjectId(game->foeIds(), game->foeCountRef());

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

    /* Detail 1: through the raw pointer, NOT the slot. */
    p->clock_ = game->clock();

    (*slot)->record_ = game->field_170a5c();

    /* Detail 2: i * 1500, as a signed int, plus the clock. */
    (*slot)->field_72 = (double)(int)(i * 0x5dc) + *game->clock();

    (*slot)->kind_ = (unsigned char)kind;
    if (kind == 2) {
        (*slot)->field_66 = 500.0;          /* 0 at +0x66, 0x407f4000 at +0x6a */
        if (s_diag_spawn && !s_logged_kind2) {
            s_logged_kind2 = 1;
            log_write("foe: first kind-2 foe\n");
        }
    } else if (kind == 3) {
        (*slot)->field_66 = 700.0;          /* 0 at +0x66, 0x4085e000 at +0x6a */
        if (s_diag_spawn && !s_logged_kind3) {
            s_logged_kind3 = 1;
            log_write("foe: first kind-3 foe\n");
        }
    }

    (*slot)->tileBase_ = game->tileBase();
    (*slot)->type_     = type;

    if (type > 0x64) {
        /* Detail 5: SUB BL,0x64 then XOR BL,BL. */
        (*slot)->facing_ = (unsigned char)(type - 0x64);
        type = 0;
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

    /* Detail 3: not in parameter order. */
    (*slot)->posU_ = (float)(int)(unsigned int)u;
    (*slot)->posY_ = (float)(int)(unsigned int)h;
    (*slot)->posV_ = (float)(int)(unsigned int)v;

    /* Detail 4: the kind byte, re-read from the slot, into the spawn cell. */
    Tile::at(game->tileBase(), (int)u, (int)v)->setField1a5((*slot)->kind_);

    /* AttachFoePathfinderToEntity 0x43a970: allocate and construct (reading
     * the foe's tile base), then store -- NULL on a failed allocation. */
    (*slot)->pathfinder_ = FoePath::create((*slot)->tileBase_, 0);

    /* Detail 5: the ZEROED working copy, not the original type. */
    (*slot)->pathfinder_->setMode(type);

    return id;
}

/* ═══ 0x00417530 -- Game::RemoveFoeObject ══════════════════════════════════ */
static unsigned long s_removals       = 0;
static int           s_logged_remove  = 0;
static int           s_logged_release = 0;

/* Release one sound field, if the foe holds one.  Owner flag 1 throughout. */
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
        sm->releasePooledForOwner(buf, 1);
    else
        sm->releaseStaticForOwner(buf, 1);
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
        /* Every argument re-reads the slot, as the original does. */
        release(sm, *slot, (*slot)->pool_9f_,  1);
        release(sm, *slot, (*slot)->sound_b3_, 0);
        release(sm, *slot, (*slot)->sound_c7_, 0);
        release(sm, *slot, (*slot)->sound_b7_, 0);
        release(sm, *slot, (*slot)->sound_bb_, 0);

        /* Halted, then released -- the pointer re-read between the two. */
        if ((*slot)->sound_c3_ != 0) {
            CStatic_HaltPlayback((*slot)->sound_c3_);
            release(sm, *slot, (*slot)->sound_c3_, 0);
        }

        release(sm, *slot, (*slot)->sound_ab_, 0);
        release(sm, *slot, (*slot)->sound_af_, 0);
        release(sm, *slot, (*slot)->sound_cb_, 0);
        release(sm, *slot, (*slot)->sound_cf_, 1);   /* a voice pool on a foe */
        release(sm, *slot, (*slot)->sound_a7_, 0);
    }

    Object_DestroyAndCompactId((void **)slot, game->foeCountRef(),
                               game->foeIds(), id,
                               1);                /* the foe path DOES null */
}

/* ═══ 0x00412240 -- Game::UpdateFoeObjectStep ══════════════════════════════ */

/* The CRT's __ftol 0x00451134: truncate toward zero, low 32 bits. */
static inline int ftol_i(double v)
{
    return (int)(long long)v;
}

/* The CDQ / XOR / SUB idiom, spelled out. */
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

    /* 1. In the original's order. */
    recordCopy_ = *record_;
    now_        = *clock_;

    /* ── 2. The contact test ─────────────────────────────────────────── */
    if ((signed char)kind_ == 3 && field_ef == 0 && field_e4 == 0) {
        /* b: the player zero-extended, the foe's own sign-extended. */
        if (iabs_orig((int)playerU - (int)cellU_) < 2 &&
            iabs_orig((int)playerV - (int)cellV_) < 2) {

            /* a: truncated, subtracted, compared UNSIGNED. */
            int last = ftol_i(field_dc);
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
                    field_dc = now_;          /* two dword MOVs originally */
                    field_e4 = 1;
                }
            }
        }
    }

    /* ── 3. The pickup ───────────────────────────────────────────────── */
    field_63 = 0;
    if ((signed char)type_ == 3 && field_14e == 0) {
        Tile *t = tile(cellU_, cellV_);

        /* d: +0x33 signed, the tile's height unsigned. */
        if ((int)heightCell_ == (int)t->height() &&
            (signed char)t->contents() == 1) {

            t->setContents(0);

            if (s_diag_step && !s_logged_consume) {
                s_logged_consume = 1;
                log_write("foe: first tile consume -- cell=(%d,%d)\n",
                          (int)cellU_, (int)cellV_);
            }

            if (pool_9f_ != 0) {
                Sim_BroadcastPoolVoiceCoordinates(pool_9f_,
                                                  (float)(int)cellU_,
                                                  (float)(int)heightCell_,
                                                  -(float)(int)cellV_,
                                                  1);
                Sim_VoicePoolCycle(pool_9f_, 0);
            }
            field_63 = 1;
        }
    }

    /* ── 4. Clear the move; a frozen foe goes straight to movement ───── */
    pendingMove_ = 0;
    if (field_14e != 0) {
        if (s_diag_step && !s_logged_frozen) {
            s_logged_frozen = 1;
            log_write("foe: first FROZEN foe -- +0x14e=%d cell=(%d,%d)\n",
                      field_14e, (int)cellU_, (int)cellV_);
        }
    }
    if (field_14e != 0 && !s_fx_unfreeze) {
        updateMovement();
        return;
    }

    /* ── 5. The chase ────────────────────────────────────────────────── */
    chase(playerU, playerV, field_64);

    if ((signed char)pendingMove_ != 0) {
        if (s_diag_step && !s_logged_chase) {
            s_logged_chase = 1;
            log_write("foe: first chase direction -- dir=%u foe=(%d,%d)\n",
                      (unsigned)pendingMove_, (int)cellU_, (int)cellV_);
        }
        /* The EARLY RETURN: steps 6 and 7 run only when the chase found
         * nothing. */
        targetU_ = playerU;
        targetV_ = playerV;
        updateMovement();
        return;
    }

    /* ── 6. The turn table ───────────────────────────────────────────── */
    if ((signed char)type_ == 2) {
        Tile *t = tile(cellU_, cellV_);

        if ((signed char)t->objectMarker() == 0x11) {
            unsigned char facing;

            if (s_diag_step && !s_logged_turn) {
                s_logged_turn = 1;
                log_write("foe: first turn table -- cell=(%d,%d)\n",
                          (int)cellU_, (int)cellV_);
            }

            if ((signed char)field_108 != 0)
                pendingMove_ = Sim_GetTurnedDirection(field_108, 2);

            facing = facing_;

            /* c: four SEQUENTIAL ifs, each able to rewrite pendingMove. */
            if (pendingMove_ == facing) {
                pendingMove_ = facing;
                field_125    = 1;
            }
            if (pendingMove_ == Sim_GetTurnedDirection(facing, 1)) {
                field_125    = 2;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
            }
            if (pendingMove_ == Sim_GetTurnedDirection(facing_, 3)) {
                field_125    = 4;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 3) + 10);
            }
            /* Tests turn 2 but WRITES turn 1 -- the original's. */
            if (pendingMove_ == Sim_GetTurnedDirection(facing_, 2)) {
                field_125    = 2;
                pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
            }
        }
    }

    /* ── 7. Type 4: return to post ───────────────────────────────────── */
    if ((signed char)type_ == 4) {
        if (s_diag_step && !s_logged_repost) {
            s_logged_repost = 1;
            log_write("foe: first return-to-post -- target=(%u,%u)\n",
                      (unsigned)targetU_, (unsigned)targetV_);
        }
        chase(targetU_, targetV_, field_64);
    }

    /* ── 8. Every path ends here ─────────────────────────────────────── */
    updateMovement();
}

/* ═══ 0x0043a9d0 -- Game::SetFoeChaseTarget ════════════════════════════════ */

/* f: __ftol, low dword kept, re-widened through a ZERO high dword. */
static inline unsigned ftol32(double v)
{
    return (unsigned)(long long)v;
}

unsigned char Foe::chase(unsigned char targetU, unsigned char targetV,
                         unsigned short speed)
{
    fx_init();

    /* 1. Stash the search parameters in the pathfinder. */
    pathfinder_->setCap(speed);
    pathfinder_->setTarget(targetU, targetV);

    if ((signed char)type_ == 2)
        pathfinder_->setMode((field_d3 != 0) ? 0 : 2);

    /* 2. A move already in flight wins. */
    if (field_14e != 0)
        return pendingMove_;

    /* 3. Search -- backward, from the target to the foe. */
    if (pathfinder_->find((int)cellU_, (int)cellV_,
                          (unsigned)targetU, (unsigned)targetV) == 0) {
        pendingMove_ = 0;
        return pendingMove_;
    }

    /* Advance the result node to its PARENT: the foe's next step. */
    pathfinder_->setResult(pathfinder_->result()->parent);

    const PathNode *node = pathfinder_->result();
    /* The low bytes of the node's int cell, as the original's byte loads. */
    const unsigned char nu = (unsigned char)node->u;
    const unsigned char nv = (unsigned char)node->v;

    /* 4. Delta -> facing.  Four independent ifs; 8-bit arithmetic. */
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

    /* 5. The tracked-mover cascade; order-dependent (b). */
    if ((signed char)field_124 == 1) {
        const unsigned char facing = facing_;

        if (pendingMove_ == facing) {
            pendingMove_ = facing;
            field_125    = 1;
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing, 2)) {
            pendingMove_ = Sim_GetTurnedDirection(facing_, 2);
            field_125    = 3;
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 1)) {
            field_125    = 2;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
        }
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 3)) {
            field_125    = 4;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 3) + 10);
        }
        /* Tests with delta 2, stores with delta 1 -- the original's. */
        if (pendingMove_ == Sim_GetTurnedDirection(facing_, 2)) {
            field_125    = 2;
            pendingMove_ = (unsigned char)(Sim_GetTurnedDirection(facing_, 1) + 10);
        }
    }

    /* The move must be the facing, or its reverse; otherwise leave it be. */
    if (facing_ != pendingMove_) {
        if (facing_ != Sim_GetTurnedDirection(pendingMove_, 2))
            return pendingMove_;
    }

    /* 6. The cancel clauses.  d: the step cell zero-extended, the foe's own
     * cell sign-extended. */
    Tile *step = Tile::at(tileBase_, (int)nu, (int)nv);
    Tile *here = tile(cellU_, cellV_);

    if (step->objectMarker() == 0)
        pendingMove_ = 0;
    if (field_11e != 0xff && step->slideTrack() != 0)       /* e */
        pendingMove_ = 0;
    if (here->objectMarker() == 0x0e)
        pendingMove_ = 0;

    if (step->objectMarker() == 9) {
        const unsigned a = ftol32(step->liftParkedSince());
        const unsigned b = ftol32(now_ - (double)a);
        const unsigned c = ftol32(step->liftDwell());
        const unsigned d = ftol32(field_132);
        if ((int)(c - b) < (int)d)
            pendingMove_ = 0;
        if ((unsigned)step->height() != (unsigned)(int)heightCell_)
            pendingMove_ = 0;
    }

    /* g: runs on the FOE'S cell kind, but tests the STEP cell's height. */
    if (here->objectMarker() == 9) {
        if ((unsigned)step->height() != (unsigned)(int)heightCell_)
            pendingMove_ = 0;
    }

    if (step->objectMarker() != 0x0c)
        return pendingMove_;

    {
        const unsigned e = ftol32(step->slideParkedSince());
        const unsigned f = ftol32(now_ - (double)e);
        const unsigned g = ftol32(step->slideDwell());
        const unsigned h = ftol32(field_132);
        if ((int)(g - f) >= (int)h)
            return pendingMove_;
    }

    pendingMove_ = 0;
    return pendingMove_;
}

/* ═══ Exports -- thin ABI shims; patch.py routes the four originals here ═ */

extern "C" __declspec(dllexport) unsigned char __attribute__((thiscall))
Sim_SpawnFoeObject(Game *self, unsigned int uArg, unsigned int vArg,
                   unsigned int hArg, unsigned int kindArg,
                   unsigned int typeArg)
{
    return Foe::spawn(self, uArg, vArg, hArg, kindArg, typeArg);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveFoeObject(Game *self, unsigned int idArg)
{
    Foe::remove(self, idArg);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_UpdateFoeObjectStep(Foe *self, unsigned char playerU, unsigned char playerV)
{
    self->step(playerU, playerV);
}

extern "C" __declspec(dllexport) unsigned char __attribute__((thiscall))
Sim_SetFoeChaseTarget(Foe *self, unsigned char targetU, unsigned char targetV,
                      unsigned short speed)
{
    return self->chase(targetU, targetV, speed);
}

/* ═══ GameTick's foe loop (was gametick.cpp, listing 0x414df0-0x416414) ═════
 *
 * Moved verbatim from the loop body; the loop keeps the order of the calls.
 * Player and Game values are the loop's arguments, read at the same point.
 */

void Foe::chooseTarget(Game *game, int hold,
                       unsigned char playerU, unsigned char playerV,
                       unsigned char escortU, unsigned char escortV,
                       unsigned char *pu, unsigned char *pv)
{
    unsigned char tu = playerU, tv = playerV;

    field_ef = hold;
    field_64 = 0x32;
    if (type_ == 1) {
        tu = escortU;
        tv = escortV;
        field_64 = 400;
    }
    if (type_ == 2) {
        tu = (unsigned char)cellU_;
        tv = (unsigned char)cellV_;
        if (Sim_FindNearestListedObjectTile(game, &tu, &tv, 7) != 0) {
            field_64 = 100;
            chase(tu, tv, 100);
            if (field_d3 == 0 && pendingMove_ == 0 && field_125 == 0) {
                tu = playerU;
                tv = playerV;
                field_64 = 0x32;
            }
        } else {
            tu = playerU;
            tv = playerV;
            field_64 = 100;
        }
    }
    if (type_ == 3) {
        tu = (unsigned char)cellU_;
        tv = (unsigned char)cellV_;
        if (Sim_FindNearestFlaggedTileInRadius(game, &tu, &tv, 5) == 0) {
            tu = playerU;
            tv = playerV;
            field_64 = 100;
        } else {
            field_64 = 0x96;
            chase(tu, tv, 0x96);
            if (pendingMove_ == 0) {
                tu = playerU;
                tv = playerV;
                field_64 = 100;
            }
        }
    }
    if (type_ == 5) {
        int found = 0;
        field_ef = 0;
        for (int j = 0; j < (int)game->foeCount(); ++j) {
            Foe *other = game->foeSlot(game->foeId(j));
            if (other->kind_ == 2) {
                found = 1;
                tu = (unsigned char)other->cellU_;
                tv = (unsigned char)other->cellV_;
                field_64 = 0x96;
            }
        }
        if (!found)
            field_ef = 1;
    }
    if (type_ == 7) {
        field_ef = 0;
        if (Sim_FindFarthestOccupiedTile(this, &tu, &tv) != 0)
            field_64 = 0x96;
        else
            field_ef = 1;
    }
    *pu = tu;
    *pv = tv;
}

/* The same "too late leaves the flag" shape as the player's bomb drop. */
void Foe::dropBomb(Game *game)
{
    if (field_e4 == 0 || type_ == 2)
        return;
    int spawn = 1, offset = 0;
    if (field_14e != 0) {
        long double since = (long double)*game->clock() - (long double)field_146;
        if (since < 50.0L)
            offset = 1;
        else
            spawn = 0;
    }
    if (!spawn)
        return;
    if (offset)
        Bomb::spawn(game, (unsigned char)((unsigned char)cellU_ - (unsigned char)field_13f),
                          (unsigned char)((unsigned char)cellV_ - (unsigned char)field_140),
                          (unsigned char)((unsigned char)heightCell_ - (unsigned char)field_141),
                          facing_);
    else
        Bomb::spawn(game, (unsigned char)cellU_, (unsigned char)cellV_,
                          (unsigned char)heightCell_, facing_);
    field_e4 = 0;
}

/* sqrt((dz^2 + dy^2) + dx^2) < 0.5, at 80 bits. */
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
    } else if (field_14e == 0) {
        field_9a = 0x28;
    }
}

bool Foe::finishDespawn(unsigned char *homeMarks)
{
    if (moveState_ == 0 || field_120 != 0)
        return false;
    field_86 = 1;
    int idx = ((int)(signed char)homeV_ + (int)(signed char)homeU_ * 100) * 0x7f;
    if (homeMarks[idx] != 0x64)
        homeMarks[idx] = 0;
    if (removeRequested_ == 0)
        return false;
    if ((long double)posY_ > 0.0L)
        tile(cellU_, cellV_)->setContents(dropContents_);
    return true;
}
