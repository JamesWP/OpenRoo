/* objectremove.cpp -- the despawn tail shared by the foe and bomb removes. */
#pragma once

/* Destroy *slot through its own vtable slot 0 (flags 1), optionally null the
 * slot, then compact the ID free-list and decrement *pCount.  Honours
 * KAROO_SIM_FX=keepid and KAROO_REMOVE_DIAG.  See objectremove.cpp. */
void Object_DestroyAndCompactId(void **slot, unsigned char *pCount,
                                unsigned char *pIds, unsigned char id,
                                int bNullSlot);
