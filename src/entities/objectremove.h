/* The object-ID free list the foe and bomb tables share (objectremove.cpp):
 * one function hands out an ID, the other takes it back. */

#pragma once

/* Allocates an ID, appends it to ids and increments *count.  PRESERVED: the ID
 * issued is the highest unused value below the count, and the first ever
 * issued is 1, not 0.  Honours KAROO_SIM_FX=lowid. */
unsigned char Object_ClaimSpareId(unsigned char *ids, unsigned char *count);

/* Destroys *slot through its scalar deleting destructor, nulls the slot if
 * bNullSlot, then removes id from the ID list and decrements *pCount.  Honours
 * KAROO_SIM_FX=keepid and KAROO_REMOVE_DIAG. */
void Object_DestroyAndCompactId(void **slot, unsigned char *pCount,
                                unsigned char *pIds, unsigned char id,
                                int bNullSlot);
