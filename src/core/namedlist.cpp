/* NamedEntryList reimplementation -- the whole class, seven functions.
 *
 *   0x00445440 SetupActionEntry       __thiscall(this)                   RET 0
 *   0x00445460 ScalarDtorActionEntry  __thiscall(this, byte) -> this     RET 4
 *   0x00445480 PrepareActionEntry     __thiscall(this)                   RET 0
 *   0x00445490 InsertSoundEntry       __thiscall(this, name, p) -> entry RET 8
 *   0x00445540 ClearBindingList       __thiscall(this)                   RET 0
 *   0x00445580 RemoveActionEntry      __thiscall(this, entry) -> 0       RET 4
 *   0x00445620 FindSoundEntry         __thiscall(this, name) -> entry    RET 4
 *
 * Every signature was read off the original's `RET n`, not off the
 * decompiler's parameter list.  Two of them needed it: Ghidra gives Insert
 * three parameters and Find two, which happens to be right, but Construct,
 * DtorBody and Clear all decompile as `__fastcall(param_1)` -- they are
 * `__thiscall` with no argument, and the difference is invisible until
 * something is called with a stale EDX.
 *
 * namedlist.h holds the reference audit, the vtable argument and why the
 * entries stay on the game heap.  What follows is the semantics.
 *
 * Bugs and quirks preserved deliberately:
 *
 *   - Insert does NOT null-check `operator new` (0x004454B8 calls it and
 *     0x004454D8 copies through the result), so an exhausted heap faults
 *     inside Insert.  Reproduced.
 *   - Insert's length gate is `strlen(name) > 0x100 -> reject`, so a name of
 *     exactly 0x100 characters is ACCEPTED and then copied as strlen+1 =
 *     0x101 bytes into a 0x100-byte field.  The stray NUL lands on the low
 *     byte of pPayload -- which the very next store overwrites, so the
 *     defect is unobservable.  It is reproduced anyway, as the shape it is:
 *     a memcpy of len+1 rather than a strcpy into a bounded field.
 *   - Insert returns the entry it made, and NULL on the length rejection.
 *     Every caller in the game ignores the value.
 *   - Remove repairs head/tail from the entry's own links and never checks
 *     the entry belongs to *this* list.  A foreign entry corrupts both.
 *     That is the original's contract.
 *   - Remove always returns 0, whatever happened.
 *   - Clear frees the entries and NOT their payloads.  Whoever owns a
 *     payload -- SoundManager::SoundMgrPurgeAssets does, and walks the list
 *     destroying payloads *before* calling Clear -- has to do it first.
 *   - DtorBody re-installs the vtable BEFORE clearing, as Destruct does in
 *     linkedlist.cpp, and the order is kept for the same reason: it matters
 *     only if freeing an entry could re-enter, which it cannot.
 *   - Find compares bytes unsigned and case-sensitively, and stops at the
 *     first match.  The original inlines strcmp two bytes at a time and
 *     folds the result to -1/0/+1 with the SBB pair; only `== 0` is ever
 *     tested, so a plain byte loop is the same program.
 */
#include "namedlist.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <windows.h>
#include "log.h"

/* ─── KAROO_NAMEDLIST_FX — the negative control (CONTROLS.md) ─────────────
 *
 * Read by value, never by presence.
 *
 *   nofind — Find always returns NULL.
 *
 * Identity-by-name is the one thing this class decides outright: every
 * caller asks "is this name already in the list?" and nothing else in the
 * game answers that question.  So this is a change of OUTCOME rather than a
 * perturbation of a value, and it is the property-breaking form CLAUDE.md
 * asks for on a pure function -- Find is pure, so injectivity is the thing
 * to break, not a coordinate.
 *
 * What it should do, predicted before running it: every acquire misses, so
 * the SoundManager loads a fresh buffer and inserts a fresh entry instead of
 * sharing one, and `inserts` rises while `findHits` falls to zero.  Every
 * release misses too, so entries are leaked rather than removed and
 * `removes` falls to zero -- the leak is bounded by the per-level purge,
 * which walks the list rather than searching it.
 *
 * Blast radius: it allocates sound buffers and leaks list entries.  It moves
 * no coordinate, axis or tile index, so it cannot reach the unbounded
 * bridge/slide spawn scans that crash levelreport.py.  It can in principle
 * exhaust DirectSound buffers; if it ever does, that is a finding about the
 * control's radius and is to be recorded as one, not worked around.
 */
enum NamedListFx { NL_FX_OFF = 0, NL_FX_NOFIND = 1 };

static NamedListFx namedlist_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (NamedListFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_NAMEDLIST_FX", buf, sizeof(buf));
    NamedListFx fx = NL_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "nofind") == 0) fx = NL_FX_NOFIND;
    }
    log_write("namedlist: FX mode = %s\n", fx == NL_FX_NOFIND ? "nofind" : "off");
    cached = (int)fx;
    return fx;
}

/* ─── KAROO_NAMEDLIST_DIAG — the census ───────────────────────────────────
 *
 * Tells "the suite never calls Remove" from "the suite calls Remove and
 * cannot see it", and reports the two numbers the control is predicted to
 * move.  `maxLen` is the longest list ever reached: with sharing working it
 * should stay at the number of DISTINCT sounds a level loads, and under
 * nofind it should climb past it.
 */
static bool namedlist_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_NAMEDLIST_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nConstruct, g_nScalarDtor, g_nDtorBody, g_nInsert;
static unsigned long g_nInsertReject, g_nClear, g_nClearFreed, g_nRemove;
static unsigned long g_nRemoveNull, g_nFind, g_nFindHit, g_nMaxLen;

/* Each function announces its own first call, because a periodic sample
 * reads zero forever for anything first reached after the last threshold --
 * the exact failure the linkedlist census was rewritten to fix. */
static void namedlist_first(const char *fn, unsigned long *pSeen)
{
    if (!namedlist_diag() || *pSeen != 0)
        return;
    *pSeen = 1;
    log_write("namedlist: first call to %s\n", fn);
}

/* No sampling threshold, and the first attempt's failure is why.  Sampling on
 * Insert (1/100/1000/... , the linkedlist pattern) printed exactly one line
 * per run: a level loads a few dozen distinct sounds, so the count never
 * reaches the second threshold and the tally never reflects the end state.
 * The doublesoundbuff census had already solved this -- dump from the two
 * COLD entry points instead, which run once per teardown. */
static void namedlist_census(void)
{
    if (!namedlist_diag())
        return;
    log_write("namedlist: census construct=%lu sdtor=%lu dtorbody=%lu "
              "insert=%lu (rejected %lu) clear=%lu (freed %lu) remove=%lu "
              "(null %lu) find=%lu (hit %lu) maxLen=%lu\n",
              g_nConstruct, g_nScalarDtor, g_nDtorBody, g_nInsert,
              g_nInsertReject, g_nClear, g_nClearFreed, g_nRemove,
              g_nRemoveNull, g_nFind, g_nFindHit, g_nMaxLen);
}

extern "C" {

/* Forward declaration: the vtable below needs its address. */
__declspec(dllexport) NamedEntryList *__attribute__((thiscall))
NamedList_ScalarDtor(NamedEntryList *self, unsigned char bFreeSelf);

/* Our own one-slot table -- namedlist.h explains why it is ours and why the
 * game's at 0x0045EFAC is left pointing at a UD2. */
static void *const g_NamedListVtable[1] = { (void *)&NamedList_ScalarDtor };

__declspec(dllexport) void __attribute__((thiscall))
NamedList_Construct(NamedEntryList *self)
{
    ++g_nConstruct;
    { static unsigned long seen; namedlist_first("Construct", &seen); }
    self->vtable  = (void **)g_NamedListVtable;
    self->pHead   = NULL;
    self->pTail   = NULL;
    self->dwCount = 0;
}

__declspec(dllexport) void __attribute__((thiscall))
NamedList_Clear(NamedEntryList *self)
{
    ++g_nClear;
    { static unsigned long seen; namedlist_first("Clear", &seen); }
    NamedEntry *p = self->pHead;
    while (p != NULL) {
        NamedEntry *next = p->pNext;
        ++g_nClearFreed;
        free(p);
        p = next;
    }
    self->pHead   = NULL;
    self->pTail   = NULL;
    self->dwCount = 0;
    /* The tally goes out from here, after the frees, so it reflects the end
     * state rather than a point part-way through a run.  Clear is the cold
     * path -- once per list teardown and once per level purge -- and
     * DtorBody reaches it too, so both are covered by this one call. */
    namedlist_census();
}

__declspec(dllexport) void __attribute__((thiscall))
NamedList_DtorBody(NamedEntryList *self)
{
    ++g_nDtorBody;
    { static unsigned long seen; namedlist_first("DtorBody", &seen); }
    self->vtable = (void **)g_NamedListVtable;
    NamedList_Clear(self);
}

__declspec(dllexport) NamedEntryList *__attribute__((thiscall))
NamedList_ScalarDtor(NamedEntryList *self, unsigned char bFreeSelf)
{
    ++g_nScalarDtor;
    { static unsigned long seen; namedlist_first("ScalarDtor", &seen); }
    NamedList_DtorBody(self);
    if ((bFreeSelf & 1) != 0)
        /* Not `delete`: whatever allocated the list object with the flag set
         * did so on the game's heap.  linkedlist.cpp's ScalarDestructor
         * carries the identical note and retires with the same owners. */
        free(self);
    return self;
}

__declspec(dllexport) NamedEntry *__attribute__((thiscall))
NamedList_Insert(NamedEntryList *self, const char *pszName, void *pPayload)
{
    ++g_nInsert;
    { static unsigned long seen; namedlist_first("Insert", &seen); }
    /* Sampled on the HOT path as well as the cold one, and the reason is the
     * nofind control: it makes every acquire miss, so the lists grow without
     * bound and the run dies before the next Clear -- i.e. exactly when the
     * cold-path tally cannot report.  A census that goes silent under the
     * condition it exists to measure is no census, so Insert reports at
     * 1/100/1000/10000 and then every 20000 too. */
    if (namedlist_diag()) {
        unsigned long n = g_nInsert;
        if (n == 1 || n == 100 || n == 1000 || n == 10000 || n % 20000 == 0)
            namedlist_census();
    }

    size_t len = strlen(pszName);
    if (len > 0x100) {           /* note: >, not >=.  See the header. */
        ++g_nInsertReject;
        return NULL;
    }

    /* No NULL check, and the copy is len+1 -- both the original's. */
    NamedEntry *entry = (NamedEntry *)malloc(sizeof(NamedEntry));
    memcpy(entry->szName, pszName, len + 1);
    entry->pPayload = pPayload;
    entry->pNext    = NULL;
    entry->pPrev    = NULL;

    if (self->pTail != NULL) {
        self->pTail->pNext = entry;
        entry->pPrev       = self->pTail;
        self->pTail        = entry;
    } else {
        self->pHead = entry;
        self->pTail = entry;
    }
    self->dwCount = self->dwCount + 1;
    if (self->dwCount > g_nMaxLen)
        g_nMaxLen = self->dwCount;
    return entry;
}

__declspec(dllexport) int __attribute__((thiscall))
NamedList_Remove(NamedEntryList *self, NamedEntry *pEntry)
{
    ++g_nRemove;
    { static unsigned long seen; namedlist_first("Remove", &seen); }
    if (pEntry == NULL)
        ++g_nRemoveNull;
    if (pEntry != NULL) {
        if (pEntry->pPrev == NULL)
            self->pHead = pEntry->pNext;
        else
            pEntry->pPrev->pNext = pEntry->pNext;

        if (pEntry->pNext == NULL)
            self->pTail = pEntry->pPrev;
        else
            pEntry->pNext->pPrev = pEntry->pPrev;

        free(pEntry);
        self->dwCount = self->dwCount - 1;
    }
    return 0;
}

__declspec(dllexport) NamedEntry *__attribute__((thiscall))
NamedList_Find(NamedEntryList *self, const char *pszName)
{
    ++g_nFind;
    { static unsigned long seen; namedlist_first("Find", &seen); }

    if (namedlist_fx() == NL_FX_NOFIND)
        return NULL;             /* negative control only */

    for (NamedEntry *p = self->pHead; p != NULL; p = p->pNext) {
        if (strcmp(p->szName, pszName) == 0) {
            ++g_nFindHit;
            return p;
        }
    }
    return NULL;
}

} // extern "C"
