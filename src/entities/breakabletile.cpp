/* GAMETICK_PLAN.md Band A — Game::UpdateBreakableTile (0x00403d40).
 *
 * The falling ("breakable") tile tick, tile kind 0x0d.  The second and last
 * genuine leaf in Band A: its only two callees, CStaticSoundbuffer's
 * Set3DPosition (0x4429c0) and TriggerPlayback (0x442900), are already ours
 * (static.cpp), so this replacement calls nothing in the game binary — it
 * calls our own exports directly, which retires two would-be callbacks rather
 * than adding any.
 *
 * `__thiscall`, no stack arguments, `RET`.  ONE call site, an E8 at
 * 0x004150AF inside GameTick, which walks the object pointer array at
 * Game+0x173b1e for the Game+0x173e3e objects in it.  tools/xref.py reports
 * that site and nothing else: no DATA reference, no `68 imm32`, and no
 * absolute-address call from our own DLL (`grep -rn 403d40 karoo-hooks/`
 * finds only comments).  So one CALL_PATCHES entry plus a UD2 covers it.
 *
 * The caller ignores the return value: the original ends `XOR AL,AL` and
 * leaves the top three bytes of EAX holding whatever the last FPU compare
 * left there, and 0x004150B4 immediately clobbers ECX and re-reads the loop
 * count.  The replacement is therefore `void`.
 *
 * ─── The object, and the tile it points at ───────────────────────────────
 *
 * Objects are built by SpawnBreakableObject (0x418240), 0x65 bytes each.
 * Fields used here, all packed and several unaligned:
 *
 *   +0x04  double   now          the game clock, copied in each tick
 *   +0x0c  double*  clock        -> Game+0x170a54, MILLISECONDS
 *   +0x10  void*    posSource    8 bytes copied to +0x15 each tick
 *   +0x15  8 bytes  pos          that copy
 *   +0x31  s8       row          MOVSX at 0x00403d6e — signed
 *   +0x32  s8       col          signed
 *   +0x34  char*    tileBase     -> Game+0x2ab58d
 *   +0x38  int      justRespawned  cleared every tick, set by RESPAWN
 *   +0x3c  int      justFell       cleared every tick, set by FALL
 *   +0x40  double   eventTime      clock stamp of the last FALL/RESPAWN
 *   +0x48  int      respawnPending
 *   +0x4d  CStaticSoundbuffer*  fallSound
 *   +0x51  CStaticSoundbuffer*  respawnSound
 *   +0x55  int      armed
 *   +0x59  int      param          SpawnBreakableObject's param_4
 *   +0x5d  double   armedAt
 *
 * The tile is reached by tileBase + (col + row * 100) * 0x7f, and then
 * +0x19c height, +0x19d kind, +0x1a5 occupant, +0x217 spent.  Note the
 * stride (0x7f) is far smaller than those offsets: tileBase does not point
 * at the start of the row-major array, so the constants are combined and are
 * reproduced literally rather than decomposed.
 *
 * ─── Lifecycle ───────────────────────────────────────────────────────────
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
 * ─── Four things that are easy to get wrong ──────────────────────────────
 *
 * 1. THE RESPAWN COMPARE IS STRICT AND THE FALL COMPARE IS NOT.  The
 *    decompile has `elapsed >= X` for the fall and `elapsed >= X && (elapsed
 *    == X) == 0` for the respawn — i.e. `>` — which is the FCOM/`JBE` vs
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
 * ─── The double fall (a defect, preserved) ───────────────────────────────
 *
 * Game::RemoveFoeObject never clears the tile's occupant byte, so a foe that
 * dies on one of these leaves it set.  When the tile respawns, that stale
 * occupant re-arms it immediately and it drops again 1.5 s later.  This is
 * what players see as "some of them fall twice"; it is a consequence of the
 * leak, not of the param byte, and tests/destr-bait.rec depends on it — the
 * recording explicitly waits out the repeat fall.  Nothing here fixes it.
 *
 * ─── Visual proof ────────────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=slowfall triples the fall delay, 1500 ms -> 4500 ms, leaving
 * the respawn window alone.  It is a measurement rather than a colour: on
 * Forest\DestrStart a tile stepped on and off takes about 270 frames to drop
 * instead of about 90, long enough to walk a bridge of tiles that would
 * normally have gone from under you.  Only this code path can produce that,
 * and the delay is exactly the constant this function owns.
 */
#include <windows.h>
#include <string.h>
#include "static.h"
#include "log.h"

/* Our own replacements, in this same DLL — see the header comment. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Set3DPosition(CStaticSoundbuffer *self,
                      float x, float y, float z, DWORD dwApply);

/* Object offsets (packed; several unaligned — hence the memcpy accessors). */
#define OBJ_NOW          0x04   /* double */
#define OBJ_CLOCK        0x0c   /* double* */
#define OBJ_POSSRC       0x10   /* void*  */
#define OBJ_POS          0x15   /* 8 bytes */
#define OBJ_ROW          0x31   /* s8 */
#define OBJ_COL          0x32   /* s8 */
#define OBJ_TILEBASE     0x34   /* char* */
#define OBJ_JUSTRESPAWN  0x38   /* int */
#define OBJ_JUSTFELL     0x3c   /* int */
#define OBJ_EVENTTIME    0x40   /* double */
#define OBJ_RESPAWNPEND  0x48   /* int */
#define OBJ_FALLSOUND    0x4d   /* CStaticSoundbuffer* */
#define OBJ_RESPAWNSOUND 0x51   /* CStaticSoundbuffer* */
#define OBJ_ARMED        0x55   /* int */
#define OBJ_PARAM        0x59   /* int */
#define OBJ_ARMEDAT      0x5d   /* double */

/* Tile offsets, relative to tileBase + (col + row * 100) * 0x7f. */
#define TILE_HEIGHT   0x19c   /* u8  */
#define TILE_KIND     0x19d   /* u8  */
#define TILE_OCCUPANT 0x1a5   /* s8  */
#define TILE_SPENT    0x217   /* int */

#define TILE_KIND_VOID      0x00
#define TILE_KIND_BREAKABLE 0x0d

/* The two doubles at 0x0045d2e0 and 0x0045d2d8. */
static const double FALL_DELAY_MS    = 1500.0;
static const double RESPAWN_DELAY_MS = 5000.0;

#define P(base, off)    ((char *)(base) + (off))
#define U8(base, off)   (*(unsigned char *)P(base, off))
#define S8(base, off)   (*(signed char *)P(base, off))
#define I32(base, off)  (*(int *)P(base, off))
#define PTR(base, off)  (*(void **)P(base, off))

/* +0x04, +0x40 and +0x5d are not 8-byte aligned. */
static inline double get_d(const void *base, unsigned off)
{
    double v;
    memcpy(&v, (const char *)base + off, sizeof v);
    return v;
}

static inline void set_d(void *base, unsigned off, double v)
{
    memcpy((char *)base + off, &v, sizeof v);
}

static int s_fxSlowFall = -1;

static double fall_delay_ms(void)
{
    if (s_fxSlowFall < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
        s_fxSlowFall = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "slowfall") == 0);
        if (s_fxSlowFall)
            log_write("breakabletile: KAROO_SIM_FX=slowfall -- fall delay %.0f ms, not %.0f\n",
                      FALL_DELAY_MS * 3.0, FALL_DELAY_MS);
    }
    return s_fxSlowFall ? FALL_DELAY_MS * 3.0 : FALL_DELAY_MS;
}

/* tileBase + (col + row * 100) * 0x7f, with row and col read as signed. */
static inline char *tile_of(void *self)
{
    const int idx = ((int)S8(self, OBJ_COL) + (int)S8(self, OBJ_ROW) * 100) * 0x7f;
    return (char *)PTR(self, OBJ_TILEBASE) + idx;
}

/* x = row, y = the tile's height byte (unsigned), z = -col. */
static void play_at_tile(void *self, CStaticSoundbuffer *snd, char *tile)
{
    CStatic_Set3DPosition(snd,
                          (float)(int)S8(self, OBJ_ROW),
                          (float)U8(tile, TILE_HEIGHT),
                          -(float)(int)S8(self, OBJ_COL),
                          1);
    CStatic_TriggerPlayback(snd, 0);
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_UpdateBreakableTile(void *self)
{
    /* Refresh the two cached copies the rest of the tick reads. */
    memcpy(P(self, OBJ_POS), PTR(self, OBJ_POSSRC), 8);
    memcpy(P(self, OBJ_NOW), PTR(self, OBJ_CLOCK), 8);

    char *tile = tile_of(self);
    const double now = get_d(self, OBJ_NOW);

    /* ── ARM ─────────────────────────────────────────────────────────── */
    if (S8(tile, TILE_OCCUPANT) != 0 && I32(self, OBJ_ARMED) == 0 &&
        I32(tile, TILE_SPENT) == 0) {
        set_d(self, OBJ_ARMEDAT, now);
        I32(self, OBJ_ARMED) = 1;
        I32(tile, TILE_SPENT) = 0;          /* dead store, preserved */
    }

    I32(self, OBJ_JUSTFELL)    = 0;
    I32(self, OBJ_JUSTRESPAWN) = 0;

    /* ── FALL — note the compare is >=, unlike the respawn ────────────── */
    if (I32(self, OBJ_ARMED) != 0) {
        const double elapsed = now - get_d(self, OBJ_ARMEDAT);
        I32(self, OBJ_JUSTFELL) = 0;
        if (elapsed >= fall_delay_ms()) {
            set_d(self, OBJ_EVENTTIME, now);
            I32(self, OBJ_JUSTFELL) = 1;

            /* silent on the second drop of a re-armed tile — note 4 */
            CStaticSoundbuffer *snd =
                (CStaticSoundbuffer *)PTR(self, OBJ_FALLSOUND);
            if (I32(self, OBJ_RESPAWNPEND) == 0 && snd != 0)
                play_at_tile(self, snd, tile);

            U8(tile, TILE_KIND) = TILE_KIND_VOID;
            if (I32(self, OBJ_PARAM) == 0)
                I32(self, OBJ_RESPAWNPEND) = 1;
            I32(tile, TILE_SPENT) = 1;
            I32(self, OBJ_ARMED)  = 0;
        }
    }

    /* ── RESPAWN — strictly greater, and measured from armedAt ─────────── */
    if (I32(self, OBJ_RESPAWNPEND) != 0) {
        const double elapsed = now - get_d(self, OBJ_ARMEDAT);
        if (elapsed >= RESPAWN_DELAY_MS && elapsed != RESPAWN_DELAY_MS) {
            set_d(self, OBJ_EVENTTIME, now);
            I32(self, OBJ_JUSTRESPAWN) = 1;

            CStaticSoundbuffer *snd =
                (CStaticSoundbuffer *)PTR(self, OBJ_RESPAWNSOUND);
            if (snd != 0)
                play_at_tile(self, snd, tile);

            U8(tile, TILE_KIND) = TILE_KIND_BREAKABLE;
            I32(self, OBJ_ARMED)       = 0;
            I32(self, OBJ_RESPAWNPEND) = 0;
            I32(tile, TILE_SPENT)      = 0;
        }
    }
}
