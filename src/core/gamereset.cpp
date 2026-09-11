/* GAMETICK_PLAN.md Band B — Game::ClearGameState 0x00418580 and the four
 *                           Purge*Objects helpers it stands on:
 *
 *     Game::PurgeLiftObjects       0x00417d90
 *     Game::PurgeSlideObjects      0x004181b0
 *     Game::PurgeBreakableObjects  0x004183f0
 *     Game::PurgeBridgeObjects     0x0041a190
 *
 * ─── What these are ──────────────────────────────────────────────────────
 *
 * The level teardown.  `ClearGameState` resets the run's scalar state, then
 * destroys every dynamic object the level created: the four fixed-object
 * arrays through the purges, and the two ID-list-managed arrays (foes and
 * enemies) through the already-replaced `Remove*Object` pair.
 *
 * They are taken as ONE cycle for the same reason `objectremove.cpp` took its
 * pair together: the four purges are the same function four times over, they
 * have no caller outside this cluster and two sibling sites, and taking
 * `ClearGameState` without them would leave it calling four UD2 stubs.
 *
 * ─── What stays in the game binary, and why ──────────────────────────────
 *
 * Callee lists, read from Ghidra this session per the plan's standing rule:
 *
 *   ClearGameState           Log_Message                       0x441b10 ours
 *                            PostQuitMessage                   user32
 *                            PurgeLiftObjects                  0x417d90 taken
 *                            PurgeSlideObjects                 0x4181b0 taken
 *                            PurgeBreakableObjects             0x4183f0 taken
 *                            PurgeBridgeObjects                0x41a190 taken
 *                            RemoveFoeObject                   0x417530 ours
 *                            RemoveEnemyObject                 0x417a20 ours
 *   all four Purge*Objects   ReleaseStaticSoundBufferForOwner  0x4432f0
 *
 * `ReleaseStaticSoundBufferForOwner` is KEPT as a named callback at its
 * original address.  That is the same ruling `objectremove.cpp` recorded for
 * it and `tileeffects.cpp` recorded for `LinkedList`: a shared asset service
 * with 49 call sites across unrelated subsystems is not simulation, and
 * replacing it is a separate decision from replacing its callers.
 *
 * Each object's own vtable slot 0 -- the MSVC scalar deleting destructor --
 * is likewise called THROUGH, not replaced.  These functions destroy what the
 * game constructed and the game still owns those vtables.  No vtable is taken
 * over by this cycle.
 *
 * `Log_Message` and both `Remove*Object` are already ours, so they are called
 * as our exports rather than at their original addresses -- those originals
 * carry UD2 stubs.
 *
 * ─── The four purges, from the LISTING ───────────────────────────────────
 *
 * All four share the shape.  Read from the disassembly, not the decompile,
 * because the decompile renders the slot arrays through named `Game` fields
 * and the offsets are what matter here:
 *
 *   count byte == 0  ->  store 0 to the count byte and return (the store is
 *                        redundant, and is preserved)
 *   otherwise, for each index i, RE-READING the count byte every iteration:
 *       if Game+0x13cc34 (sound `created`) != 0 and the object's sound
 *          handle field != 0:  ReleaseStaticSoundBufferForOwner(sm, h, 1)
 *       if the slot pointer != 0:  (*slot->vtbl[0])(slot, 1)
 *   then store 0 to the count byte.
 *
 * The per-purge differences are exactly three -- count, array, handle:
 *
 *   lift        count 0x173b19  array 0x173719  handle obj+0x3a
 *   slide       count 0x173718  array 0x173588  handle obj+0x39
 *   breakable   count 0x173e3e  array 0x173b1e  handles obj+0x4d AND obj+0x51
 *   bridge      count 0x170a43  array 0x170643  handle obj+0x47
 *
 * Three details are preserved deliberately:
 *
 *   1. BREAKABLE RE-READS THE SLOT between its two releases.  0x00418441 is
 *      `MOV EDX,[EBP]` -- the object pointer is loaded again from the array
 *      slot before the +0x51 field is read, rather than reused from the
 *      first load.  It matters if the release call can write the slot.  It is
 *      transcribed as written.
 *
 *   2. BREAKABLE'S SECOND RELEASE IS NESTED INSIDE THE `created` TEST but
 *      NOT inside the first handle's null test -- the two handle tests are
 *      siblings, so a null +0x4d does not skip +0x51.
 *
 *   3. BRIDGE INDEXES WITH A SIGNED INT, not a byte.  0x0041a1d6 increments
 *      EBP as a full dword and 0x0041a1e0 compares `CMP EBP,EAX / JL` against
 *      the zero-extended count, where the other three keep the index in AL
 *      and compare `CMP AL,CL / JC`.  With a count that fits in a byte the
 *      two agree; the difference is preserved rather than normalised.
 *
 * The entry test is `CMP byte [count],0 / JBE`, i.e. unsigned -- for a byte
 * that is simply `== 0`.  The loop-continue test is `JC`, unsigned below.
 *
 * ─── ClearGameState, from the LISTING ────────────────────────────────────
 *
 * One ordering subtlety, and it is the only thing in this function that is
 * not obvious from the decompile.  The guard byte is read at 0x00418597 and
 * compared at 0x004185af -- BEFORE the seventeen field stores -- but the
 * branch on it is not taken until 0x004185f8, AFTER them.  `MOV` does not
 * touch flags, so the compiler hoisted the load and sank the branch.  Every
 * field store therefore happens whether or not the game file is valid.  The
 * transcription keeps that order: stores first, then the guard.
 *
 * The two drain loops pass the ID as `PUSH ECX` / `PUSH EDX` having loaded
 * only CL / DL, so the pushed dword's upper three bytes are stale register
 * contents.  Both callees use the low byte only (see objectremove.cpp), so
 * passing a zero-extended byte is behaviourally identical.
 *
 * `0x175233` is set to 0x40690000 -- the float 3.625.  It is left as a raw
 * dword store here because nothing in this function reads it.
 *
 * ─── Negative control ────────────────────────────────────────────────────
 *
 * `KAROO_SIM_FX=keepobjects` makes all four purges return immediately, doing
 * nothing at all: the objects survive AND so do the count bytes, so the next
 * level ticks the previous level's lifts, hazards, breakables and bridges on
 * top of its own.  This is a change to WHAT THE WORLD IS MADE OF after a
 * teardown, not a perturbation of a value.
 *
 * THE FIRST VERSION OF THIS CONTROL SUPPRESSED ONLY THE DESTROY LOOP, still
 * zeroing the count, and it returned 16/16 -- it could not fail.  That is
 * worth recording rather than quietly fixing: leaking an object is invisible
 * to the simulation, because the arrays refill from index 0 with fresh
 * pointers and nothing ever reads the leaked ones again.  The load-bearing
 * half of a purge is the COUNT CLEAR, not the destruction, and a control has
 * to break the half the rest of the tick actually reads.  Same lesson as
 * `radiusfar`: reaching the code is not enough, and neither is doing work
 * inside it.
 *
 * `KAROO_RESET_DIAG=1` logs the first call to each of the five functions, the
 * first sound release, the first virtual destructor call and the first foe
 * and enemy drain, plus a call count every 500.
 */

#include <windows.h>
#include <string.h>

#include "log.h"
#include "game.h"
#include "liftobject.h"
#include "slideobject.h"
#include "bridgeobject.h"
#include "soundmanager.h"

/* ─── Game field offsets ─────────────────────────────────────────────────── */

#define G_SOUND_MGR       0x13cba8   /* SoundManager sub-object              */
#define G_SOUND_CREATED   0x13cc34   /* nonzero once sound is up             */

#define G_BREAK_SLOTS     0x173b1e
#define G_BREAK_COUNT     0x173e3e

#define G_FOE_COUNT       0x174fd4
#define G_FOE_IDS         0x174fd5
#define G_ENEMY_COUNT     0x17460f
#define G_ENEMY_IDS       0x174610

#define G_GAMEFILE_OK     0x4215e    /* zero == the game file did not load   */
#define G_GAMEFILE_NAME   0x4215f    /* the %s in the error message          */

#define G_CLEAR_BLOCK     0x170543   /* 0x40 dwords zeroed by the REP STOSD  */

/* ─── Callbacks kept at their original addresses ─────────────────────────── */


typedef void (__attribute__((thiscall)) *scalar_dtor_fn)(void *self, int flags);

/* Already ours -- called as exports, since the originals carry UD2 stubs. */
struct GameLogger;
extern "C" __declspec(dllexport) void __cdecl
GameLog_LogMessage(GameLogger *self, int level, const char *fmt, ...);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveFoeObject(void *self, unsigned int idArg);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveEnemyObject(void *self, unsigned int idArg);

#define GAME_LOGGER   ((GameLogger *)0x0046c4c0)
#define S_GAMEFILE_ERR ((const char *)0x004657a4)

/* Unaligned object-field read -- G_BREAK_SLOTS and friends are not all
 * 4-aligned, and neither are the handle offsets (+0x39, +0x3a, +0x47 ...). */
typedef void *__attribute__((aligned(1))) u_ptr;
typedef unsigned int __attribute__((aligned(1))) u32_ua;
#define OBJPTR(base, off)  (*(u_ptr *)((unsigned char *)(base) + (off)))
#define SLOT(G, base, i)   (*(u_ptr *)((G) + (base) + (unsigned int)(i) * 4))

/* ─── FX / diag ──────────────────────────────────────────────────────────── */

static int s_fx_keepobjects = 0;
static int s_diag           = 0;
static int s_init           = 0;

static unsigned s_calls        = 0;
static int s_logged_break      = 0;
static int s_logged_clear      = 0;
static int s_logged_release    = 0;
static int s_logged_dtor       = 0;
static int s_logged_foedrain   = 0;
static int s_logged_enemydrain = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "keepobjects") == 0) {
        s_fx_keepobjects = 1;
        log_write("gamereset: KAROO_SIM_FX=keepobjects -- the four purges "
                  "skip their destroy loops but still clear the count, so "
                  "level objects survive teardown and the arrays refill over "
                  "stale pointers\n");
    }

    n = GetEnvironmentVariableA("KAROO_RESET_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

static void diag_tick(void)
{
    s_calls++;
    if (s_diag && (s_calls % 500) == 0)
        log_write("gamereset: %u purge/clear calls\n", s_calls);
}

/* A purge entered with a NONZERO count is the only kind that destroys
 * anything.  Counting those separately is what distinguishes "the suite never
 * tears down a populated level" from "it does, and the control still cannot
 * see it" -- see the header's note on the first control. */
static unsigned s_live_purges = 0;

static void diag_live_purge(const char *which, unsigned count)
{
    if (!s_diag)
        return;
    s_live_purges++;
    log_write("gamereset: LIVE purge #%u -- %s count=%u\n",
              s_live_purges, which, count);
}

/* Release one sound handle through the game's own asset service. */
static void release_handle(unsigned char *G, void *obj, unsigned int off)
{
    void *h;

    if (*(int *)(G + G_SOUND_CREATED) == 0)
        return;

    h = OBJPTR(obj, off);
    if (h == 0)
        return;

    if (s_diag && !s_logged_release) {
        s_logged_release = 1;
        log_write("gamereset: first sound release -- obj=%p off=0x%x h=%p\n",
                  obj, off, h);
    }
    ((SoundManager *)(G + G_SOUND_MGR))->releaseStaticForOwner(h, 1);
}

/* Call the object's own vtable slot 0 (scalar deleting destructor). */
static void destroy_object(void *obj)
{
    scalar_dtor_fn *vtbl;

    if (obj == 0)
        return;

    vtbl = *(scalar_dtor_fn **)obj;
    if (s_diag && !s_logged_dtor) {
        s_logged_dtor = 1;
        log_write("gamereset: first virtual dtor -- obj=%p vtbl=%p slot0=%p\n",
                  obj, (void *)vtbl, (void *)vtbl[0]);
    }
    vtbl[0](obj, 1);
}

/* Game::PurgeSlideObjects 0x004181b0 is SlideObject::purgeAll, in
 * slideobject.cpp; Game::PurgeLiftObjects likewise in liftobject.cpp, and
 * Game::PurgeBridgeObjects 0x0041a190 in bridgeobject.cpp (detail 3 above
 * went with it). */

/* ═══ 0x004183f0 -- Game::PurgeBreakableObjects ════════════════════════════
 *
 * The only purge with TWO sound handles, and the only one that re-reads the
 * slot between them (0x00418441).  Both details are preserved; see the header.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PurgeBreakableObjects(void *self)
{
    unsigned char *G = (unsigned char *)self;
    unsigned char i;

    fx_init();
    diag_tick();

    if (s_fx_keepobjects)
        return;   /* the purge does nothing at all -- see header */

    if (s_diag && !s_logged_break) {
        s_logged_break = 1;
        log_write("gamereset: first PurgeBreakableObjects -- count=%u\n",
                  (unsigned)G[G_BREAK_COUNT]);
    }

    i = 0;
    if (G[G_BREAK_COUNT] != 0) {
        diag_live_purge("breakable", (unsigned)G[G_BREAK_COUNT]);
        do {
            {
                if (*(int *)(G + G_SOUND_CREATED) != 0) {
                    release_handle(G, SLOT(G, G_BREAK_SLOTS, i), 0x4d);
                    /* the slot is loaded AGAIN here -- 0x00418441 */
                    release_handle(G, SLOT(G, G_BREAK_SLOTS, i), 0x51);
                }
                destroy_object(SLOT(G, G_BREAK_SLOTS, i));
            }
            i++;
        } while (i < G[G_BREAK_COUNT]);
    }
    G[G_BREAK_COUNT] = 0;
}

/* ═══ 0x00418580 -- Game::ClearGameState ═══════════════════════════════════ */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ClearGameState(void *self)
{
    unsigned char *G = (unsigned char *)self;
    unsigned char gamefile_ok;
    u32_ua *p;
    int n;

    fx_init();
    diag_tick();

    if (s_diag && !s_logged_clear) {
        s_logged_clear = 1;
        log_write("gamereset: first ClearGameState -- foes=%u enemies=%u "
                  "lift=%u slide=%u break=%u bridge=%u\n",
                  (unsigned)G[G_FOE_COUNT], (unsigned)G[G_ENEMY_COUNT],
                  (unsigned)((Game *)G)->liftCount(),
                  (unsigned)((Game *)G)->slideCount(),
                  (unsigned)G[G_BREAK_COUNT],
                  (unsigned)((Game *)G)->bridgeCount());
    }

    /* The guard byte is READ here, at 0x00418597, before the stores -- the
     * branch on it is sunk to 0x004185f8.  See the header. */
    gamefile_ok = G[G_GAMEFILE_OK];

    *(unsigned int *)(G + 0x175402) = 2;   /* lives */
    G[0x28ab2d]                     = 2;
    *(unsigned int *)(G + 0x1752ef) = 0;
    *(unsigned int *)(G + 0x17522f) = 0;
    *(unsigned int *)(G + 0x170a44) = 0;
    G[0x173583]                     = 0;
    G[0x4220b]                      = 0;
    *(unsigned int *)(G + 0x175406) = 0;
    G[0x1751dd]                     = 1;
    *(unsigned int *)(G + 0x175317) = 0;
    *(unsigned int *)(G + 0x1752f3) = 0;
    *(unsigned int *)(G + 0x1752e9) = 0;
    G[0x1752e8]                     = 0;
    *(unsigned int *)(G + 0x175233) = 0x40690000;   /* float 3.625 */
    *(unsigned int *)(G + 0x1753f5) = 0;
    *(unsigned int *)(G + 0x170a48) = 0;

    if (gamefile_ok == 0) {
        GameLog_LogMessage(GAME_LOGGER, 4, S_GAMEFILE_ERR,
                           (const char *)(G + G_GAMEFILE_NAME));
        PostQuitMessage(1);
    }

    LiftObject::purgeAll((Game *)self);
    SlideObject::purgeAll((Game *)self);
    Sim_PurgeBreakableObjects(self);
    BridgeObject::purgeAll((Game *)self);

    /* REP STOSD, ECX=0x40 -- 0x100 bytes at Game+0x170543 (unaligned base). */
    p = (u32_ua *)(G + G_CLEAR_BLOCK);
    for (n = 0x40; n != 0; n--)
        *p++ = 0;

    while (G[G_FOE_COUNT] != 0) {
        if (s_diag && !s_logged_foedrain) {
            s_logged_foedrain = 1;
            log_write("gamereset: first foe drain -- count=%u id=%u\n",
                      (unsigned)G[G_FOE_COUNT], (unsigned)G[G_FOE_IDS]);
        }
        Sim_RemoveFoeObject(self, G[G_FOE_IDS]);
    }

    while (G[G_ENEMY_COUNT] != 0) {
        if (s_diag && !s_logged_enemydrain) {
            s_logged_enemydrain = 1;
            log_write("gamereset: first enemy drain -- count=%u id=%u\n",
                      (unsigned)G[G_ENEMY_COUNT], (unsigned)G[G_ENEMY_IDS]);
        }
        Sim_RemoveEnemyObject(self, G[G_ENEMY_IDS]);
    }

    G[G_BREAK_COUNT]  = 0;
    G[G_FOE_COUNT]    = 0;
    ((Game *)G)->setLiftCount(0);
    ((Game *)G)->setSlideCount(0);
    G[G_ENEMY_COUNT]  = 0;
}
