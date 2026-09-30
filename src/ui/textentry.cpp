/* DETERMINISM: every key poll goes through hooks_GetAsyncKeyState, so replays
 * see them, and their order is part of the recording: Return, Escape,
 * Backspace, Space, A..[ , 0..: , then the debounce release.
 *
 * PRESERVED:
 *   - '[' and ':' are accepted along with the letters and digits.
 *   - Shift is polled only after a letter is accepted; unshifted adds 0x20.
 *   - Backspace clears the byte after the new cursor (where the blink
 *     character was), not the one the cursor now sits on.
 *   - The debounce release is tested even when the entry is inactive.
 *
 * KAROO_SIM_FX=entrycase is a negative control: the shift sense is inverted,
 * so what is typed, and any cheat or name compared downstream, changes. */

#include <windows.h>
#include <string.h>
#include "log.h"
#include "textentry.h"
#include <stdlib.h>
#include "record.h"

static int s_fx = -1;

/* True when sin(phase * 0.01) > 0: the cursor shows '_', else ' '.  The x87
 * FSIN on an extended value, as the game computes it; it only chooses the
 * blink character, which Return and Escape overwrite. */
static int blink_positive(unsigned int phase)
{
    unsigned long long q = phase;  // the high word zeroed: an unsigned load
    static const float k = 0.0099999998f;
    unsigned short sw;

    __asm__ volatile(
        "fildq %1\n\t"
        "fmuls %2\n\t"
        "fsin\n\t"
        "fldz\n\t"
        "fcompp\n\t"  // compares 0.0 with sin
        "fnstsw %0\n\t"
        : "=a"(sw) : "m"(q), "m"(k) : "st", "st(1)");
    // C0 set (0 < sin) and C2 clear (ordered).
    return (sw & 0x0100) && !(sw & 0x0400);
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
        DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", b, sizeof(b));
        s_fx = (n > 0 && n < sizeof(b) && strcmp(b, "entrycase") == 0);
        if (s_fx)
            log_write("textentry: KAROO_SIM_FX=entrycase -- shift inverted\n");
    }

    if (ACTIVE != 0) {
        BUF[CUR] = blink_positive(phase) ? 0x5f : 0x20;

        if (hooks_GetAsyncKeyState(0x0d) != 0 && LAST != 0x0d) {
            ACTIVE = 0;
            BUF[CUR] = 0;
            LAST = 0x0d;
        }
        if (hooks_GetAsyncKeyState(0x1b) != 0 && LAST != 0x1b) {
            ACTIVE = 0;
            BUF[CUR] = 0;
            LAST = 0x1b;
        }
        if (hooks_GetAsyncKeyState(0x08) != 0 && LAST != 0x08 && CUR != 0) {
            CUR = (unsigned char)(CUR - 1);
            BUF[CUR + 1] = 0;
            LAST = 0x08;
        }
        if (hooks_GetAsyncKeyState(0x20) != 0 && LAST != 0x20 && CUR < MAXL) {
            BUF[CUR] = 0x20;
            LAST = 0x20;
            CUR = (unsigned char)(CUR + 1);
        }
        for (unsigned char k = 0x41; k < 0x5c; ++k) {
            if (hooks_GetAsyncKeyState(k) != 0 && LAST != k && CUR < MAXL) {
                int shifted = hooks_GetAsyncKeyState(0x10) != 0;
                if (s_fx)
                    shifted = !shifted;
                BUF[CUR] = shifted ? k : (unsigned char)(k + 0x20);
                LAST = k;
                CUR = (unsigned char)(CUR + 1);
            }
        }
        for (unsigned char k = 0x30; k < 0x3b; ++k) {
            if (hooks_GetAsyncKeyState(k) != 0 && LAST != k && CUR < MAXL) {
                BUF[CUR] = k;
                LAST = k;
                CUR = (unsigned char)(CUR + 1);
            }
        }
    }
    if (hooks_GetAsyncKeyState(LAST) == 0)
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

