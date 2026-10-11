/* Keys are taken on the press, in this order: Return, Escape, Backspace,
 * Space, A..Z, 0..9.
 *
 * PRESERVED:
 *   - Shift is read only for an accepted letter; unshifted is lower case.
 *   - Backspace clears the byte after the new cursor (where the blink
 *     character was), not the one the cursor now sits on.
 *
 * KAROO_SIM_FX=entrycase is a negative control: the shift sense is inverted,
 * so what is typed, and any cheat or name compared downstream, changes. */

#include <math.h>
#include <stdint.h>
#include "sysdev.h"
#include <string.h>
#include "logger.h"
#include "textentry.h"
#include <stdlib.h>
#include "keys.h"
#include "inputdev.h"

static int s_fx = -1;

/* True when sin(phase * 0.01) > 0: the cursor shows '_', else ' '.  The sine
 * is taken in extended precision, as the game computes it; it only chooses
 * the blink character, which Return and Escape overwrite. */
static int blink_positive(unsigned int phase)
{
    static const float k = 0.0099999998f;
    return sinl((long double)phase * (long double)k) > 0.0L;
}

void TextEntry::poll(unsigned int phase)
{
#define BUF    ((unsigned char *)buffer_)
#define CUR    cursor_
#define MAXL   maxLength_
#define ACTIVE active_

    if (s_fx < 0) {
        char b[32];
        uint32_t n = sysdev::getEnv("KAROO_SIM_FX", b, sizeof(b));
        s_fx = (n > 0 && n < sizeof(b) && strcmp(b, "entrycase") == 0);
        if (s_fx)
            g_logger.write("textentry: KAROO_SIM_FX=entrycase -- shift inverted\n");
    }

    if (ACTIVE == 0)
        return;
    BUF[CUR] = blink_positive(phase) ? 0x5f : 0x20;

    if (input::pressed(inputdev::KEY_RETURN)) {
        ACTIVE = 0;
        BUF[CUR] = 0;
        endKey_ = inputdev::KEY_RETURN;
    }
    if (input::pressed(inputdev::KEY_ESCAPE)) {
        ACTIVE = 0;
        BUF[CUR] = 0;
        endKey_ = inputdev::KEY_ESCAPE;
    }
    if (input::pressed(inputdev::KEY_BACKSPACE) && CUR != 0) {
        CUR = (unsigned char)(CUR - 1);
        BUF[CUR + 1] = 0;
    }
    if (input::pressed(inputdev::KEY_SPACE) && CUR < MAXL) {
        BUF[CUR] = 0x20;
        CUR = (unsigned char)(CUR + 1);
    }
    for (unsigned char k = inputdev::KEY_A; k <= inputdev::KEY_Z; ++k) {
        if (input::pressed(k) && CUR < MAXL) {
            int shifted = input::down(inputdev::KEY_LSHIFT)
                          || input::down(inputdev::KEY_RSHIFT);
            if (s_fx)
                shifted = !shifted;
            const unsigned char upper = (unsigned char)('A' + (k - inputdev::KEY_A));
            BUF[CUR] = shifted ? upper : (unsigned char)(upper + 0x20);
            CUR = (unsigned char)(CUR + 1);
        }
    }
    for (unsigned char c = '0'; c <= '9'; ++c) {
        const unsigned char k = c == 48 ? (unsigned char)inputdev::KEY_0
                                         : (unsigned char)(inputdev::KEY_1 + (c - 49));
        if (input::pressed(k) && CUR < MAXL) {
            BUF[CUR] = c;
            CUR = (unsigned char)(CUR + 1);
        }
    }
#undef BUF
#undef CUR
#undef MAXL
#undef ACTIVE
}

/* buffer_ is left as it was. */
TextEntry::TextEntry()
{
    active_    = 0;
    endKey_    = 0;
    cursor_    = 0;
    maxLength_ = 0;
}

TextEntry::~TextEntry()
{
}

