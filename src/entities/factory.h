#pragma once
#include <windows.h>

/* Stage E — owning construction (PARTICLE_PLAN.md § 6).
 *
 * The game reaches Generators and Environments through exactly two things: the
 * factory that builds them, and their vtable (§ 6.8 — the only other contact is
 * pName/dwEnabled, still at their original offsets, so untouched here).  Taking
 * the factory over therefore takes the vtable over too: our factory hands back
 * an object whose +0x00 points at a table *we* own, so from then on every
 * virtual call the game makes lands wherever we point it, with no binary patch.
 *
 * E1 (this step) keeps behaviour bit-identical: our table starts as a copy of
 * the game's, so the same functions run in the same order.  What changes is who
 * owns the table.  E2 can then replace entries without touching Karoo.exe.
 */

/* Identity of a class, for dispatchers that switch on the vtable address.
 *
 * Once an object carries one of our tables its +0x00 is no longer the game VA
 * the VTBL_* constants name, so a raw comparison would stop matching.  This maps
 * a table pointer back to the game vtable VA it was cloned from, and returns the
 * pointer unchanged for anything we did not clone.  Callers keep switching on
 * the VTBL_* constants exactly as before. */
DWORD vtbl_identity(const void *vtbl);
