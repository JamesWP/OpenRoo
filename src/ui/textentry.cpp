/* DETERMINISM: every key poll goes through input_key_down, so replays
 * see them.  Polled in this order: Return, Escape, Backspace, Space, A..Z,
 * 0..9, then the debounce release.
 *
 * PRESERVED:
 *   - Shift is polled only after a letter is accepted; unshifted is lower case.
 *   - Backspace clears the byte after the new cursor (where the blink
 *     character was), not the one the cursor now sits on.
 *   - The debounce release is tested even when the entry is inactive.
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
#include "record.h"
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
#define LAST   lastKey_
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

    if (ACTIVE != 0) {
        BUF[CUR] = blink_positive(phase) ? 0x5f : 0x20;

        if (input_key_down(inputdev::KEY_RETURN) && LAST != inputdev::KEY_RETURN) {
            ACTIVE = 0;
            BUF[CUR] = 0;
            LAST = inputdev::KEY_RETURN;
        }
        if (input_key_down(inputdev::KEY_ESCAPE) && LAST != inputdev::KEY_ESCAPE) {
            ACTIVE = 0;
            BUF[CUR] = 0;
            LAST = inputdev::KEY_ESCAPE;
        }
        if (input_key_down(inputdev::KEY_BACKSPACE) && LAST != inputdev::KEY_BACKSPACE
            && CUR != 0) {
            CUR = (unsigned char)(CUR - 1);
            BUF[CUR + 1] = 0;
            LAST = inputdev::KEY_BACKSPACE;
        }
        if (input_key_down(inputdev::KEY_SPACE) && LAST != inputdev::KEY_SPACE
            && CUR < MAXL) {
            BUF[CUR] = 0x20;
            LAST = inputdev::KEY_SPACE;
            CUR = (unsigned char)(CUR + 1);
        }
        for (unsigned char k = inputdev::KEY_A; k <= inputdev::KEY_Z; ++k) {
            if (input_key_down(k) && LAST != k && CUR < MAXL) {
                int shifted = input_key_down(inputdev::KEY_LSHIFT)
                              || input_key_down(inputdev::KEY_RSHIFT);
                if (s_fx)
                    shifted = !shifted;
                const unsigned char upper = (unsigned char)('A' + (k - inputdev::KEY_A));
                BUF[CUR] = shifted ? upper : (unsigned char)(upper + 0x20);
                LAST = k;
                CUR = (unsigned char)(CUR + 1);
            }
        }
        for (unsigned char c = '0'; c <= '9'; ++c) {
            const unsigned char k = c == 48 ? (unsigned char)inputdev::KEY_0
                                             : (unsigned char)(inputdev::KEY_1 + (c - 49));
            if (input_key_down(k) && LAST != k && CUR < MAXL) {
                BUF[CUR] = c;
                LAST = k;
                CUR = (unsigned char)(CUR + 1);
            }
        }
    }
    if (input_key_down(LAST) == 0)
        LAST = 0;
#undef BUF
#undef LAST
#undef CUR
#undef MAXL
#undef ACTIVE
}

/* buffer_ is left as it was. */
TextEntry::TextEntry()
{
    active_    = 0;
    lastKey_   = 0;
    cursor_    = 0;
    maxLength_ = 0;
}

TextEntry::~TextEntry()
{
}

