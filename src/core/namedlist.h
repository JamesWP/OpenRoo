/* NamedEntryList / NamedEntry -- the game's name-keyed doubly-linked list,
 * the tail of the CFaktSound translation unit (0x00445440..0x004456f0).
 *
 * Ghidra calls these the `*ActionEntry` family because ProgableControl was
 * the first caller anyone read.  It is not an action-table class: it is a
 * generic list of `char name[0x100]` -> `void *payload`, used by
 *
 *   - SoundManager, for its two sound-entry lists at +0x94 and +0xa4, whose
 *     payload is a `doublesoundbuff` asset entry (its +0x100 is the payload
 *     slot doublesoundbuff.h already names); and
 *   - ProgableControl, for the five action tables at +0x144, built through
 *     MSVC's vector-constructor iterator (stride 0x10, count 5).
 *
 * ── Why this cycle, and why before the SoundManager (ENDGAME_PLAN E1) ─────
 *
 * The five SoundManager callbacks left in E1 are written *in terms of this
 * class*: ReleaseStaticSoundBufferForOwner is "Find the entry, release the
 * buffer, and if the entry is fully released Remove it".  Replacing the
 * manager first would have retired five callbacks by creating six.  That is
 * the same ordering the doublesoundbuff cycle established one level down --
 * the order within a group is decided by who calls whom, not by who is on
 * the scoreboard -- and this is the second time it has chosen the cycle.
 *
 * It also closes the CFaktSound TU at 22/22 (ENDGAME_PLAN E2), because the
 * two functions of this class that nothing calls, FindEntryByIndex
 * 0x004455f0 and FindActionEntry 0x00445680, were already DEAD_STUBS.
 *
 * ── The reference audit ───────────────────────────────────────────────────
 *
 * Written per function, because a per-function split that is wrong in two
 * places while its total is right is exactly what the audit exists to catch.
 *
 *   function                     refs  CALL  JMP  PUSH   patch.py rewrote
 *   0x445440 Construct              3     2    -     1   2 CALL
 *   0x445460 ScalarDtor             0     -    -     -   -  (vtable slot only)
 *   0x445480 DtorBody              10     3    3     4   3 CALL + 3 JMP + 1 PUSH
 *   0x445490 Insert                 3     3    -     -   3 CALL
 *   0x445540 Clear                  4     3    1     -   3 CALL
 *   0x445580 Remove                 4     4    -     -   4 CALL
 *   0x445620 Find                   7     7    -     -   7 CALL
 *
 * 22 CALL, 3 JMP, 1 PUSH, and the run reports exactly that.  The totals
 * agreeing is the check -- but the two columns do NOT agree function by
 * function, and the reason is a property of patch.py worth writing down
 * rather than rediscovering:
 *
 *   **CALL_PATCHES rewrites every E8 site targeting a function, including
 *   sites inside bodies that are themselves already UD2 stubs.**  It scans
 *   .text for the target and cannot be told to skip a range.  That is
 *   harmless -- a stub overwrites only the two bytes at the entry, so the
 *   rewritten bytes further in are unreachable either way -- but it means
 *   "live sites" is not what the CALL column counts.  Four of the 22 are of
 *   this kind, all inside ProgableControl, which is 100% replaced: 0x44670B
 *   (Insert), 0x447633 (Find), 0x4459BA (Clear), and 0x445463 -- DtorBody's
 *   own caller, ScalarDtor, which this very cycle stubs.
 *
 * The rule that DOES bite is the VoicePool cycle's, and it bites only on the
 * explicitly-addressed lists: PUSH_PATCHES names a byte offset, so a PUSH
 * under a stub would be a real write to dead bytes and a corrupted patdiff.
 * Four of the five PUSH sites are exactly that -- 0x44571A / 0x44571F inside
 * ProgableControl::Setup, 0x4458C4 inside its Teardown, and 0x0045C7CE
 * inside an unwind funclet that is already a DEAD_STUB -- and are left
 * alone.  Only 0x0045C80E, whose funclet progress.py still classifies live,
 * is patched.  The lone E9 at 0x445486, DtorBody's tail-jump into Clear, is
 * likewise not in JMP_PATCHES: it is internal and becomes a C call.
 *
 * The genuinely live game references are all in the SoundManager TU -- plus
 * three SEH unwind funclets (0x0045C6E4, 0x0045C714, 0x0045C722) that
 * tail-JMP to DtorBody on behalf of InitSoundManager, adding 0x94 or 0xa4 to
 * ECX first for its two embedded lists.  JMP_PATCHES rewrites those the way
 * it did LinkedList::Destruct's fifteen; CALL_PATCHES does not touch E9.
 *
 * ── ScalarDtor has no code reference at all ───────────────────────────────
 *
 * xref.py reports *nothing* for 0x00445460: no CALL, no JMP.  It is reached
 * only through slot 0 of the vtable at 0x0045EFAC.  That is the FIFTH time
 * in this plan (LinkedList::ScalarDestructor, both texture ScalarDeletingDtors,
 * CStaticSoundbuffer::ScalarVectorDtor, now this) that an E8/E9 scan alone
 * would have missed a function entirely.  For any class, check the
 * slot-only functions separately.
 *
 * ── The vtable is ours ────────────────────────────────────────────────────
 *
 * 0x0045EFAC is one slot wide -- 0x0045EFA8 is CFaktSound's and 0x0045EFB0 is
 * ProgableControl's -- and that slot is ScalarDtor, which this cycle takes.
 * So the vtable licence (ENDGAME_PLAN) applies in full: Construct and
 * DtorBody install our own g_NamedListVtable and there is no VTABLE_PATCHES
 * entry.  The game's table keeps pointing at the UD2-stubbed 0x00445460 and
 * becomes a tripwire.
 *
 * ── The entries stay on the game heap ─────────────────────────────────────
 *
 * Insert allocates each 0x10c entry with the game's `operator new`; Remove
 * and Clear free it with FactAlloc::Free2.  All three are ours after this
 * cycle, which *looks* like COHESION_PLAN template 6's "both sides ours" --
 * but that rule wants the other side proven, and "no game function anywhere
 * frees a 0x10c entry" is not something an xref of a generic allocator can
 * show.  linkedlist.h held its nodes back on exactly this argument and was
 * right to; this follows it.  The site retires with the SoundManager, which
 * is the very next cycle.
 */
#pragma once

#include "layout.h"

struct __attribute__((packed)) NamedEntry {
    static const int ORIGIN = 0;

    char        szName[0x100];  // +0x000  the key, an inline array
    void       *pPayload;       // +0x100
    NamedEntry *pNext;          // +0x104
    NamedEntry *pPrev;          // +0x108

private:
    KAROO_LAYOUT_REGISTER(NamedEntry);
};

/* 0x10c is what Insert's `operator new` asks for -- the size is ground truth
 * and the fields tile exactly to it. */
KAROO_LAYOUT_CHECKS(NamedEntry)
{
    KAROO_LAYOUT_AT(szName,   0x000);
    KAROO_LAYOUT_AT(pPayload, 0x100);
    KAROO_LAYOUT_AT(pNext,    0x104);
    KAROO_LAYOUT_AT(pPrev,    0x108);
    KAROO_LAYOUT_SIZE(0x10c);
}

struct __attribute__((packed)) NamedEntryList {
    static const int ORIGIN = 0;

    void        **vtable;   // +0x00
    NamedEntry   *pHead;    // +0x04
    NamedEntry   *pTail;    // +0x08
    unsigned long dwCount;  // +0x0c

private:
    KAROO_LAYOUT_REGISTER(NamedEntryList);
};

/* 16 bytes, and independently confirmed: ProgableControl's vector-constructor
 * iterator at 0x0044571A pushes a stride of 0x10 for five of them.  Same
 * shape as LinkedList, and deliberately not merged with it -- these are two
 * classes in the original with two vtables and two node layouts. */
KAROO_LAYOUT_CHECKS(NamedEntryList)
{
    KAROO_LAYOUT_AT(vtable,  0x00);
    KAROO_LAYOUT_AT(pHead,   0x04);
    KAROO_LAYOUT_AT(pTail,   0x08);
    KAROO_LAYOUT_AT(dwCount, 0x0c);
    KAROO_LAYOUT_SIZE(16);
}

/* 0x00445440 SetupActionEntry -- vtable + three zeroed fields.  RET 0. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
NamedList_Construct(NamedEntryList *self);

/* 0x00445460 ScalarDtorActionEntry -- vtable[0].  DtorBody, then free `self`
 * when bit 0 of the flag is set.  Returns `self`.  RET 4. */
extern "C" __declspec(dllexport) NamedEntryList *__attribute__((thiscall))
NamedList_ScalarDtor(NamedEntryList *self, unsigned char bFreeSelf);

/* 0x00445480 PrepareActionEntry -- re-install the vtable, then Clear.  Two
 * instructions in the original, the second a tail-JMP.  RET 0. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
NamedList_DtorBody(NamedEntryList *self);

/* 0x00445490 InsertSoundEntry -- allocate a 0x10c entry, copy the name into
 * it, hang the payload off it and link it at the TAIL.  Returns the entry,
 * or NULL when the name is too long.  RET 8. */
extern "C" __declspec(dllexport) NamedEntry *__attribute__((thiscall))
NamedList_Insert(NamedEntryList *self, const char *pszName, void *pPayload);

/* 0x00445540 ClearBindingList -- frees every entry, not their payloads.
 * RET 0. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
NamedList_Clear(NamedEntryList *self);

/* 0x00445580 RemoveActionEntry -- unlink and free pEntry, decrementing
 * dwCount.  A NULL pEntry is a no-op.  Always returns 0.  RET 4. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
NamedList_Remove(NamedEntryList *self, NamedEntry *pEntry);

/* 0x00445620 FindSoundEntry -- the first entry whose name matches exactly,
 * or NULL.  RET 4. */
extern "C" __declspec(dllexport) NamedEntry *__attribute__((thiscall))
NamedList_Find(NamedEntryList *self, const char *pszName);
