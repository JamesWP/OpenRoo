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
 * `objectremove.cpp`, `gamereset.cpp` and `tileeffects.cpp` recorded for
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
 * `rand()` is reimplemented rather than called, exactly as `tileeffects.cpp`
 * does, and over the SAME global seed at 0x00469f38 — a private seed would
 * desynchronise every other `rand()` caller.
 *
 * ─── Tile addressing ─────────────────────────────────────────────────────
 *
 * The tilemap is the one `tilequery.cpp` and `entitymove.cpp` document:
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
 * argument is a stale register.  `leo.cpp`'s own signature agrees.
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
 * **The timed-item branch splits on 100.**  `d`-cells with a parameter below
 * 100 get effect 5 and a duration of `param * 1000` ms; at or above 100 they
 * get effect `param - 100` and the fixed duration 0x40b38800 (5000.0).  The
 * decompile renders the subtraction as `+ 0x9c`, a byte add of -100; the
 * listing is `SUB AL,0x64`.
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

/* ─── Game field offsets ─────────────────────────────────────────────────── */

#define G_MODE_FLAG        0x14        /* dword; 0 -> state 2, else state 1  */
#define G_STATE_BYTE       0x17565d
#define G_CAMERA_A         0x175393    /* dword 0                            */
#define G_CAMERA_B         0x175397    /* dword 0x40240000                   */
#define G_SCRIPT_READER    0x195735
#define G_EXTRA_OBJECTS    0x48b98
#define G_EXTRA_LOADED     0x48b9c
#define G_UNK_42254        0x42254
#define G_UNK_42258        0x42258

#define G_PLAYER_POS       0x2ab580    /* three floats                       */
#define G_SOUND_LISTENER   0x13cc94    /* three floats, {0, 1000, 0}         */
#define G_SOUND_E8         0x13cc90
#define G_SOUND_100        0x13cca8

#define G_COUNT_CRYSTAL    0x42252     /* WORD                               */
#define G_LIFT_COUNT       0x173b19
#define G_LIFT_COUNT2      0x173b1a
#define G_SLIDE_COUNT      0x173718
#define G_BREAK_COUNT      0x173e3e
#define G_FOE_COUNT        0x174fd4
#define G_FOE_IDS          0x174fd5
#define G_ENEMY_COUNT      0x17460f
#define G_ENEMY_IDS        0x174610
#define G_SWITCH_MAX       0x48b12
#define G_CLEAR_BLOCK      0x170543    /* 0x40 dwords, also the switch counts */
#define G_SWITCH_CELLS     0x140543    /* 3 bytes per entry, 0x100 per switch */

#define G_START_U          0x17531c    /* the marker-3 lookup's 3 bytes      */
#define G_START_V          0x17531d
#define G_START_H          0x17531e
#define G_MARK4            0x17530b    /* the marker-4 lookup's 3 bytes      */
#define G_MARK4_V          0x17530c
#define G_MARK4_H          0x17530d

#define G_TILES            0x2ab58d    /* the map reader sub-object          */
#define G_MAP_H            0x2ab727    /* tiles+0x19a, the v extent          */
#define G_MAP_W            0x2ab728    /* tiles+0x19b, the u extent          */

/* Per-tile field bases, Game-relative; index with TIDX(u,v). */
#define T_HEIGHT           0x2ab729    /* tiles+0x19c                        */
#define T_TYPE             0x2ab72a    /* tiles+0x19d                        */
#define T_PARAM            0x2ab72b    /* tiles+0x19e                        */
#define T_ITEM             0x2ab72c    /* tiles+0x19f                        */
#define T_FLAG_72D         0x2ab72d
#define T_DW_72E           0x2ab72e
#define T_B_732            0x2ab732
#define T_F_733            0x2ab733    /* float, the cell height             */
#define T_DW_749           0x2ab749
#define T_PAIR_ID          0x2ab77a
#define T_PAIR_U           0x2ab77b
#define T_PAIR_V           0x2ab77c
#define T_B_77E            0x2ab77e
#define T_B_77F            0x2ab77f
#define T_SWITCH_IDX       0x2ab780
#define T_DW_783           0x2ab783
#define T_ITEM_SHADOW      0x2ab78f
#define T_DW_790           0x2ab790
#define T_DW_79C           0x2ab79c
#define T_F_7A0            0x2ab7a0    /* float, rand() * 2pi / 32768        */
#define T_DW_7A4           0x2ab7a4

/* The second per-tile layer, same stride. */
#define T2_B_1819          0x3e1819
#define T2_B_181B          0x3e181b
#define T2_TYPE            0x3e181c

/* Counter words reset by ResetLevelObjectCounters. */
#define C_TELEPORTS        0x421df
#define C_TYPE17           0x421e1
#define C_TYPE1            0x421e3
/*      0x421e5 is NEVER reset -- see the header */
#define C_TOTAL            0x421e7
#define C_BRIDGES          0x421e9
#define C_UNUSED_EB        0x421eb
#define C_TYPE2            0x421ed
#define C_TYPE10           0x421ef
#define C_TYPE15           0x421f1
#define C_L2_8             0x421f3
#define C_L2_6             0x421f5
#define C_ITEM7            0x421f7
#define C_L2_D             0x421f9
#define C_SHADOW1          0x421fb
#define C_SHADOW7          0x421fd
#define C_L2_5             0x421ff
#define C_L2_A             0x42201
#define C_L2_9             0x42203
#define C_FREEBOMBS        0x42205
#define C_TIMED            0x42207
#define C_TYPE0E           0x42209

#define G_FREEBOMBS        0x2173d     /* 0xb per entry                      */
#define G_TIMED            0x2023d     /* 0x15 per entry                     */
#define G_CLOCK            0x170a54    /* the game clock, a double           */

#define G_GAMEFILE_FLAG    0x4220b
#define G_LEVEL_NO         0x173583
#define G_LEVEL_NAME       0x173483
#define G_SCRIPT_COUNT     0x1960e6
#define G_CD_OBJ           0x2223f
#define G_REQUIRED         0x2ab723
#define G_CARRIED          0x175406

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

typedef void (__attribute__((thiscall)) *ll_clear_fn)(void *self);
#define ORIG_LIST_CLEAR    ((ll_clear_fn)0x004254f0)

typedef void (__attribute__((thiscall)) *rel_script_fn)(void *self);
#define ORIG_RELEASE_SCRIPT ((rel_script_fn)0x0041e840)

typedef int (__attribute__((thiscall)) *cd_check_fn)(void *self);
#define ORIG_CD_CHECK      ((cd_check_fn)0x00403420)

typedef int (__attribute__((thiscall)) *noop_fn)(void *self);
#define ORIG_NOOP_440450   ((noop_fn)0x00440450)

typedef void (__cdecl *srand_fn)(unsigned int);
#define ORIG_SRAND         ((srand_fn)0x00451672)

/* time(), but through OUR hook -- see the header. */
extern "C" __declspec(dllexport) int __cdecl hooks_GameTime(int *out);

/* The CRT rand() at 0x0045167c, over the game's shared seed. */
#define CRT_RAND_SEED  (*(unsigned int *)0x00469f38)

static inline unsigned int crt_rand(void)
{
    CRT_RAND_SEED = CRT_RAND_SEED * 0x343FDu + 0x269EC3u;
    return (CRT_RAND_SEED >> 16) & 0x7FFF;
}

/* ─── Already ours -- called as exports, the originals carry UD2 stubs ───── */

extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Leo_ReleaseExtraObjectSoundBuffers(void *self);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Leo_OpenExtraObjectsFile(void *self, const char *name);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeLiftObjects(void *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeSlideObjects(void *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeBreakableObjects(void *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeBridgeObjects(void *self);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveFoeObject(void *self, unsigned int idArg);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveEnemyObject(void *self, unsigned int idArg);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnFoeObject(void *self, unsigned int uArg, unsigned int vArg,
                   unsigned int hArg, unsigned int kindArg,
                   unsigned int typeArg);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnLiftObject(void *self, unsigned int uArg, unsigned int vArg,
                    unsigned int heightArg, unsigned int param4);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnBreakableObject(void *self, unsigned int uArg, unsigned int vArg,
                         unsigned int heightArg, unsigned int param4);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnSlideObject(void *self, unsigned int uArg, unsigned int vArg,
                     unsigned int heightArg, unsigned int kindArg);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_SpawnBridgeObject(void *self, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int slotArg,
                      unsigned int axisArg);

extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_FindNearestFlaggedTileInRadius(void *self, unsigned char *pu,
                                   unsigned char *pv, unsigned char radius);

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

/* Tile index.  `(v + u*100) * 0x7f`, the addressing tilequery.cpp and
 * entitymove.cpp document.  The control transposes the two terms HERE and
 * nowhere else, so every consumer moves together. */
static inline int TIDX(unsigned u, unsigned v)
{
    if (s_fx_setupflip)
        return (int)((u + v * 100) * 0x7f);
    return (int)((v + u * 100) * 0x7f);
}

/* The same index formed from SIGNED bytes.  Three sites use MOVSX rather
 * than a zero-extended loop counter; on a well-formed level the values are
 * small positives and the two agree, but the sign extension is transcribed
 * because the bytes come from level data. */
static inline int SIDX(int u, int v)
{
    if (s_fx_setupflip)
        return (u + v * 100) * 0x7f;
    return (v + u * 100) * 0x7f;
}

/* ═══ 0x0041ceb0 -- Game::ResetLevelObjectCounters ═════════════════════════
 *
 * Transcribed in the original's store ORDER, which is not ascending, and
 * with 0x421e5 left out exactly as the original leaves it out. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ResetLevelObjectCounters(void *self)
{
    unsigned char *G = (unsigned char *)self;

    W(G, 0x421df) = 0;
    W(G, 0x421e1) = 0;
    W(G, 0x421e3) = 0;
    W(G, 0x421e7) = 0;
    W(G, 0x421e9) = 0;
    W(G, 0x421eb) = 0;
    W(G, 0x421ed) = 0;
    W(G, 0x421ef) = 0;
    W(G, 0x421f1) = 0;
    W(G, 0x421f3) = 0;
    W(G, 0x421f5) = 0;
    W(G, 0x421f9) = 0;
    W(G, 0x42201) = 0;
    W(G, 0x421ff) = 0;
    W(G, 0x421fb) = 0;
    W(G, 0x421fd) = 0;
    W(G, 0x42209) = 0;
    W(G, 0x421f7) = 0;
    W(G, 0x42203) = 0;
    W(G, 0x42205) = 0;
    W(G, 0x42207) = 0;
}

/* ═══ 0x0041f430 -- Game::FindTileByTypeMarker ═════════════════════════════
 *
 * `out` is written ONLY on success; the caller depends on that (see header). */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_FindTileByTypeMarker(void *tiles, unsigned int markerArg,
                         unsigned char *out)
{
    unsigned char *T = (unsigned char *)tiles;
    unsigned char marker = (unsigned char)markerArg;
    unsigned char v, u;

    if (T[0x19a] == 0)
        return 0;

    v = 0;
    do {
        u = 0;
        if (T[0x19b] != 0) {
            do {
                int off = TIDX(u, v);
                if (T[off + 0x19d] == marker) {
                    out[0] = u;
                    out[1] = v;
                    out[2] = T[off + 0x19c];
                    return 1;
                }
                u = (unsigned char)(u + 1);
            } while (u < T[0x19b]);
        }
        v = (unsigned char)(v + 1);
    } while (v < T[0x19a]);

    return 0;
}

/* ═══ 0x00416420 -- Game::SetupLevelObjects ════════════════════════════════ */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_SetupLevelObjects(void *self)
{
    unsigned char *G = (unsigned char *)self;
    unsigned char v, u;
    unsigned char w, h;
    int off;

    fx_init();

    /* ── run state ─────────────────────────────────────────────────────── */
    B(G, G_STATE_BYTE) = (DW(G, G_MODE_FLAG) == 0) ? 2 : 1;
    DW(G, G_CAMERA_A) = 0;
    DW(G, G_CAMERA_B) = 0x40240000;

    GameLog_LogMessage(GAME_LOGGER_VA, 2, S_INIT_STARTED);

    DW(G, G_UNK_42254) = 0;
    ORIG_RELEASE_SCRIPT(G + G_SCRIPT_READER);
    Leo_ReleaseExtraObjectSoundBuffers(G + G_EXTRA_OBJECTS);
    ORIG_NOOP_440450(G + G_UNK_42258);

    /* The player position triple is zeroed, and the listener and the two
     * position globals are set from it; the listener's y is 1000.0f. */
    DW(G, G_PLAYER_POS + 0) = 0;
    DW(G, G_PLAYER_POS + 4) = 0;
    DW(G, G_PLAYER_POS + 8) = 0;

    DW(G, G_SOUND_LISTENER + 0) = 0;
    DW(G, G_SOUND_LISTENER + 4) = 0x447a0000;   /* 1000.0f */
    DW(G, G_SOUND_LISTENER + 8) = 0;

    ((u32_ua *)GBL_LISTENER)[0] = 0;
    ((u32_ua *)GBL_LISTENER)[1] = 0x447a0000;
    ((u32_ua *)GBL_LISTENER)[2] = 0;

    ((u32_ua *)GBL_POS)[0] = DW(G, G_PLAYER_POS + 0);
    ((u32_ua *)GBL_POS)[1] = DW(G, G_PLAYER_POS + 4);
    GBL_C4B8 = 0;
    ((u32_ua *)GBL_POS)[2] = DW(G, G_PLAYER_POS + 8);
    GBL_C4BC = 0;

    Sim_ResetLevelObjectCounters(G);

    W(G, G_COUNT_CRYSTAL) = 0;
    DW(G, G_LIFT_COUNT2)  = 0;
    DW(G, 0x17520d)       = 0;
    B (G, 0x1752c8)       = 0;
    DW(G, 0x1752e3)       = 0;
    DW(G, 0x175221)       = 0;

    /* ── tear down the previous level ──────────────────────────────────── */
    Sim_PurgeLiftObjects(G);
    Sim_PurgeSlideObjects(G);
    Sim_PurgeBreakableObjects(G);
    Sim_PurgeBridgeObjects(G);

    {
        u32_ua *p = (u32_ua *)(G + G_CLEAR_BLOCK);
        int n;
        for (n = 0x40; n != 0; n--)
            *p++ = 0;
    }

    while (B(G, G_FOE_COUNT) != 0)
        Sim_RemoveFoeObject(G, B(G, G_FOE_IDS));
    while (B(G, G_ENEMY_COUNT) != 0)
        Sim_RemoveEnemyObject(G, B(G, G_ENEMY_IDS));

    B(G, G_BREAK_COUNT) = 0;
    B(G, G_FOE_COUNT)   = 0;
    B(G, G_LIFT_COUNT)  = 0;
    B(G, G_SLIDE_COUNT) = 0;
    B(G, G_ENEMY_COUNT) = 0;
    B(G, G_SWITCH_MAX)  = 0;
    B(G, 0x1753f9)      = 0;

    DW(G, 0x1751fd) = (unsigned int)(G + G_TILES);

    /* ── the player start, and the marker-4 cell ───────────────────────── */
    if (Sim_FindTileByTypeMarker(G + G_TILES, 3, G + G_START_U)) {
        /* MOVSX, not MOVZX: the index is formed from the SIGNED bytes. */
        off = SIDX(SB(G, G_START_U), SB(G, G_START_V));
        B(G, 0x1751dd) = B(G, T_PARAM + off);
    }

    /* The return value is DISCARDED: on a level with no marker-4 cell these
     * three bytes keep the previous level's values.  Preserved. */
    Sim_FindTileByTypeMarker(G + G_TILES, 4, G + G_MARK4);

    F(G, 0x1753d7) = (float)(int)SB(G, G_MARK4);
    F(G, 0x1753db) = (float)(int)SB(G, G_MARK4_H);
    F(G, 0x1753df) = (float)(int)SB(G, G_MARK4_V);

    B(G, 0x1751fa) = B(G, G_START_U);
    B(G, 0x1751fb) = B(G, G_START_V);
    B(G, 0x1751fc) = B(G, G_START_H);

    off = SIDX(SB(G, G_START_U), SB(G, G_START_V));
    B(G, T_TYPE + off) = 1;

    F(G, 0x1751ee) = (float)(int)SB(G, 0x1751fa);
    F(G, 0x1751f2) = (float)(int)SB(G, 0x1751fc);
    F(G, 0x1751f6) = (float)(int)SB(G, 0x1751fb);

    B (G, 0x1752d1) = 0;
    B (G, 0x17531b) = 4;
    DW(G, 0x17522f) = 0;
    DW(G, 0x175233) = 0x40690000;

    /* time() through OUR hook, so KAROO_SEED still governs the run. */
    ORIG_SRAND((unsigned int)hooks_GameTime(0));

    /* ── pass 1: clear one dword per cell ──────────────────────────────── */
    h = B(G, G_MAP_H);
    if (h != 0) {
        v = 0;
        do {
            w = B(G, G_MAP_W);
            if (w != 0) {
                u = 0;
                do {
                    DW(G, T_DW_749 + TIDX(u, v)) = 0;
                    u = (unsigned char)(u + 1);
                } while (u < B(G, G_MAP_W));
            }
            v = (unsigned char)(v + 1);
        } while (v < B(G, G_MAP_H));
    }

    /* ── pass 2: the walk ──────────────────────────────────────────────── */
    if (B(G, G_MAP_H) != 0) {
        v = 0;
        do {
            if (B(G, G_MAP_W) == 0)
                goto next_row;
            u = 0;
            do {
                off = TIDX(u, v);

                B (G, T_B_732    + off) = 0;
                DW(G, T_DW_783   + off) = 0;
                DW(G, T_DW_790   + off) = 0;
                DW(G, T_DW_79C   + off) = 0;
                B (G, T_FLAG_72D + off) = 0;
                DW(G, T_DW_72E   + off) = 0;
                DW(G, T_DW_7A4   + off) = 0;
                B (G, T_ITEM_SHADOW + off) = 0;

                if (B(G, T_ITEM + off) == 1) W(G, G_COUNT_CRYSTAL)++;
                if (B(G, T_ITEM + off) == 7) W(G, C_ITEM7)++;

                if (B(G, T_TYPE + off) == 0x01) W(G, C_TYPE1)++;
                if (B(G, T_TYPE + off) == 0x02) W(G, C_TYPE2)++;
                if (B(G, T_TYPE + off) == 0x10) W(G, C_TYPE10)++;
                if (B(G, T_TYPE + off) == 0x15) W(G, C_TYPE15)++;

                if (B(G, T_TYPE + off) == 0x17) {
                    unsigned char item;
                    W(G, C_TYPE17)++;
                    DW(G, T_DW_7A4 + off) = 0;
                    item = B(G, T_ITEM + off);
                    if (s_fx_noshadow)
                        item = 0;      /* the move never happens */
                    if (item != 0) {
                        B(G, T_ITEM_SHADOW + off) = item;
                        B(G, T_ITEM + off) = 0;
                        if (B(G, T_ITEM_SHADOW + off) == 1) W(G, C_SHADOW1)++;
                        if (B(G, T_ITEM_SHADOW + off) == 7) W(G, C_SHADOW7)++;
                    }
                }

                /* switch cell */
                if (B(G, T_TYPE + off) == 0x11) {
                    unsigned char param = B(G, T_PARAM + off);
                    if (param == 0) {
                        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_WARN_SWITCH);
                    } else {
                        unsigned char idx = (unsigned char)(param - 1);
                        unsigned char cnt;
                        if (idx > B(G, G_SWITCH_MAX))
                            B(G, G_SWITCH_MAX) = idx;
                        B(G, T_SWITCH_IDX + off) = idx;
                        cnt = B(G, G_CLEAR_BLOCK + idx);
                        B(G, G_SWITCH_CELLS + 0 + ((unsigned)cnt + (unsigned)idx * 0x100) * 3) = u;
                        cnt = B(G, G_CLEAR_BLOCK + idx);
                        B(G, G_SWITCH_CELLS + 1 + ((unsigned)cnt + (unsigned)idx * 0x100) * 3) = v;
                        B(G, G_CLEAR_BLOCK + idx) = (unsigned char)(B(G, G_CLEAR_BLOCK + idx) + 1);
                    }
                    B(G, T_PARAM + off) = 0;
                }

                /* bridges along U, then along V */
                if (B(G, T_TYPE + off) == 0x12) {
                    unsigned char param = B(G, T_PARAM + off);
                    if (param == 0) {
                        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_WARN_XBRIDGE);
                    } else {
                        Sim_SpawnBridgeObject(G, u, v, B(G, T_HEIGHT + off),
                                              (unsigned char)(param - 1), 1);
                        B(G, T_PARAM + off) = 0;
                        W(G, C_BRIDGES)++;
                    }
                }
                if (B(G, T_TYPE + off) == 0x13) {
                    unsigned char param = B(G, T_PARAM + off);
                    if (param == 0) {
                        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_WARN_YBRIDGE);
                    } else {
                        Sim_SpawnBridgeObject(G, u, v, B(G, T_HEIGHT + off),
                                              (unsigned char)(param - 1), 2);
                        B(G, T_PARAM + off) = 0;
                        W(G, C_BRIDGES)++;
                    }
                }

                if (B(G, T_TYPE + off) == 0x10) {
                    B(G, T_B_77F + off) = B(G, T_PARAM + off);
                    B(G, T_PARAM + off) = 0;
                }

                /* type 3 becomes type 1, before the tests below see it */
                if (B(G, T_TYPE + off) == 0x03)
                    B(G, T_TYPE + off) = 1;

                if (B(G, T_TYPE + off) == 0x09) {
                    Sim_SpawnLiftObject(G, u, v, B(G, T_HEIGHT + off),
                                        B(G, T_PARAM + off));
                    B(G, T_PARAM + off) = 0;
                }

                if (B(G, T_TYPE + off) == 0x0e) {
                    B(G, T_B_77E + off) = B(G, T_PARAM + off);
                    W(G, C_TYPE0E)++;
                    B(G, T_PARAM + off) = 0;
                }

                /* a cell holding an item gets a random phase */
                if (B(G, T_ITEM + off) != 0) {
                    long double t = (long double)(int)crt_rand();
                    t = t * (long double)K_TWO_PI;
                    t = t * (long double)K_INV_32K;
                    F(G, T_F_7A0 + off) = (float)t;
                }

                if (B(G, T_TYPE + off) == 0x0a || B(G, T_TYPE + off) == 0x0b) {
                    B(G, T_ITEM  + off) = 0;
                    B(G, T_PARAM + off) = 0;
                    Sim_SpawnSlideObject(G, u, v, B(G, T_HEIGHT + off),
                                         B(G, T_TYPE + off));
                    B(G, T_TYPE + off) = 0;
                }

                if (B(G, T_TYPE + off) == 0x0d)
                    Sim_SpawnBreakableObject(G, u, v, B(G, T_HEIGHT + off),
                                             B(G, T_PARAM + off));

                /* teleport pairing */
                if (B(G, T_TYPE + off) == 0x0f && B(G, T_PARAM + off) != 0) {
                    unsigned char id = B(G, T_PARAM + off);
                    unsigned char v2;

                    /* cleared FIRST, which is the only reason the search
                     * below cannot match this very cell */
                    B(G, T_PARAM   + off) = 0;
                    B(G, T_PAIR_ID + off) = id;

                    if (B(G, G_MAP_H) != 0) {
                        v2 = 0;
                        do {
                            if (B(G, G_MAP_W) != 0) {
                                unsigned char u2 = 0;
                                do {
                                    int o2 = TIDX(u2, v2);
                                    if (B(G, T_TYPE + o2) == 0x0f &&
                                        B(G, T_PARAM + o2) == id) {
                                        W(G, C_TELEPORTS)++;
                                        B(G, T_PAIR_U + off) = u2;
                                        B(G, T_PAIR_V + off) = v2;
                                        B(G, T_PAIR_U + o2)  = u;
                                        B(G, T_PAIR_V + o2)  = v;
                                        B(G, T_PARAM  + o2)  = 0;
                                    }
                                    u2 = (unsigned char)(u2 + 1);
                                } while (u2 < B(G, G_MAP_W));
                            }
                            v2 = (unsigned char)(v2 + 1);
                        } while (v2 < B(G, G_MAP_H));
                    }
                }

                /* ── the second tile layer ─────────────────────────────── */
                if (B(G, T2_TYPE + off) == 0x0d) W(G, C_L2_D)++;
                if (B(G, T2_TYPE + off) == 0x08) W(G, C_L2_8)++;
                if (B(G, T2_TYPE + off) == 0x06) W(G, C_L2_6)++;
                if (B(G, T2_TYPE + off) == 0x05) W(G, C_L2_5)++;
                if (B(G, T2_TYPE + off) == 0x0a) W(G, C_L2_A)++;
                if (B(G, T2_TYPE + off) == 0x09) W(G, C_L2_9)++;

                if (B(G, T2_TYPE + off) == 0x4d) {          /* free bomb */
                    unsigned idx = W(G, C_FREEBOMBS);
                    B (G, G_FREEBOMBS + 0 + idx * 0xb) = u;
                    idx = W(G, C_FREEBOMBS);
                    B (G, G_FREEBOMBS + 1 + idx * 0xb) = v;
                    idx = W(G, C_FREEBOMBS);
                    B (G, G_FREEBOMBS + 2 + idx * 0xb) = B(G, T2_B_181B + off);
                    idx = W(G, C_FREEBOMBS);
                    DW(G, G_FREEBOMBS + 3 + idx * 0xb) = DW(G, G_CLOCK + 0);
                    DW(G, G_FREEBOMBS + 7 + idx * 0xb) = DW(G, G_CLOCK + 4);
                    GameLog_LogMessage(GAME_LOGGER_VA, 3, S_FREEBOMB,
                                       (unsigned int)W(G, C_FREEBOMBS));
                    W(G, C_FREEBOMBS)++;
                }

                if (B(G, T2_TYPE + off) == 0x64) {          /* timed item */
                    unsigned idx;
                    unsigned char param;

                    idx = W(G, C_TIMED);
                    B(G, G_TIMED + 0 + idx * 0x15) = u;
                    idx = W(G, C_TIMED);
                    B(G, G_TIMED + 1 + idx * 0x15) = v;
                    idx = W(G, C_TIMED);
                    B(G, G_TIMED + 2 + idx * 0x15) = B(G, T2_B_1819 + off);

                    param = B(G, T2_B_181B + off);
                    if (param < 0x64) {
                        idx = W(G, C_TIMED);
                        B(G, G_TIMED + 0x14 + idx * 0x15) = 5;
                        param = B(G, T2_B_181B + off);
                        idx = W(G, C_TIMED);
                        D(G, G_TIMED + 0x0c + idx * 0x15) =
                            (double)(int)((unsigned)param * 1000u);
                    } else {
                        idx = W(G, C_TIMED);
                        B (G, G_TIMED + 0x14 + idx * 0x15) =
                            (unsigned char)(param - 0x64);
                        idx = W(G, C_TIMED);
                        DW(G, G_TIMED + 0x0c + idx * 0x15) = 0;
                        DW(G, G_TIMED + 0x10 + idx * 0x15) = 0x40b38800;
                    }

                    idx = W(G, C_TIMED);
                    {
                        long double t = (long double)(int)(idx * 1000u);
                        t = t + (long double)D(G, G_CLOCK);
                        D(G, G_TIMED + 0x03 + idx * 0x15) = (double)t;
                    }
                    W(G, C_TIMED)++;
                }

                /* ── foes ──────────────────────────────────────────────── */
                {
                    int spawned2 = 0;

                    if (B(G, T2_TYPE + off) == 0x02) {
                        unsigned char param = B(G, T_PARAM + off);
                        unsigned char hh;

                        if (param == 0x0b || param == 0x07) W(G, C_SHADOW1)++;
                        if (B(G, T_PARAM + off) == 0x4d)    W(G, C_SHADOW7)++;

                        param = B(G, T_PARAM + off);
                        hh    = B(G, T_HEIGHT + off);
                        if (param == 0x06) {
                            unsigned char bump =
                                (unsigned char)((unsigned char)(u + v) + 0x0a);
                            B(G, T_PARAM + off) = 0;
                            hh = (unsigned char)(hh + bump);
                        }
                        Sim_SpawnFoeObject(G, u, v, hh, 2, B(G, T_PARAM + off));
                        B(G, T_ITEM + off) = 0;
                        spawned2 = 1;
                    }

                    if (B(G, T2_TYPE + off) == 0x03) {
                        Sim_SpawnFoeObject(G, u, v, B(G, T_HEIGHT + off), 3,
                                           B(G, T_PARAM + off));
                        B(G, T_ITEM  + off) = 0;
                        B(G, T_PARAM + off) = 0;
                    } else if (!spawned2) {
                        /* every cell EXCEPT one that just spawned a type-2
                         * foe loses its parameter byte here */
                        B(G, T_PARAM + off) = 0;
                    }
                }

                F(G, T_F_733 + off) = (float)(int)(unsigned)B(G, T_HEIGHT + off);

                u = (unsigned char)(u + 1);
            } while (u < B(G, G_MAP_W));

next_row:
            v = (unsigned char)(v + 1);
        } while (v < B(G, G_MAP_H));
    }

    /* ── totals and the rest of the reset ──────────────────────────────── */
    B (G, 0x1752e7) = 0xff;
    DW(G, 0x173584) = 1;

    W(G, C_TOTAL) = (unsigned short)(W(G, C_L2_9) + W(G, C_L2_A) +
                                     W(G, C_SHADOW1) + W(G, C_L2_D) +
                                     W(G, C_L2_5) + W(G, C_SHADOW7) +
                                     W(G, C_L2_8) + W(G, C_ITEM7) +
                                     W(G, C_L2_6) + W(G, G_COUNT_CRYSTAL));

    B (G, 0x1752e8) = 0;
    DW(G, 0x1752a5) = DW(G, G_CLOCK + 0);
    DW(G, 0x1752a9) = DW(G, G_CLOCK + 4);

    ORIG_LIST_CLEAR(G + 0x1753e5);

    DW(G, 0x1751d9) = (unsigned int)(G + 0x170a5c);
    DW(G, 0x1753af) = 0;
    DW(G, 0x1753a3) = 0;
    DW(G, 0x1753d3) = 0;
    DW(G, 0x1753c7) = 0;
    B (G, 0x1752a0) = 0xff;
    DW(G, 0x17529c) = 0;
    DW(G, 0x175264) = 0;
    DW(G, 0x1752ad) = 0;
    DW(G, 0x1752a1) = 0;
    DW(G, 0x2ab591) = DW(G, 0x2ab71f);
    DW(G, 0x1751d5) = (unsigned int)(G + G_CLOCK);

    if (B(G, G_GAMEFILE_FLAG) == 0) {
        B(G, 0x4224d)   = 0;
        W(G, 0x1753e3)  = 0;
        W(G, 0x42250)   = W(G, C_TOTAL);

        if (DW(G, G_SCRIPT_COUNT) == 0) {
            F(G, G_PLAYER_POS + 0) = (float)(int)SB(G, G_START_U);
            F(G, G_PLAYER_POS + 4) = (float)(int)SB(G, G_START_H);
            F(G, G_PLAYER_POS + 8) = (float)(int)SB(G, G_START_V);
        }

        if (ORIG_CD_CHECK(G + G_CD_OBJ) == 0 &&
            DW(G, 0xc) == 0 &&
            B(G, G_LEVEL_NO) > 4) {
            unsigned char cu = B(G, G_START_U);
            unsigned char cv = B(G, G_START_V);
            if (Sim_FindNearestFlaggedTileInRadius(G, &cu, &cv, 0x14)) {
                B(G, T_ITEM + TIDX(cu, cv)) = 0;
                GameLog_LogMessage(GAME_LOGGER_VA, 3, S_CD_MISSING,
                                   (unsigned int)cu, (unsigned int)cv);
            }
        }
    }

    DW(G, 0x2ab595) = 0;
    DW(G, 0x170a65) = 0;

    off = SIDX(SB(G, 0x1751fa), SB(G, 0x1751fb));
    DW(G, T_DW_72E + off) = 0;

    B (G, 0x1752b2) = 0;
    B (G, 0x1752b1) = 0;
    DW(G, 0x1753bb) = 0;
    DW(G, 0x1752e9) = 0;
    DW(G, 0x175237) = 0;
    DW(G, 0x1752b3) = 0;
    DW(G, 0x1752b8) = 0;
    DW(G, 0x175317) = 0;
    B (G, 0x17530e) = 0;
    DW(G, 0x1752f7) = 0;
    B (G, 0x175263) = 0;
    DW(G, 0x17524f) = 0;
    DW(G, 0x175201) = 0;
    DW(G, 0x175205) = 0x407f4000;
    DW(G, G_SOUND_E8)  = 0;
    DW(G, G_SOUND_100) = 0;

    GameLog_LogMessage(GAME_LOGGER_VA, 1, S_CRYSTALS,
                       (unsigned int)W(G, G_COUNT_CRYSTAL),
                       DW(G, G_REQUIRED));

    if ((int)(DW(G, G_CARRIED) + (unsigned int)W(G, G_COUNT_CRYSTAL)) <
        (int)DW(G, G_REQUIRED))
        GameLog_LogMessage(GAME_LOGGER_VA, 3, S_WARN_CRYSTALS);

    /* ONE argument -- see the header. */
    if (Leo_OpenExtraObjectsFile(G + G_EXTRA_OBJECTS,
                                 (const char *)(G + G_LEVEL_NAME)) == 0) {
        DW(G, G_EXTRA_LOADED) = 0;
        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_LEO_FAILED,
                           (const char *)(G + G_LEVEL_NAME));
    } else {
        DW(G, G_EXTRA_LOADED) = 1;
        GameLog_LogMessage(GAME_LOGGER_VA, 1, S_LEO_LOADED,
                           (const char *)(G + G_LEVEL_NAME));
    }

    DW(G, 0x2ab576) = 0x42700000;

    s_calls++;
    if (s_diag)
        log_write("levelsetup: DIAG call #%u map=%ux%u crystals=%u total=%u "
                  "bridges=%u teleports=%u lifts=%u slides=%u breakables=%u "
                  "foes=%u freebombs=%u timed=%u switchmax=%u\n",
                  s_calls, (unsigned)B(G, G_MAP_W), (unsigned)B(G, G_MAP_H),
                  (unsigned)W(G, G_COUNT_CRYSTAL), (unsigned)W(G, C_TOTAL),
                  (unsigned)W(G, C_BRIDGES), (unsigned)W(G, C_TELEPORTS),
                  (unsigned)B(G, G_LIFT_COUNT), (unsigned)B(G, G_SLIDE_COUNT),
                  (unsigned)B(G, G_BREAK_COUNT), (unsigned)B(G, G_FOE_COUNT),
                  (unsigned)W(G, C_FREEBOMBS), (unsigned)W(G, C_TIMED),
                  (unsigned)B(G, G_SWITCH_MAX));

    /* XOR AL,AL */
    return 0;
}
