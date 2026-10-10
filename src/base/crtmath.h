/* The C runtime's integer and floor conversions, as the game's runtime
 * implements them.  Shared by the simulation, the flow and the core drivers. */

#pragma once

/* __ftol: truncates toward zero to an int64.  Out of range or NaN gives
 * 0x8000000000000000, as the x87 does. */
static inline long long ftol80(long double v)
{
    return (long long)v;
}
