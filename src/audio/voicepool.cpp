/* GAMETICK_PLAN.md Band A — VoicePool, the last two leaves under
 * UpdateEntityMovement 0x00438770.
 *
 * A VoicePool is a small round-robin bank of CStaticSoundbuffers used for
 * sounds that can overlap with themselves — footsteps, impacts, the noises
 * an entity makes while moving.  Playing one "voice" means stopping the
 * current slot, replaying it, and advancing the index for next time.
 *
 * Both methods are leaves: VoicePoolCycle's only callees (HaltPlayback and
 * TriggerPlayback) are already ours, and BroadcastPoolVoiceCoordinates calls
 * only a COM method on a proxied interface.  Neither calls into the game
 * binary, and nor do these replacements.
 *
 * The struct is Ghidra's, cross-checked against static.h: the stride the
 * disassembly uses for pBufs is 0x18, which is sizeof(CStaticSoundbuffer)
 * exactly, and that is what ties the two together.  See the static_asserts.
 */
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include "static.h"
#include "voicepool.h"
#include "alloc.h"
#include "log.h"

/* Our own replacements come from static.h, their owner (COHESION_PLAN
 * template 10) -- CStatic_Init, Reset, Copy, CreateAndLoad3DSoundFile,
 * TriggerPlayback and HaltPlayback are all declared there. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Reset(CStaticSoundbuffer *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_HaltPlayback(CStaticSoundbuffer *self);

/* The VoicePool struct and its layout checks live in voicepool.h. */

/* ─── KAROO_POOL_DIAG — the census ────────────────────────────────────────
 *
 * Read by value, never by presence (CLAUDE.md): GetEnvironmentVariableA, so
 * an empty or "0" setting is off.  Counts every one of the eight VoicePool
 * functions, announces each one's first call, and totals the voices actually
 * allocated -- so "the gates never build a pool" can be told apart from "the
 * gates build pools and assert nothing downstream of them".
 */
static int pool_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_POOL_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nBlank, g_nWipe, g_nFill3D, g_nClone,
                     g_nGetVoiceAt, g_nFirstName, g_nCycle, g_nBroadcast,
                     g_nVoices, g_nCopyFail, g_nNestBail;

static void pool_first(const char *what, unsigned long *seen)
{
    if (!pool_diag() || *seen) return;
    *seen = 1;
    log_write("voicepool: first call to %s\n", what);
}

/* Reported from the pool-building calls, which happen a handful of times per
 * level load rather than per sound -- so unlike linkedlist's census this one
 * needs no sampling threshold.  Cycle and Broadcast are the hot pair and are
 * counted but never report. */
static void pool_census(void)
{
    if (!pool_diag()) return;
    log_write("voicepool: DIAG blank=%lu wipe=%lu fill3d=%lu clone=%lu "
              "getvoice=%lu firstname=%lu cycle=%lu broadcast=%lu "
              "voicesAllocated=%lu copyFallbacks=%lu nestBails=%lu\n",
              g_nBlank, g_nWipe, g_nFill3D, g_nClone, g_nGetVoiceAt,
              g_nFirstName, g_nCycle, g_nBroadcast, g_nVoices,
              g_nCopyFail, g_nNestBail);
}

/* ─── KAROO_POOL_FX — the negative control (CONTROLS.md) ──────────────────
 *
 *   onevoice — Fill3D and Clone clamp the pool to a single voice, whatever
 *              count the caller asked for.
 *
 * WHY THIS ONE.  These functions write no pixels and move no geometry, so
 * there is nothing to tint and -- the blast-radius rule -- nothing that can
 * reach the unbounded bridge/slide spawn scans that crash levelreport.py.
 * What they own outright is how many voices a pool has, and that is the
 * whole point of the class: a one-voice pool cannot overlap a sound with
 * itself, because VoicePoolCycle halts and re-triggers the same slot every
 * time.  The control is therefore a change of *outcome* decided by nothing
 * but this code, and KAROO_POOL_DIAG's voicesAllocated reads it directly.
 */
enum PoolFx { POOL_FX_OFF = 0, POOL_FX_ONEVOICE = 1 };

static PoolFx pool_fx(void)
{
    static int cached = -1;
    if (cached >= 0) return (PoolFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_POOL_FX", buf, sizeof(buf));
    PoolFx fx = POOL_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "onevoice") == 0) fx = POOL_FX_ONEVOICE;
    }
    log_write("voicepool: FX mode = %s\n",
              fx == POOL_FX_ONEVOICE ? "onevoice" : "off");
    cached = (int)fx;
    return fx;
}

/* ─── VoicePool::VoicePoolCycle (0x00442d90) ──────────────────────────────
 *
 * `__thiscall`, RET 4, one stack argument (dwLoopFlags).  Six E8 call sites
 * (0x4123a8, 0x418db4, 0x418ddd, 0x42002c, 0x438aad, 0x43974b); xref.py
 * reports all six as CALL and nothing else.
 *
 *   if (pBufs == NULL) return DSERR_UNINITIALIZED;   // 0x887800AA
 *   HaltPlayback(&pBufs[idx]);
 *   old = idx; idx = old + 1;
 *   hr = TriggerPlayback(&pBufs[old], dwLoopFlags);
 *   if (idx >= dwVoiceCount) idx = 0;                // signed JL
 *   return hr;
 *
 * THREE THINGS THAT ARE EASY TO GET WRONG:
 *
 * 1. IT HALTS AND RE-TRIGGERS THE **SAME** SLOT.  The name suggests it stops
 *    voice n and starts voice n+1; it does not.  The disassembly computes
 *    ECX from EAX *before* the INC, so both calls address the same buffer.
 *    The increment only decides where the *next* call lands.  Getting this
 *    backwards would still sound roughly right, which is what makes it
 *    dangerous.
 *
 * 2. THE WRAP HAPPENS AFTER THE TRIGGER, NOT BEFORE.  dwCurrentIdx is stored
 *    un-wrapped (it can briefly equal dwVoiceCount), the sound is started,
 *    and only then is it clamped back to 0.  Anything reading the field
 *    between those points sees the out-of-range value.
 *
 * 3. THE COMPARE IS SIGNED (JL) even though the fields are dwords.  A pool
 *    with a negative dwVoiceCount would reset the index every call rather
 *    than run away; unsigned would do the opposite.
 *
 * The NULL check covers pBufs only — not the individual voices, and not
 * dwVoiceCount, so a pool with a zero count still halts and plays pBufs[0]
 * once before the wrap resets the index.  Preserved.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolCycle(VoicePool *self, DWORD dwLoopFlags)
{
    ++g_nCycle; { static unsigned long seen; pool_first("Cycle", &seen); }
    if (self->pBufs == 0)
        return (int)0x887800AA;          /* DSERR_UNINITIALIZED */

    CStatic_HaltPlayback(&self->pBufs[self->dwCurrentIdx]);

    const int old = self->dwCurrentIdx;
    self->dwCurrentIdx = old + 1;        /* stored before the trigger, un-wrapped */

    const int hr = CStatic_TriggerPlayback(&self->pBufs[old], dwLoopFlags);

    if (self->dwCurrentIdx >= self->dwVoiceCount)   /* signed, and after */
        self->dwCurrentIdx = 0;

    return hr;
}

/* ─── VoicePool::BroadcastPoolVoiceCoordinates (0x00442df0) ───────────────
 *
 * Was FUN_00442df0; named, typed and plate-commented in Ghidra this session.
 * `__thiscall`, four stack arguments forwarded untouched.  Four E8 call
 * sites (0x41239b, 0x420020, 0x438aa1, 0x43973f), each immediately followed
 * by the matching VoicePoolCycle call — position the pool, then play it.
 *
 * The loop reads each buffer's +0x14 (threeDBuffer) with stride 0x18 and
 * calls vtable slot 0x4c/4 = 19 on it.  Slot 19 of IDirectSound3DBuffer is
 * SetPosition, the only four-argument method at that index, so the C++ call
 * below compiles to the same dispatch.  It goes through the interface
 * pointer the game already holds, which is a com_proxy object, so the draw
 * and audio proxy layers still see it.
 *
 * TWO THINGS THAT LOOK LIKE BUGS, BOTH PRESERVED:
 *
 * 1. THE NULL GUARD CHECKS ONLY VOICE 0.  pBufs[0].threeDBuffer is tested,
 *    then every voice's threeDBuffer is dereferenced.  A pool that was
 *    partly 3D would call through NULL.  Pools are created all-3D or
 *    all-2D, so it does not fire in practice.
 *
 * 2. pBufs ITSELF IS NOT CHECKED, unlike VoicePoolCycle directly above,
 *    which returns DSERR_UNINITIALIZED for it.  This one would fault.
 *
 * The count test is `0 < dwVoiceCount` before the loop and `i < dwVoiceCount`
 * at the bottom — a do/while, so the guard is what stops a zero-count pool.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_BroadcastPoolVoiceCoordinates(VoicePool *self,
                                  float x, float y, float z, DWORD dwApply)
{
    ++g_nBroadcast; { static unsigned long seen; pool_first("Broadcast", &seen); }
    /* defect 1: only voice 0 is checked */
    if (self->pBufs->threeDBuffer == 0 || self->dwVoiceCount <= 0)
        return;

    int i = 0;
    do {
        self->pBufs[i].threeDBuffer->SetPosition(x, y, z, dwApply);
        i++;
    } while (i < self->dwVoiceCount);
}

/* ═══ ENDGAME_PLAN E1 — the rest of the class ════════════════════════════
 *
 * Blank, Wipe, Fill3D, Clone and the two accessors, which is every remaining
 * VoicePool function and the `VoicePoolWipe` callback levelsounds.cpp held.
 *
 * THEY HAD TO GO TOGETHER.  Fill3D and Clone each CALL Wipe (three sites
 * between them) and Clone CALLs Fill3D, so taking Wipe alone would have
 * moved the callback into two new ones rather than retiring it — the rule
 * the `.fon` loader and the big-text pair both established.
 *
 * Reference audit, `tools/xref.py` over Karoo.exe.orig, plus the grep of
 * karoo-hooks/ that CLAUDE.md pairs with it:
 *
 *   0x4429f0 Blank        1 ref   1 CALL (0x44391d)
 *   0x442a10 thunk        3 refs  3 CALL — incremental-linker JMP to Wipe
 *   0x442a20 Wipe         9 refs  8 CALL + the thunk's JMP
 *   0x442ae0 Fill3D       2 refs  2 CALL (0x442c44 is Clone's, 0x442d75 own)
 *   0x442c60 Clone        4 refs  4 CALL
 *   0x442de0 FirstName    2 refs  2 CALL
 *   0x442e40 GetVoiceAt   2 refs  2 CALL
 *
 * No DATA push, no JMP but the thunk's, no vtable slot — VoicePool has no
 * vtable at all.  A closed set, and the callers that are not each other live
 * in SoundManager (0x4433xx–0x443c2x) and levelsounds, so CALL_PATCHES does
 * the whole job.
 *
 * NOTHING HERE CALLS THE GAME.  Every callee is already ours — CStatic_Init,
 * CStatic_Reset, CStatic_Copy, CStatic_CreateAndLoad3DSoundFile — so the
 * replacement creates no new callback.  The one exception is deliberate and
 * named below: the voice array stays on the game's heap.
 *
 * THE ARRAY STAYS ON THE GAME HEAP, AND THAT IS NOT LAZINESS.  Fill3D and
 * Clone allocate `count*0x18 + 4` from the game's operator new, store `count`
 * in the leading dword and hand out base+4 as pBufs.  What frees it is
 * `CStaticSoundbuffer::ScalarVectorDtor` 0x00442a80 — vtable slot 0, still
 * game code — which reads that count header back and calls FactAlloc::Free2
 * on base-4.  The other side of the lifetime is theirs, so alloc.h is
 * correct here by its own rule.  It retires when 0x442a80 does.
 *
 * NO SEH, AND NO __ehvec_ctor.  Both originals wrap the array construction
 * in an SEH frame (handlers 0x45c62b and 0x45c64b) and build it with MSVC's
 * vector-constructor iterator 0x00451db5, passing it Init as the ctor and
 * ReinitBuffer as the unwind dtor.  All of that exists for one case: a
 * constructor throwing part-way through, so the already-built prefix gets
 * destroyed.  CStaticSoundbuffer::Init writes five fields and cannot throw —
 * it is the nine-instruction 0x442230 — so the frame and the iterator are
 * unreachable machinery, and a plain loop is the same program.  Calling
 * 0x451db5 would also have added a CRT callback, which E1 exists to remove.
 *
 * Four PUSH_PATCHES go with this cycle: 0x42B4D / 0x42B52 / 0x42CC0 /
 * 0x42CC5 are the ctor/dtor function pointers fed to that iterator, and they
 * sit *inside* the two functions this cycle stubs.  Leaving them would
 * rewrite bytes under a UD2.  The fifth, 0x42A95, is inside ScalarVectorDtor
 * and stays.
 */

/* ─── VoicePool::VoicePoolBlank (0x004429f0) ──────────────────────────────
 *
 * The plain ctor: zero all five fields, return `this`.  One call site, in
 * SoundManager::AcquireVoicePool.  Note it clears dwNestDepth, which Wipe
 * deliberately does not — see there.
 */
extern "C" __declspec(dllexport) VoicePool * __attribute__((thiscall))
Sim_VoicePoolBlank(VoicePool *self)
{
    ++g_nBlank; { static unsigned long seen; pool_first("Blank", &seen); }
    self->pBufs        = 0;
    self->dwVoiceCount = 0;
    self->dwCurrentIdx = 0;
    self->logger       = 0;
    self->dwNestDepth  = 0;
    return self;
}

/* ─── VoicePool::VoicePoolWipe (0x00442a20) ───────────────────────────────
 *
 * Reset every voice, destroy the array through its vtable, and clear the
 * pool.  This was the callback.
 *
 * THE FIELD IT DOES NOT CLEAR IS THE INTERESTING ONE.  It writes +0x00,
 * +0x04, +0x08 and +0x10 and leaves **+0x0c (dwNestDepth) alone**.  That is
 * not an oversight to be tidied: Fill3D calls Wipe while its own nest count
 * is raised, and clearing it here would change which branch Fill3D's guard
 * takes.  Preserved exactly, and it is load-bearing for the defect below.
 *
 * The count is re-read from +0x10 on every iteration rather than cached, so
 * a Reset that changed it would be seen.  Reproduced as written.
 *
 * The array is destroyed with flag 3 through vtable slot 0 — bit 1 "this is
 * an array", bit 0 "free the block" — which is the game's ScalarVectorDtor
 * reading the count header at pBufs[-1] and calling Free2.  We dispatch
 * through the object's own vtable exactly as the original does rather than
 * calling 0x442a80 by address, so this is a virtual call, not a callback.
 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_VoicePoolWipe(VoicePool *self)
{
    ++g_nWipe; { static unsigned long seen; pool_first("Wipe", &seen); }
    pool_census();
    if (self->pBufs != 0) {
        for (int i = 0; i < self->dwVoiceCount; i++)   /* count re-read each pass */
            CStatic_Reset(&self->pBufs[i]);

        if (self->pBufs != 0) {                        /* re-tested; harmless */
            typedef void *(__attribute__((thiscall)) *vec_dtor_fn)(void *self, int flags);
            vec_dtor_fn dtor = *(vec_dtor_fn *)self->pBufs->vtable;
            dtor(self->pBufs, 3);
        }
        self->pBufs = 0;
    }
    self->logger       = 0;
    self->dwVoiceCount = 0;
    self->dwCurrentIdx = 0;
    /* dwNestDepth (+0x0c) is deliberately NOT cleared — see the header. */
}

/* ─── VoicePool::VoicePoolFill3D (0x00442ae0) ─────────────────────────────
 *
 * `__thiscall`, RET 0x14 — five stack arguments.  Builds a pool of `count`
 * voices all playing one file: load voice 0 from disk, then duplicate it
 * into the rest, falling back to a fresh load for any voice the duplicate
 * refuses.
 *
 * THE FRAME WAS COUNTED FROM THE PROLOGUE, NOT READ OFF THE DECOMPILE.  Two
 * pushes of the SEH record, one of the saved handler, one local slot and the
 * four callee-saved registers put ESP at entry-0x20, so the arguments are at
 * [ESP+0x24] .. [ESP+0x34].  The body then *reuses two of those argument
 * slots as locals* (0x442bb3 writes pBufs over arg2's slot and the loop
 * counter over arg3's), which is why reading them as arguments after that
 * point gives nonsense.  Same trap as RenderText's, third time in this tree.
 *
 * THE SOFTWARE-BUFFER RETRY CAN NEVER RUN, AND THAT IS A REAL DEFECT.  The
 * function opens with `if (++dwNestDepth > 1) goto fail`, a re-entrancy
 * guard, and its last-ditch fallback at 0x442c27 is to Wipe and **call
 * itself** with DSBCAPS_LOCSOFTWARE (0x8) added — asking DirectSound for a
 * software buffer when hardware failed.  But the recursive call happens with
 * dwNestDepth still 1, so the inner call raises it to 2, trips its own guard
 * and returns 0 without doing anything at all.  A pool that fails in
 * hardware therefore fails outright; the software path is dead code reached
 * by a live branch.  Wipe's refusal to clear +0x0c is what makes it so.
 * Preserved, because the observable result — failure — is the same either
 * way only if you reproduce it; "fixing" it would start allocating software
 * buffers the original never allocated.
 *
 * Failure of voice 0 wipes and returns 0.  Failure of a later voice retries
 * that one voice with a fresh load before giving up on the whole pool.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Sim_VoicePoolFill3D(VoicePool *self, int count, IDirectSound *pDS,
                    DWORD dwDsFlags, const char *filename, void *logger)
{
    ++g_nFill3D; { static unsigned long seen; pool_first("Fill3D", &seen); }
    pool_census();
    if (pool_fx() == POOL_FX_ONEVOICE && count > 1)
        count = 1;                    /* KAROO_POOL_FX=onevoice */

    Sim_VoicePoolWipe(self);

    if (++self->dwNestDepth > 1) {    /* re-entrancy guard; see the header */
        ++g_nNestBail;
        goto fail;
    }

    if (count < 1 || logger == 0)
        goto fail;

    self->logger       = logger;
    self->dwVoiceCount = count;

    {
        /* count*0x18 + 4: the leading dword is the array-count header that
         * ScalarVectorDtor reads back.  NULL is not checked by the original
         * before the test below, and the test is the only guard. */
        void *block = game_operator_new((unsigned)(count * 0x18 + 4));
        CStaticSoundbuffer *bufs = 0;
        if (block != 0) {
            *(int *)block = count;
            bufs = (CStaticSoundbuffer *)((char *)block + 4);
            for (int i = 0; i < count; i++)   /* __ehvec_ctor, minus the SEH */
                CStatic_Init(&bufs[i]);
            g_nVoices += (unsigned long)count;
        }
        self->pBufs = bufs;
    }

    /* Voice 0 comes off disk.  A NULL pBufs reaches here as a NULL `this`,
     * exactly as the original does. */
    if (!CStatic_CreateAndLoad3DSoundFile(&self->pBufs[0], pDS, dwDsFlags,
                                          filename, logger)) {
        Sim_VoicePoolWipe(self);
        goto fail;
    }

    {
        CStaticSoundbuffer *src = self->pBufs;   /* voice 0, the template */
        for (int i = 1; i < self->dwVoiceCount; i++) {   /* count re-read */
            if (CStatic_Copy(&self->pBufs[i], pDS, src, 1) != 0)
                continue;
            ++g_nCopyFail;
            if (CStatic_CreateAndLoad3DSoundFile(&self->pBufs[i], pDS,
                                                 dwDsFlags, filename, logger))
                continue;

            /* The dead software retry.  Written as the original wrote it. */
            Sim_VoicePoolWipe(self);
            {
                int r = Sim_VoicePoolFill3D(self, count, pDS,
                                            dwDsFlags | DSBCAPS_LOCSOFTWARE,
                                            filename, logger);
                self->dwNestDepth--;
                return r;
            }
        }
    }

    self->dwNestDepth--;
    return 1;

fail:
    self->dwNestDepth--;
    return 0;
}

/* ─── VoicePool::VoicePoolClone (0x00442c60) ──────────────────────────────
 *
 * `__thiscall`, RET 0x10 — four arguments.  Fills the pool with `count`
 * duplicates of an existing buffer rather than of a file.
 *
 * TWO DIFFERENT NON-ZERO VALUES MEAN SUCCESS, and that is preserved rather
 * than normalised.  The duplicate path returns **`src`**, the caller's own
 * pointer; the reload fallback returns **`self->pBufs`**.  Both are merely
 * non-NULL to every caller, but they are not the same pointer and the
 * original does not pretend they are.
 *
 * The success test on each Copy is `== src`, not `!= 0` — CStatic_Copy
 * returns its `other` argument on success, so this is an identity check
 * dressed as a status check.
 *
 * `noFallback` (arg4) short-circuits: if a Copy fails and arg4 is non-zero,
 * give up immediately instead of reloading from the file.
 *
 * THE BRANCH ON src->threeDBuffer IS A DISTINCTION WITHOUT A DIFFERENCE, and
 * it is worth saying so rather than inventing one.  0x442d42 tests it and
 * then assembles the identical five-argument Fill3D call twice, once per
 * side; the only textual difference between the two blocks is `OR AL,0x8`
 * against `OR ECX,0x8`, and since OR only ever sets bits, bit 3 lives in the
 * low byte and the upper bytes are untouched, the two compute the same
 * flags.  Both branches pass (count, pDS, src->dwDsFlags | LOCSOFTWARE,
 * src->filename, src->logger).  One call here is the same program; a reader
 * looking for the 3D case's special handling will not find one because
 * there is not one.
 *
 * Note that fallback asks for LOCSOFTWARE — and by Fill3D's defect above,
 * Fill3D's *own* software retry can never fire, but this one is an outer
 * call with dwNestDepth back at 0, so it runs normally.
 */
extern "C" __declspec(dllexport) void * __attribute__((thiscall))
Sim_VoicePoolClone(VoicePool *self, int count, IDirectSound *pDS,
                   CStaticSoundbuffer *src, int noFallback)
{
    ++g_nClone; { static unsigned long seen; pool_first("Clone", &seen); }
    pool_census();
    if (pool_fx() == POOL_FX_ONEVOICE && count > 1)
        count = 1;                    /* KAROO_POOL_FX=onevoice */

    if (count < 1)
        return 0;

    Sim_VoicePoolWipe(self);

    self->dwVoiceCount = count;
    self->logger       = src->logger;

    {
        void *block = game_operator_new((unsigned)(count * 0x18 + 4));
        CStaticSoundbuffer *bufs = 0;
        if (block != 0) {
            *(int *)block = count;
            bufs = (CStaticSoundbuffer *)((char *)block + 4);
            for (int i = 0; i < count; i++)
                CStatic_Init(&bufs[i]);
            g_nVoices += (unsigned long)count;
        }
        self->pBufs = bufs;
    }

    for (int i = 0; i < self->dwVoiceCount; i++) {       /* count re-read */
        if (CStatic_Copy(&self->pBufs[i], pDS, src, 0) == (void *)src)
            continue;
        ++g_nCopyFail;

        Sim_VoicePoolWipe(self);
        if (noFallback != 0)
            return 0;

        /* Both sides of the original's threeDBuffer branch are this call. */
        if (Sim_VoicePoolFill3D(self, count, pDS,
                                src->dwDsFlags | DSBCAPS_LOCSOFTWARE,
                                src->filename, src->logger) == 0)
            return 0;
        return self->pBufs;
    }

    return src;      /* the caller's own pointer, not pBufs */
}

/* ─── VoicePool::GetVoiceAt (0x00442e40, was FUN_00442e40) ────────────────
 *
 * `__thiscall`, RET 4.  The bounds-checked accessor: &pBufs[index], or NULL
 * if index is negative or >= dwVoiceCount.  Both compares are signed.  It
 * does not check pBufs, so a NULL pool with a positive count would return an
 * offset from NULL rather than NULL — unreachable, and preserved as shape.
 */
extern "C" __declspec(dllexport) CStaticSoundbuffer * __attribute__((thiscall))
Sim_VoicePoolGetVoiceAt(VoicePool *self, int index)
{
    ++g_nGetVoiceAt; { static unsigned long seen; pool_first("GetVoiceAt", &seen); }
    if (index < 0 || index >= self->dwVoiceCount)
        return 0;
    return &self->pBufs[index];
}

/* ─── VoicePool::GetFirstVoiceFilename (0x00442de0, was FUN_00442de0) ─────
 *
 * `__thiscall`, RET 0 — seven instructions, and it forwards its own ECX to
 * GetVoiceAt(0) implicitly by never touching it.  Returns voice 0's
 * filename (+0x08), or NULL for an empty pool.  Its two call sites are
 * SoundManager's two release-by-owner functions, which compare that name.
 */
extern "C" __declspec(dllexport) char * __attribute__((thiscall))
Sim_VoicePoolFirstFilename(VoicePool *self)
{
    ++g_nFirstName; { static unsigned long seen; pool_first("FirstFilename", &seen); }
    CStaticSoundbuffer *voice = Sim_VoicePoolGetVoiceAt(self, 0);
    if (voice == 0)
        return 0;
    return voice->filename;
}
