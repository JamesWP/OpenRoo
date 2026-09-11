/* Layout assertions for classes that overlay game-owned memory.
 *
 * What we rely on is where each field sits relative to the address the GAME
 * uses for the object -- the Game pointer, the `tileBase + (v + u*100)*0x7f`
 * tile pointer, the pointer operator new returned.  That is what is asserted,
 * and nothing else: padding sizes are never asserted, so a layout may be
 * reshaped (a gap split into a new field, a struct re-rooted) without any
 * assertion changing.
 *
 * Every such class declares
 *
 *     static const int ORIGIN = N;   // our first byte, relative to the
 *                                    // game's address for the object
 *
 * and the macro checks  ORIGIN + offsetof(Class, member) == game offset.
 * ORIGIN is 0 unless the class deliberately starts elsewhere (see tile.h).
 *
 * offsetof needs member access, so use it inside the class's private static
 * assertLayout().
 */
#pragma once

#include <stddef.h>

#define KAROO_LAYOUT_AT(Class, member, gameOffset)                          \
    static_assert((Class::ORIGIN + offsetof(Class, member)) == (gameOffset),\
                  #Class "::" #member " must be at game offset " #gameOffset)
