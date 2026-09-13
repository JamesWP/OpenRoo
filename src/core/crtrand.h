/* The CRT rand() at 0x0045167c (CrtRandLcg in Ghidra), reimplemented.
 *
 * seed = seed * 0x343FD + 0x269EC3; return (seed >> 16) & 0x7FFF.
 *
 * The seed is the game's own global DAT_00469f38, NOT a private one -- other
 * code (the game's srand at 0x00451672 and every remaining original rand()
 * caller) seeds and consumes it, so a private copy would desynchronise them
 * and break replay determinism outright.  A rand() caller inside the
 * simulation is exactly what GAMETICK_PLAN.md says to suspect first if a
 * replay ever diverges; every one of ours goes through here.
 */
#pragma once

#define CRT_RAND_SEED  (*(unsigned int *)0x00469f38)

static inline unsigned int crt_rand(void)
{
    CRT_RAND_SEED = CRT_RAND_SEED * 0x343FDu + 0x269EC3u;
    return (CRT_RAND_SEED >> 16) & 0x7FFF;
}
