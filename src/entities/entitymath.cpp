/* GAMETICK_PLAN.md Band A — the leaf helpers under UpdateEntityMovement.
 *
 * ─── Why this file exists ────────────────────────────────────────────────
 *
 * GAMETICK_PLAN.md names UpdateEntityMovement 0x00438770 as "the real gate"
 * and Band A's next target after the two leaves.  Checked against Ghidra's
 * callee list, it is not takeable yet either — it has four callees of its own
 * that are not ours:
 *
 *   CheckTileIsRamp     0x41f8a0   (none)                        LEAF
 *   GetTurnedDirection  0x43ad40   (none)                        LEAF
 *   VoicePoolCycle      0x442d90   HaltPlayback, TriggerPlayback LEAF (ours)
 *   FUN_00442df0        0x442df0   (none)                        LEAF
 *
 * plus TriggerPlayback / HaltPlayback / Set3DPosition, already ours, and the
 * CRT's __ftol.  This is the same correction the plan already records once
 * for Band A's original premise: a function is only a leaf if its callee list
 * says so, and "the next target" in a plan document is a hypothesis until
 * that list is read.  So the four leaves above come first, one per cycle, and
 * UpdateEntityMovement follows them.
 *
 * This file holds the two pure ones.  They compute; they touch no state, call
 * nothing, and have no side effects, which is why they can be reasoned about
 * from the disassembly alone and why an error in them would show up as a
 * replay divergence immediately rather than 4,000 frames later.
 */
#include <windows.h>
#include "log.h"

/* ─── Game::CheckTileIsRamp (0x0041f8a0) ──────────────────────────────────
 *
 * `__stdcall`, RET 4, one byte argument (only AL of the pushed dword is
 * read).  Returns 1 or 0 in EAX.
 *
 *     MOV AL,[ESP+4] / CMP AL,5 / JC no / CMP AL,8 / JA no / EAX=1 / RET 4
 *
 * Tile kinds 5..8 are the four ramp orientations.  BOTH COMPARES ARE
 * UNSIGNED (JC, JA), so the argument is a u8 and there is no negative case
 * to worry about — but the parameter must not be widened to a signed int,
 * or a caller passing 0x80..0xff would change answer.
 *
 * SIXTEEN call sites, all E8, all inside the movement and tile-effect code
 * (0x41f5xx-0x41f6xx, 0x4389xx, 0x4397xx-0x439axx, 0x43a1xx-0x43a2xx).  No
 * DATA reference and no `68 imm32`, so CALL_PATCHES alone covers it.
 *
 * worldstate.cpp:246 already documents the same rule ("Tile kinds 5..8 are
 * ramps") and was derived independently; this is that rule's origin.
 */
extern "C" __declspec(dllexport) int __attribute__((stdcall))
Sim_CheckTileIsRamp(unsigned char kind)
{
    if (kind > 4 && kind < 9)
        return 1;
    return 0;
}

/* ─── Game::GetTurnedDirection (0x0043ad40) ───────────────────────────────
 *
 * `__stdcall`, RET 8, two byte arguments.  Turns a 4-way facing:
 *
 *     MOV AL,[ESP+4] / MOV CL,[ESP+8] / ADD AL,CL
 *     CMP AL,4 / JBE done / ADD AL,0xFC     (i.e. AL -= 4)
 *
 * Facing lives at entity+0x14 and takes values 1..4; callers pass delta 1 to
 * turn right, 3 to turn left, 2 to reverse.  The direction-to-grid mapping
 * (worldstate.h, confirmed three ways) is 1 -> (0,-1), 2 -> (+1,0),
 * 3 -> (0,+1), 4 -> (-1,0).
 *
 * THREE THINGS TO PRESERVE, all of which a "tidier" version would break:
 *
 * 1. THE WRAP IS ONE SUBTRACTION, NOT A MODULO.  `ADD AL,0xFC` runs at most
 *    once, so an input pair summing above 8 comes out wrong — dir 4 with
 *    delta 7 gives 11 - 4 = 7, not 3.  CLAUDE.md's rule is explicit that
 *    subtraction wraps and modulo does not agree at the edges, and this is
 *    the function it is about.  No caller passes a delta above 3 today, so
 *    the difference is unreachable; it is still reproduced exactly.
 *
 * 2. THE ARITHMETIC IS 8-BIT.  ADD AL,CL wraps at 256 before the compare
 *    ever happens, so the sum is computed in u8, not int.
 *
 * 3. THE COMPARE IS UNSIGNED (JBE) even though the decompiler types the
 *    parameters as `char`.  Both operands are read as bytes and compared
 *    unsigned, so a 0x80-and-up sum takes the subtract branch.
 *
 * The original returns in AL and leaves EAX's top three bytes holding
 * whatever the caller had there.  Every call site was compiled against a
 * byte-returning function and reads only AL, so returning a zero-extended
 * unsigned char is equivalent for all of them.
 *
 * THIRTY-SIX call sites, all E8 — the most of anything in Band A — spread
 * across the player controls (0x41244x-0x4124ex), the tile effects
 * (0x41f5xx-0x41fcxx), the menu (0x4298ea) and the whole foe AI
 * (0x4388xx-0x43abxx).  No DATA reference and no `68 imm32`.
 */
extern "C" __declspec(dllexport) unsigned char __attribute__((stdcall))
Sim_GetTurnedDirection(unsigned char dir, unsigned char delta)
{
    unsigned char d = (unsigned char)(dir + delta);   /* 8-bit, wraps at 256 */
    if (d > 4)
        d = (unsigned char)(d - 4);                   /* once, not a modulo */
    return d;
}
