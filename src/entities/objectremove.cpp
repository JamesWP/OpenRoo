/* The object-ID free list: the allocator and the destroy-and-compact tail that
 * Foe::remove and Bomb::remove share. */

#include <stdint.h>
#include "sysdev.h"
#include <string.h>

#include "logger.h"
#include "objectremove.h"
#include "movableentity.h"

/* KAROO_REMOVE_DIAG=1 logs a census of claims and the first destroy and
 * shift. */
static int s_diag      = 0;
static int s_init      = 0;

static void diag_init(void)
{

    if (s_init)
        return;
    s_init = 1;

    s_diag = sysdev::envFlag("KAROO_REMOVE_DIAG");
}

static unsigned s_claims              = 0;
static unsigned s_claims_with_gap     = 0;
static unsigned s_claims_lowid_differs = 0;

unsigned char Object_ClaimSpareId(unsigned char *ids, unsigned char *count)
{
    unsigned char n = *count;
    unsigned char id;
    unsigned int  i;

    diag_init();

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
    //     // free at once, as after several removals in one frame.
    for (unsigned char cand = 0; cand < n; ++cand) {
        unsigned char used = 0;
        for (i = 0; i < n; ++i)
            if (ids[i] == cand)
                used = 1;
        if (!used)
            id = cand;
    }

    //     // The census counts gaps per claim and how often the lowest unused ID
    //     // differs from the one issued.
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
        g_logger.write("objectremove: claim #%u -- count=%u issued=%u gaps=%u "
                  "lowest=%u (differs=%u of %u claims, %u with a gap)\n",
                  s_claims, (unsigned)n, (unsigned)id, (unsigned)gaps,
                  (unsigned)lowest, s_claims_lowid_differs, s_claims,
                  s_claims_with_gap);
    }

    ids[n] = id;
    *count = (unsigned char)(n + 1);
    return id;
}

static int s_logged_shift    = 0;

void Object_DestroyAndCompactId(void **slot, unsigned char *pCount,
                                unsigned char *pIds, unsigned char id,
                                int bNullSlot)
{
    void *obj = *slot;
    unsigned char found = 0;
    unsigned char i     = 0;

    diag_init();

    if (obj != 0) {
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
                pIds[i - 1] = c;
                if (s_diag && !s_logged_shift) {
                    s_logged_shift = 1;
                    g_logger.write("objectremove: first compaction shift -- "
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
