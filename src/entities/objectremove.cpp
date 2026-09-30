/* The object-ID free list: the allocator and the destroy-and-compact tail that
 * Foe::remove and Bomb::remove share. */

#include <windows.h>
#include <string.h>

#include "log.h"
#include "objectremove.h"
#include "movableentity.h"

/* KAROO_SIM_FX=keepid and =lowid are negative controls on the free list;
 * KAROO_REMOVE_DIAG=1 logs a census of claims and the first destroy and shift.
 */
static int s_fx_keepid = 0;
static int s_fx_lowid  = 0;
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
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "lowid") == 0) {
        s_fx_lowid = 1;
        log_write("objectremove: KAROO_SIM_FX=lowid -- the ID allocator's gap "
                  "scan BREAKS on the first hit, so it issues the lowest "
                  "unused ID instead of the highest\n");
    }

    n = GetEnvironmentVariableA("KAROO_REMOVE_DIAG", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "0") != 0)
        s_diag = 1;
}

static unsigned s_claims              = 0;
static unsigned s_claims_with_gap     = 0;
static unsigned s_claims_lowid_differs = 0;

unsigned char Object_ClaimSpareId(unsigned char *ids, unsigned char *count)
{
    unsigned char n = *count;
    unsigned char id;
    unsigned int  i;

    fx_init();

    id = 0;
    //     // PRESERVED: with n == 0 the max scan is skipped but the increment still
    //     // runs, so the first ID ever issued is 1, not 0.
    for (i = 0; i < n; ++i)
        if (ids[i] > id)
            id = ids[i];
    ++id;

    //     // PRESERVED: the gap scan does not break, so each unused candidate
    //     // overwrites the last and the ID issued is the highest unused value below
    //     // the count, not the lowest.  The two differ once two or more IDs are
    //     // free at once, as after several removals in one frame.  KAROO_SIM_FX=lowid
    //     // adds the break: it changes which valid ID an object gets, never whether
    //     // it is valid.
    for (unsigned char cand = 0; cand < n; ++cand) {
        unsigned char used = 0;
        for (i = 0; i < n; ++i)
            if (ids[i] == cand)
                used = 1;
        if (!used) {
            id = cand;
            if (s_fx_lowid)
                break;
        }
    }

    //     // The census counts gaps per claim and how often lowest and highest
    //     // differ, so a passing lowid run can be told from one where lowid never
    //     // acted.
    if (s_diag) {
        unsigned char lowest = id, gaps = 0;
        for (unsigned char cand = 0; cand < n; ++cand) {
            unsigned char used = 0;
            for (i = 0; i < n; ++i)
                if (ids[i] == cand)
                    used = 1;
            if (!used) {
                if (gaps == 0)
                    lowest = cand;
                ++gaps;
            }
        }
        ++s_claims;
        if (gaps > 0)
            ++s_claims_with_gap;
        if (lowest != id)
            ++s_claims_lowid_differs;
        log_write("objectremove: claim #%u -- count=%u issued=%u gaps=%u "
                  "lowest=%u (differs=%u of %u claims, %u with a gap)\n",
                  s_claims, (unsigned)n, (unsigned)id, (unsigned)gaps,
                  (unsigned)lowest, s_claims_lowid_differs, s_claims,
                  s_claims_with_gap);
    }

    ids[n] = id;
    *count = (unsigned char)(n + 1);
    return id;
}

static int s_logged_dtor     = 0;
static int s_logged_shift    = 0;

void Object_DestroyAndCompactId(void **slot, unsigned char *pCount,
                                unsigned char *pIds, unsigned char id,
                                int bNullSlot)
{
    void *obj = *slot;
    unsigned char found = 0;
    unsigned char i     = 0;

    fx_init();

    if (obj != 0) {
        if (s_diag && !s_logged_dtor) {
            void **vtbl = *(void ***)obj;
            s_logged_dtor = 1;
            log_write("objectremove: first virtual dtor -- obj=%p vtbl=%p "
                      "slot0=%p\n", obj, (void *)vtbl, vtbl[0]);
        }
        delete (MovableEntity *)obj;
    }

    //     // PRESERVED: Bomb::remove passes 0, leaving its slot holding a dangling
    //     // pointer to the freed object.
    if (bNullSlot)
        *slot = 0;

    //     // The shift starts at the element after the match because the `found`
    //     // test precedes the compare; the `i != 0` guard can never fire and is
    //     // kept.  The count is re-read every iteration although nothing here
    //     // writes it.
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
