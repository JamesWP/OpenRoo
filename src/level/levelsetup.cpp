/* GAMETICK_PLAN.md Band B — the level object builder, three functions:
 *
 *     Game::SetupLevelObjects        0x00416420   (0xE21 bytes)
 *     Game::ResetLevelObjectCounters 0x0041ceb0
 *     Game::FindTileByTypeMarker     0x0041f430
 *
 * This is the LAST of Band B.  `SetupLevelObjects` is the function that turns
 * a parsed level map into live objects: it clears the run's per-level state,
 * tears down whatever the previous level left, finds the player start, then
 * walks the whole tilemap once dispatching on each cell's type code —
 * spawning lifts, slides, breakables, bridges and foes, wiring switch and
 * teleport pairs, and counting everything the score and the HUD need.
 *
 * ─── Why these three ─────────────────────────────────────────────────────
 *
 * Callee lists, read from Ghidra first, per the plan's standing rule:
 *
 *   ResetLevelObjectCounters   (none — a true leaf)
 *   FindTileByTypeMarker       (none — a true leaf), and BOTH its call sites
 *                              are inside SetupLevelObjects
 *   SetupLevelObjects          24 callees, of which all but the six below
 *                              are already ours
 *
 * So the two leaves come with it: leaving them in the binary would make our
 * `SetupLevelObjects` call back into game code, which the no-callback rule
 * forbids.
 *
 * ─── What stays in the game binary, and why ──────────────────────────────
 *
 * | Callee                          | VA       | Ruling                     |
 * |---------------------------------|----------|----------------------------|
 * | `LinkedList::Clear`             | 0x4254f0 | shared container helper    |
 * | `ReleaseScriptStreamBuffers`    | 0x41e840 | shared asset teardown      |
 * | `ValidateCDTrackLengths`        | 0x403420 | CD service; cdm.cpp owns   |
 * |                                 |          | the layer beneath it       |
 * | `FUN_00440450`                  | 0x440450 | `return 0;` — a shared     |
 * |                                 |          | no-op with another caller  |
 * | `srand`                         | 0x451672 | CRT                        |
 * | `rand`                          | 0x45167c | CRT — reimplemented below  |
 *
 * The first four are named callbacks, the same standing ruling
 * `objectremove.cpp`, `gamereset.cpp` and `player.cpp` recorded for
 * shared services.  `LinkedList::Clear` in particular is already documented
 * in patch.py as one of four container helpers with 43 call sites across
 * unrelated subsystems, deliberately neither replaced nor stubbed.
 *
 * `FUN_00440450` is left alone for the reason CLAUDE.md gives explicitly:
 * do not stub a shared no-op helper that is still live for another caller.
 *
 * ─── TWO CRT CALLS THAT ARE NOT WHAT THEY LOOK LIKE ──────────────────────
 *
 * `0x0041672B  CALL 0x0045169a` is `time(NULL)`, feeding `srand`.  That is
 * ONE OF THE FIVE SITES patch.py already redirects to `hooks_GameTime`
 * (REPLAY_PLAN.md Stage A2) — the hook that makes `KAROO_SEED` work.  A
 * replacement that called `0x0045169a` directly would seed from the wall
 * clock and destroy replay determinism, while still passing a casual read.
 * So this file calls the EXPORT.
 *
 * `rand()` is reimplemented rather than called, exactly as `player.cpp`
 * does, and over the SAME global seed at 0x00469f38 — a private seed would
 * desynchronise every other `rand()` caller.
 *
 * ─── Tile addressing ─────────────────────────────────────────────────────
 *
 * The tilemap is the one `tilequery.cpp` and `movableentity.cpp` document:
 * pitch 0x7f, row stride 100*0x7f, index `(v + u*100) * 0x7f`.  The extents
 * live at the map reader's +0x19a (HEIGHT, the v extent) and +0x19b (WIDTH,
 * the u extent), which are Game+0x2ab727 and Game+0x2ab728.
 *
 * The listing settles the loop roles beyond doubt: the OUTER loop is bounded
 * by 0x2ab727 and its counter is the `v` term of the index; the INNER is
 * bounded by 0x2ab728 and is the `u` term.  Both are read from the listing
 * rather than the decompile, because the decompile aliases the two counters
 * with the stack slots that also hold call arguments.
 *
 * ─── FindTileByTypeMarker 0x0041f430, from the LISTING ───────────────────
 *
 * `__thiscall(tiles, byte marker, char *out)`, RET 8.  Scans for the first
 * cell whose type byte (+0x19d) equals `marker`, in the same v-outer/u-inner
 * order, and writes THREE bytes:
 *
 *     out[0] = u        (the INNER counter)
 *     out[1] = v        (the OUTER counter)
 *     out[2] = tile+0x19c   (the cell's height byte)
 *
 * returning 1; or returns 0 having written nothing.  Ghidra renders this
 * unreadably because the inner counter is kept in the stack slot that held
 * the `marker` argument — the marker is copied to BL at 0x0041f444 before
 * the slot is reused.  That is the "nonsensical decompile" signal, and the
 * listing is the answer.
 *
 * NOTE the caller relies on the failure case leaving `out` untouched: the
 * marker-4 lookup's return value is DISCARDED, so on a level with no marker-4
 * cell the three bytes at +0x17530b keep whatever they held from the previous
 * level.  Preserved.
 *
 * ─── ResetLevelObjectCounters 0x0041ceb0 ─────────────────────────────────
 *
 * 21 WORD stores of zero over Game+0x421df..0x4220a.  Two things preserved:
 * the stores are NOT in ascending address order (0x421f9 is written before
 * 0x42201, which is written before 0x421ff), and **0x421e5 is skipped** —
 * the one WORD in the span the reset does not clear.
 *
 * ─── Details of SetupLevelObjects that a summary would get wrong ─────────
 *
 * **`OpenExtraObjectsFile` takes ONE argument, not two.**  Ghidra's
 * decompile shows `OpenExtraObjectsFile(&field_0x48b98, iVar11, &name)`, but
 * 0x004171FC pushes only `EDI` (the level name).  The apparent second
 * argument is a stale register.  `extraobjects.cpp`'s own signature agrees.
 *
 * **A type-2 foe cell keeps its parameter byte; every other cell loses it.**
 * At 0x00416E96 the type-2 branch sets EAX=1, and 0x00416EC9 tests it: if a
 * type-2 foe was spawned the code JUMPS PAST the `[tile+0x2ab72b] = 0` at
 * 0x00416ECD.  So the trailing clear of the parameter byte happens for every
 * cell EXCEPT one that just spawned a type-2 foe.  Preserved.
 *
 * **The teleport-pair search cannot match its own cell**, but only by luck of
 * ordering: 0x00416B32 zeroes this cell's parameter byte BEFORE the search
 * begins, and the search matches on that byte, so the self-comparison always
 * fails.  Move the clear after the search and every teleport pairs with
 * itself.  Preserved as written.
 *
 * **Type 0x17 moves the item byte to a shadow slot.**  It copies
 * tile+0x2ab72c into tile+0x2ab78f, zeroes the original, and then counts the
 * SHADOW — so items under a 0x17 cell are counted in 0x421fb/0x421fd rather
 * than 0x42252/0x421f7.
 *
 * **Type 3 is rewritten to type 1 mid-walk** (0x00416A3A), before the later
 * type tests in the same iteration see it.
 *
 * **The timed-spawner branch splits on 100.**  Snapshot cells with contents
 * 0x64 and a parameter below 100 get a foe cap of 5 and a spawn interval of
 * `param * 1000` ms; at or above 100 they get a cap of `param - 100` and the
 * fixed interval 0x40b38800 (5000.0).  (Earlier notes called the cap an
 * "effect" and the interval a "duration"; GameTick's spawner loop, the
 * record's only reader, compares the foe count against the one and the time
 * since the last spawn against the other -- levelcensus.h.)  The decompile
 * renders the subtraction as `+ 0x9c`, a byte add of -100; the listing is
 * `SUB AL,0x64`.
 *
 * ─── Control ─────────────────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=setupflip  the tilemap walk visits `(v, u)` where the original
 *                         visits `(u, v)` — the index term order is exchanged
 *                         at the ONE point the walk computes it, so every
 *                         spawn, every counter and every tile write moves
 *                         together.  A coherent transposition of the level
 *                         rather than a perturbed value.
 *
 * KAROO_SIM_FX=noshadow   a type-0x17 cell no longer moves its item byte to
 *                         the shadow slot, so those items stay counted in
 *                         0x42252/0x421f7 instead of 0x421fb/0x421fd.  Both
 *                         pairs feed the 0x421e7 total, so the TOTAL is
 *                         unchanged and only the split moves — a control on
 *                         the counting alone, touching no geometry, so it
 *                         cannot reach the unbounded spawn scans and is safe
 *                         on BOTH gates.
 *
 * KAROO_SETUP_DIAG=1      logs each call with the map extents, then the
 *                         per-type spawn census and the final crystal and
 *                         object totals — enough to tell "this level builds
 *                         nothing of that kind" from "it builds them and the
 *                         control cannot see it".
 */

#include <windows.h>
#include <string.h>

#include "log.h"
#include "player.h"
#include "crtrand.h"
#include "game.h"
#include "bomb.h"
#include "tilequery.h"
#include "levelsetup.h"
#include "liftobject.h"
#include "slideobject.h"
#include "bridgeobject.h"
#include "breakabletile.h"
#include "foe.h"

/* ─── Game field offsets ─────────────────────────────────────────────────── */

#define G_UNK_42258        0x42258

/* Game fields with an owner now (game.h), formerly defined here:
 *   G_MODE_FLAG 0x14       nextLevelBonus()  (the peeked next-level bonus)
 *   G_PLAYER_POS 0x2ab580  cameraEye          G_SOUND_LISTENER 0x13cc94
 *   G_SOUND_E8 0x13cc90    G_SOUND_100 0x13cca8
 *   G_COUNT_CRYSTAL 0x42252 field_42252     G_LIFT_COUNT2 0x173b1a
 *   G_ENEMY_COUNT/IDS 0x17460f/0x174610 bombCount()/bombId()
 *   G_CLOCK 0x170a54 clock()                 G_LEVEL_NAME 0x173483 levelName() */
#define GAME               ((Game *)G)
/* The switch maximum (0x48b12), counts (0x170543) and cells (0x140543) are
 * Game::switchMax() and Game::switchCells() (switchcells.h). */
#define SW                 (((Game *)G)->switchCells())


/* The map (Game+0x2ab58d) and its tiles are LevelMap's and Tile's
 * (levelmap.h, tile.h).  The builder's names for the tile fields, as they
 * were Game-relative defines here:
 *   T_HEIGHT 0x19c height()      T_TYPE 0x19d objectMarker()
 *   T_PARAM  0x19e param()       T_ITEM 0x19f contents()
 *   T_FLAG_72D 0x1a0 blastHeight  T_DW_72E 0x1a1  T_B_732 0x1a5
 *   T_F_733 0x1a6 liftLiveHeight (the cell height as a float)
 *   T_DW_749 0x1bc slideTrack     T_PAIR_ID/U/V 0x1ed/0x1ee/0x1ef
 *   T_B_77E 0x1f1  T_B_77F 0x1f2  T_SWITCH_IDX 0x1f3  T_DW_783 0x1f6
 *   T_ITEM_SHADOW 0x202  T_DW_790 0x203  T_DW_79C 0x20f
 *   T_F_7A0 0x213 itemPhase       T_DW_7A4 0x217
 * and the "second tile layer" T2_* is the map's SNAPSHOT grid: +0 height,
 * +2 param, +3 contents. */

/* Counter words reset by ResetLevelObjectCounters. */
/* They are Game::census(), a LevelCensus (levelcensus.h), and keep the
 * C_* names as fields (C_L2_D -> l2_d); 0x421e5 is NEVER reset -- see the
 * header.  The free-bomb (0x2173d, 0xb each) and timed-spawner (0x2023d,
 * 0x15 each) tables they index are Game::freeBomb()/timedSpawner(). */
#define CEN                (((Game *)G)->census())
#define G_CLOCK            0x170a54    /* the game clock, a double           */

#define G_LEVEL_NAME       0x173483

/* Globals outside `Game`. */
#define GBL_LISTENER    ((float *)0x0046c4a0)
#define GBL_POS         ((float *)0x0046c4ac)
#define GBL_C4B8        (*(unsigned int *)0x0046c4b8)
#define GBL_C4BC        (*(unsigned int *)0x0046c4bc)

/* The two float constants the rand path multiplies by, read at their own
 * addresses so the bit patterns are exactly the game's:
 *   0x0045d2f8  6.2831855f   (2 pi)
 *   0x0045d3ec  3.0518509e-5 (very nearly 1/32768) */
#define K_TWO_PI   (*(const float *)0x0045d2f8)
#define K_INV_32K  (*(const float *)0x0045d3ec)

/* ─── Strings and the logger, at their original addresses ────────────────── */

struct GameLogger;
#define GAME_LOGGER_VA  ((GameLogger *)0x0046c4c0)

#define S_INIT_STARTED  ((const char *)0x00465788)
#define S_WARN_SWITCH   ((const char *)0x00465750)
#define S_WARN_XBRIDGE  ((const char *)0x00465718)
#define S_WARN_YBRIDGE  ((const char *)0x004656e0)
#define S_FREEBOMB      ((const char *)0x004656b8)
#define S_CD_MISSING    ((const char *)0x00465684)
#define S_CRYSTALS      ((const char *)0x00465658)
#define S_WARN_CRYSTALS ((const char *)0x00465618)
#define S_LEO_FAILED    ((const char *)0x004655d8)
#define S_LEO_LOADED    ((const char *)0x004655bc)

/* ─── Callbacks kept at their original addresses ─────────────────────────── */



typedef int (__attribute__((thiscall)) *noop_fn)(void *self);
#define ORIG_NOOP_440450   ((noop_fn)0x00440450)

typedef void (__cdecl *srand_fn)(unsigned int);
#define ORIG_SRAND         ((srand_fn)0x00451672)

/* time(), but through OUR hook -- see the header. */
extern "C" __declspec(dllexport) int __cdecl hooks_GameTime(int *out);

/* ─── Already ours -- called as exports, the originals carry UD2 stubs ───── */

extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...);




/* ─── Unaligned accessors ────────────────────────────────────────────────── */

typedef unsigned int   __attribute__((aligned(1))) u32_ua;
typedef int            __attribute__((aligned(1))) i32_ua;
typedef unsigned short __attribute__((aligned(1))) u16_ua;
typedef float          __attribute__((aligned(1))) f32_ua;
typedef double         __attribute__((aligned(1))) f64_ua;

#define B(g, off)    (*((unsigned char *)(g) + (off)))
#define SB(g, off)   (*(signed char *)((unsigned char *)(g) + (off)))
#define W(g, off)    (*(u16_ua *)((unsigned char *)(g) + (off)))
#define DW(g, off)   (*(u32_ua *)((unsigned char *)(g) + (off)))
#define F(g, off)    (*(f32_ua *)((unsigned char *)(g) + (off)))
#define D(g, off)    (*(f64_ua *)((unsigned char *)(g) + (off)))

/* The Player, embedded in Game (player.h).  Every function here names the
 * Game `G`; the Player is a fixed address inside it. */
#define PL  (((Game *)G)->player())

/* ─── FX / diag ──────────────────────────────────────────────────────────── */

static int s_fx_setupflip = 0;
static int s_fx_noshadow  = 0;
static int s_diag         = 0;
static int s_init         = 0;

static unsigned s_calls = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        if (strcmp(buf, "setupflip") == 0) {
            s_fx_setupflip = 1;
            log_write("levelsetup: KAROO_SIM_FX=setupflip -- the tilemap walk "
                      "transposes u and v at the one point it forms the "
                      "index\n");
        } else if (strcmp(buf, "noshadow") == 0) {
            s_fx_noshadow = 1;
            log_write("levelsetup: KAROO_SIM_FX=noshadow -- a type-0x17 cell "
                      "no longer moves its item byte to the shadow slot, so "
                      "the item counts land in the other pair of counters\n");
        }
    }

    n = GetEnvironmentVariableA("KAROO_SETUP_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

/* The tile at (u, v), `(v + u*100) * 0x7f` from the map.  The control
 * transposes the two terms HERE and nowhere else, so every consumer moves
 * together. */
static inline Tile *CELL(LevelMap *m, unsigned u, unsigned v)
{
    if (s_fx_setupflip)
        return m->tile((int)v, (int)u);
    return m->tile((int)u, (int)v);
}

/* The same cell from SIGNED bytes.  Three sites use MOVSX rather than a
 * zero-extended loop counter; on a well-formed level the values are small
 * positives and the two agree, but the sign extension is transcribed
 * because the bytes come from level data. */
static inline Tile *SCELL(LevelMap *m, int u, int v)
{
    if (s_fx_setupflip)
        return m->tile(v, u);
    return m->tile(u, v);
}

/* ═══ 0x0041ceb0 -- Game::ResetLevelObjectCounters ═════════════════════════
 *
 * Transcribed in the original's store ORDER, which is not ascending, and
 * with 0x421e5 left out exactly as the original leaves it out. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ResetLevelObjectCounters(Game *self)
{
    self->census()->reset();
}

/* ═══ 0x0041f430 -- Game::FindTileByTypeMarker ═════════════════════════════
 *
 * `out` is written ONLY on success; the caller depends on that (see header). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_FindTileByTypeMarker(LevelMap *map, unsigned int markerArg,
                         unsigned char *out)
{
    unsigned char marker = (unsigned char)markerArg;
    unsigned char v, u;

    if (map->extentV() == 0)
        return 0;

    v = 0;
    do {
        u = 0;
        if (map->extentU() != 0) {
            do {
                Tile *t = CELL(map, u, v);
                if (t->objectMarker() == marker) {
                    out[0] = u;
                    out[1] = v;
                    out[2] = t->height();
                    return 1;
                }
                u = (unsigned char)(u + 1);
            } while (u < map->extentU());
        }
        v = (unsigned char)(v + 1);
    } while (v < map->extentV());

    return 0;
}

/* ═══ 0x00416420 -- Game::SetupLevelObjects ════════════════════════════════ */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_SetupLevelObjects(Game *self)
{
    unsigned char *G = (unsigned char *)self;
    LevelMap *M = self->map();
    unsigned char v, u;
    unsigned char w, h;
    Tile *t, *s;

    fx_init();

    /* ── run state ─────────────────────────────────────────────────────── */
    /* The level-completed node 0x28's child count: 2 or 1 by mode. */
    ((Game *)G)->menu()->setChildCount(0x28, (GAME->nextLevelBonus() == 0) ? 2 : 1);
    PL->setLastSecondsMark(10.0);                /* two dwords: 0, 0x40240000 */

    GameLog_LogMessage(GAME_LOGGER_VA, 2, S_INIT_STARTED);

    ((Game *)G)->setLevelSoundsReady(0);
    ((Game *)G)->scriptPlayer()->releaseStreams();
    ((Game *)G)->extraObjects()->releaseSounds();
    ORIG_NOOP_440450(G + G_UNK_42258);

    /* The player position triple is zeroed, and the listener and the two
     * position globals are set from it; the listener's y is 1000.0f. */
    GAME->setCameraEyeBits(0, 0);
    GAME->setCameraEyeBits(1, 0);
    GAME->setCameraEyeBits(2, 0);

    GAME->setField13cc94(0, 0.0f);
    GAME->setField13cc94(1, 1000.0f);           /* 0x447a0000 */
    GAME->setField13cc94(2, 0.0f);

    ((u32_ua *)GBL_LISTENER)[0] = 0;
    ((u32_ua *)GBL_LISTENER)[1] = 0x447a0000;
    ((u32_ua *)GBL_LISTENER)[2] = 0;

    ((u32_ua *)GBL_POS)[0] = GAME->cameraEyeBits(0);
    ((u32_ua *)GBL_POS)[1] = GAME->cameraEyeBits(1);
    GBL_C4B8 = 0;
    ((u32_ua *)GBL_POS)[2] = GAME->cameraEyeBits(2);
    GBL_C4BC = 0;

    Sim_ResetLevelObjectCounters((Game *)G);

    GAME->setField42252(0);
    GAME->setField173b1a(0);
    PL->setMovingBackwards(0);
    PL->setTeleportPhase(0);
    PL->setField11a(0);
    PL->setConveyorDir(0);

    /* ── tear down the previous level ──────────────────────────────────── */
    LiftObject::purgeAll((Game *)G);
    SlideObject::purgeAll((Game *)G);
    BreakableTile::purgeAll((Game *)G);
    BridgeObject::purgeAll((Game *)G);

    /* REP STOSD, 0x40 dwords: the 256 switch counts. */
    SW->clearCounts();

    while (((Game *)G)->foeCount() != 0)
        Foe::remove((Game *)G, ((Game *)G)->foeId(0));
    while (GAME->bombCount() != 0)
        Sim_RemoveEnemyObject((Game *)G, GAME->bombId(0));

    ((Game *)G)->setBreakableCount(0);
    ((Game *)G)->setFoeCount(0);
    ((Game *)G)->setLiftCount(0);
    ((Game *)G)->setSlideCount(0);
    GAME->setBombCount(0);
    ((Game *)G)->setSwitchMax(0);
    PL->setLastRoll(0);

    PL->setTileBase(M->tileBase());

    /* ── the player start, and the marker-4 cell ───────────────────────── */
    if (Sim_FindTileByTypeMarker(M, 3, PL->homeRef())) {
        /* MOVSX, not MOVZX: the index is formed from the SIGNED bytes. */
        t = SCELL(M, (signed char)PL->homeU(), (signed char)PL->homeV());
        PL->setFacing(t->param());
    }

    /* The return value is DISCARDED: on a level with no marker-4 cell these
     * three bytes keep the previous level's values.  Preserved. */
    Sim_FindTileByTypeMarker(M, 4, PL->markerCellRef());

    PL->setMarker((float)(int)(signed char)PL->markerCellU(),
                    (float)(int)(signed char)PL->markerCellH(),
                    (float)(int)(signed char)PL->markerCellV());

    PL->setCell(PL->homeU(), PL->homeV(), PL->homeH());

    SCELL(M, (signed char)PL->homeU(), (signed char)PL->homeV())->setObjectMarker(1);

    PL->setPos((float)(int)PL->cellU(), (float)(int)PL->heightCell(), (float)(int)PL->cellV());

    PL->setLastMoveDir(0);
    PL->setKind(4);
    PL->setStepDuration(200.0);                /* two dwords: 0, 0x40690000 */

    /* time() through OUR hook, so KAROO_SEED still governs the run. */
    ORIG_SRAND((unsigned int)hooks_GameTime(0));

    /* ── pass 1: clear one dword per cell ──────────────────────────────── */
    h = M->extentV();
    if (h != 0) {
        v = 0;
        do {
            w = M->extentU();
            if (w != 0) {
                u = 0;
                do {
                    CELL(M, u, v)->setSlideTrack(0);
                    u = (unsigned char)(u + 1);
                } while (u < M->extentU());
            }
            v = (unsigned char)(v + 1);
        } while (v < M->extentV());
    }

    /* ── pass 2: the walk ──────────────────────────────────────────────── */
    if (M->extentV() != 0) {
        v = 0;
        do {
            if (M->extentU() == 0)
                goto next_row;
            u = 0;
            do {
                t = CELL(M, u, v);
                s = LevelMap::snapshotOf(t);

                t->setOccupant(0);
                t->setField1f6(0);
                t->setField203(0);
                t->setField20f(0);
                t->setBlastHeight(0);
                t->setField1a1(0);
                t->setBusy(0);
                t->setField202(0);

                if (t->contents() == 1)
                    GAME->setField42252((unsigned short)(GAME->field_42252() + 1));
                if (t->contents() == 7) CEN->item7++;

                if (t->objectMarker() == TILE_KIND_01) CEN->type1++;
                if (t->objectMarker() == TILE_GLUE) CEN->type2++;
                if (t->objectMarker() == TILE_CLIMB) CEN->type10++;
                if (t->objectMarker() == TILE_CONVEYOR) CEN->type15++;

                if (t->objectMarker() == TILE_DESTRUCTIBLE) {
                    unsigned char item;
                    CEN->type17++;
                    t->setBusy(0);
                    item = t->contents();
                    if (s_fx_noshadow)
                        item = 0;      /* the move never happens */
                    if (item != 0) {
                        t->setField202(item);
                        t->setContents(0);
                        if (t->field202() == 1) CEN->shadow1++;
                        if (t->field202() == 7) CEN->shadow7++;
                    }
                }

                /* switch cell */
                if (t->objectMarker() == TILE_SWITCH) {
                    unsigned char param = t->param();
                    if (param == 0) {
                        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_WARN_SWITCH);
                    } else {
                        unsigned char idx = (unsigned char)(param - 1);
                        if (idx > ((Game *)G)->switchMax())
                            ((Game *)G)->setSwitchMax(idx);
                        t->setField1f3(idx);
                        /* The count is re-read for each store, as the
                         * original re-reads it. */
                        SW->setCellU(idx, SW->count(idx), u);
                        SW->setCellV(idx, SW->count(idx), v);
                        SW->setCount(idx, (unsigned char)(SW->count(idx) + 1));
                    }
                    t->setParam(0);
                }

                /* bridges along U, then along V */
                if (t->objectMarker() == TILE_BRIDGE_U) {
                    unsigned char param = t->param();
                    if (param == 0) {
                        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_WARN_XBRIDGE);
                    } else {
                        BridgeObject::spawn((Game *)G, u, v, t->height(),
                                            (unsigned char)(param - 1), 1);
                        t->setParam(0);
                        CEN->bridges++;
                    }
                }
                if (t->objectMarker() == TILE_BRIDGE_V) {
                    unsigned char param = t->param();
                    if (param == 0) {
                        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_WARN_YBRIDGE);
                    } else {
                        BridgeObject::spawn((Game *)G, u, v, t->height(),
                                            (unsigned char)(param - 1), 2);
                        t->setParam(0);
                        CEN->bridges++;
                    }
                }

                if (t->objectMarker() == TILE_CLIMB) {
                    t->setClimbDir(t->param());
                    t->setParam(0);
                }

                /* type 3 becomes type 1, before the tests below see it */
                if (t->objectMarker() == TILE_KIND_03)
                    t->setObjectMarker(1);

                if (t->objectMarker() == TILE_LIFT) {
                    LiftObject::spawn((Game *)G, u, v, t->height(), t->param());
                    t->setParam(0);
                }

                if (t->objectMarker() == TILE_JUMP_PAD) {
                    t->setField1f1(t->param());
                    CEN->type0e++;
                    t->setParam(0);
                }

                /* a cell holding an item gets a random phase */
                if (t->contents() != 0) {
                    long double ph = (long double)(int)crt_rand();
                    ph = ph * (long double)K_TWO_PI;
                    ph = ph * (long double)K_INV_32K;
                    t->setItemPhase((float)ph);
                }

                if (t->objectMarker() == TILE_SLIDE_U || t->objectMarker() == TILE_SLIDE_V) {
                    t->setContents(0);
                    t->setParam(0);
                    SlideObject::spawn((Game *)G, u, v, t->height(),
                                       t->objectMarker());
                    t->setObjectMarker(0);
                }

                if (t->objectMarker() == TILE_BREAKABLE)
                    BreakableTile::spawn((Game *)G, u, v, t->height(), t->param());

                /* teleport pairing */
                if (t->objectMarker() == TILE_TELEPORTER && t->param() != 0) {
                    unsigned char id = t->param();
                    unsigned char v2;

                    /* cleared FIRST, which is the only reason the search
                     * below cannot match this very cell */
                    t->setParam(0);
                    t->setTeleportId(id);

                    if (M->extentV() != 0) {
                        v2 = 0;
                        do {
                            if (M->extentU() != 0) {
                                unsigned char u2 = 0;
                                do {
                                    Tile *t2 = CELL(M, u2, v2);
                                    if (t2->objectMarker() == TILE_TELEPORTER &&
                                        t2->param() == id) {
                                        CEN->teleports++;
                                        t->setTeleportU(u2);
                                        t->setTeleportV(v2);
                                        t2->setTeleportU(u);
                                        t2->setTeleportV(v);
                                        t2->setParam(0);
                                    }
                                    u2 = (unsigned char)(u2 + 1);
                                } while (u2 < M->extentU());
                            }
                            v2 = (unsigned char)(v2 + 1);
                        } while (v2 < M->extentV());
                    }
                }

                /* ── the snapshot: the cell as the file gave it ────────── */
                if (s->contents() == 0x0d) CEN->l2_d++;
                if (s->contents() == 0x08) CEN->l2_8++;
                if (s->contents() == 0x06) CEN->l2_6++;
                if (s->contents() == 0x05) CEN->l2_5++;
                if (s->contents() == 0x0a) CEN->l2_a++;
                if (s->contents() == 0x09) CEN->l2_9++;

                if (s->contents() == 0x4d) {                /* free bomb */
                    /* The count is re-read for every store, as the
                     * original re-reads it. */
                    ((Game *)G)->freeBomb(CEN->freeBombs)->u = u;
                    ((Game *)G)->freeBomb(CEN->freeBombs)->v = v;
                    ((Game *)G)->freeBomb(CEN->freeBombs)->param = s->param();
                    /* Two dword MOVs of the clock in the original; one
                     * double copy here, the same bits. */
                    ((Game *)G)->freeBomb(CEN->freeBombs)->placedAt =
                        *GAME->clock();
                    GameLog_LogMessage(GAME_LOGGER_VA, 3, S_FREEBOMB,
                                       (unsigned int)CEN->freeBombs);
                    CEN->freeBombs++;
                }

                if (s->contents() == 0x64) {                /* timed item */
                    unsigned idx;
                    unsigned char param;

                    /* The count is re-read for every store, as the
                     * original re-reads it. */
                    ((Game *)G)->timedSpawner(CEN->timed)->u = u;
                    ((Game *)G)->timedSpawner(CEN->timed)->v = v;
                    ((Game *)G)->timedSpawner(CEN->timed)->height = s->height();

                    /* +0x14 is the foe cap GameTick compares the foe count
                     * against; the header's "effect" is this same byte. */
                    param = s->param();
                    if (param < 0x64) {
                        ((Game *)G)->timedSpawner(CEN->timed)->maxFoes = 5;
                        param = s->param();
                        ((Game *)G)->timedSpawner(CEN->timed)->interval =
                            (double)(int)((unsigned)param * 1000u);
                    } else {
                        ((Game *)G)->timedSpawner(CEN->timed)->maxFoes =
                            (unsigned char)(param - 0x64);
                        /* Two dwords in the original, 0 and 0x40b38800:
                         * one double, 5000.0. */
                        ((Game *)G)->timedSpawner(CEN->timed)->interval = 5000.0;
                    }

                    /* Staggered: spawner k first fires k seconds late. */
                    idx = CEN->timed;
                    {
                        long double when = (long double)(int)(idx * 1000u);
                        when = when + (long double)*GAME->clock();
                        ((Game *)G)->timedSpawner(idx)->lastSpawn = (double)when;
                    }
                    CEN->timed++;
                }

                /* ── foes ──────────────────────────────────────────────── */
                {
                    int spawned2 = 0;

                    if (s->contents() == 0x02) {
                        unsigned char param = t->param();
                        unsigned char hh;

                        if (param == 0x0b || param == 0x07) CEN->shadow1++;
                        if (t->param() == 0x4d)             CEN->shadow7++;

                        param = t->param();
                        hh    = t->height();
                        if (param == 0x06) {
                            unsigned char bump =
                                (unsigned char)((unsigned char)(u + v) + 0x0a);
                            t->setParam(0);
                            hh = (unsigned char)(hh + bump);
                        }
                        Foe::spawn((Game *)G, u, v, hh, 2, t->param());
                        t->setContents(0);
                        spawned2 = 1;
                    }

                    if (s->contents() == 0x03) {
                        Foe::spawn((Game *)G, u, v, t->height(), 3, t->param());
                        t->setContents(0);
                        t->setParam(0);
                    } else if (!spawned2) {
                        /* every cell EXCEPT one that just spawned a type-2
                         * foe loses its parameter byte here */
                        t->setParam(0);
                    }
                }

                t->setLiftLiveHeight((float)(int)(unsigned)t->height());

                u = (unsigned char)(u + 1);
            } while (u < M->extentU());

next_row:
            v = (unsigned char)(v + 1);
        } while (v < M->extentV());
    }

    /* ── totals and the rest of the reset ──────────────────────────────── */
    PL->setSlideSlot(0xff);
    GAME->setField173584(1);

    CEN->total = (unsigned short)(CEN->l2_9 + CEN->l2_a +
                                     CEN->shadow1 + CEN->l2_d +
                                     CEN->l2_5 + CEN->shadow7 +
                                     CEN->l2_8 + CEN->item7 +
                                     CEN->l2_6 + GAME->field_42252());

    PL->setMoveState(0);
    PL->setLastContact(*((Game *)G)->clock());

    PL->clearEffects();

    PL->setTickStep(((Game *)G)->tickStep());
    PL->setEffect8Active(0);
    PL->setEffectBActive(0);
    PL->setEffectAActive(0);
    PL->setEffectCActive(0);
    PL->setSwitchSlot(0xff);
    PL->setFieldD3(0);
    PL->setOnLift(0);
    PL->setBombDropRequest(0);
    PL->setCompletionNumerator(0);
    ((Game *)G)->setTimeLimit(M->fileTimeLimit());
    PL->setClock(((Game *)G)->clock());

    if (((Game *)G)->restartCount() == 0) {
        ((Game *)G)->setFoesKilled(0);
        PL->setItemsCollected(0);
        ((Game *)G)->setItemTotal(CEN->total);

        if (((Game *)G)->scriptPlayer()->loaded() == 0) {
            GAME->setCameraEye(0, (float)(int)(signed char)PL->homeU());
            GAME->setCameraEye(1, (float)(int)(signed char)PL->homeH());
            GAME->setCameraEye(2, (float)(int)(signed char)PL->homeV());
        }

        if (((Game *)G)->cdThemes()->validateTrackLengths() == 0 &&
            GAME->field_0c() == 0 &&
            ((Game *)G)->levelIndex() > 4) {
            unsigned char cu = PL->homeU();
            unsigned char cv = PL->homeV();
            if (Sim_FindNearestFlaggedTileInRadius((Game *)G, &cu, &cv, 0x14)) {
                CELL(M, cu, cv)->setContents(0);
                GameLog_LogMessage(GAME_LOGGER_VA, 3, S_CD_MISSING,
                                   (unsigned int)cu, (unsigned int)cv);
            }
        }
    }

    ((Game *)G)->setTimeElapsed(0);
    ((Game *)G)->setField170a65(0);

    SCELL(M, PL->cellU(), PL->cellV())->setField1a1(0);

    PL->setGlides(0);
    PL->setFieldE8(0);
    PL->setEffectDActive(0);
    PL->setFalling(0);
    PL->setIdleStarted(0);
    PL->setGliding(0);
    PL->setHeld(0);
    PL->setMoveDir(0);
    PL->setPendingMove(0);
    PL->setField12e(0);
    PL->setAnim(0);
    PL->setDying(0);
    PL->setIdleDuration(500.0);                /* two dwords: 0, 0x407f4000 */
    GAME->setField13cc90(0);
    GAME->setField13cca8(0);

    GameLog_LogMessage(GAME_LOGGER_VA, 1, S_CRYSTALS,
                       (unsigned int)GAME->field_42252(),
                       ((Game *)G)->gemsRequired());

    if ((int)((unsigned int)PL->gemsCollected() + (unsigned int)GAME->field_42252()) <
        ((Game *)G)->gemsRequired())
        GameLog_LogMessage(GAME_LOGGER_VA, 3, S_WARN_CRYSTALS);

    /* ONE argument -- see the header. */
    if (((Game *)G)->extraObjects()->openFile(
                                 GAME->levelName()) == 0) {
        ((Game *)G)->extraObjects()->setLoaded(0);
        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_LEO_FAILED,
                           GAME->levelName());
    } else {
        ((Game *)G)->extraObjects()->setLoaded(1);
        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_LEO_LOADED,
                           GAME->levelName());
    }

    /* Config +0x20a48 (Game+0x2ab576), one dword: 60.0f. */
    GAME->config()->setField20a48Bits(0x42700000);

    s_calls++;
    if (s_diag)
        log_write("levelsetup: DIAG call #%u map=%ux%u crystals=%u total=%u "
                  "bridges=%u teleports=%u lifts=%u slides=%u breakables=%u "
                  "foes=%u freebombs=%u timed=%u switchmax=%u\n",
                  s_calls, (unsigned)M->extentU(), (unsigned)M->extentV(),
                  (unsigned)GAME->field_42252(), (unsigned)CEN->total,
                  (unsigned)CEN->bridges, (unsigned)CEN->teleports,
                  (unsigned)((Game *)G)->liftCount(), (unsigned)((Game *)G)->slideCount(),
                  (unsigned)((Game *)G)->breakableCount(), (unsigned)((Game *)G)->foeCount(),
                  (unsigned)CEN->freeBombs, (unsigned)CEN->timed,
                  (unsigned)((Game *)G)->switchMax());

    /* XOR AL,AL */
    return 0;
}
