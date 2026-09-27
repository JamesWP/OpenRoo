/* The C runtime's rand() and srand(), as the game's runtime implements them:
 * seed = seed * 0x343FD + 0x269EC3, returning bits 16 to 30.  DETERMINISM:
 * there is one seed and every rand() caller shares it; replays depend on the
 * order of the calls.  A new rand() caller in the simulation is the first
 * thing to suspect when a replay diverges.  Never call the libc rand() or
 * srand(): they keep a separate seed. */

#pragma once

/* Starts at 1, as the runtime's does.  inline makes it one object across every
 * translation unit. */
inline unsigned int g_crtRandSeed = 1;
#define CRT_RAND_SEED  g_crtRandSeed

static inline unsigned int crt_rand(void)
{
    CRT_RAND_SEED = CRT_RAND_SEED * 0x343FDu + 0x269EC3u;
    return (CRT_RAND_SEED >> 16) & 0x7FFF;
}

static inline void crt_srand(unsigned int seed)
{
    CRT_RAND_SEED = seed;
}
