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
#include <windows.h>
#include "static.h"
#include "log.h"

/* Our own replacements, in this same DLL. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_HaltPlayback(CStaticSoundbuffer *self);

/* VoicePool — 20 bytes (0x14). */
#pragma pack(push, 1)
struct VoicePool {
    void               *logger;        // +0x00  opaque Logger*
    CStaticSoundbuffer *pBufs;         // +0x04  array of dwVoiceCount voices
    int                 dwCurrentIdx;  // +0x08  round-robin cursor
    int                 dwNestDepth;   // +0x0c  not touched by either method
    int                 dwVoiceCount;  // +0x10
};
#pragma pack(pop)

static_assert(offsetof(VoicePool, logger)       == 0x00, "logger offset");
static_assert(offsetof(VoicePool, pBufs)        == 0x04, "pBufs offset");
static_assert(offsetof(VoicePool, dwCurrentIdx) == 0x08, "dwCurrentIdx offset");
static_assert(offsetof(VoicePool, dwNestDepth)  == 0x0c, "dwNestDepth offset");
static_assert(offsetof(VoicePool, dwVoiceCount) == 0x10, "dwVoiceCount offset");
static_assert(sizeof(VoicePool)                 == 0x14, "VoicePool size");

/* The disassembly indexes pBufs with LEA EAX,[EAX+EAX*2] then
 * LEA ECX,[ECX+EAX*8] — a multiply by 24.  If CStaticSoundbuffer ever grew,
 * plain C++ pointer arithmetic below would silently stop matching, so pin it. */
static_assert(sizeof(CStaticSoundbuffer) == 0x18, "pBufs stride must stay 0x18");

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
    /* defect 1: only voice 0 is checked */
    if (self->pBufs->threeDBuffer == 0 || self->dwVoiceCount <= 0)
        return;

    int i = 0;
    do {
        self->pBufs[i].threeDBuffer->SetPosition(x, y, z, dwApply);
        i++;
    } while (i < self->dwVoiceCount);
}
