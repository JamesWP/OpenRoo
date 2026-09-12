/* GAMETICK_PLAN.md Band A — Game::RemoveFoeObject 0x00417530 and
 *                            Game::RemoveEnemyObject 0x00417a20.
 *
 * ─── What these are ──────────────────────────────────────────────────────
 *
 * The despawn half of Band A's spawn/despawn quartet.  Both are `__thiscall`
 * on `Game *`, take ONE dword stack argument (the object's byte ID; the
 * epilogue is `RET 4`), and return void.  Each:
 *
 *   1. Looks the object up in its slot array by ID.  Nothing happens if the
 *      slot is empty.
 *   2. If the sound subsystem exists, releases every sound buffer the object
 *      holds, one named field at a time.
 *   3. Calls the object's OWN vtable slot 0 with argument 1 -- the MSVC
 *      scalar deleting destructor, which runs the dtor and frees.
 *   4. Compacts the ID free-list, removing this ID and shifting the tail
 *      down, then decrements the count.
 *
 * They are taken as ONE cycle because the two listings are the same function
 * with different offsets and a different owner flag -- the same judgement
 * `foepath.cpp` made about `FindOpenPathNodeByKey`/`FindClosedPathNodeByKey`.
 * The four differences are all recorded below, and one of them is a defect.
 *
 * ─── Why they were takeable, and what stays in the game binary ───────────
 *
 * Callee lists, read this session per the plan's standing rule:
 *
 *   RemoveFoeObject     ReleaseStaticSoundBufferForOwner  0x4432f0
 *                       ReleaseVoicePoolBufferForOwner    0x443400
 *                       HaltPlayback                      0x4429a0  ours
 *   RemoveEnemyObject   ReleaseStaticSoundBufferForOwner  0x4432f0
 *
 * `HaltPlayback` is already ours (static.cpp).  The two release functions are
 * NOT taken, and that is a decision rather than an omission.  They are a
 * generic asset service, not simulation: `ReleaseStaticSoundBufferForOwner`
 * has 49 call sites across `PurgeLiftObjects`, `PurgeSlideObjects`,
 * `PurgeBreakableObjects`, `PurgeBridgeObjects` and `ReleaseAllSoundBuffers`,
 * and internally walks two entry lists through `FindSoundEntry`,
 * `RemoveActionEntry`, `DestroyAssetEntry`, `FactAlloc::Free2` and the game's
 * `Logger`.  `ReleaseVoicePoolBufferForOwner` is the same shape with 5.
 *
 * That is exactly the case `tileeffects.cpp` already ruled on for the four
 * `LinkedList` methods: a shared service with call sites across unrelated
 * subsystems is kept as a NAMED CALLBACK at its original address, with the
 * reason recorded rather than left implicit.  Both are named below.
 *
 * The object's own vtable slot 0 is likewise called through, not replaced --
 * these functions destroy whatever the game constructed, and the game still
 * owns those vtables.
 *
 * ─── The four differences between the two ────────────────────────────────
 *
 *   1. Slot array and free-list.  Foes:   objects Game+0x174804 + id*4,
 *      count Game+0x174fd4, ID list Game+0x174fd5.  Enemies: objects
 *      Game+0x173e3f + id*4 (note the UNALIGNED base, which is what it is),
 *      count Game+0x17460f, ID list Game+0x174610.
 *
 *   2. The owner flag passed to the release calls.  Foes pass 1, enemies
 *      pass 0.  Inside the release function that flag is the difference
 *      between "detach this owner" and "detach, and if nobody else holds the
 *      buffer, destroy and free it": with 0 it returns early at
 *      `if (param_2 == 0) return;`.  So removing a foe can free the shared
 *      buffer; removing an enemy never does.  Not cosmetic.
 *
 *   3. The field lists differ, in content and in ORDER, and the order is
 *      preserved verbatim below because the release calls have side effects
 *      on shared state.  The foe list has eleven entries including two
 *      voice-pool buffers (+0x9f and +0xcf); the enemy list has eight, all
 *      static, and includes two fields the foe list does not (+0x15e, +0x15a)
 *      while omitting the foe-only ones.
 *
 *   4. THE DEFECT.  `RemoveFoeObject` nulls the slot after destroying the
 *      object (`MOV dword ptr [EDI],0x0` at 0x00417699).  `RemoveEnemyObject`
 *      DOES NOT -- there is no such store anywhere between the virtual
 *      destructor call at 0x00417b2c and the epilogue.  So an enemy slot is
 *      left holding a DANGLING pointer to freed memory, and the very next
 *      `RemoveEnemyObject` for the same ID would pass the emptiness test at
 *      the top and destroy it a second time.  Reproduced deliberately; a
 *      "cleaned up" version that nulls both would not be this game.
 *
 * ─── Two transcription traps in the shared tail ──────────────────────────
 *
 * THE HALTPLAYBACK FIELD IS RE-READ.  At 0x004175e4 the foe's +0xc3 is loaded
 * into ECX for `HaltPlayback`; then 0x004175f5 re-loads the object pointer
 * and 0x004175f9 re-loads +0xc3 for the release call.  Every field access in
 * both functions re-reads the slot pointer from the array rather than
 * caching it, and this file does the same.  Nothing in the release path
 * writes the slot, so the values agree -- but transcribing it as a cached
 * local would be assuming that rather than preserving it.
 *
 * THE COMPACTION SHIFT LAGS BY ONE, AND THAT IS THE POINT.  The loop is:
 *
 *     found = 0; i = 0;
 *     if (count != 0) do {
 *         c = list[i];
 *         if (found && i != 0) list[i - 1] = c;   <- [EAX + count_offset]
 *         if (c == id) found = 1;
 *         i++;
 *     } while (i < count);
 *     count--;
 *
 * The `found` test happens BEFORE the comparison against `id`, so the shift
 * begins at the element AFTER the match -- which is what makes it a deletion
 * rather than an off-by-one.  The `i != 0` guard matters for a reason that is
 * invisible in C: the destination `list[i-1]` is addressed as
 * `[EAX + <count offset>]`, and the count byte sits immediately BELOW the
 * list (0x174fd4 vs 0x174fd5).  At i == 0 that store would land on the count
 * itself.  It cannot, because `found` is still 0 at i == 0 -- but the guard
 * is there, and both are kept.
 *
 * `while (i < count)` is `CMP DL,AL; JC`, i.e. UNSIGNED, over bytes; the
 * entry test `TEST AL,AL; JBE` is count == 0.  The count is re-read from
 * memory every iteration (0x004176d9), though nothing in the loop writes it.
 *
 * ─── Interception ────────────────────────────────────────────────────────
 *
 * `__thiscall`, `this` in ECX, one dword stack argument, `RET 4`.
 * `tools/xref.py` over `Karoo.exe.orig` reports FOUR `E8` call sites each and
 * nothing else -- no `E9` tail-jump, no DATA ref, no `68 imm32`:
 *
 *   RemoveFoeObject    0x00414BE1  0x00415C52  0x004165A2  0x0041865D
 *   RemoveEnemyObject  0x00414C03  0x004151A5  0x004165C0  0x0041867B
 *
 * So two CALL_PATCHES entries and two SAFETY_STUBS.
 *
 * ─── Measurable proof ────────────────────────────────────────────────────
 *
 * KAROO_SIM_FX=keepid suppresses the compaction SHIFT in both functions -- the
 * count still decrements, but the removed ID is never lifted out of the
 * list, so the tail is not pulled down and the list keeps a stale ID while
 * silently dropping its last live one.  Every subsequent frame that walks the
 * list by ID therefore addresses the WRONG objects: it is not a value
 * perturbation but a change to which entity each later tick operates on.
 * That is the right control for this function, because the compaction is the
 * one piece of it whose output later frames actually read back.
 *
 * KAROO_REMOVE_DIAG=1 logs the first foe removal, the first enemy removal,
 * the first sound release, the first virtual-destructor call and the first
 * compaction shift (each once), plus a removal count every 500 -- the same
 * instrument the last five cycles used to tell "unexercised" from "exercised
 * but unobserved".
 */

#include <windows.h>
#include <string.h>

#include "log.h"
#include "objectremove.h"

/* The two sound-manager release calls the removes make are SoundManager
 * methods now (soundmanager.h), called from foe.cpp and bomb.cpp. */

/* The object's own scalar deleting destructor, vtable slot 0.  The game
 * constructed these objects and still owns their vtables, so this dispatches
 * through the pointer the object carries rather than through anything here. */
typedef void (__attribute__((thiscall)) *scalar_dtor_fn)(void *self, int flags);

/* ─── KAROO_SIM_FX / KAROO_REMOVE_DIAG, read by value ────────────────────
 * By VALUE, never by presence (RENDER_PLAN.md, 2026-09-02). */
static int s_fx_keepid = 0;
static int s_diag      = 0;
static int s_init      = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "keepid") == 0) {
        s_fx_keepid = 1;
        log_write("objectremove: KAROO_SIM_FX=keepid -- the ID free-list "
                  "compaction shift is suppressed, so the list keeps the "
                  "removed ID and every later tick addresses the wrong "
                  "object\n");
    }

    n = GetEnvironmentVariableA("KAROO_REMOVE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

static int s_logged_dtor     = 0;
static int s_logged_shift    = 0;

/* The shared tail: destroy the object through its own vtable slot 0, then
 * compact the ID free-list and decrement the count.
 *
 * `bNullSlot` is difference 4 -- the foe path nulls the slot, the enemy path
 * does not, leaving a dangling pointer.  Deliberate; see the header. */
void Object_DestroyAndCompactId(void **slot, unsigned char *pCount,
                                unsigned char *pIds, unsigned char id,
                                int bNullSlot)
{
    void *obj = *slot;
    unsigned char found = 0;
    unsigned char i     = 0;

    /* The only entry point left in this file since the removes moved to
     * foe.cpp and bomb.cpp -- which took their fx_init() calls with them,
     * leaving keepid and this half of KAROO_REMOVE_DIAG silently off. */
    fx_init();

    /* `MOV EDX,[ECX]; PUSH 1; CALL [EDX]` -- vtable slot 0, argument 1. */
    if (obj != 0) {
        scalar_dtor_fn *vtbl = *(scalar_dtor_fn **)obj;
        if (s_diag && !s_logged_dtor) {
            s_logged_dtor = 1;
            log_write("objectremove: first virtual dtor -- obj=%p vtbl=%p "
                      "slot0=%p\n", obj, (void *)vtbl, (void *)vtbl[0]);
        }
        vtbl[0](obj, 1);
    }

    if (bNullSlot)
        *slot = 0;

    /* The compaction.  See the header: the `found` test precedes the compare,
     * and the count is re-read from memory every iteration. */
    if (*pCount != 0) {
        do {
            unsigned char c = pIds[i];

            if (found && i != 0) {
                if (!s_fx_keepid)
                    pIds[i - 1] = c;
                if (s_diag && !s_logged_shift) {
                    s_logged_shift = 1;
                    log_write("objectremove: first compaction shift -- "
                              "i=%u id=%u count=%u\n",
                              (unsigned)i, (unsigned)id, (unsigned)*pCount);
                }
            }
            if (c == id)
                found = 1;

            ++i;
        } while (i < *pCount);
    }

    *pCount = (unsigned char)(*pCount - 1);
}

/* Both removes now live with their classes: Game::RemoveFoeObject 0x00417530
 * in foe.cpp (Foe::remove, bNullSlot 1) and Game::RemoveEnemyObject
 * 0x00417a20 in bomb.cpp (Bomb::remove, bNullSlot 0).  The header above
 * still describes both, since they are one listing. */
