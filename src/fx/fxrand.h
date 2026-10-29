/* The particle effects' own random numbers.  They are kept apart from the
 * rules' (World::random(), crtrand.h) so that loading or cloning an effect cannot move
 * the simulation. */
#pragma once
#include <random>
#include <utility>

inline std::minstd_rand &fx_engine()
{
    static std::minstd_rand engine;
    return engine;
}

/* Uniform between lo and hi, either way round. */
inline float fx_uniform(float lo, float hi)
{
    if (hi < lo)
        std::swap(lo, hi);
    if (!(lo < hi))
        return lo;
    return std::uniform_real_distribution<float>(lo, hi)(fx_engine());
}

/* Uniform in [0, n-1]; 0 when n is 0. */
inline unsigned fx_index(unsigned n)
{
    return n ? std::uniform_int_distribution<unsigned>(0, n - 1)(fx_engine()) : 0;
}
