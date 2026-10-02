/* Two pure helpers for entity movement: they touch no state and call nothing.
 */

#include <windows.h>
#include "logger.h"
#include "entitymath.h"

/* Tile kinds 5 to 8 are the four ramps.  The compares are unsigned, so kind
 * must stay a byte: widened to a signed int, 0x80 and up would change answer.
 */
  int __attribute__((stdcall))
Sim_CheckTileIsRamp(unsigned char kind)
{
    if (kind > 4 && kind < 9)
        return 1;
    return 0;
}

/* Turns a facing 1..4: delta 1 turns right, 3 left, 2 reverses.  PRESERVED,
 * all unreachable with the deltas callers pass:
 *   - the wrap is one subtraction of 4, not a modulo, so a sum above 8 comes
 *     out wrong (4 + 7 gives 7, not 3);
 *   - the sum is 8-bit, wrapping at 256;
 *   - the compare is unsigned, so a sum of 0x80 and up is reduced. */
  unsigned char __attribute__((stdcall))
Sim_GetTurnedDirection(unsigned char dir, unsigned char delta)
{
    unsigned char d = (unsigned char)(dir + delta);  // 8-bit: wraps at 256
    if (d > 4)
        d = (unsigned char)(d - 4);  // once, not a modulo
    return d;
}
