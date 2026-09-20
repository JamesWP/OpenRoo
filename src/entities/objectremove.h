/* objectremove.cpp -- the ID free-list shared by the foe and bomb spawns and
 * removes: one function allocates an ID, the other returns it. */
#pragma once

/* ClaimSpareObjectIdSlot 0x00417250 -- allocate the next object ID, append it
 * to `ids` and bump `*count`.  Honours KAROO_SIM_FX=lowid.  See the .cpp for
 * the two preserved defects; in short, the ID issued is the HIGHEST unused
 * value below the count, and the first one ever issued is 1, not 0. */
unsigned char Object_ClaimSpareId(unsigned char *ids, unsigned char *count);

/* Destroy *slot through its own vtable slot 0 (flags 1), optionally null the
 * slot, then compact the ID free-list and decrement *pCount.  Honours
 * KAROO_SIM_FX=keepid and KAROO_REMOVE_DIAG.  See objectremove.cpp. */
void Object_DestroyAndCompactId(void **slot, unsigned char *pCount,
                                unsigned char *pIds, unsigned char id,
                                int bNullSlot);
