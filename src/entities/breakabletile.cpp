/* BreakableTile -- the falling tile, tile kind 0x0d: spawn, tick and purge.
 *
 *     Game::SpawnBreakableObject      0x00418240   (was objectplace.cpp)
 *     Game::PurgeBreakableObjects     0x004183f0   (was gamereset.cpp)
 *     BreakableTile::UpdateBreakableTile 0x00403d40
 *
 *     BreakableTile ctor / dtor       0x00403ce0 / 0x00403d10 + 0x00403d30
 *
 * Every function that reads or writes a BreakableTile field lives here;
 * breakabletile.h keeps the fields private.  Outside accessors: the original
 * RenderGameFrame reads +0x3c and +0x31/+0x32/+0x33 (listing 00428E2D ..
 * 00428ECE), and levelsounds.cpp writes the two sound handles by raw offset.
 * Game::Load, Game::Destruct and WriteLevelReport touch the COUNT only.
 *
 * ─── The tick ────────────────────────────────────────────────────────────
 *
 * `__thiscall`, no stack arguments, `RET`.  ONE call site, an E8 at
 * 0x004150AF inside GameTick (ours now: game->breakableSlot(i)->tick()).  The
 * original ends `XOR AL,AL` with the top of EAX holding the last FPU compare's
 * flags; its caller never reads it, so the tick is `void`.
 *
 * The tile is reached by tileBase + (cellV + cellU * 100) * 0x7f with both
 * cells read SIGNED (MOVSX at 0x00403d6e), then +0x19c height, +0x19d kind,
 * +0x1a5 occupant, +0x217 spent.
 *
 *   ARM      occupant != 0 && !armed && tile.spent == 0
 *                -> armedAt = now, armed = 1
 *   FALL     armed && now - armedAt >= 1500 ms  (_DAT_0045d2e0, ~90 frames)
 *                -> tile.kind = 0 (the cell becomes void, standing on it kills)
 *                   tile.spent = 1, armed = 0
 *                   if (param == 0) respawnPending = 1
 *   RESPAWN  respawnPending && now - armedAt > 5000 ms  (_DAT_0045d2d8)
 *                -> tile.kind = 0x0d, tile.spent = 0, re-armable
 *
 * Four things that are easy to get wrong:
 *
 * 1. THE RESPAWN COMPARE IS STRICT AND THE FALL COMPARE IS NOT.  The
 *    decompile has `elapsed >= X` for the fall and `elapsed >= X && (elapsed
 *    == X) == 0` for the respawn -- i.e. `>` -- which is the FCOM/`JBE` vs
 *    `JB` distinction.  With a fixed 16.667 ms step the two differ only on an
 *    exact hit, but an exact hit is precisely what a fixed timestep makes
 *    reachable, so both forms are kept as written.
 *
 * 2. BOTH DELAYS ARE MEASURED FROM armedAt, NOT FROM THE FALL.  The respawn
 *    window is 5000 ms after the tile was *armed*, so a tile that fell at
 *    1500 ms is back 3500 ms later, not 5000.
 *
 * 3. THE ARM BRANCH RE-WRITES tile.spent = 0 having just tested it for 0.
 *    A dead store in the original is still a store, so it is kept.  (The
 *    decompile's separate stores to +0x5d and +0x61 are not two fields: they
 *    are the low and high halves of the one double at +0x5d, split around
 *    the `armed = 1` store by the scheduler.)
 *
 * 4. THE FALL SOUND IS GATED ON respawnPending BEING CLEAR, so the second
 *    drop of a re-armed tile is silent.  That reads like a bug and is kept.
 *
 * The double fall (a defect, preserved): Game::RemoveFoeObject never clears
 * the tile's occupant byte, so a foe that dies on one of these leaves it set.
 * When the tile respawns, that stale occupant re-arms it immediately and it
 * drops again 1.5 s later.  tests/destr-bait.rec depends on it -- the
 * recording explicitly waits out the repeat fall.  Nothing here fixes it.
 *
 * ─── Construction and destruction ────────────────────────────────────────
 *
 * 0x403ce0 constructs in two layers: the shared level-object base
 * constructor 0x401000 (installs the base vtable 0x45d290, zeroes +0x25,
 * +0x29, +0x2d), then the breakable's own (installs 0x45d2d0, zeroes +0x4d,
 * +0x51, +0x55, +0x48, +0x3c, +0x38).  Every other byte is left as operator
 * new returned it.  None of those is read before it is written: armedAt is
 * read only once armed or respawnPending is set, and both paths store it
 * first.  So allocating from our own heap cannot change what the tick reads.
 *
 * 0x403d10 (vtable slot 0; the vtable 0x45d2d0 has ONE slot -- the next
 * dword is 0) calls 0x403d30, which re-installs 0x45d2d0 and tail-jumps to
 * the base destructor 0x401060; then Free2 if flags & 1.  Both vtable stores
 * are dead -- the only caller passes flags 1 -- and are not reproduced.
 *
 * A byte scan of .text for 0x45d2d0 finds only the ctor (00403CEC) and the
 * dtor (00403D32); xref.py finds the ctor's one caller, the original spawn
 * (stubbed), and 0x403d30's one caller, 0x403d10; 0x403d10 is reached only
 * through the vtable.  So we both create and destroy every breakable, and use
 * our own new/delete.
 *
 * ─── Floating-point copies ───────────────────────────────────────────────
 *
 * Doubles are copied by plain assignment; the original copies them with
 * integer MOVs (now_ <- *clock_, armedAt_ / eventTime_ <- now_).  The two
 * differ only for a signalling NaN, which no clock value holds.  Accepted
 * deliberately (COHESION_PLAN.md, template 3).
 *
 * ─── Controls and diags ──────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=slowfall triples the fall delay, 1500 ms -> 4500 ms, leaving
 * the respawn window alone -- a measurement rather than a colour: on
 * Forest\DestrStart a tile stepped on and off takes about 270 frames to drop
 * instead of about 90.  KAROO_SIM_FX=placeaxis exchanges u and v in the
 * spawn (shared with liftobject.cpp).  KAROO_SIM_FX=keepobjects makes the
 * purge do nothing, shared with the other purges.
 *
 * KAROO_PLACE_DIAG=1 logs the first spawn and the first failed allocation,
 * plus a spawn count every 500; KAROO_RESET_DIAG=1 the first purge, the
 * first sound release and every live purge.
 */

#include <windows.h>
#include <new>               /* std::nothrow */
#include <string.h>          /* strcmp */

#include "breakabletile.h"
#include "game.h"
#include "tile.h"
#include "soundmanager.h"
#include "static.h"
#include "log.h"

/* The two doubles at 0x0045d2e0 and 0x0045d2d8. */
static const double FALL_DELAY_MS    = 1500.0;
static const double RESPAWN_DELAY_MS = 5000.0;


/* ─── Controls and diags, read by VALUE, never by presence ──────────────── */
static int s_fx_slowfall    = 0;
static int s_fx_placeaxis   = 0;
static int s_fx_keepobjects = 0;
static int s_diag_place     = 0;   /* KAROO_PLACE_DIAG */
static int s_diag_reset     = 0;   /* KAROO_RESET_DIAG */
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
        if (lstrcmpiA(buf, "slowfall") == 0) {
            s_fx_slowfall = 1;
            log_write("breakabletile: KAROO_SIM_FX=slowfall -- fall delay "
                      "%.0f ms, not %.0f\n",
                      FALL_DELAY_MS * 3.0, FALL_DELAY_MS);
        } else if (strcmp(buf, "placeaxis") == 0) {
            s_fx_placeaxis = 1;
            log_write("breakabletile: KAROO_SIM_FX=placeaxis -- u and v are "
                      "exchanged at the single point the spawn reads them\n");
        } else if (strcmp(buf, "keepobjects") == 0) {
            s_fx_keepobjects = 1;
            log_write("breakabletile: KAROO_SIM_FX=keepobjects -- breakable "
                      "purge does nothing\n");
        }
    }

    if (env_set("KAROO_PLACE_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_place = 1;
    if (env_set("KAROO_RESET_DIAG", buf, sizeof(buf)) && strcmp(buf, "0") != 0)
        s_diag_reset = 1;
}

/* ═══ Construction and destruction -- see the header ════════════════════ */
const BreakableTile::Vtbl BreakableTile::VTABLE = { &BreakableTile::scalarDeletingDtor };

BreakableTile *BreakableTile::create()
{
    return new (std::nothrow) BreakableTile;
}

BreakableTile::BreakableTile()
{
    /* base constructor 0x401000 */
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
    /* breakable constructor 0x403ce0, in its store order */
    vtable_         = &VTABLE;
    fallSound_      = 0;
    respawnSound_   = 0;
    armed_          = 0;
    respawnPending_ = 0;
    justFell_       = 0;
    justRespawned_  = 0;
}

void *BreakableTile::scalarDeletingDtor(BreakableTile *self, unsigned int flags)
{
    if (flags & 1)
        delete self;
    return self;
}

void BreakableTile::destroy()
{
    vtable_->scalarDeletingDtor(this, 1);
}

/* ═══ 0x00418240 -- Game::SpawnBreakableObject ═════════════════════════════
 *
 * __thiscall on Game, four dword stack arguments (RET 0x10): u, v, height,
 * param.  ONE E8 call site, 0x00416B0E, inside SetupLevelObjects, for tile
 * type 0x0d.
 *
 * From the LISTING:
 *
 * 1. Every store re-reads the slot it was just written to (0041829f loads it
 *    before 004182a8 stores), so a failed allocation faults on the first
 *    field store, here as there.
 *
 * 2. `+0x32` is written TWICE, at 0041830c and again at 004183a6, with the
 *    same value both times.  Redundant, and preserved.
 *
 * 3. `+0x2d` is NEGATED (`FCHS` at 00418372), where `+0x25` and `+0x29` are
 *    not.  The slide's is not negated.
 *
 * 4. It stamps the marker 0x0d on its tile, which SetupLevelObjects later
 *    reads back through FindNearestFlaggedTileInRadius when placing further
 *    objects -- why `placeaxis` fails levelreport.py as well as the replays.
 *
 * The count byte is re-read from Game before every store rather than held in
 * a register; it cannot change mid-function, so a local is equivalent.
 *
 * The original's MSVC EH frame around the allocation is not reproduced: the
 * allocator returns NULL rather than throwing (the original NULL-checks it),
 * so the frame is unobservable.
 */
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
        log_write("breakabletile: %u spawns\n", s_spawns);

    u = uArg & 0xff;
    v = vArg & 0xff;
    /* The single point the spawn reads its coordinates. */
    if (s_fx_placeaxis) {
        unsigned int t = u;
        u = v;
        v = t;
    }
    height = heightArg & 0xff;

    obj = create();
    if (obj == 0 && s_diag_place && !s_logged_oom) {
        s_logged_oom = 1;
        log_write("breakabletile: ALLOCATION FAILED in spawn -- the original "
                  "would store through the slot, which now holds NULL\n");
    }

    n = game->breakableCount();
    game->setBreakableSlot(n, obj);

    if (s_diag_place && !s_logged_spawn) {
        s_logged_spawn = 1;
        log_write("breakabletile: first breakable spawn -- slot=%u u=%u v=%u "
                  "height=%u p4=%u obj=%p\n",
                  (unsigned)n, u, v, height, paramArg & 0xff, (void *)obj);
    }

    obj->clock_    = game->clock();
    obj->tickStep_   = game->tickStep();
    obj->tileBase_ = game->tileBase();

    obj->cellU_      = (signed char)u;
    obj->cellV_      = (signed char)v;
    obj->heightCell_ = (signed char)height;

    obj->posU_ = (float)(int)u;
    obj->posY_ = (float)(int)height;
    obj->posV_ = -(float)(int)v;              /* FCHS at 00418372 */

    obj->noRespawn_ = (int)(paramArg & 0xff);

    /* Detail 2: +0x32 a SECOND time, with the same value. */
    obj->cellV_ = (signed char)v;

    idx = v + u * 100;
    Tile::at(game->tileBase(), (int)u, (int)v)->setObjectMarker(TILE_BREAKABLE);

    game->setBreakableCount((unsigned char)(n + 1));

    return idx & 0xffffff00;
}

/* ═══ 0x004183f0 -- Game::PurgeBreakableObjects ════════════════════════════
 *
 * The only purge with TWO sound handles.  From the LISTING:
 *
 *   1. THE SLOT IS RE-READ between the two releases.  0x00418441 is
 *      `MOV EDX,[EBP]` -- the object pointer is loaded again from the array
 *      before +0x51 is read, rather than reused.  Transcribed as written.
 *
 *   2. The second release is nested inside the `created` test but NOT inside
 *      the first handle's null test -- the two handle tests are siblings, so
 *      a null +0x4d does not skip +0x51.
 *
 * The count is RE-READ every iteration and the index is a byte (`JC`).  The
 * trailing count store is redundant when the count was already 0, and is
 * preserved.
 */
static int      s_logged_purge   = 0;
static int      s_logged_release = 0;
static unsigned s_live_purges    = 0;

static void release_sound(Game *game, CStaticSoundbuffer *h)
{
    if (h == 0)
        return;
    if (s_diag_reset && !s_logged_release) {
        s_logged_release = 1;
        log_write("breakabletile: first sound release -- h=%p\n", (void *)h);
    }
    game->soundManager()->releaseStaticForOwner(h, 1);
}

void BreakableTile::purgeAll(Game *game)
{
    unsigned char i;

    fx_init();

    if (s_fx_keepobjects)
        return;   /* objects AND count survive -- see gamereset.cpp */

    if (s_diag_reset && !s_logged_purge) {
        s_logged_purge = 1;
        log_write("breakabletile: first purge -- count=%u\n",
                  (unsigned)game->breakableCount());
    }

    i = 0;
    if (game->breakableCount() != 0) {
        if (s_diag_reset)
            log_write("breakabletile: LIVE purge #%u -- count=%u\n",
                      ++s_live_purges, (unsigned)game->breakableCount());
        do {
            if (game->soundCreated() != 0) {
                release_sound(game, game->breakableSlot(i)->fallSound_);
                /* the slot is loaded AGAIN here -- 0x00418441 */
                release_sound(game, game->breakableSlot(i)->respawnSound_);
            }
            BreakableTile *obj = game->breakableSlot(i);
            if (obj != 0)
                obj->destroy();
            i++;
        } while (i < game->breakableCount());
    }
    game->setBreakableCount(0);
}

/* ═══ 0x00403d40 -- BreakableTile::UpdateBreakableTile ════════════════════ */

Tile *BreakableTile::tile() const
{
    return Tile::at(tileBase_, (int)cellU_, (int)cellV_);
}

/* x = cellU, y = the tile's height byte (unsigned), z = -cellV. */
void BreakableTile::playAtTile(CStaticSoundbuffer *snd, const Tile *t) const
{
    CStatic_Set3DPosition(snd,
                          (float)(int)cellU_,
                          (float)t->height(),
                          -(float)(int)cellV_,
                          1);
    CStatic_TriggerPlayback(snd, 0);
}

void BreakableTile::tick()
{
    fx_init();
    const double fallDelay = s_fx_slowfall ? FALL_DELAY_MS * 3.0 : FALL_DELAY_MS;

    /* Refresh the two cached copies the rest of the tick reads. */
    tickStepCopy_ = *tickStep_;
    now_        = *clock_;

    Tile *t = tile();
    const double now = now_;

    /* ── ARM ─────────────────────────────────────────────────────────── */
    if ((signed char)t->occupant() != 0 && armed_ == 0 && t->busy() == 0) {
        armedAt_ = now;
        armed_   = 1;
        t->setBusy(0);                  /* dead store, preserved */
    }

    justFell_      = 0;
    justRespawned_ = 0;

    /* ── FALL -- note the compare is >=, unlike the respawn ───────────── */
    if (armed_ != 0) {
        const double elapsed = now - armedAt_;
        justFell_ = 0;
        if (elapsed >= fallDelay) {
            eventTime_ = now;
            justFell_  = 1;

            /* silent on the second drop of a re-armed tile -- note 4 */
            if (respawnPending_ == 0 && fallSound_ != 0)
                playAtTile(fallSound_, t);

            t->setObjectMarker(TILE_EMPTY);
            if (noRespawn_ == 0)
                respawnPending_ = 1;
            t->setBusy(1);
            armed_ = 0;
        }
    }

    /* ── RESPAWN -- strictly greater, and measured from armedAt ─────────── */
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

/* ═══ Exports -- thin ABI shims; patch.py routes the three originals here ═ */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_UpdateBreakableTile(BreakableTile *self)
{
    self->tick();
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_SpawnBreakableObject(Game *self, unsigned int uArg, unsigned int vArg,
                         unsigned int heightArg, unsigned int paramArg)
{
    return BreakableTile::spawn(self, uArg, vArg, heightArg, paramArg);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeBreakableObjects(Game *self)
{
    BreakableTile::purgeAll(self);
}
