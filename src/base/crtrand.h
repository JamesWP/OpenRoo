/* The C runtime's rand(), as the game's runtime implements it: seed = seed *
 * 0x343FD + 0x269EC3, returning bits 16 to 30.  A value: the World holds the
 * rules' one (World::random()), seeded as each level is set up, so a copy of
 * the World rolls what the original would.  Never call the libc rand(): it
 * keeps a seed of its own. */

#pragma once

struct CrtRand {
    unsigned int seed = 1;  // as the runtime's starts

    unsigned int next()
    {
        seed = seed * 0x343FDu + 0x269EC3u;
        return (seed >> 16) & 0x7FFF;
    }
};
